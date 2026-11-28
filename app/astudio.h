#ifndef ASTUDIO_H
#define ASTUDIO_H
#include <stdint.h>
#include <stddef.h>
#include "ga_remote.h"

/* ---- memory ---- */
void   *as_memcpy(void *d, const void *s, size_t n);
void   *as_memset(void *d, int c, size_t n);
int     as_memcmp(const void *a, const void *b, size_t n);
void   *as_memmove(void *d, const void *s, size_t n);

/* ---- string ---- */
size_t  as_strlen(const char *s);
int     as_strcmp(const char *a, const char *b);
int     as_strncmp(const char *a, const char *b, size_t n);
char   *as_strcpy(char *d, const char *s);
char   *as_strncpy(char *d, const char *s, size_t n);
char   *as_strcat(char *d, const char *s);
char   *as_strchr(const char *s, int c);
char   *as_strstr(const char *h, const char *n);

/* ---- ctype ---- */
int     as_isdigit(int c);
int     as_isalpha(int c);
int     as_isspace(int c);
int     as_toupper(int c);
int     as_tolower(int c);

/* ---- numbers ---- */
int     as_abs(int v);
int     as_min(int a, int b);
int     as_max(int a, int b);
int     as_clamp(int v, int lo, int hi);
int32_t as_sqrt_i(int32_t v);
char   *as_itoa(int v, char *buf, int base);

/* ---- random (LCG) ---- */
void     as_srand(uint32_t seed);
uint32_t as_rand(void);
uint32_t as_rand_range(uint32_t lo, uint32_t hi);

/* ---- formatted output ---- */
int     as_sprintf(char *buf, size_t cap, const char *fmt, ...);
int     as_printf(const char *fmt, ...);
void    as_hexdump(const void *p, size_t n);

/* ---- time / process (via exported syslib) ---- */
uint64_t as_uptime_ms(void);
uint32_t as_uptime_sec(void);
void     as_format_uptime(char *buf, size_t cap, uint64_t ms);
int      as_tid(void);
void     as_exit(int code);


/* ---- graphic API (Ring 3 -> kernel GA bridge) ---- */
int  as_ga_open(const char *title, int32_t x, int32_t y, int32_t w, int32_t h);
void as_ga_fill(int h, int32_t x, int32_t y, int32_t w, int32_t hh, uint32_t col);
void as_ga_frame(int h, int32_t x, int32_t y, int32_t w, int32_t hh, uint16_t t, uint32_t col);
void as_ga_line(int h, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t col);
void as_ga_text(int h, int32_t x, int32_t y, const char *s, uint32_t col);
void as_ga_grad(int h, int32_t x, int32_t y, int32_t w, int32_t hh, uint32_t top, uint32_t bot);
void as_ga_clear(int h);
int  as_ga_event(int h, gar_ev_t *ev);
void as_ga_present(int h);
void as_ga_close(int h);
#endif
