#include "kstring.h"

/* Копии словами по 8 байт. x86_64 допускает невыровненный доступ,
   поэтому выравнивание не проверяем — это быстрее. */
void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n >= 8) {
        *(uint64_t *)d = *(const uint64_t *)s;
        d += 8; s += 8; n -= 8;
    }
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (d == s || n == 0) return dst;
    if (d < s) {
        while (n >= 8) {
            *(uint64_t *)d = *(const uint64_t *)s;
            d += 8; s += 8; n -= 8;
        }
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        while (n >= 8) {
            d -= 8; s -= 8; n -= 8;
            *(uint64_t *)d = *(const uint64_t *)s;
        }
        while (n--) *--d = *--s;
    }
    return dst;
}

void *memset(void *dst, int c, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    uint8_t b = (uint8_t)c;
    uint64_t w = 0;
    for (int i = 0; i < 8; i++) w |= (uint64_t)b << (i * 8);
    while (n >= 8) { *(uint64_t *)d = w; d += 8; n -= 8; }
    while (n--) *d++ = b;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    return 0;
}

void *memchr(const void *s, int c, size_t n) {
    const uint8_t *p = (const uint8_t *)s;
    uint8_t b = (uint8_t)c;
    for (size_t i = 0; i < n; i++)
        if (p[i] == b) return (void *)(p + i);
    return NULL;
}

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

size_t strnlen(const char *s, size_t max) {
    size_t n = 0;
    while (n < max && s[n]) n++;
    return n;
}

char *strcpy(char *d, const char *s) {
    char *r = d;
    while ((*d++ = *s++)) { }
    return r;
}

char *strncpy(char *d, const char *s, size_t n) {
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}

char *strcat(char *d, const char *s) {
    return strcpy(d + strlen(d), s);
}

char *strncat(char *d, const char *s, size_t n) {
    size_t len = strlen(d);
    size_t i = 0;
    for (; i < n && s[i]; i++) d[len + i] = s[i];
    d[len + i] = 0;
    return d;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)(uint8_t)a[i] - (int)(uint8_t)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}

char *strchr(const char *s, int c) {
    char b = (char)c;
    for (; *s; s++) if (*s == b) return (char *)s;
    return b == 0 ? (char *)s : NULL;
}

char *strrchr(const char *s, int c) {
    char b = (char)c;
    const char *last = NULL;
    for (; *s; s++) if (*s == b) last = s;
    if (b == 0) return (char *)s;
    return (char *)last;
}

char *strstr(const char *h, const char *n) {
    size_t nl = strlen(n);
    if (!nl) return (char *)h;
    for (; *h; h++)
        if (strncmp(h, n, nl) == 0) return (char *)h;
    return NULL;
}
