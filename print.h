#ifndef PRINT_H
#define PRINT_H

#include "types.h"

/* Инициализация не нужна — UART инициализирует kernel */
void print_init(void);

/* Базовый вывод */
void putc(char c);
void puts(const char *s);

/* Числа */
void putuint(uint32_t v);
void putint(int32_t v);
void puthex(uint32_t v);
void puthex8(uint8_t v);
void putbin(uint32_t v);

/* Форматированный вывод */
int printf(const char *fmt, ...);
int snprintf(char *buf, int size, const char *fmt, ...);

#endif
