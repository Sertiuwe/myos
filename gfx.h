#ifndef GFX_H
#define GFX_H

#include <stdint.h>

uint32_t *gfx_backbuffer(void);
uint32_t  gfx_width(void);
uint32_t  gfx_height(void);
void      gfx_draw_char(char c, int x, int y, uint32_t col);
void      gfx_flush_rows(uint32_t y0, uint32_t y1);
void      gfx_flush(void);

#endif
