#ifndef LCD_H
#define LCD_H

#include "types.h"

#define LCD_BASE             0x10120000u
#define LCD_DEFAULT_WIDTH    320
#define LCD_DEFAULT_HEIGHT   240
#define LCD_FB_DEFAULT_ADDR  0x00200000u    /* 2 MB */

/* RGB565 */
#define LCD_RGB565(r,g,b) ((((r) & 0x1F) << 11) | (((g) & 0x3F) << 5) | ((b) & 0x1F))
#define LCD_BLACK    0x0000
#define LCD_WHITE    0xFFFF
#define LCD_RED      0xF800
#define LCD_GREEN    0x07E0
#define LCD_BLUE     0x001F
#define LCD_YELLOW   0xFFE0
#define LCD_CYAN     0x07FF
#define LCD_MAGENTA  0xF81F
#define LCD_GRAY     0x8410

void      lcd_init(int w, int h, uint32_t fb_addr);
void      lcd_set_fb(uint32_t arm_addr);
void      lcd_enable(void);
void      lcd_disable(void);
void      lcd_flush(void);

void      lcd_clear(uint16_t color);
void      lcd_pixel(int x, int y, uint16_t color);
void      lcd_hline(int x, int y, int w, uint16_t color);
void      lcd_vline(int x, int y, int h, uint16_t color);
void      lcd_rect(int x, int y, int w, int h, uint16_t color);
void      lcd_fill_rect(int x, int y, int w, int h, uint16_t color);

int       lcd_width(void);
int       lcd_height(void);
uint16_t* lcd_fb(void);

#endif
