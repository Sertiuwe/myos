#include "graphic_server.h"
#include "gfx.h"
#include "mouse.h"
#include "kbd.h"
#include "pit.h"
#include "kstring.h"
#include "kstdio.h"

static void gs_win_thread(void *arg);
static void gs_input_wake(void);

#define TASKBAR_H 30
#define CUR_W 12
#define CUR_H 18
#define GRID  24
#define GRIP  15
#define MAX_DESK 8
#define MIN_W 300
#define MIN_H 200

typedef struct {
    char label[24];
    asset_icon_id_t icon;
    void (*fn)(void);
    int used;
} gs_desk_t;

static gs_window_t  pool[GS_MAX_WINDOWS];
static gs_window_t *zhead;
static gs_window_t *focus;
static gs_window_t *drag_win;
static gs_window_t *resize_win;
static int32_t      drag_dx, drag_dy;
static int32_t      rs_ox, rs_oy, rs_ow, rs_oh;
static int          running;
static void       (*launcher_fn)(void);

static gs_desk_t desk[MAX_DESK];
static int       desk_sel = -1;
static int       desk_last_idx = -1;
static uint32_t  desk_last;

static ga_rect_t dirty;
static int       dirty_valid;
static int32_t   cur_x, cur_y;
static uint8_t   prev_btn;
static int       tb_hover;          /* -2 none, -1 exit, -3 launcher, >=0 item */
static uint64_t  clock_last;
static int       gui_tid = -1;
static int       opt_grid = 1, opt_shadow = 1, opt_clock = 1;
static uint32_t  opt_dbl_ms = 500;
static int       idle_runs;

static const uint16_t cur_shape[CUR_H] = {
    0x8000,0xC000,0xE000,0xF000,0xF800,0xFC00,0xFE00,0xFF00,0xFF80,
    0xFFC0,0xFE00,0xEF00,0xC700,0x8380,0x0380,0x01C0,0x01C0,0x00C0
};
static const uint16_t cur_inner[CUR_H] = {
    0x0000,0x0000,0x4000,0x6000,0x7000,0x7800,0x7C00,0x7E00,0x7F00,
    0x7F80,0x7C00,0x4600,0x0600,0x0300,0x0300,0x0180,0x0180,0x0000
};

/* ---------------- geometry ---------------- */

static ga_rect_t screen_rect(void) {
    ga_rect_t r = { 0, 0, (int32_t)gfx_width(), (int32_t)gfx_height() };
    return r;
}

static ga_rect_t wrect(const gs_window_t *w) {
    ga_rect_t r = w->r;
    r.x -= 2; r.y -= 2; r.w += 14; r.h += 16;
    return r;
}

static kmutex_t gs_dirty;
static int gs_dirty_ready;

static void mark(ga_rect_t r) {
    ga_rect_t s = screen_rect();
    int32_t x0 = r.x < 0 ? 0 : r.x;
    int32_t y0 = r.y < 0 ? 0 : r.y;
    int32_t x1 = r.x + r.w > s.w ? s.w : r.x + r.w;
    int32_t y1 = r.y + r.h > s.h ? s.h : r.y + r.h;
    if (x1 <= x0 || y1 <= y0) return;
    ga_rect_t c = { x0, y0, x1 - x0, y1 - y0 };
    if (!gs_dirty_ready) { kmutex_init(&gs_dirty); gs_dirty_ready = 1; }
    kmutex_lock(&gs_dirty);
    if (!dirty_valid) { dirty = c; dirty_valid = 1; kmutex_unlock(&gs_dirty); return; }
    int32_t ax = dirty.x < c.x ? dirty.x : c.x;
    int32_t ay = dirty.y < c.y ? dirty.y : c.y;
    int32_t bx = (dirty.x + dirty.w) > (c.x + c.w) ? (dirty.x + dirty.w) : (c.x + c.w);
    int32_t by = (dirty.y + dirty.h) > (c.y + c.h) ? (dirty.y + dirty.h) : (c.y + c.h);
    dirty.x = ax; dirty.y = ay; dirty.w = bx - ax; dirty.h = by - ay;
    kmutex_unlock(&gs_dirty);
}

