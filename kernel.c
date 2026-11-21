#include <stdint.h>

void kmain(void) {
    // Здесь будет код ядра
    for (;;) __asm__ volatile ("hlt");
}
