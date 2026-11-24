#include "ata.h"
#include "dma.h"

static int dma_ok;
static uint32_t dma_reads_total, dma_fallbacks_total;
#include "bdev.h"
#include "kstdio.h"
#include "kstring.h"
#include <stdint.h>

/* Primary channel */
#define ATA_IO    0x1F0
#define ATA_CTRL  0x3F6

#define REG_DATA   0
#define REG_ERR    1   /* read */
#define REG_FEAT   1   /* write */
#define REG_CNT    2
#define REG_LBA0   3
#define REG_LBA1   4
#define REG_LBA2   5
#define REG_DEV    6
#define REG_CMD    7   /* write */
#define REG_STATUS 7   /* read */
#define REG_ALTST  0   /* ctrl base + 0 = alternate status (read) */
#define REG_DEVCTL 0   /* ctrl base + 0 = device control (write) */

#define ST_ERR  0x01
#define ST_DRQ  0x08
#define ST_BSY  0x80

#define CMD_IDENTIFY  0xEC
#define CMD_READ48    0x24
#define CMD_WRITE48   0x34
#define CMD_FLUSH48   0xEA

#define CTRL_SRST 0x04
#define MAX_WAIT 400000

struct ata_dev {
    uint16_t io, ctrl;
    int      dev;           /* 0=master, 1=slave */
    uint64_t sectors;
    char     model[41];
    char     serial[21];
    /* статистика */
    uint32_t reads, writes, errors;
    uint64_t bytes_r, bytes_w;
};

static uint8_t  inb(uint16_t p)  { uint8_t v;  __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v; }
static void     outb(uint16_t p, uint8_t v)  { __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static uint16_t inw(uint16_t p)  { uint16_t v; __asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p)); return v; }
static void     outw(uint16_t p, uint16_t v) { __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p)); }

static uint8_t status(struct ata_dev *d) { return inb(d->io + REG_STATUS); }
static uint8_t alt_status(struct ata_dev *d) { return inb(d->ctrl + REG_ALTST); }
static uint8_t read_err(struct ata_dev *d) { return inb(d->io + REG_ERR); }

static void ata_delay(struct ata_dev *d) { for (int i = 0; i < 4; i++) (void)alt_status(d); }

static int wait_bsy(struct ata_dev *d) {
    for (int i = 0; i < MAX_WAIT; i++) if (!(status(d) & ST_BSY)) return 0;
    return -1;
}
static int wait_drq(struct ata_dev *d) {
    for (int i = 0; i < MAX_WAIT; i++) {
        uint8_t s = status(d);
        if (s & ST_ERR) return -1;
        if (s & ST_DRQ) return 0;
    }
    return -1;
}

/* Парсинг ASCII-строк из IDENTIFY: слова переставлены байтами */
static void parse_ata_string(char *dst, const uint16_t *src, int words) {
    int j = 0;
    for (int i = 0; i < words; i++) {
        dst[j++] = (char)(src[i] >> 8);
        dst[j++] = (char)(src[i] & 0xFF);
    }
    /* убираем trailing пробелы */
    while (j > 0 && dst[j - 1] == ' ') j--;
    dst[j] = '\0';
}

static void ata_reset(struct ata_dev *d) {
    outb(d->ctrl, CTRL_SRST);
    for (volatile int i = 0; i < 10000; i++) {}
    outb(d->ctrl, 0);
    for (volatile int i = 0; i < 100000; i++) {}
    (void)status(d);
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
    if (st == 0) return -1;
    if (wait_bsy(d)) return -1;
    for (;;) {
        st = status(d);
        if (st & ST_ERR) return -1;
        if (st & ST_DRQ) break;
        if (st == 0) return -1;
    }

    uint16_t id[256];
    for (int i = 0; i < 256; i++) id[i] = inw(d->io + REG_DATA);

    /* Word 0: 0x0040 или 0x00C0 = ATA device */
    if (id[0] != 0x0040 && id[0] != 0x00C0) return -1;

    /* LBA48: word 100..103 (48-bit LBA) */
    uint64_t lba48 = (uint64_t)id[100] | ((uint64_t)id[101] << 16) |
                     ((uint64_t)id[102] << 32) | ((uint64_t)id[103] << 48);
    if (lba48) {
        d->sectors = lba48;
    } else {
        /* Fallback на LBA28 */
        uint32_t lba28 = (uint32_t)id[60] | ((uint32_t)id[61] << 16);
        d->sectors = lba28 ? lba28 : (uint64_t)id[1] * id[3] * id[6];
    }
    if (!d->sectors) return -1;

    /* Модель и серийник */
    parse_ata_string(d->model, &id[27], 20);
    parse_ata_string(d->serial, &id[10], 10);

    return 0;
}

