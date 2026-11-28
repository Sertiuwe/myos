#include "graphic_api.h"
#include "gfx.h"
#include "kstring.h"

#define CLIP_DEPTH 8

static ga_canvas_t scr;
static ga_rect_t   clip_stack[CLIP_DEPTH];
static int         clip_top;
static uint16_t    f_w = 8, f_h = 16;
static int         cell_mode;

/* ---------- font probing: no guessing about the internal font ---------- */

static uint32_t probe_save[64 * 40];

static void probe_clear(uint32_t *bb, uint32_t W) {
    for (int y = 0; y < 40; y++)
        for (int x = 0; x < 64; x++)
            bb[(uint32_t)y * W + (uint32_t)x] = 0;
}

static void probe_ink(uint32_t *bb, uint32_t W,
                      int *minx, int *miny, int *maxx, int *maxy) {
    *minx = -1; *miny = -1; *maxx = -1; *maxy = -1;
    for (int y = 0; y < 40; y++) {
        for (int x = 0; x < 64; x++) {
            if (!bb[(uint32_t)y * W + (uint32_t)x]) continue;
            if (*minx < 0 || x < *minx) *minx = x;
            if (*miny < 0 || y < *miny) *miny = y;
            if (x > *maxx) *maxx = x;
            if (y > *maxy) *maxy = y;
        }
    }
}

static void probe_font(void) {
    uint32_t *bb = gfx_backbuffer();
    uint32_t W = gfx_width(), H = gfx_height();
    if (!bb || W < 64 || H < 40) return;

    for (int y = 0; y < 40; y++)
        for (int x = 0; x < 64; x++)
            probe_save[y * 64 + x] = bb[(uint32_t)y * W + (uint32_t)x];

    int ax0, ay0, ax1, ay1;
    probe_clear(bb, W);
    gfx_draw_char('X', 0, 0, 0xFFFFFF);
    probe_ink(bb, W, &ax0, &ay0, &ax1, &ay1);

    int bx0, by0, bx1, by1;
    probe_clear(bb, W);
    gfx_draw_char('X', 1, 0, 0xFFFFFF);
    probe_ink(bb, W, &bx0, &by0, &bx1, &by1);

    int cx0, cy0, cx1, cy1;
    probe_clear(bb, W);
    gfx_draw_char('X', 0, 1, 0xFFFFFF);
    probe_ink(bb, W, &cx0, &cy0, &cx1, &cy1);

    for (int y = 0; y < 40; y++)
        for (int x = 0; x < 64; x++)
            bb[(uint32_t)y * W + (uint32_t)x] = probe_save[y * 64 + x];

    if (ax0 < 0 || ax1 < 0 || ay1 < 0) return;   /* keep defaults */

    int ink_w = ax1 - ax0 + 1;
    int ink_h = ay1 - ay0 + 1;
    int dx = (bx0 >= 0) ? (bx0 - ax0) : 1;
    int dy = (cy0 >= 0) ? (cy0 - ay0) : 1;

    if (dx <= 1 && dy <= 1) {
        cell_mode = 0;                  /* gfx_draw_char takes pixels */
        f_w = (uint16_t)(ink_w + 1);
        f_h = (uint16_t)(ink_h + 2);
    } else {
        cell_mode = 1;                  /* gfx_draw_char takes character cells */
        f_w = (uint16_t)((dx > 1) ? dx : ink_w + 1);
        f_h = (uint16_t)((dy > 1) ? dy : ink_h + 2);
    }
    if (f_w < 4) f_w = 8;
    if (f_h < 6) f_h = 16;
}

void ga_init(void) {
    probe_font();
    scr.pix    = gfx_backbuffer();
    scr.stride = gfx_width();
    scr.w      = gfx_width();
    scr.h      = gfx_height();
    scr.clip.x = 0; scr.clip.y = 0;
    scr.clip.w = (int32_t)scr.w; scr.clip.h = (int32_t)scr.h;
    clip_top = 0;
    clip_stack[0] = scr.clip;
    asset_init(f_w, f_h);
}

