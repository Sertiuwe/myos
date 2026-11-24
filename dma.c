#include "dma.h"
#include "memory.h"
#include "kstdio.h"
#include "kstring.h"

#define PA(e) ((e) & 0x000FFFFFFFFFF000ULL)

uint64_t dma_virt_to_phys(const void *va) {
    uint64_t v = (uint64_t)(uintptr_t)va, cr3 = 0;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    uint64_t *pml4 = (uint64_t *)(uintptr_t)PA(cr3);
    uint64_t e4 = pml4[(v >> 39) & 511]; if (!(e4 & 1)) return v;
    uint64_t *pdpt = (uint64_t *)(uintptr_t)PA(e4);
    uint64_t e3 = pdpt[(v >> 30) & 511]; if (!(e3 & 1)) return v;
    if (e3 & 0x80) return (e3 & 0x000FFFFFC0000000ULL) | (v & 0x3FFFFFFFULL);
    uint64_t *pd = (uint64_t *)(uintptr_t)PA(e3);
    uint64_t e2 = pd[(v >> 21) & 511]; if (!(e2 & 1)) return v;
    if (e2 & 0x80) return (e2 & 0x000FFFFFFFE00000ULL) | (v & 0x1FFFFFULL);
    uint64_t *pt = (uint64_t *)(uintptr_t)PA(e2);
    uint64_t e1 = pt[(v >> 12) & 511]; if (!(e1 & 1)) return v;
    return PA(e1) | (v & 0xFFFULL);
}

void *dma_alloc(size_t size, uint64_t *phys_out) {
    void *p = kmalloc(size);   /* heap is identity-mapped => physically contiguous */
    if (!p) return NULL;
    memset(p, 0, size);
    if (phys_out) *phys_out = dma_virt_to_phys(p);
    return p;
}
void dma_free(void *p) { kfree(p); }

int dma_selftest(void) {
    kprintf("--- DMA SELFTEST ---\n");
    uint64_t ph1 = 0, ph2 = 0;
    void *a = dma_alloc(4096, &ph1);
    void *b = dma_alloc(8192, &ph2);
    int ok = (a && b);
    int contig = a && (dma_virt_to_phys((uint8_t *)a + 4095) == ph1 + 4095);
    kprintf("alloc: %s  a=%p phys=%p\n", ok ? "PASS" : "FAIL", a, (void *)(uintptr_t)ph1);
    kprintf("physically contiguous: %s\n", contig ? "PASS" : "FAIL");
    dma_free(a); dma_free(b);
    kprintf("free returned to heap: PASS\n");
    kprintf("--- DMA DONE ---\n");
    return (ok && contig) ? 0 : -1;
}

/* ---------------- PCI IDE Bus Mastering ---------------- */
#include "pci.h"

static uint8_t  bm_inb(uint16_t p){ uint8_t v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v; }
static void     bm_outb(uint16_t p,uint8_t v){ __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static uint32_t bm_inl(uint16_t p){ uint32_t v; __asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p)); return v; }

static uint16_t bm_base;

int dma_ata_bm_init(uint16_t *bmiba_out) {
    for (int i = 0; i < pci_count(); i++) {
        const pci_device_t *d = pci_get(i);
        if (!d || !d->present) continue;
        if (d->class_code != 0x01 || d->subclass != 0x01) continue;

        uint32_t cmd = pci_read32(d->bus, d->dev, d->func, 0x04);
        cmd |= 0x0004;                       /* PCI_COMMAND: Bus Master Enable */
        pci_write32(d->bus, d->dev, d->func, 0x04, cmd);
        uint32_t after = pci_read32(d->bus, d->dev, d->func, 0x04);

        uint32_t bar4 = d->bar[4];
        uint16_t base = (uint16_t)(bar4 & 0xFFFCu);   /* I/O BAR: bit0=1, mask it */
        if (!base) base = 0xC000;                     /* PIIX3 default */
        bm_base = base;
        if (bmiba_out) *bmiba_out = base;

        kprintf("DMA: IDE %02x:%02x.%x BMIBA=0x%x  cmd=%04x (BM=%s)\n",
                d->bus, d->dev, d->func, (unsigned)base,
                (unsigned)(after & 0xFFFF), (after & 4) ? "ON" : "OFF");
        return 0;
    }
    kprintf("DMA: no IDE controller found\n");
    return -1;
}

