#include "term.h"
#include "graphic_server.h"
#include "graphic_api.h"
#include "asset.h"
#include "syslib.h"
#include "config.h"
#include "kstdlib.h"
#include "sched.h"
#include "kbd.h"
#include "pit.h"
#include "rtc.h"
#include "kstring.h"
#include "kstdio.h"


static int parse_int(const char *s) {
    int v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return v;
}

#define TERM_COLS   160
#define TERM_MAXL   512
#define TERM_INST   2
#define TERM_HIST   16

typedef struct { char text[TERM_COLS]; uint32_t fg; } term_line_t;

typedef struct {
    int          used;
    gs_window_t *win;
    term_line_t  lines[TERM_MAXL];
    int          nlines;
    int          scroll;
    char         input[TERM_COLS];
    int          ilen;
    char         hist[TERM_HIST][TERM_COLS];
    int          hcount, hidx;
    char         cwd[192];
    int          rows;
    int          tid;
    ga_rect_t    grid, sb;
    int          sb_drag, sb_grab;
} term_t;

static term_t terms[TERM_INST];

static void tput(term_t *t, uint32_t fg, const char *s) {
    while (*s) {
        if (t->nlines == 0) {
            t->nlines = 1;
            memset(&t->lines[0], 0, sizeof(term_line_t));
            t->lines[0].fg = fg;
        }
        term_line_t *ln = &t->lines[t->nlines - 1];
        if (ln->fg == 0) ln->fg = fg;
        if (*s == '\n') {
            if (t->nlines < TERM_MAXL) {
                t->nlines++;
                memset(&t->lines[t->nlines - 1], 0, sizeof(term_line_t));
            } else {
                memmove(&t->lines[0], &t->lines[1], sizeof(term_line_t) * (TERM_MAXL - 1));
                memset(&t->lines[TERM_MAXL - 1], 0, sizeof(term_line_t));
            }
            s++;
            continue;
        }
        size_t cur = strlen(ln->text);
        if (cur >= TERM_COLS - 1) {
            if (t->nlines < TERM_MAXL) {
                t->nlines++;
                memset(&t->lines[t->nlines - 1], 0, sizeof(term_line_t));
            } else {
                memmove(&t->lines[0], &t->lines[1], sizeof(term_line_t) * (TERM_MAXL - 1));
                memset(&t->lines[TERM_MAXL - 1], 0, sizeof(term_line_t));
            }
            continue;
        }
        ln->text[cur] = *s++;
        ln->text[cur + 1] = '\0';
    }
}

static void tprintf(term_t *t, uint32_t fg, const char *fmt, ...) {
    char buf[512];
    va_list ap;
    __builtin_va_start(ap, fmt);
    kvsnprintf(buf, sizeof(buf), fmt, ap);
    __builtin_va_end(ap);
    tput(t, fg, buf);
}

/* ---------------- commands ---------------- */

static void term_resolve(term_t *t, const char *arg, char *out, size_t osz) {
    if (arg[0] == '/') { strncpy(out, arg, osz - 1); out[osz - 1] = 0; return; }
    if (!strcmp(t->cwd, "/")) ksnprintf(out, osz, "/%s", arg);
    else ksnprintf(out, osz, "%s/%s", t->cwd, arg);
}

