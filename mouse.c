#include "mouse.h"
#include "pic.h"
#include "kstdio.h"
#include <stdint.h>

#define PS2_DATA    0x60
#define PS2_STATUS  0x64
#define PS2_CMD     0x64

static volatile mouse_state_t state;
static volatile uint64_t mouse_packets, mouse_sync_drops, mouse_ovf_drops;
static volatile uint8_t cycle;      /* 0=flags, 1=x, 2=y */
static volatile uint8_t packet[3];
static uint16_t screen_w = 1024;
static uint16_t screen_h = 768;

static uint8_t inb(uint16_t p){uint8_t v;__asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p));return v;}
static void outb(uint16_t p,uint8_t v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}

static void ps2_wait_input(void) {
    for (int i = 0; i < 100000; i++)
        if (!(inb(PS2_STATUS) & 0x02)) return;
}

static void ps2_write_cmd(uint8_t cmd) {
    ps2_wait_input();
    outb(PS2_CMD, cmd);
}

static void ps2_write_data(uint8_t data) {
    ps2_wait_input();
    outb(PS2_DATA, data);
}

static void clamp(void) {
    if (state.x < 0) state.x = 0;
    if (state.y < 0) state.y = 0;
    if (state.x >= (int16_t)screen_w) state.x = (int16_t)(screen_w - 1);
    if (state.y >= (int16_t)screen_h) state.y = (int16_t)(screen_h - 1);
}

/* Вызывается из IRQ12 handler */
static void mouse_consume(uint8_t data) {
    switch (cycle) {
    case 0:
        /* Первый байт: бит 3 (Y overflow) должен быть 0 для корректного пакета.
           Бит 3 = 1 означает синхронизацию. Если не установлен — сброс цикла. */
        if (!(data & 0x08)) { mouse_sync_drops++; break; }  /* потеря синхронизации */
        if (data & 0xC0) { mouse_ovf_drops++; break; }   /* X/Y overflow: packet is garbage */
        packet[0] = data;
        cycle = 1;
        break;
    case 1:
        packet[1] = data;
        cycle = 2;
        break;
    case 2:
        packet[2] = data;
        cycle = 0;

        /* Декодируем пакет */
        uint8_t flags = packet[0];
        int dx = (int)packet[1];
        int dy = (int)packet[2];

        /* Sign extension для 9-битных значений */
        if (flags & 0x10) dx |= 0xFFFFFF00;
        if (flags & 0x20) dy |= 0xFFFFFF00;

        /* Y инвертирован в PS/2 (вверх = положительно) */
        state.x += (int16_t)dx;
        state.y -= (int16_t)dy;
        state.buttons = flags & 0x07;
        mouse_packets++;
        clamp();
        break;
    }
}

static uint8_t ps2_read_data(void) {
    /* Ждём пока output buffer full (бит 0 статуса) */
    for (int i = 0; i < 200000; i++)
        if (inb(PS2_STATUS) & 0x01) return inb(PS2_DATA);
    return 0xFF;  /* timeout */
}

static void ps2_flush(void) {
    for (int i = 0; i < 32; i++) {
        if (!(inb(PS2_STATUS) & 0x01)) break;
        (void)inb(PS2_DATA);
    }
}

void mouse_init(void) {
    state.x = (int16_t)(screen_w / 2);
    state.y = (int16_t)(screen_h / 2);
    state.buttons = 0;
    state.present = 0;
    cycle = 0;

    /* 1. Disable both ports so the controller stays quiet while we reconfigure it */
    ps2_write_cmd(0xAD);   /* disable keyboard port */
    ps2_write_cmd(0xA7);   /* disable aux port */
    ps2_flush();

    /* 2. Read controller command byte */
    ps2_write_cmd(0x20);
    uint8_t cfg = ps2_read_data();

    /* 3. bit0 = IRQ1 enable, bit1 = IRQ12 enable  <-- THIS BIT WAS MISSING.
          bit4/bit5 are "port disabled" flags -> clear both.
          bit6 (scancode translation) left untouched: our maps are set-1. */
    cfg |= 0x03;
    cfg &= (uint8_t)~0x30;
    cfg |= 0x04;           /* system flag */

    ps2_write_cmd(0x60);   /* write controller command byte */
    ps2_write_data(cfg);

    /* 4. Re-enable both ports */
    ps2_write_cmd(0xAE);
    ps2_write_cmd(0xA8);
    ps2_flush();

    /* 5. Tell the mouse to start reporting packets */
    ps2_write_cmd(0xD4);
    ps2_write_data(0xF4);
    uint8_t ack = ps2_read_data();

    state.present = 1;
    if (ack == 0xFA)
        kprintf("Mouse: PS/2 enabled (i8042 cfg=0x%02x), cursor at (%d,%d)\n",
                cfg, state.x, state.y);
    else
        kprintf("Mouse: ack=0x%02x (i8042 cfg=0x%02x), running anyway\n",
                ack, cfg);
}

void mouse_poll(void) {
    uint8_t status = inb(PS2_STATUS);
    if (!(status & 0x20)) return;
    uint8_t data = inb(PS2_DATA);
    switch (cycle) {
    case 0:
        if (!(data & 0x08)) { mouse_sync_drops++; break; }
        if (data & 0xC0) { mouse_ovf_drops++; break; }   /* X/Y overflow: packet is garbage */
        packet[0] = data; cycle = 1; break;
    case 1:
        packet[1] = data; cycle = 2; break;
    case 2:
        packet[2] = data; cycle = 0;
        {
            uint8_t flags = packet[0];
            int dx = (int)packet[1];
            int dy = (int)packet[2];
            if (flags & 0x10) dx |= 0xFFFFFF00;
            if (flags & 0x20) dy |= 0xFFFFFF00;
            state.x += (int16_t)dx;
            state.y -= (int16_t)dy;
            state.buttons = flags & 0x07;
        mouse_packets++;
            clamp();
        }
        break;
    }
}

const mouse_state_t *mouse_get_state(void) { return (const mouse_state_t *)&state; }

void mouse_set_screen_size(uint16_t w, uint16_t h) {
    screen_w = w; screen_h = h;
    clamp();
}

static inline uint64_t ps2_irq_save(void) {
    uint64_t f;
    __asm__ volatile("pushfq\n\tpopq %0\n\tcli" : "=r"(f) :: "memory");
    return f;
}

static inline void ps2_irq_restore(uint64_t f) {
    __asm__ volatile("pushq %0\n\tpopfq" :: "r"(f) : "memory", "cc");
}

/* Single byte intake, atomic against the keyboard-side drain path. */
void mouse_irq_handler(void) {
    uint64_t f = ps2_irq_save();
    if (inb(PS2_STATUS) & 0x01) {
        uint8_t d = inb(PS2_DATA);
        mouse_consume(d);
    }
    ps2_irq_restore(f);
}

void mouse_get_stats(uint64_t *packets, uint64_t *sync_drops, uint64_t *ovf_drops) {
    if (packets)   *packets   = mouse_packets;
    if (sync_drops)*sync_drops= mouse_sync_drops;
    if (ovf_drops) *ovf_drops = mouse_ovf_drops;
}