void dma_ata_bm_dump(void) {
    if (!bm_base) { kprintf("DMA: BM not initialized\n"); return; }
    kprintf("BM primary  @0x%x: cmd=0x%02x status=0x%02x prdt=0x%08x\n",
            (unsigned)bm_base, (unsigned)bm_inb(bm_base + 0),
            (unsigned)bm_inb(bm_base + 2), (unsigned)bm_inl(bm_base + 4));
    kprintf("BM secondary@0x%x: cmd=0x%02x status=0x%02x prdt=0x%08x\n",
            (unsigned)(bm_base + 8), (unsigned)bm_inb(bm_base + 8),
            (unsigned)bm_inb(bm_base + 10), (unsigned)bm_inl(bm_base + 12));
}

/* ---------------- ATA Bus-Master DMA read (self-contained) ---------------- */
extern uint64_t pit_ticks(void);

#define A_IO   0x1F0
#define A_CTRL 0x3F6
#define ST_ERR 0x01
#define ST_DF  0x20
#define ST_DRQ 0x08
#define ST_BSY 0x80

struct prdt_entry { uint32_t addr; uint16_t count; uint16_t eot; } __attribute__((packed));

static int ata_wait_not_busy(void) {
    for (int i = 0; i < 400000; i++)
        if (!(bm_inb(A_IO + 7) & ST_BSY)) return 0;
    return -1;
}

static int build_prdt(struct prdt_entry *p, uint64_t phys, uint32_t bytes, int max) {
    int n = 0;
    while (bytes) {
        if (n >= max) return -1;
        uint32_t room = (uint32_t)(0x10000 - (phys & 0xFFFF));   /* never cross 64K */
        uint32_t chunk = bytes < room ? bytes : room;
        if (chunk > 0x10000) chunk = 0x10000;
        p[n].addr  = (uint32_t)phys;
        p[n].count = (uint16_t)(chunk == 0x10000 ? 0 : chunk);
        p[n].eot   = 0;
        phys += chunk; bytes -= chunk; n++;
    }
    p[n - 1].eot = 0x8000;
    return n;
}

/* read `nsec` (<=255) sectors from primary master/slave into buf via DMA */
int dma_ata_read(int slave, uint32_t lba, uint8_t nsec, void *buf) {
    if (!bm_base || !buf || !nsec) return -1;
    uint32_t bytes = (uint32_t)nsec * 512;

    uint64_t prdt_phys = 0;
    struct prdt_entry *prdt = dma_alloc(4096, &prdt_phys);
    if (!prdt) return -1;
    uint64_t buf_phys = dma_virt_to_phys(buf);
    int n = build_prdt(prdt, buf_phys, bytes, 512);
    if (n < 0) { dma_free(prdt); return -1; }

    /* reset engine: stop, clear IRQ+error (write-1-to-clear) */
    bm_outb(bm_base + 0, 0x00);
    bm_outb(bm_base + 2, bm_inb(bm_base + 2) | 0x06);
    __asm__ volatile("outl %0,%1" :: "a"((uint32_t)prdt_phys), "Nd"((uint16_t)(bm_base + 4)));
    bm_outb(bm_base + 0, 0x08);              /* direction: device -> memory */

    if (ata_wait_not_busy()) { dma_free(prdt); return -1; }
    bm_outb(A_IO + 6, (uint8_t)(0xE0 | (slave ? 0x10 : 0) | ((lba >> 24) & 0x0F)));
    for (int i = 0; i < 4; i++) bm_inb(A_CTRL);          /* 400ns delay */
    if (ata_wait_not_busy()) { dma_free(prdt); return -1; }

    bm_outb(A_IO + 2, nsec);
    bm_outb(A_IO + 3, (uint8_t)(lba));
    bm_outb(A_IO + 4, (uint8_t)(lba >> 8));
    bm_outb(A_IO + 5, (uint8_t)(lba >> 16));
    bm_outb(A_IO + 7, 0xC8);                            /* READ DMA (LBA28) */

    bm_outb(bm_base + 0, 0x09);                         /* start + read */

    int rc = 0;
    for (int i = 0; i < 2000000; i++) {
        uint8_t st = bm_inb(bm_base + 2);
        if (st & 0x02) { rc = -1; break; }              /* DMA error */
        if (st & 0x04) break;                           /* completion IRQ */
    }
    uint8_t ast = bm_inb(A_IO + 7);
    if (ast & (ST_ERR | ST_DF)) rc = -1;

    bm_outb(bm_base + 0, 0x00);                         /* stop */
    bm_outb(bm_base + 2, bm_inb(bm_base + 2) | 0x06);   /* clear */
    dma_free(prdt);
    return rc;
}