static ga_rect_t cur_rect(int32_t x, int32_t y) {
    ga_rect_t r = { x - 1, y - 1, CUR_W + 3, CUR_H + 3 };
    return r;
}

static ga_rect_t grip_rect(const gs_window_t *w) {
    ga_rect_t r = { w->r.x + w->r.w - GRIP, w->r.y + w->r.h - GRIP, GRIP, GRIP };
    return r;
}

/* ---------------- taskbar geometry ---------------- */

static ga_rect_t taskbar_rect(void) {
    ga_rect_t r = { 0, (int32_t)gfx_height() - TASKBAR_H, (int32_t)gfx_width(), TASKBAR_H };
    return r;
}

static ga_rect_t tb_exit_rect(void) {
    ga_rect_t tb = taskbar_rect();
    ga_rect_t e = { tb.x + tb.w - 72, tb.y + 4, 64, tb.h - 8 };
    return e;
}

static ga_rect_t tb_clock_rect(void) {
    ga_rect_t tb = taskbar_rect();
    ga_rect_t c = { tb.x + tb.w - 72 - 8 - 116, tb.y + 4, 116, tb.h - 8 };
    return c;
}

static ga_rect_t tb_launch_rect(void) {
    ga_rect_t tb = taskbar_rect();
    ga_rect_t r = { tb.x + 54, tb.y + 4, 30, tb.h - 8 };
    return r;
}

static int tb_item_w(void) {
    int n = gs_window_count();
    if (n < 1) n = 1;
    int32_t start = 92;
    int32_t end = opt_clock ? tb_clock_rect().x - 8 : tb_exit_rect().x - 8;
    int32_t avail = end - start;
    if (avail < 60) avail = 60;
    int w = (int)(avail / n) - 4;
    if (w > 168) w = 168;
    if (w < 52) w = 52;
    return w;
}

static ga_rect_t tb_item_rect(int idx) {
    ga_rect_t tb = taskbar_rect();
    int w = tb_item_w();
    ga_rect_t r = { tb.x + 92 + idx * (w + 4), tb.y + 4, w, tb.h - 8 };
    return r;
}

/* double-click window in ticks, derived from the live tick rate:
   500 ms regardless of whether PIT runs at 100 Hz or 1000 Hz */
uint32_t gs_dblclick_ticks(void) {
    uint32_t hz = pit_hz();
    return hz ? (uint32_t)((uint64_t)hz * opt_dbl_ms / 1000) : opt_dbl_ms / 10;
}

void gs_set_options(int grid, int shadow, int clock) {
    opt_grid = grid; opt_shadow = shadow; opt_clock = clock;
    gs_invalidate_all();
}

void gs_set_dblclick_ms(uint32_t ms) {
    if (ms < 50) ms = 50;
    if (ms > 2000) ms = 2000;
    opt_dbl_ms = ms;
}

static ga_rect_t desk_rect(int i) {
    ga_rect_t r = { 14, 14 + i * 78, 76, 70 };
    return r;
}

/* ---------------- painting ---------------- */

static void paint_desktop(ga_rect_t d) {
    const asset_theme_t *t = asset_theme();
    ga_fill(d, t->desktop_bg);
    ga_rect_t band = { d.x, d.y, d.w, d.h };
    ga_grad_v(band, ga_mix(t->desktop_bg, t->accent, 26), t->desktop_bg);
    if (opt_grid) {
        int32_t gx0 = (d.x / GRID) * GRID;
        for (int32_t x = gx0; x < d.x + d.w; x += GRID)
            ga_vline(x, d.y, d.h, ga_mix(t->desktop_grid, t->accent, 18));
        int32_t gy0 = (d.y / GRID) * GRID;
        for (int32_t y = gy0; y < d.y + d.h; y += GRID)
            ga_hline(d.x, y, d.w, ga_mix(t->desktop_grid, t->accent, 18));
    }
}

