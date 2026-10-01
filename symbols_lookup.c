/* ============================================================
 * symbols_lookup.c — поиск символов
 * ============================================================ */
#include "symbols.h"
#include "print.h"

uint32_t sym_lookup(const char *name) {
    for (int i = 0; i < g_symbol_count; i++) {
        const volatile char *a = g_symbols[i].name;
        const volatile char *b = name;
        int eq = 1;
        for (int j = 0; j < 64; j++) {
            char ca = a[j];
            char cb = b[j];
            if (ca != cb) { eq = 0; break; }
            if (ca == 0) break;
        }
        if (eq) return g_symbols[i].addr;
    }
    return 0;
}

const char *sym_by_addr(uint32_t addr, uint32_t *offset) {
    const char *best = 0;
    uint32_t best_addr = 0;
    for (int i = 0; i < g_symbol_count; i++) {
        uint32_t a = g_symbols[i].addr;
        if (a <= addr && a >= best_addr) {
            best = g_symbols[i].name;
            best_addr = a;
        }
    }
    if (best && offset) *offset = addr - best_addr;
    return best;
}

void sym_dump_all(void) {
    puts("Symbols: ");
    putuint((uint32_t)g_symbol_count);
    puts("\n");
    for (int i = 0; i < g_symbol_count; i++) {
        puts("  0x");
        puthex(g_symbols[i].addr);
        puts("  ");
        puts(g_symbols[i].name);
        puts("\n");
    }
}

void sym_dump_range(uint32_t start, uint32_t end) {
    for (int i = 0; i < g_symbol_count; i++) {
        uint32_t a = g_symbols[i].addr;
        if (a >= start && a < end) {
            puts("  0x");
            puthex(a);
            puts("  ");
            puts(g_symbols[i].name);
            puts("\n");
        }
    }
}
