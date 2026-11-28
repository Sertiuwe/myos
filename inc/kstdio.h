#ifndef KSTDIO_H
#define KSTDIO_H

#include <stddef.h>
#include <stdarg.h>

int  kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int  ksnprintf(char *buf, size_t size, const char *fmt, ...);
int  kvprintf(const char *fmt, va_list ap);
int  kprintf(const char *fmt, ...);
void kstdio_set_sink(void (*fn)(const char *));

#endif
