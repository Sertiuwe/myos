#ifndef GRAPHIC_API_H
#define GRAPHIC_API_H

#include <stdint.h>
#include "asset.h"

typedef struct { int32_t x, y, w, h; } ga_rect_t;

typedef struct {
    uint32_t *pix;      /* authoritative canvas = gfx_backbuffer() */
    uint32_t  stride;   /* pixels per row */
    uint32_t  w, h;
    ga_rect_t clip;
} ga_canvas_t;

/* button visual state */
#define GA_BTN_NORMAL  0
#define GA_BTN_HOVER   1
#define GA_BTN_PRESSED 2

void ga_init(void);
ga_canvas_t *ga_screen(void);

void      ga_set_clip(ga_rect_t r);
ga_rect_t ga_clip(void);
void      ga_push_clip(ga_rect_t r);   /* intersect with current */
void      ga_pop_clip(void);

uint16_t ga_font_w(void);
uint16_t ga_font_h(void);
int      ga_font_cell_mode(void);
uint32_t ga_text_width(const char *s);

void ga_fill(ga_rect_t r, uint32_t col);
void ga_frame(ga_rect_t r, uint16_t t, uint32_t col);
void ga_hline(int32_t x, int32_t y, int32_t w, uint32_t col);
void ga_vline(int32_t x, int32_t y, int32_t h, uint32_t col);
void ga_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t col);
void ga_text(int32_t x, int32_t y, const char *s, uint32_t col);
void ga_text_fit(int32_t x, int32_t y, int32_t maxw, const char *s, uint32_t col);
void ga_blit1(const uint16_t *rows, int32_t x, int32_t y,
              int32_t w, int32_t h, uint32_t col);
void ga_icon(int32_t x, int32_t y, asset_icon_id_t id, uint32_t col);

int  ga_hit(ga_rect_t r, int32_t px, int32_t py);
ga_rect_t ga_button(int32_t x, int32_t y, const char *label, int icon, int state);
void ga_list_row(ga_rect_t r, int idx, int selected, int icon,
                 const char *name, const char *right);

void      ga_window_frame(ga_rect_t r, const char *title, int active);
ga_rect_t ga_window_client(ga_rect_t r);
ga_rect_t ga_window_close(ga_rect_t r);

void ga_flush_rect(ga_rect_t r);

void ga_scrollbar(ga_rect_t r, int total, int visible, int offset);
ga_rect_t ga_scroll_thumb(ga_rect_t r, int total, int visible, int offset);
uint32_t ga_mix(uint32_t a, uint32_t b, int t);
void ga_grad_v(ga_rect_t r, uint32_t top, uint32_t bottom);
void ga_darken(ga_rect_t r, int amt);
void ga_shadow(ga_rect_t r);
void ga_icon_ex(int32_t x, int32_t y, asset_icon_id_t id, int scale, uint32_t col);
#endif
