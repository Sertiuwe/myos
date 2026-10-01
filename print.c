/* ============================================================
 * print.c — библиотека форматированного вывода
 *   * Без libc, без stdarg.h — используем __builtin_va_*
 *   * Без операций деления — вычитание по разрядам
 * ============================================================ */
#include "print.h"

/* UART0 MMIO */
#define UART0_BASE   0x10009000u
#define UART_DR      (*(volatile uint32_t*)(UART0_BASE + 0x00))
#define UART_FR      (*(volatile uint32_t*)(UART0_BASE + 0x18))

void print_init(void) { /* no-op */ }

void putc(char c) {
    while (UART_FR & (1u << 5)) { }
    UART_DR = (uint32_t)(uint8_t)c;
}

void puts(const char *s) {
    while (*s) {
        if (*s == '\n') putc('\r');
        putc(*s++);
    }
}

static const uint32_t pow10[] = {
    1u, 10u, 100u, 1000u, 10000u, 100000u, 1000000u,
    10000000u, 100000000u, 1000000000u
};

void putuint(uint32_t v) {
    char buf[12];
    int pos = 0;
    if (v == 0) {
        buf[pos++] = '0';
    } else {
        int digits = 0;
        while (digits < 9 && v >= pow10[digits + 1]) digits++;
        while (digits >= 0) {
            uint32_t p = pow10[digits];
            int d = 0;
            while (v >= p) { v -= p; d++; }
            buf[pos++] = (char)('0' + d);
            digits--;
        }
    }
    buf[pos] = 0;
    puts(buf);
}

void putint(int32_t v) {
    if (v < 0) {
        putc('-');
        putuint((uint32_t)(-v));
    } else {
        putuint((uint32_t)v);
    }
}

void puthex(uint32_t v) {
    for (int i = 28; i >= 0; i -= 4) {
        int n = (v >> i) & 0xF;
        putc(n < 10 ? (char)('0' + n) : (char)('a' + n - 10));
    }
}

void puthex8(uint8_t v) {
    int hi = (v >> 4) & 0xF;
    int lo = v & 0xF;
    putc(hi < 10 ? (char)('0' + hi) : (char)('a' + hi - 10));
    putc(lo < 10 ? (char)('0' + lo) : (char)('a' + lo - 10));
}

void putbin(uint32_t v) {
    for (int i = 31; i >= 0; i--) {
        putc((v >> i) & 1 ? '1' : '0');
        if (i % 4 == 0 && i > 0) putc(' ');
    }
}

/* ============================================================
 * Простой printf с varargs
 * Поддерживает: %d %i %u %x %X %08x %c %s %p %%
 * ============================================================ */