static void paint_desk_icons(void) {
    const asset_theme_t *t = asset_theme();
    for (int i = 0; i < MAX_DESK; i++) {
        if (!desk[i].used) continue;
        ga_rect_t r = desk_rect(i);
        if (i == desk_sel) {
            ga_fill(r, ga_mix(t->list_sel, t->desktop_bg, 110));
            ga_frame(r, 1, ga_mix(t->accent, 0xFFFFFF, 60));
        }
        ga_icon_ex(r.x + (r.w - 32) / 2, r.y + 6, desk[i].icon, 2,
                   i == desk_sel ? 0xFFFFFF : ga_mix(t->accent, 0xFFFFFF, 130));
        int32_t tw = (int32_t)ga_text_width(desk[i].label);
        int32_t tx = r.x + (r.w - tw) / 2;
        if (tx < r.x + 2) tx = r.x + 2;
        ga_text_fit(tx, r.y + 46, r.w - 4, desk[i].label,
                    i == desk_sel ? 0xFFFFFF : t->text);
    }
}

static void paint_taskbar(void) {
    const asset_theme_t *t = asset_theme();
    const asset_metrics_t *m = asset_metrics();
    ga_rect_t tb = taskbar_rect();
    ga_grad_v(tb, ga_mix(t->win_bg, 0xFFFFFF, 22), t->win_bg);
    ga_hline(tb.x, tb.y, tb.w, ga_mix(t->win_border, t->accent, 80));
    ga_text(tb.x + 8, tb.y + (tb.h - (int32_t)ga_font_h()) / 2, "MyOS", t->accent);

    if (launcher_fn) {
        ga_rect_t lb = tb_launch_rect();
        ga_fill(lb, tb_hover == -3 ? t->btn_hover : t->btn_face);
        ga_frame(lb, 1, t->btn_border);
        ga_icon(lb.x + (lb.w - (int32_t)m->icon) / 2,
                lb.y + (lb.h - (int32_t)m->icon) / 2, ASSET_ICO_PLUS, t->btn_text);
    }

    int idx = 0;
    for (int i = 0; i < GS_MAX_WINDOWS; i++) {
        if (!pool[i].used) continue;
        ga_rect_t r = tb_item_rect(idx);
        int act = (&pool[i] == focus);
        int hov = (tb_hover == idx);
        uint32_t face = act ? t->win_title_active : (hov ? t->btn_hover : t->btn_face);
        ga_grad_v(r, ga_mix(face, 0xFFFFFF, act ? 40 : 16), face);
        ga_frame(r, 1, act ? ga_mix(t->accent, 0xFFFFFF, 70) : t->btn_border);
        if (act) ga_hline(r.x + 1, r.y + r.h - 2, r.w - 2, t->accent);
        ga_text_fit(r.x + 6, r.y + (r.h - (int32_t)ga_font_h()) / 2, r.w - 12,
                    pool[i].title, act ? t->win_title_text : t->btn_text);
        idx++;
    }

    ga_rect_t ck = tb_clock_rect();
    if (opt_clock) {
    ga_fill(ck, ga_mix(t->win_bg, 0x000000, 60));
    ga_frame(ck, 1, t->win_border);
    uint64_t tt = pit_ticks();
    uint32_t hz = pit_hz();
    uint32_t sec = (uint32_t)(hz ? tt / hz : 0);
    char cb[32];
    ksnprintf(cb, sizeof(cb), "up %02u:%02u:%02u",
              (unsigned)(sec / 3600), (unsigned)((sec / 60) % 60), (unsigned)(sec % 60));
    ga_text_fit(ck.x + 6, ck.y + (ck.h - (int32_t)ga_font_h()) / 2, ck.w - 12, cb, t->ok);
    }

    ga_rect_t e = tb_exit_rect();
    ga_fill(e, tb_hover == -1 ? ga_mix(t->danger, 0xFFFFFF, 60) : t->danger);
    ga_frame(e, 1, ga_mix(t->danger, 0x000000, 80));
    ga_text_fit(e.x + 8, e.y + (e.h - (int32_t)ga_font_h()) / 2, e.w - 16,
                "Exit", t->win_title_text);
}

