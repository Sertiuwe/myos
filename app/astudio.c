/* astudio: unified Ring 3 user-space library for XNL ELF applications.
   Pure user code; kernel services only via exported sl_* imports. */
#include "astudio.h"
#include <stdarg.h>

#include "myos_app.h"

/* astudio owns the import table for the whole application ELF */
enum { AS_I_KPRINT, AS_I_TID, AS_I_UPTIME, AS_I_EXIT,
       AS_I_GA_OPEN, AS_I_GA_CMD, AS_I_GA_EVENT, AS_I_GA_PRESENT, AS_I_GA_CLOSE };
MYOS_IMPORTS(
    { "sl_kprint",    0, 0 },
    { "sl_tid",       0, 0 },
    { "sl_uptime_ms", 0, 0 },
    { "sl_exit",      0, 0 },
    { "sl_ga_open",   0, 0 },
    { "sl_ga_cmd",    0, 0 },
    { "sl_ga_event",  0, 0 },
    { "sl_ga_present",0, 0 },
    { "sl_ga_close",  0, 0 }
);

static void     sl_kprint(const char *s) { app_call(myos_imports[AS_I_KPRINT].sysnum, (uint64_t)(uintptr_t)s, 0, 0); }
static int      sl_tid(void)             { return (int)app_call(myos_imports[AS_I_TID].sysnum, 0, 0, 0); }
static uint64_t sl_uptime_ms(void)       { return (uint64_t)app_call(myos_imports[AS_I_UPTIME].sysnum, 0, 0, 0); }
static void     sl_exit(int c)           { app_call(myos_imports[AS_I_EXIT].sysnum, (uint64_t)c, 0, 0); for (;;) { } }

/* ---- memory ---- */
void *as_memcpy(void *d, const void *s, size_t n) {
    uint8_t *dd = d; const uint8_t *ss = s;
    while (n--) *dd++ = *ss++;
    return d;
}
void *as_memset(void *d, int c, size_t n) {
    uint8_t *dd = d; while (n--) *dd++ = (uint8_t)c; return d;
}
int as_memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *x = a, *y = b;
    while (n--) { if (*x != *y) return (int)*x - (int)*y; x++; y++; }
    return 0;
}
void *as_memmove(void *d, const void *s, size_t n) {
    uint8_t *dd = d; const uint8_t *ss = s;
    if (dd < ss) { while (n--) *dd++ = *ss++; }
    else { dd += n; ss += n; while (n--) *--dd = *--ss; }
    return d;
}

/* ---- string ---- */
size_t as_strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
int as_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}
int as_strncmp(const char *a, const char *b, size_t n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    return n ? ((int)(uint8_t)*a - (int)(uint8_t)*b) : 0;
}
char *as_strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)) { } return r; }
char *as_strncpy(char *d, const char *s, size_t n) {
    char *r = d;
    while (n && (*d = *s)) { d++; s++; n--; }
    while (n--) *d++ = 0;
    return r;
}
char *as_strcat(char *d, const char *s) { char *r = d; d += as_strlen(d); as_strcpy(d, s); return r; }
char *as_strchr(const char *s, int c) { while (*s && *s != c) s++; return (*s == c) ? (char *)s : 0; }
char *as_strstr(const char *h, const char *n) {
    size_t nl = as_strlen(n);
    if (!nl) return (char *)h;
    for (; *h; h++) if (!as_strncmp(h, n, nl)) return (char *)h;
    return 0;
}

/* ---- ctype ---- */
int as_isdigit(int c) { return c >= '0' && c <= '9'; }
int as_isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int as_isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
int as_toupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
int as_tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

/* ---- numbers ---- */
int as_abs(int v) { return v < 0 ? -v : v; }
int as_min(int a, int b) { return a < b ? a : b; }
int as_max(int a, int b) { return a > b ? a : b; }
int as_clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
int32_t as_sqrt_i(int32_t v) {
    if (v < 0) return -1;
    int32_t r = 0, bit = 1 << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}
