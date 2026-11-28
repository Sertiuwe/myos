#ifndef PIT_H
#define PIT_H
#include <stdint.h>
void pit_tick(void);
void pit_init(uint32_t freq_hz);
uint64_t pit_ticks(void);
uint32_t pit_hz(void);
void pit_sleep_ms(uint32_t ms);
#endif
