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
#include "rtc.h"
#include "bcache.h"
#include "myfs.h"
#include "pit.h"
#include "mouse.h"
#include "cursor.h"
#include "asset.h"
#include "graphic_api.h"
#include "app_demo.h"
#include "myfsapp.h"
#include "graphic_server.h"
#include "sched.h"
#include "term.h"
#include "config.h"
#include "pci.h"
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
    con_puts("commands: help mouse theme guitest gui files term ps lspci testfault uptime sleep <ms> ls cat touch write rm mkdir rename sync clear heap mem sysinfo date atastat bcstat disk blk blktest echo alloc free slots history reboot\n");
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
    int rc = 0;
    for (uint32_t s = 0; s < cnt && !rc; s++)
        rc = bcache_read(b, lba + s, buf + (size_t)s * 512);
    if (rc) { kprintf("read error\n"); kfree(buf); return; }
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
    if (bcache_write(b, BLKTEST_LBA, w)) { kprintf("write error\n"); goto done; }
    if (bcache_read(b, BLKTEST_LBA, r)) { kprintf("read error\n"); goto done; }
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

static void cmd_date(void) {
    rtc_time_t t;
    if (rtc_read(&t) != 0) { kprintf("RTC read failed\n"); return; }
    char buf[32];
    rtc_format(&t, buf, sizeof buf);
    kprintf("%s\n", buf);
}

static void cmd_bcstat(void) {
    uint32_t h, m, d, t;
    bcache_stats(&h, &m, &d, &t);
    kprintf("bcache: hits=%u misses=%u dirty=%u bufs=%u\n", h, m, d, t);
}

static int resolve_path_shell(const char *path, uint16_t *ino, uint8_t *type) {
    return myfs_resolve(path, ino, type);
}

static int myfs_stat_full(uint16_t ino, myfs_inode_t *out) {
    extern int myfs_read_inode_raw(uint16_t ino, myfs_inode_t *out);
    return myfs_read_inode_raw(ino, out);
}

