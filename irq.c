#include "pic.h"
#include "pit.h"
#include "sched.h"
#include "kbd.h"
#include "mouse.h"
#include "kstdio.h"
#include <stdint.h>

void irq_handler(uint64_t vec) {
    uint8_t irq = (uint8_t)(vec - 32);
    switch (irq) {
    case 0:  /* PIT */
        pit_tick();
        pic_eoi(0);
        sched_tick();
        return;   /* EOI already sent before the switch */
    case 1:  /* Keyboard — пока не обрабатываем, но EOI нужен */
        break;
        kbd_irq_notify();
    case 12: /* PS/2 Mouse */
        mouse_irq_handler();
        break;
    default:
        kprintf("unhandled IRQ%u\n", irq);
        break;
    }
    pic_eoi(irq);
}
