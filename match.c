/* ============================================================
 * match.c — строковые утилиты и pattern matching
 * ============================================================ */
#include "match.h"

int m_strlen(const char *s) {
    const char *p = s;
    while (*p) p++;
    return (int)(p - s);
}

int m_streq(const char *a, const char *b) {
    const volatile char *va = a;
    const volatile char *vb = b;
    for (int i = 0; i < 64; i++) {
        if (va[i] != vb[i]) return 0;
        if (va[i] == 0) return 1;
    }
    return 0;
}

char *m_strcpy(char *d, const char *s) {
    char *r = d;
    while ((*d++ = *s++) != 0);
    return r;
}

char *m_strcat(char *d, const char *s) {
    char *r = d;
    while (*d) d++;
    while ((*d++ = *s++) != 0);
    return r;
}

int m_startswith(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s != *prefix) return 0;
        s++; prefix++;
    }
    return 1;
}

int m_endswith(const char *s, const char *suffix) {
    int sl = m_strlen(s);
    int xl = m_strlen(suffix);
    if (xl > sl) return 0;
    return m_streq(s + sl - xl, suffix);
}

int m_contains(const char *s, const char *needle) {
    if (!*needle) return 1;
    while (*s) {
        const char *a = s;
        const char *b = needle;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*b) return 1;
        s++;
    }
    return 0;
}

/* Простой wildcard match: '*' — любая последовательность, '?' — один символ */
int m_match(const char *s, const char *p) {
    while (*p) {
        if (*p == '*') {
            p++;
            if (!*p) return 1;
            while (*s) {
                if (m_match(s, p)) return 1;
                s++;
            }
            return 0;
        }
        if (*p == '?') {
            if (!*s) return 0;
            s++; p++;
            continue;
        }
        if (*p != *s) return 0;
        s++; p++;
    }
    return *s == 0;
}

int m_isdigit(char c) { return c >= '0' && c <= '9'; }
int m_isalpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int m_isspace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
int m_tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
int m_toupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

int m_atoi(const char *s) {
    int neg = 0, v = 0;
    while (m_isspace(*s)) s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    while (m_isdigit(*s)) {
        v = v * 10 + (*s - '0');
        s++;
    }
    return neg ? -v : v;
}

int m_parse_hex(const char *s, uint32_t *out) {
    uint32_t v = 0;
    int any = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    while (*s) {
        char c = *s++;
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        v = (v << 4) | d;
        any = 1;
    }
    if (!any) return 0;
    *out = v;
    return 1;
}

int m_count_words(const char *s) {
    int n = 0;
    while (*s) {
        while (*s && m_isspace(*s)) s++;
        if (*s) n++;
        while (*s && !m_isspace(*s)) s++;
    }
    return n;
}

char *m_next_word(char **pp) {
    char *s = *pp;
    while (*s && m_isspace(*s)) s++;
    if (!*s) { *pp = s; return 0; }
    char *start = s;
    while (*s && !m_isspace(*s)) s++;
    if (*s) *s++ = 0;
    *pp = s;
    return start;
}
