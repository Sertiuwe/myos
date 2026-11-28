#include "osdisk.h"
#include "bdev.h"
#include "kstdio.h"
#include "kstring.h"

/* Layout produced by the Makefile: boot.bin | stage2.bin | kernel.bin */
#define BOOT_LBA    0u
#define BOOT_SECT   1u
#define STG2_LBA    1u
#define STG2_SECT   32u
#define KERN_LBA    33u
#define KERN_SECT   384u

static bdev_t *osdev(void) { return bdev_by_name("ata0"); }

void osdisk_info(void) {
    bdev_t *d = osdev();
    if (!d) { kprintf("osdisk: ata0 not present\n"); return; }
    uint8_t b[512];
    kprintf("--- OS DISK (ata0) ---\n");
    kprintf("capacity : %u sectors (%u KiB)\n",
            (unsigned)d->sectors, (unsigned)(d->sectors / 2));
    kprintf("region     LBA range      size\n");
    kprintf("boot       %4u..%-4u    %u B\n",
            BOOT_LBA, BOOT_LBA + BOOT_SECT - 1, BOOT_SECT * 512);
    kprintf("stage2     %4u..%-4u    %u KiB\n",
            STG2_LBA, STG2_LBA + STG2_SECT - 1, (STG2_SECT * 512) / 1024);
    kprintf("kernel     %4u..%-4u    %u KiB\n",
            KERN_LBA, KERN_LBA + KERN_SECT - 1, (KERN_SECT * 512) / 1024);
    kprintf("unused     %4u..%-4u\n",
            KERN_LBA + KERN_SECT, (unsigned)d->sectors - 1);

    if (d->read(d, BOOT_LBA, 1, b) == 0) {
        kprintf("boot sig : %02x%02x %s\n", b[510], b[511],
                (b[510] == 0x55 && b[511] == 0xAA) ? "(valid)" : "(BAD)");
        kprintf("boot[0]  : %02x %02x %02x (jmp)\n", b[0], b[1], b[2]);
    }
    if (d->read(d, KERN_LBA, 1, b) == 0)
        kprintf("kernel[0]: %02x %02x %02x %02x\n", b[0], b[1], b[2], b[3]);
    kprintf("use: oshex <lba> [sectors]\n");
}

static void hexline(uint32_t off, const uint8_t *p, int n) {
    kprintf("%08x  ", (unsigned)off);
    for (int i = 0; i < 16; i++) {
        if (i < n) kprintf("%02x ", p[i]); else kprintf("   ");
        if (i == 7) kprintf(" ");
    }
    kprintf(" |");
    for (int i = 0; i < n; i++) {
        uint8_t c = p[i];
        kprintf("%c", (c >= 32 && c < 127) ? c : '.');
    }
    kprintf("|\n");
}

void osdisk_hex(uint32_t lba, uint32_t nsec) {
    bdev_t *d = osdev();
    if (!d) { kprintf("osdisk: ata0 not present\n"); return; }
    if (nsec == 0) nsec = 1;
    if (nsec > 8) nsec = 8;
    if (lba >= d->sectors) { kprintf("osdisk: lba out of range\n"); return; }
    uint8_t b[512];
    for (uint32_t s = 0; s < nsec; s++) {
        if (lba + s >= d->sectors) break;
        if (d->read(d, lba + s, 1, b) != 0) { kprintf("osdisk: read error\n"); return; }
        kprintf("--- LBA %u (offset 0x%x) ---\n",
                (unsigned)(lba + s), (unsigned)((lba + s) * 512));
        for (int off = 0; off < 512; off += 16) hexline((uint32_t)off, b + off, 16);
    }
}

static uint32_t parse_num(const char **p, uint32_t def) {
    while (**p == ' ') (*p)++;
    if (**p < '0' || **p > '9') return def;
    uint32_t v = 0;
    while (**p >= '0' && **p <= '9') { v = v * 10 + (uint32_t)(**p - '0'); (*p)++; }
    return v;
}

void osdisk_cmd(const char *arg) {
    if (!arg) { osdisk_hex(0, 1); return; }
    const char *p = arg;
    uint32_t lba = parse_num(&p, 0);
    uint32_t n   = parse_num(&p, 1);
    osdisk_hex(lba, n);
}
