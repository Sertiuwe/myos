#include "myfsapp.h"
#include "graphic_server.h"
#include "graphic_api.h"
#include "asset.h"
#include "myfs.h"
#include "kstring.h"
#include "kstdio.h"
#include "pit.h"
#include "fslock.h"
#include "sched.h"
#include "kbd.h"

extern int myfs_read_inode_raw(uint16_t ino, myfs_inode_t *out);

/* ================= FS adapter: ONLY place that talks to myfs =================
   Every entry takes the global FS mutex: myfs/bcache are not reentrant, and
   from now on several app threads (file manager windows, terminal) call in. */
static int fa_list(uint16_t dir, int idx, char *nm, size_t nsz, uint16_t *ino, uint8_t *ty) {
    fs_lock(); int r = myfs_readdir(dir, idx, nm, nsz, ino, ty); fs_unlock(); return r;
}
static int fa_resolve(const char *p, uint16_t *ino, uint8_t *ty) { fs_lock(); int r = myfs_resolve(p, ino, ty); fs_unlock(); return r; }
static int fa_inode(uint16_t ino, myfs_inode_t *o) { fs_lock(); int r = myfs_read_inode_raw(ino, o); fs_unlock(); return r; }
static int fa_mkdir(const char *p) { fs_lock(); int r = myfs_mkdir(p, 0755, NULL); fs_unlock(); return r; }
static int fa_create(const char *p) { fs_lock(); int r = myfs_create(p, 0644, NULL); fs_unlock(); return r; }
static int fa_unlink(const char *p) { fs_lock(); int r = myfs_unlink(p); fs_unlock(); return r; }
static int fa_rename(const char *a, const char *b) { fs_lock(); int r = myfs_rename(a, b); fs_unlock(); return r; }
static int fa_read(uint16_t ino, uint32_t off, void *b, uint32_t n) { fs_lock(); int r = myfs_read(ino, off, b, n); fs_unlock(); return r; }
static void fa_sync(void) { fs_lock(); myfs_sync(); fs_unlock(); }
static uint16_t fa_root(void) { return myfs_root_ino(); }
/* =========================================================================== */

#define FA_MAX_ENT   96
#define FA_NAME      64
#define FA_INST      2
#define FV_BUF       4096
#define FV_INST      2

#define B_UP 0
#define B_OPEN 1
#define B_MKDIR 2
#define B_MKFILE 3
#define B_RENAME 4
#define B_DELETE 5
#define B_REFRESH 6
#define B_SYNC 7

typedef struct {
    char     name[FA_NAME];
    uint16_t ino;
    uint8_t  type;
    uint32_t size;
    uint32_t mtime;
} fa_entry_t;

typedef struct {
    int          used;
    gs_window_t *win;
    char         path[192];
    uint16_t     dir;
    fa_entry_t   ent[FA_MAX_ENT];
    int          count, sel, scroll, rows;
    char         status[80];
    int          input_mode;          /* 0 none, 1 mkdir, 2 mkfile, 3 rename */
    char         input[64];
    int          ilen;
    int          hover_btn;
    uint32_t     last_click;
    int          last_idx;
    int          sb_drag, sb_grab;
    ga_rect_t    btn[8];
    ga_rect_t    list, sb, pathr, inputr, okr, cancelr;
} fa_inst_t;

typedef struct {
    int          used;
    gs_window_t *win;
    char         path[192];
    char         buf[FV_BUF + 1];
    int          len, scroll, rows, lines;
    int          sb_drag, sb_grab;
    ga_rect_t    list, sb;
} fv_inst_t;

static fa_inst_t inst[FA_INST];
static fv_inst_t viewers[FV_INST];

/* ---------------- helpers ---------------- */

static void path_join(char *d, size_t dsz, const char *dir, const char *nm) {
    if (!strcmp(dir, "/")) ksnprintf(d, dsz, "/%s", nm);
    else                   ksnprintf(d, dsz, "%s/%s", dir, nm);
}

