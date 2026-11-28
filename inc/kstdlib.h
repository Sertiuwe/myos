#ifndef KSTDLIB_H
#define KSTDLIB_H
#include <stddef.h>
long strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
int atoi(const char *s);
int abs(int x);
long labs(long x);
#endif