static void composite(ga_rect_t d) {
    const asset_theme_t *t = asset_theme();
    ga_set_clip(d);
    paint_desktop(d);
    paint_desk_icons();

    gs_window_t *arr[GS_MAX_WINDOWS];
    int n = 0;
    for (gs_window_t *w = zhead; w; w = w->next)
        if (n < GS_MAX_WINDOWS) arr[n++] = w;

    for (int i = n - 1; i >= 0; i--) {
        gs_window_t *w = arr[i];
        if (opt_shadow) ga_shadow(w->r);
        ga_window_frame(w->r, w->title, w == focus);
        ga_rect_t cl = ga_window_client(w->r);
        ga_push_clip(cl);
        kmutex_lock(&w->lock);
        if (w->paint && w->used) w->paint(w, w->user);
        kmutex_unlock(&w->lock);
        ga_pop_clip();
    }

    paint_taskbar();
    ga_blit1(cur_shape, cur_x, cur_y, CUR_W, CUR_H, t->cursor_edge);
    ga_blit1(cur_inner, cur_x, cur_y, CUR_W, CUR_H, t->cursor_fill);
    ga_flush_rect(d);
}

/* ---------------- window API ---------------- */

void gs_init(void) {
    memset(pool, 0, sizeof(pool));
    zhead = NULL; focus = NULL; drag_win = NULL; resize_win = NULL;
    dirty_valid = 0; prev_btn = 0; running = 0;
    tb_hover = -2; clock_last = 0;
    desk_sel = -1; desk_last_idx = -1; desk_last = 0;
}

void gs_desktop_clear(void) {
    memset(desk, 0, sizeof(desk));
    desk_sel = -1;
    mark(screen_rect());
}

void gs_desktop_add(const char *label, asset_icon_id_t icon, void (*fn)(void)) {
    for (int i = 0; i < MAX_DESK; i++) {
        if (desk[i].used) continue;
        desk[i].used = 1;
        desk[i].icon = icon;
        desk[i].fn = fn;
        strncpy(desk[i].label, label ? label : "app", sizeof(desk[i].label) - 1);
        desk[i].label[sizeof(desk[i].label) - 1] = '\0';
        mark(desk_rect(i));
        return;
    }
}

gs_window_t *gs_create(const char *title, ga_rect_t r,
                       gs_paint_fn paint, gs_event_fn event, void *user) {
    gs_window_t *w = NULL;
    for (int i = 0; i < GS_MAX_WINDOWS; i++)
        if (!pool[i].used) { w = &pool[i]; break; }
    if (!w) return NULL;
    memset(w, 0, sizeof(*w));
    w->r = r;
    w->paint = paint;
    w->event = event;
    w->user = user;
    w->used = 1;
    w->dirty = 1;
    strncpy(w->title, title ? title : "window", GS_TITLE_MAX - 1);
    w->title[GS_TITLE_MAX - 1] = '\0';
    if (focus) mark(wrect(focus));
    w->next = zhead;
    zhead = w;
    focus = w;
    kmutex_init(&w->lock);
    w->ev_head = 0; w->ev_tail = 0;
    w->thread_quit = 0; w->thread_done = 0;
    w->tid = sched_spawn(w->title, gs_win_thread, w, 24576);
    mark(wrect(w));
    mark(taskbar_rect());
    return w;
}

