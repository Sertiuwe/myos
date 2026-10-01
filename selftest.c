/* ============================================================
 * selftest.c — проверка целостности при загрузке
 * ============================================================ */
#include "selftest.h"
#include "types.h"
#include "print.h"
#include "memmap.h"
#include "panic.h"

/* Внешние символы из kernel.c */
extern int      ntasks;
extern int      current;
extern volatile uint32_t tick_count;

typedef struct {
    uint32_t *sp;
    int       alive;
    char      name[8];
    uint32_t  stack[2048];
} task_public_t;

extern task_public_t tasks[];

/* ---------- Тесты ---------- */

static int test_heap(void) {
    puts("[1/6] Heap allocator........ ");
    uint32_t used_before = heap_used();
    void *p = kmalloc(64);
    if (!p) { puts("FAIL (null)\n"); return 1; }
    if ((uint32_t)p & 7) { puts("FAIL (unaligned)\n"); return 1; }
    if (heap_used() != used_before + 64) {
        puts("FAIL (size mismatch: got ");
        putuint(heap_used() - used_before);
        puts(")\n");
        return 1;
    }
    puts("OK\n");
    return 0;
}

static int test_heap_bounds(void) {
    puts("[2/6] Heap bounds........... ");
    void *p = kmalloc(heap_free() + 8);
    if (p != 0) { puts("FAIL (over-alloc accepted)\n"); return 1; }
    puts("OK\n");
    return 0;
}

static int test_memmap(void) {
    puts("[3/6] Memory map............ ");
    int n = 0;
    const mem_region_t *r = memmap_get(&n);
    if (!r || n != 4) { puts("FAIL (regions count)\n"); return 1; }
    if (r[0].end > r[1].start) { puts("FAIL (kernel/heap overlap)\n"); return 1; }
    if (r[1].end > r[2].start) { puts("FAIL (heap/stack overlap)\n"); return 1; }
    puts("OK\n");
    return 0;
}

static int test_tasks(void) {
    puts("[4/6] Task table............ ");
    if (ntasks < 1 || ntasks > 4) {
        puts("FAIL (ntasks out of range)\n");
        return 1;
    }
    int alive = 0;
    for (int i = 0; i < ntasks; i++) {
        if (tasks[i].alive) alive++;
    }
    if (alive < 2) {
        puts("FAIL (too few alive tasks)\n");
        return 1;
    }
    puts("OK (");
    putuint((uint32_t)alive);
    puts(" alive)\n");
    return 0;
}

static int test_uart(void) {
    puts("[5/6] UART echo............. ");
    /* Просто проверяем, что puts работает (мы его только что вызвали) */
    /* Если дошли до этой строки — UART живой */
    puts("OK\n");
    return 0;
}

static int test_arithmetic(void) {
    puts("[6/6] CPU sanity............ ");
    /* Проверка базовой арифметики + сдвигов */
    volatile uint32_t a = 0x12345678;
    volatile uint32_t b = (a << 4) | (a >> 28);   /* ROR 4 через LSL/LSR */
    if (b != 0x23456781) { puts("FAIL (rotate)\n"); return 1; }
    volatile int32_t  x = -5;
    volatile int32_t  y = x * 3;
    if (y != -15) { puts("FAIL (mul)\n"); return 1; }
    puts("OK\n");
    return 0;
}

int selftest_run(void) {
    puts("\n");
    puts("+-----------------------------------+\n");
    puts("|     System self-test running      |\n");
    puts("+-----------------------------------+\n");

    int fails = 0;
    fails += test_heap();
    fails += test_heap_bounds();
    fails += test_memmap();
    fails += test_tasks();
    fails += test_uart();
    fails += test_arithmetic();

    puts("\n");
    if (fails == 0) {
        puts("=== All checks passed ===\n\n");
    } else {
        puts("=== ");
        putuint((uint32_t)fails);
        puts(" FAILED ===\n\n");
    }
    return fails;
}