static void format_time(uint32_t unix_ts, char *buf) {
    /* Простая конвертация unix timestamp → YYYY-MM-DD HH:MM */
    uint32_t s = unix_ts;
    uint32_t days = s / 86400; s %= 86400;
    uint32_t hours = s / 3600; s %= 3600;
    uint32_t mins = s / 60;
    /* Упрощённая дата без високосных */
    uint32_t y = 1970, m = 1, d = 1;
    while (days >= 365) { days -= 365; y++; }
    static const uint8_t mdays[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    for (m = 1; m <= 12 && days >= mdays[m-1]; m++) days -= mdays[m-1];
    d = days + 1;
    buf[0]='0'+y/1000; buf[1]='0'+(y/100)%10; buf[2]='0'+(y/10)%10; buf[3]='0'+y%10;
    buf[4]='-'; buf[5]='0'+m/10; buf[6]='0'+m%10;
    buf[7]='-'; buf[8]='0'+d/10; buf[9]='0'+d%10;
    buf[10]=' '; buf[11]='0'+hours/10; buf[12]='0'+hours%10;
    buf[13]=':'; buf[14]='0'+mins/10; buf[15]='0'+mins%10;
    buf[16]='\0';
}

static void cmd_ls(const char *arg) {
    const char *path = arg[0] ? arg : "/";
    uint16_t ino; uint8_t tp;
    if (resolve_path_shell(path, &ino, &tp)) { kprintf("not found: %s\n", path); return; }
    if (tp != 2) { kprintf("%s is not a directory\n", path); return; }
    char name[256]; uint16_t ei; uint8_t et;
    for (int i = 0; myfs_readdir(ino, i, name, sizeof name, &ei, &et) == 0; i++) {
        myfs_inode_t st;
        if (myfs_stat_full(ei, &st) == 0) {
            char tbuf[20]; format_time(st.mtime, tbuf);
            kprintf("%s %8u %s %s\n", et == MYFS_TYPE_DIR ? "DIR " : "FILE",
                    st.size, tbuf, name);
        } else {
            kprintf("%s %s\n", et == MYFS_TYPE_DIR ? "DIR " : "FILE", name);
        }
    }
}

static void cmd_cat(const char *arg) {
    uint16_t ino; uint8_t tp;
    if (resolve_path_shell(arg, &ino, &tp)) { kprintf("not found: %s\n", arg); return; }
    if (tp == 2) { kprintf("is a directory\n"); return; }
    char buf[512];
    int n;
    uint32_t off = 0;
    while ((n = myfs_read(ino, off, buf, sizeof buf - 1)) > 0) {
        buf[n] = '\0';
        con_puts(buf);
        off += (uint32_t)n;
    }
    con_puts("\n");
}

static void cmd_touch(const char *arg) {
    uint16_t ino;
    if (myfs_create(arg, 0100644, &ino) == 0)
        kprintf("created: %s (ino %u)\n", arg, ino);
    else
        kprintf("create failed: %s\n", arg);
}

static void cmd_write(const char *arg) {
    char path[128]; const char *text;
    const char *sp = strchr(arg, ' ');
    if (!sp) { kprintf("usage: write <path> <text>\n"); return; }
    size_t plen = (size_t)(sp - arg);
    if (plen >= sizeof path) plen = sizeof path - 1;
    memcpy(path, arg, plen); path[plen] = '\0';
    text = sp + 1;
    uint16_t ino; uint8_t tp;
    if (resolve_path_shell(path, &ino, &tp)) { kprintf("not found: %s\n", path); return; }
    int n = myfs_write(ino, 0, text, (uint32_t)strlen(text));
    kprintf("wrote %d bytes to %s\n", n, path);
}

static void cmd_rm(const char *arg) {
    if (myfs_unlink(arg) == 0) kprintf("removed: %s\n", arg);
    else kprintf("unlink failed: %s\n", arg);
}

/* helper: обёртка над resolve_path для шелла */
static void cmd_testfault(void) {
    kprintf("Triggering #DE (divide by zero)...\n");
    volatile int x = 1;
    volatile int y = 0;
    volatile int z = x / y;
    (void)z;
}

static void cmd_uptime(void) {
    uint64_t t = pit_ticks();
    uint32_t hz = pit_hz();
    uint64_t sec = t / hz;
    uint64_t ms = (t % hz) * 1000 / hz;
    kprintf("uptime: %u.%03u s (%u ticks @ %u Hz)\n",
            (unsigned)sec, (unsigned)ms, (unsigned)t, hz);
}

static void cmd_sleep(const char *arg) {
    uint32_t ms = (uint32_t)parse_u(arg);
    if (!ms) { kprintf("usage: sleep <ms>\n"); return; }
    kprintf("sleeping %u ms...\n", ms);
    pit_sleep_ms(ms);
    kprintf("done\n");
}

static void cmd_lspci(void) {
    int n = pci_count();
    if (!n) { kprintf("no PCI devices\n"); return; }
    for (int i = 0; i < n; i++) {
        const pci_device_t *d = pci_get(i);
        if (!d || !d->present) continue;
        kprintf("%02x:%02x.%x %04x:%04x class=%02x:%02x hdr=%x irq=%u",
                d->bus, d->dev, d->func, d->vendor_id, d->device_id,
                d->class_code, d->subclass, d->header_type, d->irq_line);
        if (d->header_type == 0) {
            kprintf("\n  BAR: ");
            for (int b = 0; b < 6; b++)
                if (d->bar[b]) kprintf("[%d]=0x%x ", b, d->bar[b]);
        }
        kprintf("\n");
    }
}

static void cmd_mouse(void) {
    const mouse_state_t *ms = mouse_get_state();
    uint64_t pk, sd, od;
    mouse_get_stats(&pk, &sd, &od);
    kprintf("mouse: %s x=%d y=%d btn=%u\n",
            ms->present ? "present" : "absent", ms->x, ms->y, ms->buttons);
    kprintf("  packets=%u sync_drops=%u ovf_drops=%u\n",
            (unsigned)pk, (unsigned)sd, (unsigned)od);
}

static void cmd_theme(const char *arg) {
    if (arg && arg[0]) {
        if (config_set_theme(arg)) { kprintf("unknown theme: %s (dark|light)\n", arg); return; }
        kprintf("theme saved to %s\n", config_path());
    }
    const asset_theme_t *t = asset_theme();
    const asset_metrics_t *m = asset_metrics();
    kprintf("theme=%s  font=%ux%u  title_h=%u row_h=%u btn_h=%u border=%u\n",
            asset_theme_name(), m->font_w, m->font_h, m->title_h, m->row_h,
            m->btn_h, m->border);
    kprintf("  desktop=%06x win=%06x title=%06x client=%06x\n",
            t->desktop_bg, t->win_bg, t->win_title_active, t->client_bg);
    kprintf("  text=%06x btn=%06x sel=%06x accent=%06x danger=%06x\n",
            t->text, t->btn_face, t->list_sel, t->accent, t->danger);
    kprintf("  icons:");
    for (int i = 0; i < ASSET_ICO_COUNT; i++) kprintf(" %s", asset_icon_name((asset_icon_id_t)i));
    kprintf("\n  strings: %u entries, e.g. \"%s\", \"%s\"\n",
            (unsigned)ASSET_STR_COUNT, asset_str(ASSET_STR_APP_FILES),
            asset_str(ASSET_STR_BTN_NEW_FOLDER));
}

static void cmd_guitest(void) {
    const asset_theme_t *t = asset_theme();
    const asset_metrics_t *m = asset_metrics();

    ga_rect_t desk = { 0, 0, (int32_t)gfx_width(), (int32_t)gfx_height() };
    ga_set_clip(desk);
    ga_fill(desk, t->desktop_bg);
    for (int32_t gy = 0; gy < desk.h; gy += 24) ga_hline(0, gy, desk.w, t->desktop_grid);
    for (int32_t gx = 0; gx < desk.w; gx += 24) ga_vline(gx, 0, desk.h, t->desktop_grid);

    ga_rect_t win = { 60, 60, 620, 460 };
    ga_window_frame(win, asset_str(ASSET_STR_APP_FILES), 1);
    ga_rect_t cl = ga_window_client(win);
    ga_push_clip(cl);

    int32_t bx = cl.x + (int32_t)m->gap;
    int32_t by = cl.y + (int32_t)m->gap;
    ga_rect_t b1 = ga_button(bx, by, asset_str(ASSET_STR_BTN_UP), ASSET_ICO_UP, GA_BTN_NORMAL);
    bx = b1.x + b1.w + (int32_t)m->gap;
    ga_rect_t b2 = ga_button(bx, by, asset_str(ASSET_STR_BTN_NEW_FOLDER), ASSET_ICO_PLUS, GA_BTN_HOVER);
    bx = b2.x + b2.w + (int32_t)m->gap;
    ga_rect_t b3 = ga_button(bx, by, asset_str(ASSET_STR_BTN_REFRESH), ASSET_ICO_REFRESH, GA_BTN_PRESSED);
    bx = b3.x + b3.w + (int32_t)m->gap;
    ga_button(bx, by, asset_str(ASSET_STR_BTN_SYNC), ASSET_ICO_SAVE, GA_BTN_NORMAL);

    int32_t ly = by + b1.h + (int32_t)m->gap;
    ga_rect_t path = { cl.x + (int32_t)m->gap, ly,
                       cl.w - 2 * (int32_t)m->gap, (int32_t)m->row_h };
    ga_fill(path, t->list_bg);
    ga_frame(path, 1, t->win_border);
    ga_text(path.x + 6, path.y + (path.h - (int32_t)ga_font_h()) / 2, "/", t->accent);
    ly += path.h + (int32_t)m->gap;

    static const char *names[] = { "data", "test.txt", "docs", "info.txt", "kernel.log" };
    static const int ico[] = { ASSET_ICO_FOLDER, ASSET_ICO_FILE, ASSET_ICO_FOLDER,
                               ASSET_ICO_FILE, ASSET_ICO_FILE };
    static const char *sizes[] = { "12 B", "35 B", "0 B", "24 B", "1024 B" };
    for (int i = 0; i < 5; i++) {
        ga_rect_t row = { cl.x + (int32_t)m->gap, ly,
                          cl.w - 2 * (int32_t)m->gap, (int32_t)m->row_h };
        if (row.y + row.h > cl.y + cl.h) break;
        ga_list_row(row, i, i == 1, ico[i], names[i], sizes[i]);
        ly += row.h;
    }

    ga_pop_clip();
    ga_flush_rect(desk);

    kprintf("guitest: mockup on screen, press any key to return\n");
    while (!kbd_getchar()) pit_sleep_ms(20);
    gfx_clear_all(0x000000);
    con_clear();
    con_flush();
}

static volatile int gui_done;
static char gui_done_chan;

static void gui_thread(void *arg) {
    int mode = (int)(intptr_t)arg;
    cursor_reset();
    gs_init();
    if (config_load() != 0) config_init_defaults();
    asset_theme_set(config_get()->theme);
    gs_set_options(config_get()->grid, config_get()->shadow, config_get()->clock);
    gs_set_dblclick_ms(config_get()->dblclick_ms);
    gs_desktop_clear();
    gs_desktop_add("Files", ASSET_ICO_FOLDER, myfsapp_open);
    gs_desktop_add("Term", ASSET_ICO_TERMINAL, term_open);
    gs_desktop_add("System", ASSET_ICO_DISK, app_demo_about);
    gs_set_launcher(myfsapp_open);
    myfsapp_open();
    if (mode == 0) app_demo_about();
    if (mode == 2) term_open();
    gs_run();
    cursor_reset();
    gfx_clear_all(0x000000);
    con_clear();
    con_flush();
    gui_done = 1;
    sched_wake(&gui_done_chan);
    sched_exit();
}

static void enter_gui(int mode) {
    gui_done = 0;
    sched_spawn("gui", gui_thread, (void *)(intptr_t)mode, 65536);
    sched_block_on(&gui_done_chan);
    kprintf("GUI closed, back to text mode\n");
}

static void cmd_gui(void)  { enter_gui(0); }
static void cmd_files(void) { enter_gui(1); }
static void cmd_term(void)  { enter_gui(2); }

static void cmd_ps(void) {
    kprintf("  id name                 state     pr cpu_ticks  boost/demote\n");
    static const char *sn[] = { "DEAD", "READY", "RUNNING", "BLOCKED" };
    for (int i = 0; i < SCHED_MAX_THREADS; i++) {
        const thread_t *t = sched_get(i);
        if (!t) continue;
        kprintf("%4d %-20s %-9s p%d %-9u %u/%u\n", t->id, t->name, sn[t->state],
                sched_eff_prio_tid(t->id), (unsigned)t->cpu_ticks,
                (unsigned)t->boosts, (unsigned)t->demotes);
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
    else if (!strcmp(cmd, "date")) cmd_date();
    else if (!strcmp(cmd, "bcstat")) cmd_bcstat();
    else if (!strcmp(cmd, "testfault")) cmd_testfault();
    else if (!strcmp(cmd, "uptime")) cmd_uptime();
    else if (!strcmp(cmd, "sleep")) cmd_sleep(arg);
    else if (!strcmp(cmd, "ls")) cmd_ls(arg);
    else if (!strcmp(cmd, "cat")) cmd_cat(arg);
    else if (!strcmp(cmd, "touch")) cmd_touch(arg);
    else if (!strcmp(cmd, "write")) cmd_write(arg);
    else if (!strcmp(cmd, "rm")) cmd_rm(arg);
    else if (!strcmp(cmd, "mkdir")) {
        uint16_t ino;
        if (myfs_mkdir(arg, 0755, &ino) == 0) kprintf("mkdir: %s (ino %u)\n", arg, ino);
        else kprintf("mkdir failed: %s\n", arg);
    }
    else if (!strcmp(cmd, "rename")) {
        char *sp = strchr(arg, ' ');
        if (!sp) { kprintf("usage: rename <old> <new>\n"); }
        else { *sp = 0; if (myfs_rename(arg, sp+1)==0) kprintf("renamed\n"); else kprintf("rename failed\n"); }
    }
    else if (!strcmp(cmd, "sync")) { myfs_sync(); kprintf("synced\n"); }
    else if (!strcmp(cmd, "lspci")) cmd_lspci();
    else if (!strcmp(cmd, "mouse")) cmd_mouse();
    else if (!strcmp(cmd, "theme")) cmd_theme(arg);
    else if (!strcmp(cmd, "guitest")) cmd_guitest();
    else if (!strcmp(cmd, "gui")) cmd_gui();
    else if (!strcmp(cmd, "files")) cmd_files();
    else if (!strcmp(cmd, "ps")) cmd_ps();
    else if (!strcmp(cmd, "term")) cmd_term();
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
        /* Обновляем курсор мыши каждый цикл */
        {
            const mouse_state_t *ms = mouse_get_state();
            if (ms->present) cursor_update(ms->x, ms->y);
        }
        char c = kbd_getchar();
        if (!c) pit_sleep_ms(2);
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