static void term_exec(term_t *t, const char *line) {
    const asset_theme_t *th = asset_theme();
    char cmd[64], arg[TERM_COLS];
    const char *sp = strchr(line, ' ');
    size_t cl = sp ? (size_t)(sp - line) : strlen(line);
    if (cl >= sizeof(cmd)) cl = sizeof(cmd) - 1;
    memcpy(cmd, line, cl); cmd[cl] = 0;
    arg[0] = 0;
    if (sp) { strncpy(arg, sp + 1, sizeof(arg) - 1); arg[sizeof(arg) - 1] = 0; }

    tprintf(t, th->accent, "%s:%s$ %s\n", sched_current_name(), t->cwd, line);

    if (!cmd[0]) return;

    if (!strcmp(cmd, "help")) {
        tput(t, th->text,
             "commands: help ls [p] cat <f> cd <d> pwd echo <s> mkdir <d> touch <f>\n"
             "          rm <f> write <f> <text> sync ps uptime date sys set <k> <v>\n"
             "          clear theme <n> exit\n");
    } else if (!strcmp(cmd, "pwd")) {
        tprintf(t, th->text, "%s\n", t->cwd);
    } else if (!strcmp(cmd, "cd")) {
        char full[256];
        if (!arg[0] || !strcmp(arg, "/")) { strcpy(t->cwd, "/"); return; }
        term_resolve(t, arg, full, sizeof(full));
        sl_stat_t st0;
        int rc = sl_stat(full, &st0);
        if (rc == SL_OK && st0.type == SL_TYPE_DIR) strcpy(t->cwd, full);
        else tprintf(t, th->danger, "cd: no such directory: %s\n", full);
    } else if (!strcmp(cmd, "ls")) {
        char full[256];
        if (arg[0]) term_resolve(t, arg, full, sizeof(full));
        else strcpy(full, t->cwd);
        int fd = sl_open(full, SL_O_READ);
        if (fd < 0) { tprintf(t, th->danger, "ls: cannot open: %s\n", full); return; }
        int n = 0;
        for (int i = 0; ; i++) {
            sl_dirent_t de;
            if (sl_readdir(fd, i, &de) != SL_OK) break;
            if (!de.name[0]) continue;
            tprintf(t, de.type == SL_TYPE_DIR ? th->accent : th->text,
                    "%s %-24s %8u B\n",
                    de.type == SL_TYPE_DIR ? "DIR " : "FILE", de.name, (unsigned)de.size);
            n++;
        }
        sl_close(fd);
        tprintf(t, th->text_dim, "%d entries\n", n);
    } else if (!strcmp(cmd, "cat")) {
        char full[256];
        term_resolve(t, arg, full, sizeof(full));
        int fd = sl_open(full, SL_O_READ);
        if (fd < 0) { tprintf(t, th->danger, "cat: no such file\n"); return; }
        static char buf[1024];
        int got;
        while ((got = sl_read(fd, buf, sizeof(buf) - 1)) > 0) {
            buf[got] = 0;
            tput(t, th->text, buf);
        }
        sl_close(fd);
        tput(t, th->text, "\n");
    } else if (!strcmp(cmd, "echo")) {
        tprintf(t, th->text, "%s\n", arg);
    } else if (!strcmp(cmd, "mkdir") || !strcmp(cmd, "touch")) {
        char full[256];
        term_resolve(t, arg, full, sizeof(full));
        int rc = (cmd[1] == 'k') ? sl_mkdir(full)
                                 : (sl_open(full, SL_O_READ | SL_O_CREATE) >= 0 ? SL_OK : SL_EGENERIC);
        tprintf(t, rc == 0 ? th->ok : th->danger, "%s: %s\n", full, rc == 0 ? "created" : "failed");
    } else if (!strcmp(cmd, "rm")) {
        char full[256];
        term_resolve(t, arg, full, sizeof(full));
        int rc = sl_unlink(full);
        tprintf(t, rc == 0 ? th->ok : th->danger, "%s: %s\n", full, rc == 0 ? "deleted" : "failed");
    } else if (!strcmp(cmd, "write")) {
        char *sp2 = strchr(arg, ' ');
        if (!sp2) { tput(t, th->danger, "usage: write <file> <text>\n"); return; }
        *sp2 = 0;
        char full[256];
        term_resolve(t, arg, full, sizeof(full));
        int fd = sl_open(full, SL_O_WRITE | SL_O_CREATE);
        int rc = fd;
        if (fd >= 0) {
            rc = sl_write(fd, sp2 + 1, (uint32_t)strlen(sp2 + 1));
            sl_close(fd);
            rc = (rc >= 0) ? SL_OK : SL_EGENERIC;
        }
        tprintf(t, rc == 0 ? th->ok : th->danger, "write: %s\n", rc == 0 ? "ok" : "failed");
    } else if (!strcmp(cmd, "sync")) {
        sl_sync();
        tput(t, th->ok, "synced\n");
    } else if (!strcmp(cmd, "ps")) {
        tput(t, th->text_dim, "  id name                 state     pr cpu_ticks  boost/demote\n");
        for (int i = 0; i < SCHED_MAX_THREADS; i++) {
            const thread_t *th2 = sched_get(i);
            if (!th2) continue;
            static const char *sn[] = { "DEAD", "READY", "RUNNING", "BLOCKED" };
            tprintf(t, th2->state == T_RUNNING ? th->ok : th->text,
                    "%4d %-20s %-9s p%d %-9u %u/%u\n", th2->id, th2->name, sn[th2->state],
                    sched_eff_prio_tid(th2->id), (unsigned)th2->cpu_ticks,
                    (unsigned)th2->boosts, (unsigned)th2->demotes);
        }
    } else if (!strcmp(cmd, "uptime")) {
        uint64_t ms = sl_uptime_ms();
        uint32_t sec = (uint32_t)(ms / 1000);
        tprintf(t, th->text, "up %02u:%02u:%02u.%03u (tid=%d)\n",
                (unsigned)(sec / 3600), (unsigned)((sec / 60) % 60),
                (unsigned)(sec % 60), (unsigned)(ms % 1000), sl_tid());
    } else if (!strcmp(cmd, "date")) {
        sl_datetime_t dt;
        if (sl_datetime(&dt) == SL_OK)
            tprintf(t, th->text, "%04u-%02u-%02u %02u:%02u:%02u\n",
                    (unsigned)dt.year, (unsigned)dt.month, (unsigned)dt.day,
                    (unsigned)dt.hour, (unsigned)dt.min, (unsigned)dt.sec);
        else tput(t, th->danger, "date: rtc failed\n");
    } else if (!strcmp(cmd, "sys")) {
        tput(t, th->text_dim, "  num syscall      calls\n");
        for (int i = 1; i <= SYS_MAX; i++) {
            sl_sysstat_t ss;
            if (sl_sysstat(i, &ss) != SL_OK) continue;
            tprintf(t, ss.calls ? th->text : th->text_dim,
                    "%5d %-12s %u\n", (int)ss.num, ss.name, (unsigned)ss.calls);
        }
    } else if (!strcmp(cmd, "set")) {
        char *sp2 = strchr(arg, ' ');
        if (!sp2) { tput(t, th->danger, "usage: set <grid|shadow|clock|dblclick_ms> <v>\n"); return; }
        *sp2 = 0;
        int v = (int)parse_int(sp2 + 1);
        if (config_set_int(arg, v)) { tput(t, th->danger, "unknown key\n"); return; }
        os_config_t *c = config_get();
        gs_set_options(c->grid, c->shadow, c->clock);
        gs_set_dblclick_ms(c->dblclick_ms);
        tprintf(t, th->ok, "%s=%d saved\n", arg, v);
    } else if (!strcmp(cmd, "clear")) {
        t->nlines = 0;
    } else if (!strcmp(cmd, "theme")) {
        if (config_set_theme(arg)) tput(t, th->danger, "themes: dark light\n");
        else {
            tprintf(t, th->ok, "theme: %s (saved to %s)\n", asset_theme_name(), config_path());
            gs_invalidate_all();
        }
    } else if (!strcmp(cmd, "exit")) {
        t->used = 0;
        t->win = NULL;
        gs_destroy(t->win ? t->win : NULL);
    } else {
        tprintf(t, th->danger, "unknown: %s\n", cmd);
    }
}