ga_canvas_t *ga_screen(void) { return &scr; }
uint16_t ga_font_w(void) { return f_w; }
uint16_t ga_font_h(void) { return f_h; }
int      ga_font_cell_mode(void) { return cell_mode; }

/* ---------- clipping ---------- */

static ga_rect_t isect(ga_rect_t a, ga_rect_t b) {
    int32_t x0 = a.x > b.x ? a.x : b.x;
    int32_t y0 = a.y > b.y ? a.y : b.y;
    int32_t x1 = (a.x + a.w) < (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    int32_t y1 = (a.y + a.h) < (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
    ga_rect_t r;
    r.x = x0; r.y = y0;
    r.w = (x1 > x0) ? (x1 - x0) : 0;
    r.h = (y1 > y0) ? (y1 - y0) : 0;
    return r;
}

void ga_set_clip(ga_rect_t r) {
    ga_rect_t full = { 0, 0, (int32_t)scr.w, (int32_t)scr.h };
    scr.clip = isect(r, full);
    if (clip_top >= 0) clip_stack[clip_top] = scr.clip;
}

ga_rect_t ga_clip(void) { return scr.clip; }

void ga_push_clip(ga_rect_t r) {
    if (clip_top + 1 >= CLIP_DEPTH) return;
    clip_stack[clip_top] = scr.clip;
    clip_top++;
    scr.clip = isect(r, scr.clip);
    clip_stack[clip_top] = scr.clip;
}

void ga_pop_clip(void) {
    if (clip_top <= 0) return;
    clip_top--;
    scr.clip = clip_stack[clip_top];
}

/* ---------- primitives ---------- */

void ga_fill(ga_rect_t r, uint32_t col) {
    if (!scr.pix) return;
    ga_rect_t c = isect(r, scr.clip);
    if (c.w <= 0 || c.h <= 0) return;
    for (int32_t y = c.y; y < c.y + c.h; y++) {
        uint32_t *row = scr.pix + (uint32_t)y * scr.stride + (uint32_t)c.x;
        for (int32_t x = 0; x < c.w; x++) row[x] = col;
    }
}

void ga_hline(int32_t x, int32_t y, int32_t w, uint32_t col) {
    ga_rect_t r = { x, y, w, 1 };
    ga_fill(r, col);
}

void ga_vline(int32_t x, int32_t y, int32_t h, uint32_t col) {
    ga_rect_t r = { x, y, 1, h };
    ga_fill(r, col);
}

void ga_frame(ga_rect_t r, uint16_t t, uint32_t col) {
    if (t == 0) return;
    ga_rect_t top = { r.x, r.y, r.w, (int32_t)t };
    ga_rect_t bot = { r.x, r.y + r.h - (int32_t)t, r.w, (int32_t)t };
    ga_rect_t lef = { r.x, r.y, (int32_t)t, r.h };
    ga_rect_t rig = { r.x + r.w - (int32_t)t, r.y, (int32_t)t, r.h };
    ga_fill(top, col); ga_fill(bot, col);
    ga_fill(lef, col); ga_fill(rig, col);
}

void ga_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t col) {
    int32_t dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int32_t dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int32_t sx = (x0 < x1) ? 1 : -1;
    int32_t sy = (y0 < y1) ? 1 : -1;
    int32_t err = dx - dy;
    for (;;) {
        ga_rect_t p = { x0, y0, 1, 1 };
        ga_fill(p, col);
        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = err * 2;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

void ga_blit1(const uint16_t *rows, int32_t x, int32_t y,
              int32_t w, int32_t h, uint32_t col) {
    if (!rows || !scr.pix) return;
    for (int32_t ry = 0; ry < h; ry++) {
        uint16_t bits = rows[ry];
        for (int32_t rx = 0; rx < w; rx++) {
            if (bits & (uint16_t)(0x8000u >> rx)) {
                ga_rect_t p = { x + rx, y + ry, 1, 1 };
                ga_fill(p, col);
            }
        }
    }
}

void ga_icon(int32_t x, int32_t y, asset_icon_id_t id, uint32_t col) {
    ga_blit1(asset_icon(id), x, y, ASSET_ICON_SIZE, ASSET_ICON_SIZE, col);
}

uint32_t ga_text_width(const char *s) {
    return s ? (uint32_t)(strlen(s) * f_w) : 0;
}

void ga_text(int32_t x, int32_t y, const char *s, uint32_t col) {
    if (!s || !scr.pix) return;
    ga_rect_t c = scr.clip;
    int32_t cx = x;
    for (const char *p = s; *p; p++) {
        ga_rect_t cell = { cx, y, (int32_t)f_w, (int32_t)f_h };
        ga_rect_t vis = isect(cell, c);
        if (vis.w == cell.w && vis.h == cell.h) {
            if (cell_mode)
                gfx_draw_char(*p, (int)(cx / (int32_t)f_w), (int)(y / (int32_t)f_h), col);
            else
                gfx_draw_char(*p, (int)cx, (int)y, col);
        }
        cx += (int32_t)f_w;
        if (cx >= c.x + c.w) break;
    }
}

void ga_text_fit(int32_t x, int32_t y, int32_t maxw, const char *s, uint32_t col) {
    if (!s || maxw <= 0) return;
    int32_t fit = maxw / (int32_t)f_w;
    int32_t len = (int32_t)strlen(s);
    if (len <= fit) { ga_text(x, y, s, col); return; }
    char buf[256];
    int32_t keep = fit - 2;
    if (keep < 0) keep = 0;
    if (keep > 253) keep = 253;
    memcpy(buf, s, (size_t)keep);
    buf[keep] = '.'; buf[keep + 1] = '.'; buf[keep + 2] = '\0';
    ga_text(x, y, buf, col);
}

/* ---------- widgets ---------- */

int ga_hit(ga_rect_t r, int32_t px, int32_t py) {
    return px >= r.x && py >= r.y && px < r.x + r.w && py < r.y + r.h;
}

ga_rect_t ga_button(int32_t x, int32_t y, const char *label, int icon, int state) {
    const asset_theme_t *t = asset_theme();
    const asset_metrics_t *m = asset_metrics();
    int32_t tw = (int32_t)ga_text_width(label);
    int32_t iw = (icon >= 0) ? (int32_t)m->icon + 4 : 0;
    ga_rect_t r = { x, y, tw + iw + 2 * (int32_t)m->btn_pad_x, (int32_t)m->btn_h };

    uint32_t face = (state == GA_BTN_PRESSED) ? t->btn_pressed
                  : (state == GA_BTN_HOVER)   ? t->btn_hover
                                              : t->btn_face;
    ga_fill(r, face);
    ga_frame(r, 1, t->btn_border);

    int32_t tx = x + (int32_t)m->btn_pad_x;
    int32_t iy = y + ((int32_t)m->btn_h - (int32_t)m->icon) / 2;
    if (icon >= 0) {
        ga_icon(tx, iy, (asset_icon_id_t)icon, t->btn_text);
        tx += iw;
    }
    int32_t ty = y + ((int32_t)m->btn_h - (int32_t)f_h) / 2;
    ga_text(tx, ty, label, t->btn_text);
    return r;
}

void ga_list_row(ga_rect_t r, int idx, int selected, int icon,
                 const char *name, const char *right) {
    const asset_theme_t *t = asset_theme();
    const asset_metrics_t *m = asset_metrics();
    uint32_t bg = selected ? t->list_sel : ((idx & 1) ? t->list_alt : t->list_bg);
    uint32_t fg = selected ? t->list_sel_text : t->text;
    ga_fill(r, bg);
    int32_t ty = r.y + (r.h - (int32_t)f_h) / 2;
    int32_t tx = r.x + 4;
    if (icon >= 0) {
        ga_icon(tx, r.y + (r.h - (int32_t)m->icon) / 2, (asset_icon_id_t)icon, fg);
        tx += (int32_t)m->icon + 6;
    }
    int32_t rw = right ? (int32_t)ga_text_width(right) + 8 : 0;
    ga_text_fit(tx, ty, r.x + r.w - rw - tx - 4, name, fg);
    if (right) ga_text(r.x + r.w - rw + 4, ty, right, selected ? fg : t->text_dim);
}

ga_rect_t ga_window_client(ga_rect_t r) {
    const asset_metrics_t *m = asset_metrics();
    ga_rect_t c;
    c.x = r.x + (int32_t)m->border;
    c.y = r.y + (int32_t)m->title_h;
    c.w = r.w - 2 * (int32_t)m->border;
    c.h = r.h - (int32_t)m->title_h - (int32_t)m->border;
    return c;
}

ga_rect_t ga_window_close(ga_rect_t r) {
    const asset_metrics_t *m = asset_metrics();
    int32_t s = (int32_t)m->close_size;
    ga_rect_t b;
    b.x = r.x + r.w - (int32_t)m->border - s - 4;
    b.y = r.y + ((int32_t)m->title_h - s) / 2;
    b.w = s; b.h = s;
    return b;
}

void ga_window_frame(ga_rect_t r, const char *title, int active) {
    const asset_theme_t *t = asset_theme();
    const asset_metrics_t *m = asset_metrics();

    ga_fill(r, t->win_bg);
    ga_frame(r, m->border, active ? ga_mix(t->win_border, t->accent, 90) : t->win_border);

    ga_rect_t tb = { r.x + (int32_t)m->border, r.y + (int32_t)m->border,
                     r.w - 2 * (int32_t)m->border,
                     (int32_t)m->title_h - (int32_t)m->border };
    if (active) ga_grad_v(tb, ga_mix(t->win_title_active, 0xFFFFFF, 45), t->win_title_active);
    else        ga_grad_v(tb, ga_mix(t->win_title_inactive, 0xFFFFFF, 18), t->win_title_inactive);
    ga_hline(tb.x, tb.y, tb.w, ga_mix(active ? t->win_title_active : t->win_title_inactive, 0xFFFFFF, 110));

    ga_rect_t cb = ga_window_close(r);
    int32_t avail = cb.x - tb.x - 8;
    ga_text_fit(tb.x + 8, tb.y + (tb.h - (int32_t)f_h) / 2, avail,
                title, active ? t->win_title_text : t->win_title_text_dim);

    ga_fill(cb, active ? t->danger : ga_mix(t->btn_face, t->win_title_inactive, 120));
    ga_frame(cb, 1, ga_mix(t->danger, 0x000000, 90));
    ga_icon(cb.x + (cb.w - (int32_t)m->icon) / 2,
            cb.y + (cb.h - (int32_t)m->icon) / 2,
            ASSET_ICO_CLOSE, t->win_title_text);

    ga_rect_t cl = ga_window_client(r);
    ga_fill(cl, t->client_bg);

    /* resize grip */
    int32_t gx = r.x + r.w - 13, gy = r.y + r.h - 13;
    for (int i = 0; i < 3; i++) {
        int32_t o = 3 + i * 4;
        ga_line(gx + o, gy + 11, gx + 11, gy + o, ga_mix(t->win_border, t->accent, 70));
    }
}

void ga_flush_rect(ga_rect_t r) {
    ga_rect_t full = { 0, 0, (int32_t)scr.w, (int32_t)scr.h };
    ga_rect_t c = isect(r, full);
    if (c.w <= 0 || c.h <= 0) return;
    gfx_flush_rect((uint32_t)c.x, (uint32_t)c.y, (uint32_t)c.w, (uint32_t)c.h);
}

ga_rect_t ga_scroll_thumb(ga_rect_t r, int total, int visible, int offset) {
    ga_rect_t th = r;
    if (total <= visible || visible <= 0 || r.h <= 0) return th;
    int32_t h = (int32_t)((int64_t)r.h * visible / total);
    if (h < 14) h = 14;
    if (h > r.h) h = r.h;
    int maxoff = total - visible;
    if (offset < 0) offset = 0;
    if (offset > maxoff) offset = maxoff;
    int32_t y = r.y + (maxoff ? (int32_t)((int64_t)(r.h - h) * offset / maxoff) : 0);
    th.y = y; th.h = h;
    return th;
}

void ga_scrollbar(ga_rect_t r, int total, int visible, int offset) {
    const asset_theme_t *t = asset_theme();
    ga_fill(r, t->scroll_track);
    ga_rect_t th = ga_scroll_thumb(r, total, visible, offset);
    ga_fill(th, (total <= visible) ? t->scroll_track : t->scroll_thumb);
    ga_frame(th, 1, t->win_border);
}

uint32_t ga_mix(uint32_t a, uint32_t b, int t) {
    if (t < 0) t = 0;
    if (t > 255) t = 255;
    uint32_t ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    uint32_t br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    uint32_t r = ar + ((br - ar) * (uint32_t)t) / 255;
    uint32_t g = ag + ((bg - ag) * (uint32_t)t) / 255;
    uint32_t bl = ab + ((bb - ab) * (uint32_t)t) / 255;
    return (r << 16) | (g << 8) | bl;
}

void ga_grad_v(ga_rect_t r, uint32_t top, uint32_t bottom) {
    ga_rect_t c = isect(r, scr.clip);
    if (c.w <= 0 || c.h <= 0 || !scr.pix) return;
    for (int32_t y = c.y; y < c.y + c.h; y++) {
        int t = (r.h > 1) ? (int)((int64_t)(y - r.y) * 255 / (r.h - 1)) : 0;
        uint32_t col = ga_mix(top, bottom, t);
        uint32_t *row = scr.pix + (uint32_t)y * scr.stride + (uint32_t)c.x;
        for (int32_t x = 0; x < c.w; x++) row[x] = col;
    }
}

void ga_darken(ga_rect_t r, int amt) {
    ga_rect_t c = isect(r, scr.clip);
    if (c.w <= 0 || c.h <= 0 || !scr.pix) return;
    if (amt < 0) amt = 0;
    if (amt > 256) amt = 256;
    uint32_t k = (uint32_t)(256 - amt);
    for (int32_t y = c.y; y < c.y + c.h; y++) {
        uint32_t *row = scr.pix + (uint32_t)y * scr.stride + (uint32_t)c.x;
        for (int32_t x = 0; x < c.w; x++) {
            uint32_t p = row[x];
            uint32_t rr = ((p >> 16) & 0xFF) * k / 256;
            uint32_t gg = ((p >> 8) & 0xFF) * k / 256;
            uint32_t bb = (p & 0xFF) * k / 256;
            row[x] = (rr << 16) | (gg << 8) | bb;
        }
    }
}

void ga_shadow(ga_rect_t r) {
    ga_rect_t s3 = { r.x + 8, r.y + 9, r.w, r.h };
    ga_rect_t s2 = { r.x + 5, r.y + 6, r.w, r.h };
    ga_rect_t s1 = { r.x + 3, r.y + 3, r.w, r.h };
    ga_darken(s3, 26);
    ga_darken(s2, 34);
    ga_darken(s1, 52);
}

void ga_icon_ex(int32_t x, int32_t y, asset_icon_id_t id, int scale, uint32_t col) {
    const uint16_t *rows = asset_icon(id);
    if (scale < 1) scale = 1;
    for (int ry = 0; ry < ASSET_ICON_SIZE; ry++) {
        uint16_t bits = rows[ry];
        for (int rx = 0; rx < ASSET_ICON_SIZE; rx++) {
            if (bits & (uint16_t)(0x8000u >> rx)) {
                ga_rect_t p = { x + rx * scale, y + ry * scale, scale, scale };
                ga_fill(p, col);
            }
        }
    }
}
