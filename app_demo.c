#include "app_demo.h"
#include "graphic_server.h"
#include "asset.h"
#include "pit.h"
#include "kstdio.h"
#include "kstring.h"

static void files_paint(gs_window_t *w, void *user) {
    (void)user;
    const asset_theme_t *t = asset_theme();
    const asset_metrics_t *m = asset_metrics();
    ga_rect_t cl = gs_client(w);

    int32_t bx = cl.x + (int32_t)m->gap;
    int32_t by = cl.y + (int32_t)m->gap;
    ga_rect_t b1 = ga_button(bx, by, asset_str(ASSET_STR_BTN_UP), ASSET_ICO_UP, GA_BTN_NORMAL);
    bx = b1.x + b1.w + (int32_t)m->gap;
    ga_rect_t b2 = ga_button(bx, by, asset_str(ASSET_STR_BTN_NEW_FOLDER), ASSET_ICO_PLUS, GA_BTN_NORMAL);
    bx = b2.x + b2.w + (int32_t)m->gap;
    ga_rect_t b3 = ga_button(bx, by, asset_str(ASSET_STR_BTN_REFRESH), ASSET_ICO_REFRESH, GA_BTN_NORMAL);
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
    static const int   ico[]   = { ASSET_ICO_FOLDER, ASSET_ICO_FILE, ASSET_ICO_FOLDER,
                                   ASSET_ICO_FILE, ASSET_ICO_FILE };
    static const char *sizes[] = { "12 B", "35 B", "0 B", "24 B", "1024 B" };
    for (int i = 0; i < 5; i++) {
        ga_rect_t row = { cl.x + (int32_t)m->gap, ly,
                          cl.w - 2 * (int32_t)m->gap, (int32_t)m->row_h };
        if (row.y + row.h > cl.y + cl.h) break;
        ga_list_row(row, i, i == 1, ico[i], names[i], sizes[i]);
        ly += row.h;
    }
    ga_text(cl.x + (int32_t)m->gap, cl.y + cl.h - (int32_t)ga_font_h() - 4,
            "stage 2: window manager demo (static content)", t->text_dim);
}

static void files_event(gs_window_t *w, const gs_event_t *ev, void *user) {
    (void)user;
    if (ev->type == GS_EV_MOUSE_DOWN) gs_invalidate(w);
}

static void about_paint(gs_window_t *w, void *user) {
    (void)user;
    const asset_theme_t *t = asset_theme();
    ga_rect_t cl = gs_client(w);
    int32_t y = cl.y + 8;
    int32_t lh = (int32_t)ga_font_h() + 4;
    ga_text(cl.x + 10, y, "MyOS - custom x86_64 kernel", t->text); y += lh;
    ga_text(cl.x + 10, y, "graphic_server: windows / focus / drag", t->text_dim); y += lh;
    ga_text(cl.x + 10, y, "graphic_api: primitives + widgets", t->text_dim); y += lh;
    ga_text(cl.x + 10, y, "asset: theme / metrics / icons", t->text_dim); y += lh * 2;

    char buf[96];
    uint64_t tt = pit_ticks();
    uint32_t hz = pit_hz();
    ksnprintf(buf, sizeof(buf), "uptime: %u s   windows: %d",
              (unsigned)(hz ? tt / hz : 0), gs_window_count());
    ga_text(cl.x + 10, y, buf, t->accent); y += lh;
    ga_text(cl.x + 10, y, "drag title bar, click taskbar, red X closes", t->text_dim);
}

void app_demo_open(void) {
    ga_rect_t a = { 60, 60, 620, 460 };
    gs_create(asset_str(ASSET_STR_APP_FILES), a, files_paint, files_event, NULL);
    ga_rect_t b = { 340, 250, 400, 230 };
    gs_create("About MyOS", b, about_paint, NULL, NULL);
}

void app_demo_about(void) {
    ga_rect_t b = { 400, 300, 400, 230 };
    gs_create("About MyOS", b, about_paint, NULL, NULL);
}