static int vprint_args(const char *fmt, const uint32_t *args, char *out, int maxlen) {
    int count = 0;
    int truncated = 0;
    int argi = 0;

    #define OUT(c) do { \
        if (out) { if (count < maxlen - 1) out[count] = (c); else truncated = 1; } \
        else putc(c); \
        count++; \
    } while(0)

    #define NEXT_ARG() (args[argi++])

    while (*fmt) {
        if (*fmt != '%') {
            if (*fmt == '\n' && !out) OUT('\r');
            OUT(*fmt++);
            continue;
        }
        fmt++;

        int left_align = 0;
        int zero_pad = 0;
        int width = 0;
        if (*fmt == '-') { left_align = 1; fmt++; }
        if (*fmt == '0') { zero_pad = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        switch (*fmt) {
            case 'd':
            case 'i': {
                int32_t v = (int32_t)NEXT_ARG();
                int neg = 0;
                if (v < 0) { neg = 1; v = -v; }
                char tmp[12];
                int pos = 0;
                if (v == 0) tmp[pos++] = '0';
                else {
                    int digits = 0;
                    while (digits < 9 && (uint32_t)v >= pow10[digits + 1]) digits++;
                    while (digits >= 0) {
                        uint32_t p = pow10[digits];
                        int d = 0;
                        while ((uint32_t)v >= p) { v -= p; d++; }
                        tmp[pos++] = (char)('0' + d);
                        digits--;
                    }
                }
                int total = pos + (neg ? 1 : 0);
                if (!left_align && !zero_pad) {
                    for (int i = total; i < width; i++) OUT(' ');
                }
                if (neg) OUT('-');
                if (!left_align && zero_pad) {
                    for (int i = total; i < width; i++) OUT('0');
                }
                for (int i = 0; i < pos; i++) OUT(tmp[i]);
                if (left_align) {
                    for (int i = total; i < width; i++) OUT(' ');
                }
                break;
            }
            case 'u': {
                uint32_t v = NEXT_ARG();
                char tmp[12];
                int pos = 0;
                if (v == 0) tmp[pos++] = '0';
                else {
                    int digits = 0;
                    while (digits < 9 && v >= pow10[digits + 1]) digits++;
                    while (digits >= 0) {
                        uint32_t p = pow10[digits];
                        int d = 0;
                        while (v >= p) { v -= p; d++; }
                        tmp[pos++] = (char)('0' + d);
                        digits--;
                    }
                }
                if (!left_align) {
                    for (int i = pos; i < width; i++) OUT(' ');
                }
                for (int i = 0; i < pos; i++) OUT(tmp[i]);
                if (left_align) {
                    for (int i = pos; i < width; i++) OUT(' ');
                }
                break;
            }
            case 'x':
            case 'X': {
                uint32_t v = NEXT_ARG();
                char tmp[8];
                for (int i = 0; i < 8; i++) {
                    int n = (v >> (28 - i*4)) & 0xF;
                    tmp[i] = n < 10 ? (char)('0' + n) : (char)('a' + n - 10);
                }
                int start = 0;
                if (!zero_pad) {
                    while (start < 7 && tmp[start] == '0') start++;
                }
                int len = 8 - start;
                if (!left_align && !zero_pad) {
                    for (int i = len; i < width; i++) OUT(' ');
                }
                for (int i = start; i < 8; i++) OUT(tmp[i]);
                if (left_align) {
                    for (int i = len; i < width; i++) OUT(' ');
                }
                break;
            }
            case 'p': {
                uint32_t v = NEXT_ARG();
                OUT('0'); OUT('x');
                for (int i = 0; i < 8; i++) {
                    int n = (v >> (28 - i*4)) & 0xF;
                    OUT(n < 10 ? (char)('0' + n) : (char)('a' + n - 10));
                }
                break;
            }
            case 'c': {
                int c = (int)NEXT_ARG();
                OUT((char)c);
                break;
            }
            case 's': {
                const char *s = (const char *)(uint32_t)NEXT_ARG();
                if (!s) s = "(null)";
                int n = 0;
                while (s[n]) n++;
                if (!left_align) {
                    for (int i = n; i < width; i++) OUT(' ');
                }
                for (int i = 0; i < n; i++) OUT(s[i]);
                if (left_align) {
                    for (int i = n; i < width; i++) OUT(' ');
                }
                break;
            }
            case '%': OUT('%'); break;
            case 0:   OUT('%'); fmt--; break;
            default:  OUT('%'); OUT(*fmt); break;
        }
        fmt++;
    }

    if (out) {
        if (count < maxlen) out[count] = 0;
        else if (maxlen > 0) out[maxlen - 1] = 0;
    }

    #undef OUT
    #undef NEXT_ARG
    return count;
}

int snprintf(char *buf, int size, const char *fmt, ...) {
    if (!buf || size <= 0) return 0;
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    uint32_t args[8];
    for (int i = 0; i < 8; i++) args[i] = __builtin_va_arg(ap, uint32_t);
    __builtin_va_end(ap);
    return vprint_args(fmt, args, buf, size);
}

int printf(const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    uint32_t args[8];
    for (int i = 0; i < 8; i++) args[i] = __builtin_va_arg(ap, uint32_t);
    __builtin_va_end(ap);
    return vprint_args(fmt, args, 0, 0);
}