/* ---------------- paint / events ---------------- */

static void term_paint(gs_window_t *w, void *user) {
    term_t *t = user;
    const asset_theme_t *th = asset_theme();
    ga_rect_t cl = gs_client(w);
    int32_t lh = (int32_t)ga_font_h();
    int32_t gap = 4;

    t->grid = (ga_rect_t){ cl.x + gap, cl.y + gap,
                           cl.w - 2 * gap - (int32_t)asset_metrics()->scroll_w - 2,
                           cl.h - 2 * gap - lh - gap };
    t->rows = t->grid.h / lh;
    if (t->rows < 1) t->rows = 1;

    ga_fill(t->grid, 0x0B0E12);
    ga_frame(t->grid, 1, th->win_border);
    ga_push_clip(t->grid);

    int maxoff = t->nlines - t->rows + 1;
    if (maxoff < 0) maxoff = 0;
    if (t->scroll > maxoff) t->scroll = maxoff;
    int start = maxoff - t->scroll;
    if (start < 0) start = 0;

    int32_t y = t->grid.y + 2;
    for (int i = start; i < t->nlines && i < start + t->rows; i++) {
        uint32_t fg = t->lines[i].fg ? t->lines[i].fg : th->text;
        ga_text_fit(t->grid.x + 4, y, t->grid.w - 8, t->lines[i].text, fg);
        y += lh;
    }
    ga_pop_clip();

    t->sb = (ga_rect_t){ t->grid.x + t->grid.w + 2, t->grid.y,
                         (int32_t)asset_metrics()->scroll_w, t->grid.h };
    ga_scrollbar(t->sb, t->nlines, t->rows, t->scroll);

    ga_rect_t in = { cl.x + gap, cl.y + cl.h - gap - lh, cl.w - 2 * gap, lh };
    ga_fill(in, 0x10151B);
    ga_frame(in, 1, th->accent);
    char prompt[224];
    const thread_t *self = sched_get(t->tid);
    ksnprintf(prompt, sizeof(prompt), "%s:%s$ %s",
              self ? self->name : "term", t->cwd, t->input);
    ga_text_fit(in.x + 6, in.y + (lh - (int32_t)ga_font_h()) / 2, in.w - 14, prompt, th->ok);
    int32_t cx = in.x + 6 + (int32_t)(strlen(prompt) * ga_font_w());
    if (cx < in.x + in.w - 6) ga_vline(cx, in.y + 2, lh - 4, th->accent);
}

