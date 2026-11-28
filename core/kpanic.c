#include "kpanic.h"
#include "kstdio.h"
#include <stdarg.h>

void kpanic(const char *fmt, ...) {
    kprintf("\n*** KERNEL PANIC ***\n");
    va_list ap; va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
    kprintf("\nsystem halted.\n");
    for (;;) __asm__ volatile ("hlt");
}
