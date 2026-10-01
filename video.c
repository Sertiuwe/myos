#include "video.h"
#include "lcd.h"
#include "sdcard.h"
#include "print.h"

typedef struct __attribute__((packed)) {
    char     magic[4];
    uint16_t width;
    uint16_t height;
    uint8_t  fps;
    uint8_t  bpp;
    uint16_t reserved;
    uint32_t frames;
} mvid_header_t;

int video_load(uint32_t lba, video_t *v) {
    uint8_t blk[512];
    if (sd_read_block(lba, blk) != 0) return -1;
    mvid_header_t *h = (mvid_header_t*)blk;
    if (h->magic[0] != 'M' || h->magic[1] != 'V' ||
        h->magic[2] != '0' || h->magic[3] != '1') return -2;
    if (h->bpp != 16) return -3;
    v->width  = h->width;
    v->height = h->height;
    v->fps    = h->fps;
    v->bpp    = h->bpp;
    v->frames = h->frames;
    v->lba    = lba;
    
    return 0;
}

/* 80*60*2 = 9600, с запасом 12 KB */
static uint8_t g_frame_buf[12 * 1024];

int video_play(const video_t *v) {
    if (v->width == 0 || v->height == 0) return -1;

    /* Очистить терминал хоста перед воспроизведением */
    puts("\033[2J\033[H");

    uint32_t frame_size = (uint32_t)v->width * v->height * 2;
    if (frame_size > sizeof(g_frame_buf)) return -2;

    int scale = lcd_width() / v->width;
    if (scale < 1) scale = 1;

    uint16_t *fb = lcd_fb();
    int fb_w = lcd_width();

    for (uint32_t fi = 0; fi < v->frames; fi++) {
        /* Абсолютный байтовый offset кадра от начала видеофайла */
        uint32_t off = 16 + fi * frame_size;

        /* Читаем кадр поблочно в g_frame_buf */
        uint32_t cur_lba = v->lba + off / 512;
        uint32_t cur_off = off % 512;
        uint32_t remaining = frame_size;
        uint32_t dst = 0;
        uint8_t  blk[512];

        while (remaining > 0) {
            if (sd_read_block(cur_lba, blk) != 0) return -3;
            uint32_t avail = 512 - cur_off;
            uint32_t take  = (avail < remaining) ? avail : remaining;
            for (uint32_t i = 0; i < take; i++) g_frame_buf[dst + i] = blk[cur_off + i];
            dst += take;
            remaining -= take;
            cur_lba++;
            cur_off = 0;
        }

        /* Рендер кадра с масштабированием (pixel replication) */
        const uint16_t *src = (const uint16_t*)g_frame_buf;
        for (uint32_t y = 0; y < v->height; y++) {
            const uint16_t *srow = src + y * v->width;
            for (int sy = 0; sy < scale; sy++) {
                uint16_t *drow = fb + (y * scale + sy) * fb_w;
                for (uint32_t x = 0; x < v->width; x++) {
                    uint16_t c = srow[x];
                    for (int sx = 0; sx < scale; sx++)
                        drow[x * scale + sx] = c;
                }
            }
        }

        lcd_flush();

        /* Простая пауза ~100 мс при 10 fps (зависит от CPU) */
        for (volatile uint32_t i = 0; i < 50000u; i++);
    }
    return 0;
}
