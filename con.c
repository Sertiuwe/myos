#include "con.h"
#include "gfx.h"
#include "kstring.h"

static uint32_t rx0, ry0, rx1, ry1;
static uint32_t cx, cy;
static uint32_t fg = 0xFFFFFF;
static uint32_t d0, d1;

static void dirty(uint32_t y) {
    if (y < d0) d0 = y;
    if (y + 16 > d1) d1 = y + 16;
}

static void flush_dirty(void) {
    if (d1 > ry1) d1 = ry1;
    if (d0 < d1) gfx_flush_rows(d0, d1);
    d0 = ry1; d1 = ry0;
}

void con_set_color(uint32_t c) { fg = c; }

void con_init(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1) {
    rx0 = x0; ry0 = y0; rx1 = x1; ry1 = y1;
    con_clear();
}

void con_clear(void) {
    uint32_t *bb = gfx_backbuffer();
    uint32_t w = gfx_width();
    for (uint32_t y = ry0; y < ry1; y++)
        for (uint32_t x = rx0; x < rx1; x++)
            bb[y * w + x] = 0x1E1E1E;
    cx = rx0; cy = ry0;
    d0 = ry0; d1 = ry1;
    flush_dirty();
}

static void scroll(void) {
    uint32_t *bb = gfx_backbuffer();
    uint32_t w = gfx_width();
    uint32_t h = ry1 - ry0;
    memmove(bb + ry0 * w, bb + (ry0 + 16) * w, (h - 16) * w * sizeof(uint32_t));
    for (uint32_t y = ry1 - 16; y < ry1; y++)
        for (uint32_t x = 0; x < w; x++)
            bb[y * w + x] = 0x1E1E1E;
    d0 = ry0; d1 = ry1;
}

void con_putc(char c) {
    if (c == '\n') { cx = rx0; cy += 16; }
    else if (c == '\r') { cx = rx0; }
    else if (c == '\t') { cx = rx0 + ((cx - rx0) / 64 + 1) * 64; }
    else if (c == '\b') {
        if (cx > rx0) {
            cx -= 8;
            gfx_draw_char(' ', (int)cx, (int)cy, fg);
            dirty(cy);
        }
    }
    else {
        gfx_draw_char(c, (int)cx, (int)cy, fg);
        cx += 8;
        dirty(cy);
    }
    if (cx + 8 > rx1) { cx = rx0; cy += 16; }
    if (cy + 16 > ry1) { scroll(); cy = ry1 - 16; }
}

void con_flush(void) { flush_dirty(); }

void con_puts(const char *s) {
    while (*s) con_putc(*s++);
    flush_dirty();
}

void con_put_u(uint64_t v) {
    char t[24]; int i = 0;
    if (!v) { con_putc('0'); flush_dirty(); return; }
    while (v) { t[i++] = (char)('0' + v % 10); v /= 10; }
    while (i) con_putc(t[--i]);
    flush_dirty();
}

void con_put_i(int64_t v) {
    if (v < 0) { con_putc('-'); con_put_u((uint64_t)(-v)); }
    else con_put_u((uint64_t)v);
}

void con_put_x(uint64_t v) {
    static const char *d = "0123456789ABCDEF";
    con_puts("0x");
    for (int i = 60; i >= 0; i -= 4) con_putc(d[(v >> i) & 0xF]);
    flush_dirty();
}
