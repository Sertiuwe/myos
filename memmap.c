/* ============================================================
 * memmap.c — карта памяти и простой аллокатор
 * ============================================================ */
#include "memmap.h"
#include "print.h"

extern char _end;
extern char _stack_top;

#define HEAP_SIZE   (1024 * 1024)

static mem_region_t regions[] = {
    { "kernel", 0x00000000, 0,           MEM_TYPE_KERNEL },
    { "heap",   0,          0,           MEM_TYPE_HEAP },
    { "stack",  0,          0,           MEM_TYPE_STACK },
    { "mmio",   0x10000000, 0x10200000,  MEM_TYPE_MMIO },
};
#define NUM_REGIONS (sizeof(regions) / sizeof(regions[0]))

static uint8_t *g_heap_base  = 0;
static uint8_t *g_heap_ptr   = 0;
static uint8_t *g_heap_limit = 0;

void memmap_init(void) {
    uint32_t kend = (uint32_t)&_end;
    kend = (kend + 0xFFFu) & ~0xFFFu;

    g_heap_base  = (uint8_t*)kend;
    g_heap_ptr   = g_heap_base;
    g_heap_limit = g_heap_base + HEAP_SIZE;

    uint32_t stack_end = (uint32_t)&_stack_top;
    if ((uint32_t)g_heap_limit > stack_end) {
        g_heap_limit = (uint8_t*)stack_end;
    }

    regions[0].end   = kend;
    regions[1].start = kend;
    regions[1].end   = (uint32_t)g_heap_limit;
    regions[2].start = (uint32_t)g_heap_limit;
    regions[2].end   = stack_end;
}

const mem_region_t *memmap_get(int *count) {
    if (count) *count = (int)NUM_REGIONS;
    return regions;
}

/* Прямой вывод без printf (puthex/putuint) */
static void print_region(const mem_region_t *r) {
    puts("  ");
    puts(r->name);
    int n = 0;
    while (r->name[n]) n++;
    for (int i = n; i < 7; i++) putc(' ');
    puts("  0x");
    puthex(r->start);
    puts(" - 0x");
    puthex(r->end);
    puts("   ");
    putuint((r->end - r->start) >> 10);
    puts(" KB\n");
}

void memmap_dump(void) {
    puts("Memory map:\n");
    for (unsigned i = 0; i < NUM_REGIONS; i++) {
        print_region(&regions[i]);
    }
    puts("\n");
    puts("Heap:\n");
    puts("  base  = 0x"); puthex((uint32_t)g_heap_base);  puts("\n");
    puts("  limit = 0x"); puthex((uint32_t)g_heap_limit); puts("\n");
    puts("  used  = ");   putuint(heap_used());          puts(" bytes\n");
    puts("  free  = ");   putuint(heap_free());          puts(" bytes\n");
    puts("  total = ");   putuint(heap_size());          puts(" bytes\n");
}

void *kmalloc(uint32_t size) {
    if (size == 0) return 0;
    size = (size + 7u) & ~7u;
    if (g_heap_ptr + size > g_heap_limit) return 0;
    void *p = g_heap_ptr;
    g_heap_ptr += size;
    return p;
}

void kfree(void *p) { (void)p; }

uint32_t heap_used(void)  { return (uint32_t)(g_heap_ptr - g_heap_base); }
uint32_t heap_free(void)  { return (uint32_t)(g_heap_limit - g_heap_ptr); }
uint32_t heap_size(void)  { return (uint32_t)(g_heap_limit - g_heap_base); }
void    *heap_start(void) { return g_heap_base; }
void    *heap_end(void)   { return g_heap_limit; }
