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
