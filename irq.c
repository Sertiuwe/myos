#include "pic.h"
#include "pit.h"
#include "mouse.h"
#include "kstdio.h"
#include <stdint.h>

void irq_handler(uint64_t vec) {
    uint8_t irq = (uint8_t)(vec - 32);
    switch (irq) {
    case 0:  /* PIT */
        pit_tick();
        break;
    case 1:  /* Keyboard — пока не обрабатываем, но EOI нужен */
        break;
    case 12: /* PS/2 Mouse */
        mouse_irq_handler();
        break;
    default:
        kprintf("unhandled IRQ%u\n", irq);
        break;
    }
    pic_eoi(irq);
}