void gs_destroy(gs_window_t *w) {
    if (!w || !w->used) return;
    int self = (sched_current_id() == w->tid);
    mark(wrect(w));
    if (zhead == w) zhead = w->next;
    else {
        for (gs_window_t *p = zhead; p; p = p->next)
            if (p->next == w) { p->next = w->next; break; }
    }
    w->used = 0;
    w->next = NULL;
    if (focus == w) focus = zhead;
    if (drag_win == w) drag_win = NULL;
    if (resize_win == w) resize_win = NULL;
    if (focus) mark(wrect(focus));
    mark(taskbar_rect());
    w->thread_quit = 1;
    sched_wake(w);
    if (!self) {
        int spins = 0;
        while (!w->thread_done && spins < 5000000) { sched_yield(); spins++; }
    }
}

void gs_raise(gs_window_t *w) {
    if (!w || !w->used) return;
    if (focus && focus != w) mark(wrect(focus));
    if (zhead != w) {
        for (gs_window_t *p = zhead; p; p = p->next)
            if (p->next == w) { p->next = w->next; break; }
        w->next = zhead;
        zhead = w;
    }
    focus = w;
    mark(wrect(w));
    mark(taskbar_rect());
}

void gs_set_title(gs_window_t *w, const char *title) {
    if (!w || !w->used || !title) return;
    strncpy(w->title, title, GS_TITLE_MAX - 1);
    w->title[GS_TITLE_MAX - 1] = '\0';
    mark(wrect(w));
    mark(taskbar_rect());
}

void gs_invalidate(gs_window_t *w) {
    if (!w || !w->used) return;
    w->dirty = 1;
    mark(w->r);   /* content only: shadow band repainted on geometry change */
    if (gui_tid >= 0 && sched_current_id() != gui_tid)
        sched_wake_tid(gui_tid);
}
void gs_invalidate_all(void) { dirty_valid = 0; mark(screen_rect()); }
ga_rect_t gs_client(const gs_window_t *w) { return ga_window_client(w->r); }
int  gs_active(const gs_window_t *w) { return w == focus; }
void gs_quit(void) { running = 0; }

int gs_window_count(void) {
    int n = 0;
    for (int i = 0; i < GS_MAX_WINDOWS; i++) if (pool[i].used) n++;
    return n;
}

void gs_pointer(int32_t *sx, int32_t *sy, uint8_t *buttons) {
    if (sx) *sx = cur_x;
    if (sy) *sy = cur_y;
    if (buttons) *buttons = prev_btn;
}

void gs_set_launcher(void (*fn)(void)) {
    launcher_fn = fn;
    mark(taskbar_rect());
}

/* ---------------- routing ---------------- */

static gs_window_t *hit_window(int32_t x, int32_t y) {
    for (gs_window_t *w = zhead; w; w = w->next)
        if (ga_hit(w->r, x, y)) return w;
    return NULL;
}

static void evq_push(gs_window_t *w, const gs_event_t *ev) {
    int nxt = (w->ev_tail + 1) % 32;
    if (nxt == w->ev_head) return;      /* overflow: drop, app will repaint */
    w->evq[w->ev_tail] = *ev;
    w->ev_tail = nxt;
}

static int evq_pop(gs_window_t *w, gs_event_t *ev) {
    if (w->ev_head == w->ev_tail) return 0;
    *ev = w->evq[w->ev_head];
    w->ev_head = (w->ev_head + 1) % 32;
    return 1;
}

/* The window's own task: pumps its event queue. App logic therefore runs on
   the scheduler, not inside the compositor. */
static void gs_input_wake(void) {
    /* called from IRQ context: wake the compositor so idle sleep is free */
    if (gui_tid >= 0) sched_wake_tid(gui_tid);
}

static void gs_win_thread(void *arg) {
    gs_window_t *w = arg;
    sched_set_base_prio(sched_current_id(), PRIO_NORM);
    for (;;) {
        if (w->thread_quit) break;
        gs_event_t ev;
        if (!evq_pop(w, &ev)) { sched_block_on(w); continue; }
        kmutex_lock(&w->lock);
        if (w->event && w->used) w->event(w, &ev, w->user);
        kmutex_unlock(&w->lock);
    }
    w->thread_done = 1;
    sched_exit();
}

