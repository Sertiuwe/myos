#include "cursor.h"
#include "kstring.h"

#define CURSOR_W 8
#define CURSOR_H 12

static const uint8_t cursor_mask[CURSOR_H] = {
    0xFF, 0x7F, 0x3F, 0x1F, 0x0F, 0x07, 0x03, 0x01,
    0x03, 0x07, 0x0C, 0x00
};

static uint32_t *framebuffer;
static uint16_t fb_w, fb_h, fb_pitch_bytes;
static uint8_t fb_bpp;

static uint32_t saved_bg[CURSOR_W * CURSOR_H];
static int16_t cur_x = -1, cur_y = -1;
static int bg_valid;

static uint32_t get_pixel(uint16_t x, uint16_t y) {
    uint8_t *row = (uint8_t *)framebuffer + (size_t)y * fb_pitch_bytes;
    if (fb_bpp == 32) return ((uint32_t *)row)[x];
    if (fb_bpp == 24) {
        uint8_t *p = row + (size_t)x * 3;
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
    }
    return 0;
}

static void set_pixel(uint16_t x, uint16_t y, uint32_t color) {
    uint8_t *row = (uint8_t *)framebuffer + (size_t)y * fb_pitch_bytes;
    if (fb_bpp == 32) {
        ((uint32_t *)row)[x] = color;
    } else if (fb_bpp == 24) {
        uint8_t *p = row + (size_t)x * 3;
        p[0] = (uint8_t)(color);
        p[1] = (uint8_t)(color >> 8);
        p[2] = (uint8_t)(color >> 16);
    }
}

static void save_bg(int16_t x, int16_t y) {
    for (int cy = 0; cy < CURSOR_H; cy++) {
        for (int cx = 0; cx < CURSOR_W; cx++) {
            uint16_t px = (uint16_t)(x + cx);
            uint16_t py = (uint16_t)(y + cy);
            if (px < fb_w && py < fb_h)
                saved_bg[cy * CURSOR_W + cx] = get_pixel(px, py);
            else
                saved_bg[cy * CURSOR_W + cx] = 0;
        }
    }
}

static void restore_bg(int16_t x, int16_t y) {
    for (int cy = 0; cy < CURSOR_H; cy++) {
        for (int cx = 0; cx < CURSOR_W; cx++) {
            uint16_t px = (uint16_t)(x + cx);
            uint16_t py = (uint16_t)(y + cy);
            if (px < fb_w && py < fb_h)
                set_pixel(px, py, saved_bg[cy * CURSOR_W + cx]);
        }
    }
}

static void draw_cursor(int16_t x, int16_t y) {
    uint32_t white = 0xFFFFFF;
    uint32_t black = 0x000000;
    for (int cy = 0; cy < CURSOR_H; cy++) {
        uint8_t mask = cursor_mask[cy];
        for (int cx = 0; cx < CURSOR_W; cx++) {
            if (mask & (0x80 >> cx)) {
                uint16_t px = (uint16_t)(x + cx);
                uint16_t py = (uint16_t)(y + cy);
                if (px < fb_w && py < fb_h)
                    set_pixel(px, py, (cx < 1 || cy < 1) ? black : white);
            }
        }
    }
}

void cursor_init(uint32_t *fb, uint16_t w, uint16_t h, uint16_t pitch, uint8_t bpp) {
    framebuffer = fb;
    fb_w = w; fb_h = h;
    fb_pitch_bytes = pitch;
    fb_bpp = bpp;
    bg_valid = 0;
    cur_x = -1; cur_y = -1;
}

void cursor_update(int16_t x, int16_t y) {
    if (!framebuffer) return;
    if (bg_valid && cur_x >= 0)
        restore_bg(cur_x, cur_y);
    save_bg(x, y);
    draw_cursor(x, y);
    cur_x = x; cur_y = y;
    bg_valid = 1;
}

void cursor_hide(void) {
    if (bg_valid && cur_x >= 0) {
        restore_bg(cur_x, cur_y);
        bg_valid = 0;
        cur_x = -1;
    }
}

void cursor_reset(void) {
    bg_valid = 0;
    cur_x = -1; cur_y = -1;
}