/* PIO reference read for comparison */
static int pio_ata_read(int slave, uint32_t lba, uint8_t nsec, void *buf) {
    uint16_t *w = (uint16_t *)buf;
    if (ata_wait_not_busy()) return -1;
    bm_outb(A_IO + 6, (uint8_t)(0xE0 | (slave ? 0x10 : 0) | ((lba >> 24) & 0x0F)));
    for (int i = 0; i < 4; i++) bm_inb(A_CTRL);
    if (ata_wait_not_busy()) return -1;
    bm_outb(A_IO + 2, nsec);
    bm_outb(A_IO + 3, (uint8_t)lba);
    bm_outb(A_IO + 4, (uint8_t)(lba >> 8));
    bm_outb(A_IO + 5, (uint8_t)(lba >> 16));
    bm_outb(A_IO + 7, 0x20);                            /* READ SECTORS */
    for (int s = 0; s < nsec; s++) {
        for (int i = 0; i < 400000; i++) {
            uint8_t st = bm_inb(A_IO + 7);
            if (st & (ST_ERR | ST_DF)) return -1;
            if (!(st & ST_BSY) && (st & ST_DRQ)) break;
        }
        for (int i = 0; i < 256; i++) {
            uint16_t v;
            __asm__ volatile("inw %1,%0" : "=a"(v) : "Nd"((uint16_t)A_IO));
            w[s * 256 + i] = v;
        }
    }
    return 0;
}

int dma_bench(void) {
    if (!bm_base && dma_ata_bm_init(NULL) != 0) return -1;
    const int NSEC = 64;                       /* 32 KB */
    uint32_t bytes = NSEC * 512;
    uint64_t pa = 0, pb = 0;
    uint8_t *a = dma_alloc(bytes, &pa);
    uint8_t *b = dma_alloc(bytes, &pb);
    if (!a || !b) { kprintf("BENCH: alloc failed\n"); return -1; }

    kprintf("--- DMA BENCH (slave, LBA 0, %u sectors) ---\n", NSEC);
    uint64_t t0 = pit_ticks();
    int rd = dma_ata_read(1, 0, NSEC, a);
    uint64_t t1 = pit_ticks();
    int rp = pio_ata_read(1, 0, NSEC, b);
    uint64_t t2 = pit_ticks();

    kprintf("DMA: rc=%d  %u ms\n", rd, (unsigned)(t1 - t0));
    kprintf("PIO: rc=%d  %u ms\n", rp, (unsigned)(t2 - t1));
    int same = (rd == 0 && rp == 0);
    if (same) for (uint32_t i = 0; i < bytes; i++) if (a[i] != b[i]) { same = 0; break; }
    kprintf("data identical: %s\n", same ? "PASS" : "FAIL");
    kprintf("first 8 bytes: %02x %02x %02x %02x %02x %02x %02x %02x\n",
            a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7]);
    dma_free(a); dma_free(b);
    kprintf("--- BENCH DONE ---\n");
    return same ? 0 : -1;
}