static int ata_transfer(struct ata_dev *d, uint64_t lba, uint32_t count,
                        void *buf, int write) {
    uint8_t *p = (uint8_t *)buf;
    while (count) {
        uint32_t n = count > 256 ? 256 : count;

        /* DMA fast path (read only). Any failure falls through to the
           untouched PIO code below. */
        if (!write && dma_ok && lba <= 0x0FFFFFFFULL) {
            uint32_t dn = n > 255 ? 255 : n;
            if (dma_ata_read(d->dev, (uint32_t)lba, (uint8_t)dn, p) == 0) {
                dma_reads_total++;
                d->reads++;
                d->bytes_r += (uint64_t)dn * 512;
                lba += dn; count -= dn; p += (size_t)dn * 512;
                continue;
            }
            dma_fallbacks_total++;
        }

        /* LBA48: выбираем устройство с битом LBA=1 */
        outb(d->io + REG_DEV, (uint8_t)(0xE0 | (d->dev << 4)));
        ata_delay(d);

        /* LBA48: счётчик и адрес в два приёма (старшие байты первыми) */
        outb(d->io + REG_CNT, (uint8_t)(n >> 8));
        outb(d->io + REG_LBA0, (uint8_t)(lba >> 24));
        outb(d->io + REG_LBA1, (uint8_t)(lba >> 32));
        outb(d->io + REG_LBA2, (uint8_t)(lba >> 40));
        outb(d->io + REG_CNT, (uint8_t)(n & 0xFF));
        outb(d->io + REG_LBA0, (uint8_t)(lba));
        outb(d->io + REG_LBA1, (uint8_t)(lba >> 8));
        outb(d->io + REG_LBA2, (uint8_t)(lba >> 16));

        outb(d->io + REG_CMD, write ? CMD_WRITE48 : CMD_READ48);

        if (wait_bsy(d)) { d->errors++; return -1; }

        for (uint32_t s = 0; s < n; s++) {
            if (wait_drq(d)) { d->errors++; return -1; }
            uint16_t *w = (uint16_t *)(p + (size_t)s * 512);
            if (write) for (int i = 0; i < 256; i++) outw(d->io + REG_DATA, w[i]);
            else       for (int i = 0; i < 256; i++) w[i] = inw(d->io + REG_DATA);
        }

        if (wait_bsy(d)) { d->errors++; return -1; }

        /* Проверяем ERR после завершения трансфера */
        if (status(d) & ST_ERR) {
            uint8_t err = read_err(d);
            kprintf("ata%d: transfer error 0x%02x at lba %u\n", d->dev, err, (unsigned)lba);
            d->errors++;
            return -1;
        }

        if (write) {
            d->writes++;
            d->bytes_w += (uint64_t)n * 512;
        } else {
            d->reads++;
            d->bytes_r += (uint64_t)n * 512;
        }

        lba += n; count -= n; p += (size_t)n * 512;
    }

    if (write) {
        /* FLUSH CACHE EXT (LBA48) */
        outb(d->io + REG_DEV, (uint8_t)(0xE0 | (d->dev << 4)));
        ata_delay(d);
        outb(d->io + REG_CMD, CMD_FLUSH48);
        if (wait_bsy(d)) { d->errors++; return -1; }
    }
    return 0;
}

static int ata_bread(bdev_t *b, uint64_t lba, uint32_t c, void *buf) {
    return ata_transfer((struct ata_dev *)b->priv, lba, c, buf, 0);
}
static int ata_bwrite(bdev_t *b, uint64_t lba, uint32_t c, const void *buf) {
    return ata_transfer((struct ata_dev *)b->priv, lba, c, (void *)buf, 1);
}


void ata_dma_enable(void) {
    dma_ok = (dma_ata_bm_init(NULL) == 0);
    kprintf("ATA: DMA %s\n", dma_ok ? "enabled (read path)" : "unavailable, PIO only");
}

void ata_dma_stats(uint32_t *reads, uint32_t *fb) {
    if (reads) *reads = dma_reads_total;
    if (fb)    *fb    = dma_fallbacks_total;
}

static struct ata_dev devs[2];
static bdev_t bdevs[2];

void ata_init(void) {
    memset(devs, 0, sizeof devs);
    memset(bdevs, 0, sizeof bdevs);
    for (int i = 0; i < 2; i++) {
        devs[i].io = ATA_IO; devs[i].ctrl = ATA_CTRL; devs[i].dev = i;
        if (ata_identify(&devs[i]) != 0) continue;
        bdev_t *b = &bdevs[i];
        ksnprintf((char *)devs[i].model + 0, 0, ""); /* name в bdev ниже */
        static const char *names[] = {"ata0", "ata1"};
        b->name = names[i];
        b->sectors = devs[i].sectors;
        b->read = ata_bread;
        b->write = ata_bwrite;
        b->priv = &devs[i];
        bdev_register(b);
        kprintf("ata%d: %s [%s] %u sectors (%u MiB)\n",
                i, devs[i].model, devs[i].serial,
                (unsigned)b->sectors, (unsigned)(b->sectors / 2048));
    }
}

int ata_count(void) {
    int n = 0;
    for (int i = 0; i < 2; i++) if (devs[i].sectors) n++;
    return n;
}

int ata_get_info(int idx, ata_info_t *out) {
    if (idx < 0 || idx >= 2 || !devs[idx].sectors) return -1;
    struct ata_dev *d = &devs[idx];
    ksnprintf(out->name, sizeof out->name, "ata%d", idx);
    memcpy(out->model, d->model, 41);
    memcpy(out->serial, d->serial, 21);
    out->sectors = d->sectors;
    out->reads = d->reads;
    out->writes = d->writes;
    out->errors = d->errors;
    out->bytes_read = d->bytes_r;
    out->bytes_written = d->bytes_w;
    return 0;
}
