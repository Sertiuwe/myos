#ifndef CURSOR_H_INCLUDED
#define CURSOR_H_INCLUDED
#include <stdint.h>
void cursor_init(uint32_t *fb, uint16_t w, uint16_t h, uint16_t pitch, uint8_t bpp);
void cursor_update(int16_t x, int16_t y);
void cursor_hide(void);
#endif
