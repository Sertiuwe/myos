#include "pit.h"
#include "pic.h"
#include "kstdio.h"

#define PIT_BASE 1193182
#define PIT_CH0  0x40
#define PIT_CMD  0x43

static volatile uint64_t tick_count;
static uint32_t freq;

static void outb(uint16_t p, uint8_t v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}

/* Вызывается из IRQ0 handler */
void pit_tick(void) {
    tick_count++;
}

void pit_init(uint32_t hz) {
    freq = hz;
    uint16_t div = (uint16_t)(PIT_BASE / hz);
    /* Ch0, lobyte/hibyte, mode 2 (rate generator), binary */
    outb(PIT_CMD, 0x34);
    outb(PIT_CH0, (uint8_t)(div & 0xFF));
    outb(PIT_CH0, (uint8_t)(div >> 8));
    tick_count = 0;
    kprintf("PIT: %u Hz (div=%u)\n", hz, div);
}

uint64_t pit_ticks(void) { return tick_count; }
uint32_t pit_hz(void) { return freq; }

void pit_sleep_ms(uint32_t ms) {
    uint64_t target = tick_count + (uint64_t)ms * freq / 1000;
    while (tick_count < target)
        __asm__ volatile("hlt");
}