static void deliver(gs_window_t *w, gs_event_type_t type, int32_t sx, int32_t sy, char key) {
    if (!w || !w->used || !w->event) return;
    gs_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.sx = sx; ev.sy = sy;
    ev.x = sx - w->r.x;
    ev.y = sy - w->r.y;
    ev.buttons = prev_btn;
    ev.key = key;
    evq_push(w, &ev);
    sched_wake(w);
}

static void clamp_window(gs_window_t *w) {
    ga_rect_t s = screen_rect();
    int32_t maxw = s.w, maxh = s.h - TASKBAR_H;
    if (w->r.w > maxw) w->r.w = maxw;
    if (w->r.h > maxh) w->r.h = maxh;
    if (w->r.w < MIN_W) w->r.w = MIN_W;
    if (w->r.h < MIN_H) w->r.h = MIN_H;
    int32_t maxx = s.w - w->r.w;
    int32_t maxy = maxh - w->r.h;
    if (maxx < 0) maxx = 0;
    if (maxy < 0) maxy = 0;
    if (w->r.x < 0) w->r.x = 0;
    if (w->r.y < 0) w->r.y = 0;
    if (w->r.x > maxx) w->r.x = maxx;
    if (w->r.y > maxy) w->r.y = maxy;
}

static int taskbar_hover_at(int32_t x, int32_t y) {
    ga_rect_t tb = taskbar_rect();
    if (!ga_hit(tb, x, y)) return -2;
    if (ga_hit(tb_exit_rect(), x, y)) return -1;
    if (launcher_fn && ga_hit(tb_launch_rect(), x, y)) return -3;
    int idx = 0;
    for (int i = 0; i < GS_MAX_WINDOWS; i++) {
        if (!pool[i].used) continue;
        if (ga_hit(tb_item_rect(idx), x, y)) return idx;
        idx++;
    }
    return -2;
}

static void on_press(int32_t x, int32_t y) {
    ga_rect_t tb = taskbar_rect();
    if (ga_hit(tb, x, y)) {
        if (ga_hit(tb_exit_rect(), x, y)) { gs_quit(); return; }
        if (launcher_fn && ga_hit(tb_launch_rect(), x, y)) { launcher_fn(); return; }
        int idx = 0;
        for (int i = 0; i < GS_MAX_WINDOWS; i++) {
            if (!pool[i].used) continue;
            if (ga_hit(tb_item_rect(idx), x, y)) { gs_raise(&pool[i]); return; }
            idx++;
        }
        return;
    }

    for (int i = 0; i < MAX_DESK; i++) {
        if (!desk[i].used) continue;
        if (!ga_hit(desk_rect(i), x, y)) continue;
        uint32_t now = (uint32_t)pit_ticks();
        /* open on a real double click, OR on a second tap of an icon that is
           already selected (touch-friendly: select, then tap again to open) */
        int dbl = (desk_sel == i) ||
                  (desk_last_idx == i && (now - desk_last) < gs_dblclick_ticks());
        if (desk_sel != i) { mark(desk_rect(desk_sel >= 0 ? desk_sel : i)); desk_sel = i; mark(desk_rect(i)); }
        desk_last_idx = i;
        desk_last = now;
        if (dbl && desk[i].fn) desk[i].fn();
        return;
    }
    if (desk_sel >= 0) { mark(desk_rect(desk_sel)); desk_sel = -1; }

    gs_window_t *w = hit_window(x, y);
    if (!w) return;
    gs_raise(w);

    if (ga_hit(ga_window_close(w->r), x, y)) {
        deliver(w, GS_EV_CLOSE, x, y, 0);
        if (w->used) gs_destroy(w);
        return;
    }
    if (ga_hit(grip_rect(w), x, y)) {
        resize_win = w;
        rs_ox = x; rs_oy = y;
        rs_ow = w->r.w; rs_oh = w->r.h;
        return;
    }
    ga_rect_t cl = ga_window_client(w->r);
    if (!ga_hit(cl, x, y)) {
        drag_win = w;
        drag_dx = x - w->r.x;
        drag_dy = y - w->r.y;
        return;
    }
    deliver(w, GS_EV_MOUSE_DOWN, x, y, 0);
}

