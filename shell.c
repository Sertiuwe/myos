#include "shell.h"
#include "con.h"
#include "kbd.h"
#include "memory.h"
#include "memmap.h"
#include "kstring.h"
#include "kstdio.h"
#include "gfx.h"
#include "bdev.h"
#include "ata.h"
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
    con_puts("commands: help clear heap mem sysinfo atastat disk blk <d> <lba> [n] blktest [d] echo <t> alloc <n> free <i> slots history reboot\n");
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

static void hexdump(const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i += 16) {
        kprintf("%04x: ", (unsigned)i);
        for (int j = 0; j < 16; j++)
            if (i + (size_t)j < n) kprintf("%02x ", p[i + j]);
            else kprintf("   ");
        kprintf("|");
        for (int j = 0; j < 16 && i + (size_t)j < n; j++) {
            char c = (char)p[i + j];
            kprintf("%c", (c >= 32 && c < 127) ? c : '.');
        }
        kprintf("|\n");
    }
}

static void cmd_disk(void) {
    int n = bdev_count();
    if (!n) { kprintf("no block devices\n"); return; }
    for (int i = 0; i < n; i++) {
        bdev_t *b = bdev_by_index(i);
        kprintf("%s: %u sectors (%u MiB)\n", b->name,
                (unsigned)b->sectors, (unsigned)(b->sectors / 2048));
    }
}

static void cmd_blk(const char *arg) {
    char name[16]; size_t i = 0;
    const char *p = arg;
    while (p[i] && p[i] != ' ' && i < 15) { name[i] = p[i]; i++; }
    name[i] = 0;
    p += i; while (*p == ' ') p++;
    uint64_t lba = parse_u(p);
    while (*p && *p != ' ') p++; while (*p == ' ') p++;
    uint32_t cnt = *p ? (uint32_t)parse_u(p) : 1;
    if (cnt < 1) cnt = 1; if (cnt > 2) cnt = 2;
    bdev_t *b = bdev_by_name(name);
    if (!b) { kprintf("no such disk: %s\n", name); return; }
    uint8_t *buf = (uint8_t *)kmalloc((size_t)cnt * 512);
    if (!buf) { kprintf("oom\n"); return; }
    if (bdev_read(b, lba, cnt, buf)) { kprintf("read error\n"); kfree(buf); return; }
    for (uint32_t s = 0; s < cnt; s++) {
        kprintf("--- %s lba %u ---\n", name, (unsigned)(lba + s));
        hexdump(buf + (size_t)s * 512, 512);
    }
    kfree(buf);
}

#define BLKTEST_LBA 4096
static void cmd_blktest(const char *arg) {
    bdev_t *b = bdev_by_name(arg[0] ? arg : "ata1");
    if (!b) { kprintf("no such disk\n"); return; }
    uint8_t *w = (uint8_t *)kmalloc(512), *r = (uint8_t *)kmalloc(512);
    if (!w || !r) { kprintf("oom\n"); goto done; }
    for (int i = 0; i < 512; i++) w[i] = (uint8_t)(i * 7 + 3);
    if (bdev_write(b, BLKTEST_LBA, 1, w)) { kprintf("write error\n"); goto done; }
    if (bdev_read(b, BLKTEST_LBA, 1, r)) { kprintf("read error\n"); goto done; }
    int ok = (memcmp(w, r, 512) == 0);
    kprintf("blktest %s lba %u: %s\n", b->name, BLKTEST_LBA, ok ? "PASS" : "FAIL");
done:
    if (w) kfree(w);
    if (r) kfree(r);
}

static void cmd_atastat(void) {
    int n = ata_count();
    if (!n) { kprintf("no ATA devices\n"); return; }
    for (int i = 0; i < n; i++) {
        ata_info_t ai;
        if (ata_get_info(i, &ai) != 0) continue;
        kprintf("--- %s ---\n", ai.name);
        kprintf("  model : %s\n", ai.model);
        kprintf("  serial: %s\n", ai.serial);
        kprintf("  size  : %u sectors (%u MiB)\n",
                (unsigned)ai.sectors, (unsigned)(ai.sectors / 2048));
        kprintf("  reads : %u (%u KiB)\n",
                ai.reads, (unsigned)(ai.bytes_read / 1024));
        kprintf("  writes: %u (%u KiB)\n",
                ai.writes, (unsigned)(ai.bytes_written / 1024));
        kprintf("  errors: %u\n", ai.errors);
    }
}

static void cmd_sysinfo(void) {
    kheap_stats_t st;
    kheap_get_stats(&st);
    kprintf("ram usable  : %u MiB (%u entries)\n",
            (unsigned)(memmap_usable() / (1024 * 1024)), memmap_count());
    kprintf("heap        : used=%u free=%u live=%u peak=%u\n",
            (unsigned)st.used_bytes, (unsigned)st.free_bytes,
            st.live_blocks, (unsigned)st.peak_used);
    kprintf("framebuffer : %ux%u @ %p\n",
            gfx_width(), gfx_height(), gfx_backbuffer());
    kprintf("libk        : kprintf/kstring/kstdlib/kpanic online\n");
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
    else if (!strcmp(cmd, "atastat")) cmd_atastat();
    else if (!strcmp(cmd, "disk")) cmd_disk();
    else if (!strcmp(cmd, "blk")) cmd_blk(arg);
    else if (!strcmp(cmd, "blktest")) cmd_blktest(arg);
    else if (!strcmp(cmd, "sysinfo")) cmd_sysinfo();
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
