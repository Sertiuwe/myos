#include "ata.h"
#include "bdev.h"
#include "kstdio.h"
#include <stdint.h>

/* Primary channel */
#define ATA_IO    0x1F0
#define ATA_CTRL  0x3F6

#define REG_DATA   0
#define REG_ERR    1
#define REG_CNT    2
#define REG_LBA0   3
#define REG_LBA1   4
#define REG_LBA2   5
#define REG_DEV    6
#define REG_CMD    7   /* write */
#define REG_STATUS 7   /* read */

#define ST_ERR  0x01
#define ST_DRQ  0x08
#define ST_BSY  0x80

#define CTRL_SRST 0x04

#define CMD_IDENTIFY 0xEC
#define CMD_READ     0x20
#define CMD_WRITE    0x30
#define CMD_FLUSH    0xE7

struct ata_dev {
    uint16_t io, ctrl;
    int      dev;          /* 0 = master, 1 = slave */
    uint64_t sectors;
};

static uint8_t inb(uint16_t p) { uint8_t v; __asm__ volatile ("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static void outb(uint16_t p, uint8_t v) { __asm__ volatile ("outb %0,%1" : : "a"(v), "Nd"(p)); }
static uint16_t inw(uint16_t p) { uint16_t v; __asm__ volatile ("inw %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static void outw(uint16_t p, uint16_t v) { __asm__ volatile ("outw %0,%1" : : "a"(v), "Nd"(p)); }

static uint8_t status(struct ata_dev *d) { return inb(d->io + REG_STATUS); }

/* ~400 нс после выбора устройства */
static void ata_delay(struct ata_dev *d) { for (int i = 0; i < 4; i++) (void)inb(d->ctrl); }

static int wait_bsy(struct ata_dev *d) {
    for (int i = 0; i < 400000; i++) if (!(status(d) & ST_BSY)) return 0;
    return -1;
}
static int wait_drq(struct ata_dev *d) {
    for (int i = 0; i < 400000; i++) {
        uint8_t s = status(d);
        if (s & ST_ERR) return -1;
        if (s & ST_DRQ) return 0;
    }
    return -1;
}

static void ata_reset(struct ata_dev *d) {
    /* Software Reset: устанавливаем SRST, ждём, снимаем */
    outb(d->ctrl, CTRL_SRST);
    for (volatile int i = 0; i < 10000; i++) {}   /* ~10 мкс задержка */
    outb(d->ctrl, 0);
    for (volatile int i = 0; i < 100000; i++) {}  /* даём устройству очнуться */
    /* Читаем статус, чтобы сбросить pending IRQ */
    (void)inb(d->io + REG_STATUS);
}

static int ata_identify(struct ata_dev *d) {
    ata_reset(d);
    outb(d->io + REG_DEV, (uint8_t)(0xA0 | (d->dev << 4)));
    ata_delay(d);
    outb(d->io + REG_CNT, 0);
    outb(d->io + REG_LBA0, 0);
    outb(d->io + REG_LBA1, 0);
    outb(d->io + REG_LBA2, 0);
    outb(d->io + REG_CMD, CMD_IDENTIFY);

    uint8_t st = status(d);
    if (st == 0) return -1;                 /* устройства нет */
    if (wait_bsy(d)) return -1;
    for (;;) {
        st = status(d);
        if (st & ST_ERR) return -1;
        if (st & ST_DRQ) break;
        if (st == 0) return -1;
    }
    uint16_t id[256];
    for (int i = 0; i < 256; i++) id[i] = inw(d->io + REG_DATA);

    /* Word 0: 0x0040 или 0x00C0 = ATA device, иначе это не диск */
    if (id[0] != 0x0040 && id[0] != 0x00C0) return -1;

    uint32_t lba28 = (uint32_t)id[60] | ((uint32_t)id[61] << 16);
    if (lba28) d->sectors = lba28;
    else d->sectors = (uint64_t)id[1] * id[3] * id[6];   /* CHS fallback */
    if (!d->sectors) return -1;
    return 0;
}

static int ata_transfer(struct ata_dev *d, uint64_t lba, uint32_t count,
                        void *buf, int write) {
    uint8_t *p = (uint8_t *)buf;
    while (count) {
        uint32_t n = count > 256 ? 256 : count;
        outb(d->io + REG_DEV, (uint8_t)(0xE0 | (d->dev << 4) | ((lba >> 24) & 0x0F)));
        ata_delay(d);
        outb(d->io + REG_CNT, (uint8_t)n);
        outb(d->io + REG_LBA0, (uint8_t)(lba));
        outb(d->io + REG_LBA1, (uint8_t)(lba >> 8));
        outb(d->io + REG_LBA2, (uint8_t)(lba >> 16));
        outb(d->io + REG_CMD, write ? CMD_WRITE : CMD_READ);

        if (wait_bsy(d)) return -1;
        for (uint32_t s = 0; s < n; s++) {
            if (wait_drq(d)) return -1;
            uint16_t *w = (uint16_t *)(p + (size_t)s * 512);
            if (write) for (int i = 0; i < 256; i++) outw(d->io + REG_DATA, w[i]);
            else       for (int i = 0; i < 256; i++) w[i] = inw(d->io + REG_DATA);
        }
        if (wait_bsy(d)) return -1;
        lba += n; count -= n; p += (size_t)n * 512;
    }
    if (write) { outb(d->io + REG_CMD, CMD_FLUSH); wait_bsy(d); }
    return 0;
}

static int ata_bread(bdev_t *b, uint64_t lba, uint32_t c, void *buf) {
    return ata_transfer((struct ata_dev *)b->priv, lba, c, buf, 0);
}
static int ata_bwrite(bdev_t *b, uint64_t lba, uint32_t c, const void *buf) {
    return ata_transfer((struct ata_dev *)b->priv, lba, c, (void *)buf, 1);
}

static struct ata_dev devs[2];
static bdev_t bdevs[2];

void ata_init(void) {
    for (int i = 0; i < 2; i++) {
        devs[i].io = ATA_IO; devs[i].ctrl = ATA_CTRL; devs[i].dev = i;
        if (ata_identify(&devs[i]) != 0) continue;
        bdev_t *b = &bdevs[i];
        b->name = i ? "ata1" : "ata0";
        b->sectors = devs[i].sectors;
        b->read = ata_bread;
        b->write = ata_bwrite;
        b->priv = &devs[i];
        bdev_register(b);
        kprintf("ata%d: %u sectors (%u MiB)\n", i,
                (unsigned)b->sectors, (unsigned)(b->sectors / 2048));
    }
}