static void path_parent(char *d, size_t dsz, const char *p) {
    size_t n = strlen(p);
    while (n > 1 && p[n - 1] == '/') n--;
    size_t cut = 0;
    for (size_t i = n; i > 0; i--) {
        if (p[i - 1] == '/') { cut = i - 1; break; }
    }
    if (cut == 0) { d[0] = '/'; d[1] = '\0'; return; }
    if (cut >= dsz) cut = dsz - 1;
    memcpy(d, p, cut);
    d[cut] = '\0';
}

static void fmt_size(uint32_t sz, char *out, size_t osz) {
    if (sz < 1024)        ksnprintf(out, osz, "%u B", (unsigned)sz);
    else if (sz < 1048576) ksnprintf(out, osz, "%u KB", (unsigned)(sz / 1024));
    else                  ksnprintf(out, osz, "%u MB", (unsigned)(sz / 1048576));
}

static void fmt_time(uint32_t ts, char *out, size_t osz) {
    if (!ts) { ksnprintf(out, osz, "--"); return; }
    uint32_t days = ts / 86400, rem = ts % 86400;
    uint32_t hh = rem / 3600, mm = (rem % 3600) / 60;
    uint32_t y = 1970, mo = 1;
    static const uint16_t md[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    for (;;) {
        uint32_t leap = ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 1 : 0;
        uint32_t dy = 365 + leap;
        if (days < dy) break;
        days -= dy; y++;
    }
    int leap = ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 1 : 0;
    for (mo = 1; mo <= 12; mo++) {
        uint32_t dm = md[mo - 1] + ((mo == 2 && leap) ? 1 : 0);
        if (days < dm) break;
        days -= dm;
    }
    ksnprintf(out, osz, "%04u-%02u-%02u %02u:%02u",
              (unsigned)y, (unsigned)mo, (unsigned)(days + 1),
              (unsigned)hh, (unsigned)mm);
}

static int fa_less(const fa_entry_t *a, const fa_entry_t *b) {
    int ad = (a->type == MYFS_TYPE_DIR), bd = (b->type == MYFS_TYPE_DIR);
    if (ad != bd) return ad > bd;
    return strcmp(a->name, b->name) < 0;
}

static void clamp_scroll(fa_inst_t *a) {
    int maxoff = a->count - a->rows;
    if (maxoff < 0) maxoff = 0;
    if (a->scroll > maxoff) a->scroll = maxoff;
    if (a->scroll < 0) a->scroll = 0;
}

/* ---------------- viewer ---------------- */

static void fv_count_lines(fv_inst_t *v) {
    int n = 1;
    for (int i = 0; i < v->len; i++) if (v->buf[i] == '\n') n++;
    v->lines = n;
}

static void fv_paint(gs_window_t *w, void *user) {
    fv_inst_t *v = user;
    const asset_theme_t *t = asset_theme();
    const asset_metrics_t *m = asset_metrics();
    ga_rect_t cl = gs_client(w);
    int32_t gap = (int32_t)m->gap;

    ga_rect_t bar = { cl.x + gap, cl.y + gap, cl.w - 2 * gap, (int32_t)m->row_h };
    ga_fill(bar, t->list_bg);
    ga_frame(bar, 1, t->win_border);
    ga_text_fit(bar.x + 6, bar.y + (bar.h - (int32_t)ga_font_h()) / 2,
                bar.w - 12, v->path, t->accent);

    int32_t ly = bar.y + bar.h + gap;
    int32_t bh = cl.y + cl.h - gap - (int32_t)m->row_h;
    v->list = (ga_rect_t){ cl.x + gap, ly,
                           cl.w - 2 * gap - (int32_t)m->scroll_w - 2, bh - ly };
    if (v->list.h < (int32_t)m->row_h) v->list.h = (int32_t)m->row_h;
    v->rows = v->list.h / (int32_t)ga_font_h();
    if (v->rows < 1) v->rows = 1;

    ga_fill(v->list, t->list_bg);
    ga_push_clip(v->list);
    int line = 0, start = 0;
    int32_t ty = v->list.y;
    for (int i = 0; i <= v->len; i++) {
        if (i == v->len || v->buf[i] == '\n') {
            if (line >= v->scroll && line < v->scroll + v->rows) {
                char tmp[256];
                int n = i - start;
                if (n > 254) n = 254;
                if (n > 0) memcpy(tmp, v->buf + start, (size_t)n);
                tmp[n] = '\0';
                char ln[12];
                ksnprintf(ln, sizeof(ln), "%4d", line + 1);
                int32_t gut = 5 * (int32_t)ga_font_w();
                ga_text(v->list.x + 4, ty, ln, t->text_dim);
                ga_vline(v->list.x + 4 + gut - 4, ty, (int32_t)ga_font_h(), t->win_border);
                ga_text_fit(v->list.x + 8 + gut, ty, v->list.w - 12 - gut, tmp, t->text);
                ty += (int32_t)ga_font_h();
            }
            line++;
            start = i + 1;
            if (line >= v->scroll + v->rows) break;
        }
    }
    ga_pop_clip();

    v->sb = (ga_rect_t){ v->list.x + v->list.w + 2, v->list.y,
                         (int32_t)m->scroll_w, v->list.h };
    ga_scrollbar(v->sb, v->lines, v->rows, v->scroll);

    ga_rect_t st = { cl.x + gap, cl.y + cl.h - gap - (int32_t)m->row_h,
                     cl.w - 2 * gap, (int32_t)m->row_h };
    ga_fill(st, t->win_bg);
    char info[96];
    ksnprintf(info, sizeof(info), "%d bytes  %d lines", v->len, v->lines);
    ga_text(st.x + 6, st.y + (st.h - (int32_t)ga_font_h()) / 2, info, t->text_dim);
}

static void fv_event(gs_window_t *w, const gs_event_t *ev, void *user) {
    fv_inst_t *v = user;
    if (ev->type == GS_EV_CLOSE) { v->used = 0; v->win = NULL; gs_destroy(w); return; }
    int maxoff = v->lines - v->rows;
    if (maxoff < 0) maxoff = 0;

    if (ev->type == GS_EV_MOUSE_MOVE && v->sb_drag) {
        ga_rect_t th = ga_scroll_thumb(v->sb, v->lines, v->rows, v->scroll);
        int32_t travel = v->sb.h - th.h;
        if (travel > 0 && maxoff > 0) {
            int32_t rel = ev->sy - v->sb.y - v->sb_grab;
            if (rel < 0) rel = 0;
            if (rel > travel) rel = travel;
            v->scroll = (int)((int64_t)rel * maxoff / travel);
            gs_invalidate(w);
        }
        return;
    }
    if (ev->type == GS_EV_MOUSE_UP) { v->sb_drag = 0; return; }
    if (ev->type == GS_EV_MOUSE_DOWN && ga_hit(v->sb, ev->sx, ev->sy)) {
        ga_rect_t th = ga_scroll_thumb(v->sb, v->lines, v->rows, v->scroll);
        if (ga_hit(th, ev->sx, ev->sy)) { v->sb_drag = 1; v->sb_grab = ev->sy - th.y; }
        else if (ev->sy < th.y) v->scroll -= v->rows;
        else v->scroll += v->rows;
        if (v->scroll < 0) v->scroll = 0;
        if (v->scroll > maxoff) v->scroll = maxoff;
        gs_invalidate(w);
        return;
    }
    if (ev->type == GS_EV_KEY) {
        if (ev->key == KEY_DOWN) v->scroll += 1;
        else if (ev->key == KEY_UP) v->scroll -= 1;
        else if (ev->key == KEY_PGDN || ev->key == '\n') v->scroll += v->rows;
        else if (ev->key == KEY_PGUP || ev->key == '\b') v->scroll -= v->rows;
        else if (ev->key == KEY_HOME) v->scroll = 0;
        else if (ev->key == KEY_END) v->scroll = maxoff;
        if (v->scroll < 0) v->scroll = 0;
        if (v->scroll > maxoff) v->scroll = maxoff;
        gs_invalidate(w);
    }
}

static void fv_open(const char *path, uint16_t ino, uint32_t size) {
    fv_inst_t *v = NULL;
    for (int i = 0; i < FV_INST; i++) if (!viewers[i].used) { v = &viewers[i]; break; }
    if (!v) return;
    memset(v, 0, sizeof(*v));
    v->used = 1;
    strncpy(v->path, path, sizeof(v->path) - 1);
    uint32_t want = size > FV_BUF ? FV_BUF : size;
    int got = want ? fa_read(ino, 0, v->buf, want) : 0;
    if (got < 0) got = 0;
    for (int i = 0; i < got; i++) {
        if (v->buf[i] == '\t') v->buf[i] = ' ';
        if (v->buf[i] == '\r') v->buf[i] = ' ';
    }
    v->len = got;
    v->buf[got] = '\0';
    fv_count_lines(v);
    ga_rect_t r = { 140, 120, 620, 420 };
    v->win = gs_create(path, r, fv_paint, fv_event, v);
    if (!v->win) v->used = 0;
}

/* ---------------- browser ---------------- */

static void fa_reload(fa_inst_t *a) {
    a->count = 0;
    uint8_t ty = 0;
    if (fa_resolve(a->path, &a->dir, &ty) != 0 || ty != MYFS_TYPE_DIR) {
        strcpy(a->path, "/");
        a->dir = fa_root();
    }
    for (int i = 0; i < FA_MAX_ENT * 2 && a->count < FA_MAX_ENT; i++) {
        char nm[FA_NAME];
        uint16_t ino = 0;
        uint8_t t = 0;
        if (fa_list(a->dir, i, nm, sizeof(nm), &ino, &t) != 0) break;
        if (!nm[0]) continue;
        fa_entry_t *e = &a->ent[a->count++];
        memset(e, 0, sizeof(*e));
        strncpy(e->name, nm, FA_NAME - 1);
        e->ino = ino;
        e->type = t;
        myfs_inode_t st;
        if (fa_inode(ino, &st) == 0) { e->size = st.size; e->mtime = st.mtime; }
    }
    for (int i = 1; i < a->count; i++) {
        fa_entry_t k = a->ent[i];
        int j = i - 1;
        while (j >= 0 && fa_less(&k, &a->ent[j])) { a->ent[j + 1] = a->ent[j]; j--; }
        a->ent[j + 1] = k;
    }
    if (a->sel >= a->count) a->sel = a->count - 1;
    if (a->sel < 0) a->sel = 0;
    clamp_scroll(a);
    ksnprintf(a->status, sizeof(a->status), "%d %s", a->count, asset_str(ASSET_STR_LBL_ITEMS));
    if (a->win) {
        char title[GS_TITLE_MAX];
        ksnprintf(title, sizeof(title), "%s - %s", asset_str(ASSET_STR_APP_FILES), a->path);
        gs_set_title(a->win, title);
        gs_invalidate(a->win);
    }
}

static void fa_input_commit(fa_inst_t *a) {
    if (a->ilen == 0) {
        a->input_mode = 0;
        if (a->win) gs_invalidate(a->win);
        return;
    }
    char full[256], oldp[256];
    int rc = -1;
    if (a->input_mode == 1) {
        path_join(full, sizeof(full), a->path, a->input);
        rc = fa_mkdir(full);
    } else if (a->input_mode == 2) {
        path_join(full, sizeof(full), a->path, a->input);
        rc = fa_create(full);
    } else if (a->input_mode == 3 && a->sel >= 0 && a->sel < a->count) {
        path_join(oldp, sizeof(oldp), a->path, a->ent[a->sel].name);
        path_join(full, sizeof(full), a->path, a->input);
        rc = fa_rename(oldp, full);
    }
    ksnprintf(a->status, sizeof(a->status), "%s: %s",
              a->input, rc == 0 ? asset_str(ASSET_STR_MSG_CREATED)
                                : asset_str(ASSET_STR_MSG_FAILED));
    a->input_mode = 0;
    a->ilen = 0;
    a->input[0] = '\0';
    fa_reload(a);
}

static void fa_action(fa_inst_t *a, int btn) {
    char full[256];
    switch (btn) {
    case B_UP:
        path_parent(full, sizeof(full), a->path);
        strcpy(a->path, full);
        a->sel = 0; a->scroll = 0;
        fa_reload(a);
        break;
    case B_OPEN:
        if (a->sel >= 0 && a->sel < a->count) {
            fa_entry_t *e = &a->ent[a->sel];
            path_join(full, sizeof(full), a->path, e->name);
            if (e->type == MYFS_TYPE_DIR) {
                strcpy(a->path, full);
                a->sel = 0; a->scroll = 0;
                fa_reload(a);
            } else {
                fv_open(full, e->ino, e->size);
            }
        }
        break;
    case B_MKDIR:
        a->input_mode = 1; a->ilen = 0; a->input[0] = '\0';
        ksnprintf(a->status, sizeof(a->status), "%s:", asset_str(ASSET_STR_BTN_NEW_FOLDER));
        break;
    case B_MKFILE:
        a->input_mode = 2; a->ilen = 0; a->input[0] = '\0';
        ksnprintf(a->status, sizeof(a->status), "%s:", asset_str(ASSET_STR_BTN_NEW_FILE));
        break;
    case B_RENAME:
        if (a->sel >= 0 && a->sel < a->count) {
            a->input_mode = 3;
            strncpy(a->input, a->ent[a->sel].name, sizeof(a->input) - 1);
            a->input[sizeof(a->input) - 1] = '\0';
            a->ilen = (int)strlen(a->input);
            ksnprintf(a->status, sizeof(a->status), "%s:", asset_str(ASSET_STR_BTN_RENAME));
        }
        break;
    case B_DELETE:
        if (a->sel >= 0 && a->sel < a->count) {
            path_join(full, sizeof(full), a->path, a->ent[a->sel].name);
            int rc = fa_unlink(full);
            ksnprintf(a->status, sizeof(a->status), "%s: %s", a->ent[a->sel].name,
                      rc == 0 ? asset_str(ASSET_STR_MSG_DELETED)
                              : asset_str(ASSET_STR_MSG_FAILED));
            fa_reload(a);
        }
        break;
    case B_REFRESH:
        fa_reload(a);
        break;
    case B_SYNC:
        fa_sync();
        ksnprintf(a->status, sizeof(a->status), "%s", asset_str(ASSET_STR_MSG_SYNCED));
        break;
    default: break;
    }
    if (a->win) gs_invalidate(a->win);
}

static void fa_paint(gs_window_t *w, void *user) {
    fa_inst_t *a = user;
    const asset_theme_t *t = asset_theme();
    const asset_metrics_t *m = asset_metrics();
    ga_rect_t cl = gs_client(w);
    int32_t gap = (int32_t)m->gap;
    int32_t rh = (int32_t)m->row_h;
    int32_t mx, my; uint8_t mb;
    gs_pointer(&mx, &my, &mb);
    int active = gs_active(w);

    static const asset_str_id_t lbl[8] = {
        ASSET_STR_BTN_UP, ASSET_STR_BTN_OPEN, ASSET_STR_BTN_NEW_FOLDER, ASSET_STR_BTN_NEW_FILE,
        ASSET_STR_BTN_RENAME, ASSET_STR_BTN_DELETE, ASSET_STR_BTN_REFRESH, ASSET_STR_BTN_SYNC
    };
    static const int ico[8] = {
        ASSET_ICO_UP, ASSET_ICO_FOLDER, ASSET_ICO_PLUS, ASSET_ICO_PLUS,
        ASSET_ICO_FILE, ASSET_ICO_TRASH, ASSET_ICO_REFRESH, ASSET_ICO_SAVE
    };

    int32_t x0 = cl.x + gap;
    int32_t bx = x0, by = cl.y + gap;
    for (int i = 0; i < 8; i++) {
        if (i == 4) { bx = x0; by += (int32_t)m->btn_h + gap; }
        int st = GA_BTN_NORMAL;
        if (active && a->hover_btn == i) st = (mb & 1) ? GA_BTN_PRESSED : GA_BTN_HOVER;
        a->btn[i] = ga_button(bx, by, asset_str(lbl[i]), ico[i], st);
        bx = a->btn[i].x + a->btn[i].w + gap;
    }
    int32_t y = by + (int32_t)m->btn_h + gap;

    a->pathr = (ga_rect_t){ x0, y, cl.w - 2 * gap, rh };
    ga_fill(a->pathr, t->list_bg);
    ga_frame(a->pathr, 1, t->win_border);
    ga_text_fit(a->pathr.x + 6, a->pathr.y + (rh - (int32_t)ga_font_h()) / 2,
                a->pathr.w - 12, a->path, t->accent);
    y += rh + gap;

    ga_rect_t hdr = { x0, y, cl.w - 2 * gap, rh };
    ga_fill(hdr, t->win_bg);
    int32_t hty = hdr.y + (rh - (int32_t)ga_font_h()) / 2;
    ga_text(hdr.x + 6 + (int32_t)m->icon + 6, hty, asset_str(ASSET_STR_COL_NAME), t->text_dim);
    ga_text(hdr.x + hdr.w - 200, hty, asset_str(ASSET_STR_COL_SIZE), t->text_dim);
    ga_text(hdr.x + hdr.w - 130, hty, asset_str(ASSET_STR_COL_MODIFIED), t->text_dim);
    y += rh + 2;

    int32_t bottom = cl.y + cl.h - gap - rh;
    if (a->input_mode) bottom -= rh + gap;

    a->list = (ga_rect_t){ x0, y, cl.w - 2 * gap - (int32_t)m->scroll_w - 2, bottom - y };
    if (a->list.h < rh) a->list.h = rh;
    a->rows = a->list.h / rh;
    if (a->rows < 1) a->rows = 1;
    clamp_scroll(a);

    ga_fill(a->list, t->list_bg);
    ga_push_clip(a->list);
    for (int i = 0; i < a->rows; i++) {
        int idx = a->scroll + i;
        if (idx >= a->count) break;
        fa_entry_t *e = &a->ent[idx];
        ga_rect_t row = { a->list.x, a->list.y + i * rh, a->list.w, rh };
        char sz[24], tm[32], right[72];
        fmt_size(e->size, sz, sizeof(sz));
        fmt_time(e->mtime, tm, sizeof(tm));
        ksnprintf(right, sizeof(right), "%8s  %s", sz, tm);
        int icon = (e->type == MYFS_TYPE_DIR) ? ASSET_ICO_FOLDER : ASSET_ICO_FILE;
        ga_list_row(row, i, idx == a->sel, icon, e->name, right);
    }
    if (a->count == 0) {
        ga_text(a->list.x + 8, a->list.y + 6, asset_str(ASSET_STR_LBL_EMPTY), t->text_dim);
    }
    ga_pop_clip();

    a->sb = (ga_rect_t){ a->list.x + a->list.w + 2, a->list.y,
                         (int32_t)m->scroll_w, a->list.h };
    ga_scrollbar(a->sb, a->count, a->rows, a->scroll);

    if (a->input_mode) {
        int32_t iy = bottom + gap;
        a->okr = (ga_rect_t){ cl.x + cl.w - gap - 66, iy, 66, rh };
        a->cancelr = (ga_rect_t){ a->okr.x - gap - 82, iy, 82, rh };
        a->inputr = (ga_rect_t){ x0, iy, a->cancelr.x - gap - x0, rh };
        ga_fill(a->inputr, t->list_bg);
        ga_frame(a->inputr, 1, t->accent);
        int32_t ity = iy + (rh - (int32_t)ga_font_h()) / 2;
        ga_text_fit(a->inputr.x + 6, ity, a->inputr.w - 20, a->input, t->text);
        int32_t cx = a->inputr.x + 6 + (int32_t)(a->ilen * ga_font_w());
        if (cx < a->inputr.x + a->inputr.w - 6) ga_vline(cx, ity, (int32_t)ga_font_h(), t->accent);

        int ho = active && ga_hit(a->okr, mx, my);
        int hc = active && ga_hit(a->cancelr, mx, my);
        ga_fill(a->okr, ho ? ga_mix(t->ok, 0xFFFFFF, 70) : t->ok);
        ga_frame(a->okr, 1, ga_mix(t->ok, 0x000000, 90));
        ga_text(a->okr.x + 16, ity, "OK", t->text_invert);
        ga_fill(a->cancelr, hc ? t->btn_hover : t->btn_face);
        ga_frame(a->cancelr, 1, t->btn_border);
        ga_text(a->cancelr.x + 10, ity, "Cancel", t->btn_text);
    }

    ga_rect_t st = { x0, cl.y + cl.h - gap - rh, cl.w - 2 * gap, rh };
    ga_fill(st, t->win_bg);
    ga_hline(st.x, st.y, st.w, t->win_border);
    int32_t sty = st.y + (rh - (int32_t)ga_font_h()) / 2;
    ga_text_fit(st.x + 6, sty, st.w - 100, a->status, t->text);
    ga_text(st.x + st.w - 80, sty, "myfs", t->text_dim);
}

static void fa_event(gs_window_t *w, const gs_event_t *ev, void *user) {
    fa_inst_t *a = user;
    if (ev->type == GS_EV_CLOSE) { a->used = 0; a->win = NULL; gs_destroy(w); return; }
    const asset_metrics_t *m = asset_metrics();
    int32_t rh = (int32_t)m->row_h;

    if (ev->type == GS_EV_MOUSE_MOVE) {
        if (a->sb_drag) {
            ga_rect_t th = ga_scroll_thumb(a->sb, a->count, a->rows, a->scroll);
            int32_t travel = a->sb.h - th.h;
            int maxoff = a->count - a->rows;
            if (maxoff < 0) maxoff = 0;
            if (travel > 0 && maxoff > 0) {
                int32_t rel = ev->sy - a->sb.y - a->sb_grab;
                if (rel < 0) rel = 0;
                if (rel > travel) rel = travel;
                a->scroll = (int)((int64_t)rel * maxoff / travel);
                gs_invalidate(w);
            }
            return;
        }
        int h = -1;
        for (int i = 0; i < 8; i++) if (ga_hit(a->btn[i], ev->sx, ev->sy)) h = i;
        if (h != a->hover_btn) { a->hover_btn = h; gs_invalidate(w); }
        return;
    }

    if (ev->type == GS_EV_MOUSE_UP) { a->sb_drag = 0; return; }
    if (ev->type == GS_EV_TIMER) { if (!a->input_mode && !a->sb_drag) fa_reload(a); return; }

    if (ev->type == GS_EV_MOUSE_DOWN) {
        for (int i = 0; i < 8; i++) {
            if (ga_hit(a->btn[i], ev->sx, ev->sy)) { fa_action(a, i); return; }
        }
        if (a->input_mode) {
            if (ga_hit(a->okr, ev->sx, ev->sy)) { fa_input_commit(a); return; }
            if (ga_hit(a->cancelr, ev->sx, ev->sy)) {
                a->input_mode = 0; a->ilen = 0; a->input[0] = '\0';
                fa_reload(a);
                return;
            }
        }
        if (ga_hit(a->sb, ev->sx, ev->sy)) {
            ga_rect_t th = ga_scroll_thumb(a->sb, a->count, a->rows, a->scroll);
            if (ga_hit(th, ev->sx, ev->sy)) { a->sb_drag = 1; a->sb_grab = ev->sy - th.y; }
            else if (ev->sy < th.y) a->scroll -= a->rows;
            else a->scroll += a->rows;
            clamp_scroll(a);
            gs_invalidate(w);
            return;
        }
        if (ga_hit(a->list, ev->sx, ev->sy)) {
            int idx = (ev->sy - a->list.y) / rh + a->scroll;
            if (idx >= 0 && idx < a->count) {
                uint32_t now = (uint32_t)pit_ticks();
                uint32_t dblwin = gs_dblclick_ticks();
                int dbl = (a->last_idx == idx && (now - a->last_click) < dblwin);
                a->sel = idx;
                a->last_idx = idx;
                a->last_click = now;
                if (dbl) fa_action(a, B_OPEN);
                gs_invalidate(w);
            }
            return;
        }
        return;
    }

    if (ev->type == GS_EV_KEY) {
        char c = ev->key;
        if (a->input_mode) {
            if (c == '\n') { fa_input_commit(a); return; }
            if (c == '\b') {
                if (a->ilen > 0) a->input[--a->ilen] = '\0';
                gs_invalidate(w);
                return;
            }
            if (c >= 32 && c < 127 && a->ilen < (int)sizeof(a->input) - 2) {
                a->input[a->ilen++] = c;
                a->input[a->ilen] = '\0';
                gs_invalidate(w);
            }
            return;
        }
        if (c == KEY_UP || c == KEY_DOWN) {
            if (a->count > 0) {
                a->sel += (c == KEY_DOWN) ? 1 : -1;
                if (a->sel < 0) a->sel = 0;
                if (a->sel >= a->count) a->sel = a->count - 1;
                if (a->sel < a->scroll) a->scroll = a->sel;
                if (a->sel >= a->scroll + a->rows) a->scroll = a->sel - a->rows + 1;
                clamp_scroll(a);
                gs_invalidate(w);
            }
            return;
        }
        if (c == KEY_PGUP || c == KEY_PGDN) {
            a->scroll += (c == KEY_PGDN) ? a->rows : -a->rows;
            clamp_scroll(a); gs_invalidate(w); return;
        }
        if (c == KEY_HOME) { a->sel = 0; a->scroll = 0; gs_invalidate(w); return; }
        if (c == KEY_END) {
            a->sel = a->count - 1;
            if (a->sel < 0) a->sel = 0;
            a->scroll = a->sel - a->rows + 1;
            clamp_scroll(a); gs_invalidate(w); return;
        }
        if (c == KEY_LEFT)  { fa_action(a, B_UP); return; }
        if (c == KEY_RIGHT) { fa_action(a, B_OPEN); return; }
        if (c == KEY_DEL)   { fa_action(a, B_DELETE); return; }
        if (c == '\n') fa_action(a, B_OPEN);
        else if (c == '\b') fa_action(a, B_UP);
        return;
    }
}

void myfsapp_open(void) {
    fa_inst_t *a = NULL;
    for (int i = 0; i < FA_INST; i++) if (!inst[i].used) { a = &inst[i]; break; }
    if (!a) return;
    memset(a, 0, sizeof(*a));
    a->used = 1;
    a->hover_btn = -1;
    a->last_idx = -1;
    strcpy(a->path, "/");
    a->dir = fa_root();
    ga_rect_t r = { 50, 40, 700, 520 };
    a->win = gs_create(asset_str(ASSET_STR_APP_FILES), r, fa_paint, fa_event, a);
    if (!a->win) { a->used = 0; return; }
    fa_reload(a);
}
