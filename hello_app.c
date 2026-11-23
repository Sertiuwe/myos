#include "myos_app.h"

MYOS_APP_META("hello", 16384);

enum { I_KPRINT, I_TID, I_UPTIME, I_EXIT };
MYOS_IMPORTS(
    { "sl_kprint",    0, 0 },
    { "sl_tid",       0, 0 },
    { "sl_uptime_ms", 0, 0 },
    { "sl_exit",      0, 0 }
);

static void puts_app(const char *s) {
    app_call(myos_imports[I_KPRINT].sysnum, (uint64_t)(uintptr_t)s, 0, 0);
}

static void num_app(const char *pre, int64_t v, const char *post) {
    char b[96];
    int n = 0;
    for (const char *p = pre; *p && n < 80; p++) b[n++] = *p;
    char t[24]; int k = 0;
    if (v < 0) { b[n++] = '-'; v = -v; }
    do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v && k < 20);
    while (k > 0 && n < 80) b[n++] = t[--k];
    for (const char *p = post; *p && n < 90; p++) b[n++] = *p;
    b[n] = 0;
    puts_app(b);
}

void _start(void) {
    puts_app("\n[ELF] hello from a loaded .elf application\n");
    num_app("[ELF] my tid = ", app_call(myos_imports[I_TID].sysnum, 0, 0, 0), "\n");
    num_app("[ELF] uptime = ", app_call(myos_imports[I_UPTIME].sysnum, 0, 0, 0), " ms\n");
    puts_app("[ELF] imports were resolved by the kernel loader\n");
    app_call(myos_imports[I_EXIT].sysnum, 7, 0, 0);
    for (;;) { }
}
