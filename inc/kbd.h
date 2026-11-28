#ifndef KBD_H
#define KBD_H
char kbd_getchar(void);   /* 0 если ничего не нажато */
#define KEY_UP    0x10
#define KEY_DOWN  0x11
#define KEY_LEFT  0x12
#define KEY_RIGHT 0x13
#define KEY_HOME  0x14
#define KEY_END   0x15
#define KEY_PGUP  0x16
#define KEY_PGDN  0x17
#define KEY_DEL   0x18
void kbd_set_input_hook(void (*fn)(void));
void kbd_irq_notify(void);
#endif
