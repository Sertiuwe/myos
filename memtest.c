#include "memory.h"
#include "kstring.h"

static uint64_t rs = 0x12345678ABCDEFULL;
static uint64_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }

#define SLOTS 256
static void    *slot[SLOTS];
static uint32_t sz[SLOTS];
static uint8_t  tg[SLOTS];

#define RING 16
static char ring[RING][56];
static int ring_i;

static char *put_u(char *p, uint64_t v) {
    char t[24]; int i = 0;
    if (!v) { *p++ = '0'; return p; }
    while (v) { t[i++] = (char)('0' + v % 10); v /= 10; }
    while (i) *p++ = t[--i];
    return p;
}
static char *put_x(char *p, uint64_t v) {
    static const char *d = "0123456789ABCDEF";
    for (int i = 60; i >= 0; i -= 4) *p++ = d[(v >> i) & 0xF];
    return p;
}
static void ring_log(int iter, char op, int slot, uint32_t n, void *p) {
    char *q = ring[ring_i];
    q = put_u(q, (uint64_t)iter); *q++ = ' '; *q++ = op; *q++ = ' ';
    q = put_u(q, (uint64_t)slot); *q++ = ':';
    q = put_u(q, n); *q++ = '@';
    q = put_x(q, (uint64_t)(uintptr_t)p); *q = 0;
    ring_i = (ring_i + 1) % RING;
}
static void ring_dump(void) {
    kheap_report("--- last ops ---\n");
    for (int k = 0; k < RING; k++) {
        int i = (ring_i + k) % RING;
        if (ring[i][0]) { kheap_report(ring[i]); kheap_report("\n"); }
    }
}

static void result(const char *name, int pass) {
    char b[160];
    size_t i = 0;
    while (name[i] && i < 120) { b[i] = name[i]; i++; }
    const char *s = pass ? ": PASS\n" : ": FAIL\n";
    while (*s && i < 150) b[i++] = *s++;
    b[i] = 0;
    kheap_report(b);
}

void kheap_selftest(void) {
    kheap_stats_t s, base;
    kheap_report("\n--- KMALLOC SELFTEST ---\n");
    result("integrity initial", kheap_check() == 0);
    kheap_get_stats(&base);

    uint8_t *z = (uint8_t *)kcalloc(1000, 1);
    int pass = (z != NULL);
    if (pass) for (int i = 0; i < 1000; i++) if (z[i]) { pass = 0; break; }
    result("kcalloc zeroed", pass);
    kfree(z);

    pass = 1;
    for (int i = 0; i < 8; i++) {
        size_t al = (size_t)16 << i;
        void *p = kmalloc_aligned(al, 100);
        if (!p || ((uintptr_t)p & (al - 1))) pass = 0;
        else memset(p, 0x5A, 100);
        kfree(p);
    }
    result("kmalloc_aligned 16..2048", pass);

    char *r = (char *)kmalloc(64);
    pass = (r != NULL);
    if (pass) {
        for (int i = 0; i < 64; i++) r[i] = (char)(i + 1);
        r = (char *)krealloc(r, 5000);
        if (!r) pass = 0;
        else for (int i = 0; i < 64; i++) if (r[i] != (char)(i + 1)) { pass = 0; break; }
        if (pass) {
            r = (char *)krealloc(r, 32);
            if (!r) pass = 0;
            else for (int i = 0; i < 32; i++) if (r[i] != (char)(i + 1)) { pass = 0; break; }
        }
    }
    result("krealloc preserves data", pass);
    kfree(r);

    for (int i = 0; i < SLOTS; i++) { slot[i] = NULL; sz[i] = 0; tg[i] = 0; }
    for (int i = 0; i < RING; i++) ring[i][0] = 0;

    pass = 1;
    int bad_iter = -1;
    for (int iter = 0; iter < 8000 && pass; iter++) {
        int i = (int)(rnd() % SLOTS);
        if (slot[i]) {
            uint8_t *p = (uint8_t *)slot[i];
            for (uint32_t k = 0; k < sz[i]; k++)
                if (p[k] != tg[i]) { pass = 0; bad_iter = iter; break; }
            if (!pass) break;
            ring_log(iter, 'F', i, sz[i], slot[i]);
            kfree(slot[i]);
            slot[i] = NULL;
        } else {
            uint32_t n = (uint32_t)(rnd() % 4096) + 1;
            void *p = kmalloc(n);
            if (!p) { pass = 0; bad_iter = iter; break; }
            slot[i] = p; sz[i] = n; tg[i] = (uint8_t)(rnd() & 0xFF);
            memset(p, tg[i], n);
            ring_log(iter, 'A', i, n, p);
        }
        if (kheap_check() != 0) { pass = 0; bad_iter = iter; break; }
    }
    if (!pass) {
        char b[64]; char *q = b;
        q = put_u(q, (uint64_t)(bad_iter < 0 ? -bad_iter : bad_iter));
        *q++ = ':'; *q = 0;
        kheap_report("churn broke heap at iter ");
        kheap_report(b);
        kheap_report("\n");
        ring_dump();
        kheap_dump();
        for (;;) __asm__ volatile ("hlt");
    }
    result("8000 random ops + patterns", pass);
    for (int i = 0; i < SLOTS; i++) if (slot[i]) kfree(slot[i]);

    result("integrity after churn", kheap_check() == 0);

    kheap_get_stats(&s);
    result("heap fully coalesced",
           s.live_blocks == base.live_blocks &&
           s.free_blocks == 1 &&
           s.largest_free == s.free_bytes &&
           (s.used_bytes + s.free_bytes + s.overhead_bytes) == s.total_bytes);
    kheap_report("--- SELFTEST DONE ---\n");
}
