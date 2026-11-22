#ifndef MOUSE_H
#define MOUSE_H
#include <stdint.h>

typedef struct {
    int16_t x, y;           /* экранные координаты */
    uint8_t buttons;        /* bit0=left, bit1=right, bit2=middle */
    uint8_t present;
} mouse_state_t;

void mouse_init(void);
void mouse_irq_handler(void);
const mouse_state_t *mouse_get_state(void);
void mouse_set_screen_size(uint16_t w, uint16_t h);

void mouse_poll(void);
void mouse_get_stats(uint64_t *packets, uint64_t *sync_drops, uint64_t *ovf_drops);
void mouse_set_input_hook(void (*fn)(void));
#endif