static void on_release(int32_t x, int32_t y) {
    if (drag_win) { drag_win = NULL; return; }
    if (resize_win) { resize_win = NULL; return; }
    gs_window_t *w = hit_window(x, y);
    if (w) deliver(w, GS_EV_MOUSE_UP, x, y, 0);
}

/* ---------------- loop ---------------- */

void gs_run(void) {
    const mouse_state_t *ms = mouse_get_state();
    cur_x = ms->x; cur_y = ms->y;
    prev_btn = ms->buttons;
    running = 1;
    gui_tid = sched_current_id();
    mouse_set_input_hook(gs_input_wake);
    kbd_set_input_hook(gs_input_wake);
    idle_runs = 0;
    dirty_valid = 0;
    clock_last = pit_ticks();
    mark(screen_rect());

    while (running) {
        ms = mouse_get_state();
        int32_t nx = ms->x, ny = ms->y;

        int work = 0;
        if (nx != cur_x || ny != cur_y) {
            work = 1;
            mark(cur_rect(cur_x, cur_y));
            if (resize_win) {
                ga_rect_t old = wrect(resize_win);
                resize_win->r.w = rs_ow + (nx - rs_ox);
                resize_win->r.h = rs_oh + (ny - rs_oy);
                clamp_window(resize_win);
                mark(old);
                mark(wrect(resize_win));
                resize_win->dirty = 1;
            } else if (drag_win) {
                ga_rect_t old = wrect(drag_win);
                drag_win->r.x = nx - drag_dx;
                drag_win->r.y = ny - drag_dy;
                clamp_window(drag_win);
                drag_dx = nx - drag_win->r.x;
                drag_dy = ny - drag_win->r.y;
                mark(old);
                mark(wrect(drag_win));
            } else {
                int h = taskbar_hover_at(nx, ny);
                if (h != tb_hover) { tb_hover = h; mark(taskbar_rect()); }
                gs_window_t *hw = hit_window(nx, ny);
                if (hw) deliver(hw, GS_EV_MOUSE_MOVE, nx, ny, 0);
            }
            cur_x = nx; cur_y = ny;
            mark(cur_rect(cur_x, cur_y));
        }

        uint8_t b = ms->buttons;
        if ((b & 1) && !(prev_btn & 1)) { prev_btn = b; on_press(cur_x, cur_y); }
        else if (!(b & 1) && (prev_btn & 1)) { prev_btn = b; on_release(cur_x, cur_y); }
        else prev_btn = b;

        char c = kbd_getchar();
        if (c) work = 1;
        if (c && focus) deliver(focus, GS_EV_KEY, cur_x, cur_y, c);

        for (int i = 0; i < GS_MAX_WINDOWS; i++) {
            if (pool[i].used && pool[i].dirty) {
                mark(wrect(&pool[i]));
                pool[i].dirty = 0;
            }
        }

        uint64_t now = pit_ticks();
        uint32_t hz = pit_hz();
        if (hz && now - clock_last >= hz) {
            clock_last = now;
            mark(tb_clock_rect());
            if (focus) deliver(focus, GS_EV_TIMER, cur_x, cur_y, 0);
        }

        kmutex_lock(&gs_dirty);
        ga_rect_t d = dirty;
        int has = dirty_valid;
        dirty_valid = 0;
        kmutex_unlock(&gs_dirty);
        if (has) { composite(d); work = 1; }
        if (work) idle_runs = 0;
        else if (idle_runs < 4) idle_runs++;
        pit_sleep_ms(idle_runs ? 20 : 2);
    }
    mouse_set_input_hook(NULL);
    kbd_set_input_hook(NULL);
    mark(screen_rect());
}