char *as_itoa(int v, char *buf, int base) {
    char tmp[24]; int i = 0, neg = 0;
    unsigned u = (unsigned)v;
    if (base == 10 && v < 0) { neg = 1; u = (unsigned)(-v); }
    do { int d = (int)(u % (unsigned)base); tmp[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); u /= (unsigned)base; } while (u);
    if (neg) tmp[i++] = '-';
    int j = 0; while (i) buf[j++] = tmp[--i];
    buf[j] = 0;
    return buf;
}

/* ---- random ---- */
static uint32_t rng = 1;
void as_srand(uint32_t s) { rng = s ? s : 1; }
uint32_t as_rand(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
uint32_t as_rand_range(uint32_t lo, uint32_t hi) {
    if (hi < lo) { uint32_t t = lo; lo = hi; hi = t; }
    return lo + (as_rand() % (hi - lo + 1));
}

/* ---- formatted output ---- */
typedef struct { char *p; size_t pos, cap; } out_t;
static void oc(out_t *o, char c) { if (o->pos + 1 < o->cap) o->p[o->pos] = c; o->pos++; }
static void os_(out_t *o, const char *s) { while (*s) oc(o, *s++); }
static void num_str(out_t *o, unsigned long long v, int base, int neg, int width, int zeropad) {
    char t[28]; int i = 0;
    do { int d = (int)(v % (unsigned)base); t[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); v /= (unsigned)base; } while (v);
    if (neg) t[i++] = '-';
    int pad = width > i ? width - i : 0;
    if (zeropad && neg) { oc(o, '-'); i--; pad = width > i + 1 ? width - (i + 1) : 0; }
    for (int k = 0; k < pad; k++) oc(o, zeropad ? '0' : ' ');
    while (i) oc(o, t[--i]);
}
static int vspr(char *buf, size_t cap, const char *f, va_list ap) {
    out_t o = { buf, 0, cap };
    for (; *f; f++) {
        if (*f != '%') { oc(&o, *f); continue; }
        f++;
        int zeropad = 0, width = 0;
        if (*f == '0') { zeropad = 1; f++; }
        else if (*f == '-') { f++; }
        while (*f >= '0' && *f <= '9') { width = width * 10 + (*f - '0'); f++; }
        switch (*f) {
        case 's': { const char *t = va_arg(ap, const char *); int l = 0; while (t[l]) l++;
                    for (int k = l; k < width; k++) oc(&o, ' '); os_(&o, t); } break;
        case 'c': oc(&o, (char)va_arg(ap, int)); break;
        case 'd': { long long v = va_arg(ap, int);
                    num_str(&o, (unsigned long long)(v < 0 ? -v : v), 10, v < 0, width, zeropad); } break;
        case 'u': num_str(&o, va_arg(ap, unsigned), 10, 0, width, zeropad); break;
        case 'x': num_str(&o, va_arg(ap, unsigned), 16, 0, width, zeropad); break;
        case 'X': num_str(&o, va_arg(ap, unsigned), 16, 0, width, zeropad); break;
        case 'p': num_str(&o, (unsigned long long)(uintptr_t)va_arg(ap, void *), 16, 0, width, zeropad); break;
        case '%': oc(&o, '%'); break;
        default:  oc(&o, '%'); if (*f) oc(&o, *f); break;
        }
    }
    if (cap) buf[o.pos < cap ? o.pos : cap - 1] = 0;
    return (int)o.pos;
}
int as_sprintf(char *buf, size_t cap, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = vspr(buf, cap, fmt, ap);
    va_end(ap);
    return n;
}
int as_printf(const char *fmt, ...) {
    char buf[512];
    va_list ap; va_start(ap, fmt);
    int n = vspr(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    sl_kprint(buf);
    return n;
}
void as_hexdump(const void *p, size_t n) {
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i += 16) {
        as_printf("%04x  ", (unsigned)i);
        for (size_t j = 0; j < 16; j++) {
            if (i + j < n) as_printf("%02x ", b[i + j]); else as_printf("   ");
        }
        as_printf(" |");
        for (size_t j = 0; j < 16 && i + j < n; j++) {
            uint8_t c = b[i + j];
            as_printf("%c", (c >= 32 && c < 127) ? (int)c : '.');
        }
        as_printf("|\n");
    }
}

/* ---- time / process ---- */
uint64_t as_uptime_ms(void) { return sl_uptime_ms(); }
uint32_t as_uptime_sec(void) { return (uint32_t)(sl_uptime_ms() / 1000); }
void as_format_uptime(char *buf, size_t cap, uint64_t ms) {
    uint64_t s = ms / 1000;
    uint64_t h = s / 3600, m = (s / 60) % 60, ss = s % 60;
    as_sprintf(buf, cap, "%02u:%02u:%02u", (unsigned)h, (unsigned)m, (unsigned)ss);
}
int  as_tid(void) { return sl_tid(); }
void as_exit(int code) { sl_exit(code); }


/* ---- GA wrappers ---- */
static int ga_cmd(int h, const gar_cmd_t *c) {
    return (int)app_call(myos_imports[AS_I_GA_CMD].sysnum, (uint64_t)h, (uint64_t)(uintptr_t)c, 0);
}
int as_ga_open(const char *title, int32_t x, int32_t y, int32_t w, int32_t h) {
    gar_open_t o; as_memset(&o, 0, sizeof o);
    as_strncpy(o.title, title, sizeof(o.title) - 1);
    o.x = x; o.y = y; o.w = w; o.h = h;
    return (int)app_call(myos_imports[AS_I_GA_OPEN].sysnum, (uint64_t)(uintptr_t)&o, 0, 0);
}
void as_ga_fill(int h, int32_t x, int32_t y, int32_t w, int32_t hh, uint32_t col) {
    gar_cmd_t c; as_memset(&c,0,sizeof c); c.op=GAR_FILL; c.a=x;c.b=y;c.c=w;c.d=hh;c.col=col; ga_cmd(h,&c);
}
void as_ga_frame(int h, int32_t x, int32_t y, int32_t w, int32_t hh, uint16_t t, uint32_t col) {
    gar_cmd_t c; as_memset(&c,0,sizeof c); c.op=GAR_FRAME; c.a=x;c.b=y;c.c=w;c.d=hh;c.t=t;c.col=col; ga_cmd(h,&c);
}
void as_ga_line(int h, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t col) {
    gar_cmd_t c; as_memset(&c,0,sizeof c); c.op=GAR_LINE; c.a=x0;c.b=y0;c.c=x1;c.d=y1;c.col=col; ga_cmd(h,&c);
}
void as_ga_text(int h, int32_t x, int32_t y, const char *s, uint32_t col) {
    gar_cmd_t c; as_memset(&c,0,sizeof c); c.op=GAR_TEXT; c.a=x;c.b=y;c.col=col;
    as_strncpy(c.str, s, GAR_STR_MAX-1); ga_cmd(h,&c);
}
void as_ga_grad(int h, int32_t x, int32_t y, int32_t w, int32_t hh, uint32_t top, uint32_t bot) {
    gar_cmd_t c; as_memset(&c,0,sizeof c); c.op=GAR_GRAD; c.a=x;c.b=y;c.c=w;c.d=hh;c.col=top;c.col2=bot; ga_cmd(h,&c);
}
void as_ga_clear(int h) { gar_cmd_t c; as_memset(&c,0,sizeof c); c.op=GAR_CLEAR; ga_cmd(h,&c); }
int  as_ga_event(int h, gar_ev_t *ev) {
    return (int)app_call(myos_imports[AS_I_GA_EVENT].sysnum, (uint64_t)h, (uint64_t)(uintptr_t)ev, 0);
}
void as_ga_present(int h) { app_call(myos_imports[AS_I_GA_PRESENT].sysnum, (uint64_t)h, 0, 0); }
void as_ga_close(int h)   { app_call(myos_imports[AS_I_GA_CLOSE].sysnum, (uint64_t)h, 0, 0); }
