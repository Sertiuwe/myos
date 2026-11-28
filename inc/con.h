#ifndef CON_H
#define CON_H

#include <stdint.h>

void con_init(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1);
void con_clear(void);
void con_putc(char c);
void con_puts(const char *s);
void con_put_u(uint64_t v);
void con_put_i(int64_t v);
void con_put_x(uint64_t v);
void con_set_color(uint32_t fg);
void con_flush(void);

#endif