static void term_event(gs_window_t *w, const gs_event_t *ev, void *user) {
    term_t *t = user;
    if (t->tid < 0) t->tid = sched_current_id();
    if (ev->type == GS_EV_CLOSE) { t->used = 0; t->win = NULL; gs_destroy(w); return; }

    if (ev->type == GS_EV_MOUSE_MOVE && t->sb_drag) {
        ga_rect_t th2 = ga_scroll_thumb(t->sb, t->nlines, t->rows, t->scroll);
        int32_t travel = t->sb.h - th2.h;
        int maxoff = t->nlines - t->rows + 1;
        if (maxoff < 0) maxoff = 0;
        if (travel > 0 && maxoff > 0) {
            int32_t rel = ev->sy - t->sb.y - t->sb_grab;
            if (rel < 0) rel = 0;
            if (rel > travel) rel = travel;
            t->scroll = (int)((int64_t)rel * maxoff / travel);
            gs_invalidate(w);
        }
        return;
    }
    if (ev->type == GS_EV_MOUSE_UP) { t->sb_drag = 0; return; }
    if (ev->type == GS_EV_MOUSE_DOWN && ga_hit(t->sb, ev->sx, ev->sy)) {
        ga_rect_t th2 = ga_scroll_thumb(t->sb, t->nlines, t->rows, t->scroll);
        if (ga_hit(th2, ev->sx, ev->sy)) { t->sb_drag = 1; t->sb_grab = ev->sy - th2.y; }
        else if (ev->sy < th2.y) t->scroll += t->rows;
        else t->scroll -= t->rows;
        gs_invalidate(w);
        return;
    }

    if (ev->type != GS_EV_KEY) return;
    char c = ev->key;
    if (c == KEY_PGUP) { t->scroll += t->rows; gs_invalidate(w); return; }
    if (c == KEY_PGDN) { t->scroll -= t->rows; gs_invalidate(w); return; }
    if (c == KEY_UP) {
        if (t->hcount) {
            if (t->hidx < 0) t->hidx = t->hcount - 1;
            else if (t->hidx > 0) t->hidx--;
            strncpy(t->input, t->hist[t->hidx], sizeof(t->input) - 1);
            t->ilen = (int)strlen(t->input);
        }
        gs_invalidate(w); return;
    }
    if (c == KEY_DOWN) {
        if (t->hidx >= 0) {
            t->hidx++;
            if (t->hidx >= t->hcount) { t->hidx = -1; t->input[0] = 0; t->ilen = 0; }
            else { strncpy(t->input, t->hist[t->hidx], sizeof(t->input) - 1); t->ilen = (int)strlen(t->input); }
        }
        gs_invalidate(w); return;
    }
    if (c == '\b') { if (t->ilen > 0) t->input[--t->ilen] = 0; gs_invalidate(w); return; }
    if (c == '\n') {
        if (t->ilen) {
            strncpy(t->hist[t->hcount % TERM_HIST], t->input, TERM_COLS - 1);
            t->hcount++;
            t->hidx = -1;
            char line[TERM_COLS];
            strncpy(line, t->input, sizeof(line) - 1);
            line[sizeof(line) - 1] = 0;
            t->input[0] = 0; t->ilen = 0;
            term_exec(t, line);
        } else {
            tput(t, asset_theme()->accent, "\n");
        }
        gs_invalidate(w);
        return;
    }
    if (c >= 32 && c < 127 && t->ilen < TERM_COLS - 2) {
        t->input[t->ilen++] = c;
        t->input[t->ilen] = 0;
        gs_invalidate(w);
    }
}

void term_open(void) {
    term_t *t = NULL;
    for (int i = 0; i < TERM_INST; i++) if (!terms[i].used) { t = &terms[i]; break; }
    if (!t) return;
    memset(t, 0, sizeof(*t));
    t->used = 1;
    t->tid = -1;
    strcpy(t->cwd, "/");
    t->hidx = -1;
    const asset_theme_t *th = asset_theme();
    tput(t, th->accent, "MyOS terminal - each window is its own scheduler task\n");
    tput(t, th->text_dim, "type 'help' for commands, arrows for history/scroll\n");
    ga_rect_t r = { 90, 90, 660, 460 };
    t->win = gs_create(asset_str(ASSET_STR_APP_TERM), r, term_paint, term_event, t);
    if (!t->win) t->used = 0;
}
