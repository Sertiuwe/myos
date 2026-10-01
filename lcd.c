#include "lcd.h"

#define LCDTiming0   (*(volatile uint32_t*)(LCD_BASE + 0x000))
#define LCDTiming1   (*(volatile uint32_t*)(LCD_BASE + 0x004))
#define LCDTiming2   (*(volatile uint32_t*)(LCD_BASE + 0x008))
#define LCDTiming3   (*(volatile uint32_t*)(LCD_BASE + 0x00C))
#define LCDUPBASE    (*(volatile uint32_t*)(LCD_BASE + 0x010))
#define LCDLPBASE    (*(volatile uint32_t*)(LCD_BASE + 0x014))
#define LCDControl   (*(volatile uint32_t*)(LCD_BASE + 0x018))

#define CNTL_LCDEN      (1u << 0)
#define CNTL_LCDBPP_16  (4u << 1)
#define CNTL_LCDTFT     (1u << 5)
#define CNTL_LCDPWR     (1u << 11)

static int       g_w = LCD_DEFAULT_WIDTH;
static int       g_h = LCD_DEFAULT_HEIGHT;
static volatile uint16_t* g_fb = 0;
static uint32_t  g_control = 0;

void lcd_init(int w, int h, uint32_t fb_addr) {
    g_w = (w > 0) ? w : LCD_DEFAULT_WIDTH;
    g_h = (h > 0) ? h : LCD_DEFAULT_HEIGHT;
    g_fb = (volatile uint16_t*)fb_addr;

    LCDTiming0 = ((uint32_t)(g_w - 1) & 0xFFu) | (((uint32_t)(g_h - 1) & 0x3FFu) << 16);
    LCDTiming1 = 0;
    LCDTiming2 = 0;
    LCDTiming3 = 0;
    LCDUPBASE  = fb_addr & 0xFFFFFFF0u;
    LCDLPBASE  = 0;

    g_control  = CNTL_LCDTFT | CNTL_LCDBPP_16;
    LCDControl = g_control;
}

void lcd_set_fb(uint32_t arm_addr) {
    g_fb = (volatile uint16_t*)arm_addr;
    LCDUPBASE = arm_addr & 0xFFFFFFF0u;
}

void lcd_enable(void) {
    g_control |= (CNTL_LCDEN | CNTL_LCDPWR);
    LCDControl = g_control;
}

void lcd_disable(void) {
    g_control &= ~(CNTL_LCDEN | CNTL_LCDPWR);
    LCDControl = g_control;
}

void lcd_flush(void) {
    LCDControl = g_control | CNTL_LCDEN;
}

void lcd_clear(uint16_t color) {
    if (!g_fb) return;
    int n = g_w * g_h;
    for (int i = 0; i < n; i++) g_fb[i] = color;
}

void lcd_pixel(int x, int y, uint16_t color) {
    if (!g_fb || x < 0 || y < 0 || x >= g_w || y >= g_h) return;
    g_fb[y * g_w + x] = color;
}

void lcd_hline(int x, int y, int w, uint16_t color) {
    if (!g_fb || y < 0 || y >= g_h || w <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > g_w) w = g_w - x;
    if (w <= 0) return;
    volatile uint16_t* p = g_fb + y * g_w + x;
    for (int i = 0; i < w; i++) p[i] = color;
}

void lcd_vline(int x, int y, int h, uint16_t color) {
    if (!g_fb || x < 0 || x >= g_w || h <= 0) return;
    if (y < 0) { h += y; y = 0; }
    if (y + h > g_h) h = g_h - y;
    if (h <= 0) return;
    volatile uint16_t* p = g_fb + y * g_w + x;
    for (int i = 0; i < h; i++) { *p = color; p += g_w; }
}

void lcd_rect(int x, int y, int w, int h, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    lcd_hline(x, y, w, color);
    lcd_hline(x, y + h - 1, w, color);
    lcd_vline(x, y, h, color);
    lcd_vline(x + w - 1, y, h, color);
}

void lcd_fill_rect(int x, int y, int w, int h, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++) lcd_hline(x, y + j, w, color);
}

int       lcd_width(void)  { return g_w; }
int       lcd_height(void) { return g_h; }
uint16_t* lcd_fb(void)     { return (uint16_t*)g_fb; }
