#include "shell.h"
#include "con.h"
#include "kbd.h"
#include "memory.h"
#include "memmap.h"
#include "kstring.h"
#include <stdint.h>

static char  *line;  static size_t len, cap;
static char **hist;  static size_t hlen, hcap;
static void  *slots[8]; static size_t slot_sz[8];

static void outb(uint16_t p, uint8_t v) {
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(p));
}

static void line_init(void) {
    cap = 64; len = 0;
    line = (char *)kmalloc(cap);
    line[0] = 0;
}
static void line_push(char c) {
    if (len + 2 > cap) { cap *= 2; line = (char *)krealloc(line, cap); }
    line[len++] = c; line[len] = 0;
}
static void line_pop(void) { if (len) { len--; line[len] = 0; } }

static void hist_add(const char *s) {
    if (!s[0]) return;
    if (hlen == hcap) {
        hcap = hcap ? hcap * 2 : 8;
        hist = (char **)krealloc(hist, hcap * sizeof(char *));
    }
    hist[hlen++] = kstrdup(s);
}

static uint64_t parse_u(const char *s) {
    uint64_t v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (uint64_t)(*s - '0'); s++; }
    return v;
}
static const char *skipsp(const char *s) { while (*s == ' ') s++; return s; }

static void cmd_help(void) {
    con_puts("commands: help clear heap mem echo <t> alloc <n> free <i> slots history reboot\n");
}
static void cmd_alloc(const char *a) {
    uint64_t n = parse_u(skipsp(a));
    if (!n) { con_puts("usage: alloc <bytes>\n"); return; }
    int i = -1;
    for (int k = 0; k < 8; k++) if (!slots[k]) { i = k; break; }
    if (i < 0) { con_puts("no free slot\n"); return; }
    void *p = kmalloc((size_t)n);
    if (!p) { con_puts("alloc failed\n"); return; }
    slots[i] = p; slot_sz[i] = (size_t)n;
    con_puts("slot "); con_put_u((uint64_t)i); con_puts(" = ");
    con_put_x((uint64_t)(uintptr_t)p); con_puts(" ("); con_put_u(n); con_puts(" B)\n");
}
static void cmd_free(const char *a) {
    uint64_t i = parse_u(skipsp(a));
    if (i >= 8 || !slots[i]) { con_puts("bad slot\n"); return; }
    kfree(slots[i]); slots[i] = NULL;
    con_puts("freed slot "); con_put_u(i); con_puts("\n");
}
static void cmd_slots(void) {
    for (int i = 0; i < 8; i++) if (slots[i]) {
        con_puts("  ["); con_put_u((uint64_t)i); con_puts("] ");
        con_put_x((uint64_t)(uintptr_t)slots[i]); con_puts(" ");
        con_put_u(slot_sz[i]); con_puts(" B\n");
    }
}

static void dispatch(char *cmd) {
    hist_add(cmd);
    char *sp = strchr(cmd, ' ');
    char *arg = sp ? sp + 1 : (char *)"";
    if (sp) *sp = 0;

    if (!strcmp(cmd, "help")) cmd_help();
    else if (!strcmp(cmd, "clear")) con_clear();
    else if (!strcmp(cmd, "heap")) kheap_dump();
    else if (!strcmp(cmd, "mem")) memmap_dump();
    else if (!strcmp(cmd, "echo")) { con_puts(arg); con_puts("\n"); }
    else if (!strcmp(cmd, "alloc")) cmd_alloc(arg);
    else if (!strcmp(cmd, "free")) cmd_free(arg);
    else if (!strcmp(cmd, "slots")) cmd_slots();
    else if (!strcmp(cmd, "history")) {
        for (size_t i = 0; i < hlen; i++) {
            con_put_u((uint64_t)i); con_puts(": "); con_puts(hist[i]); con_puts("\n");
        }
    }
    else if (!strcmp(cmd, "reboot")) { outb(0x64, 0xFE); for (;;) { } }
    else { con_puts("unknown: "); con_puts(cmd); con_puts("\n"); }
}

void shell_run(void) {
    line_init();
    con_puts("myos> ");
    for (;;) {
        char c = kbd_getchar();
        if (!c) { __asm__ volatile ("pause"); continue; }   /* IF=0, hlt усыпил бы навечно */
        if (c == '\n') {
            con_putc('\n'); con_flush();
            dispatch(line);
            len = 0; line[0] = 0;
            con_puts("myos> ");
        } else if (c == '\b') {
            if (len) { line_pop(); con_putc('\b'); con_flush(); }
        } else {
            line_push(c);
            con_putc(c); con_flush();
        }
    }
}
