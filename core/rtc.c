#include "rtc.h"

static uint8_t inb(uint16_t p){uint8_t v;__asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p));return v;}
static void outb(uint16_t p,uint8_t v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}

static uint8_t cmos_read(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static int rtc_updating(void) {
    outb(0x70, 0x0A);
    return (inb(0x71) & 0x80) != 0;
}

static uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }

int rtc_read(rtc_time_t *t) {
    /* Ждём окончания обновления */
    while (rtc_updating()) {}
    t->sec   = cmos_read(0x00);
    t->min   = cmos_read(0x02);
    t->hour  = cmos_read(0x04);
    t->day   = cmos_read(0x07);
    t->month = cmos_read(0x08);
    t->year  = cmos_read(0x09);

    /* Проверяем формат: если бит 2 регистра B = 0, данные в BCD */
    uint8_t regB = cmos_read(0x0B);
    if (!(regB & 0x04)) {
        t->sec   = bcd2bin(t->sec);
        t->min   = bcd2bin(t->min);
        t->hour  = bcd2bin(t->hour & 0x7F) | (t->hour & 0x80);
        t->day   = bcd2bin(t->day);
        t->month = bcd2bin(t->month);
        t->year  = bcd2bin(t->year);
    }
    /* 12/24 часовой формат */
    if (!(regB & 0x02) && (t->hour & 0x80))
        t->hour = ((t->hour & 0x7F) + 12) % 24;
    else
        t->hour &= 0x7F;

    /* Год: CMOS хранит 0-99, добавляем 2000 */
    t->year += 2000;
    return 0;
}

void rtc_format(const rtc_time_t *t, char *buf, size_t sz __attribute__((unused))) {
    /* YYYY-MM-DD HH:MM:SS */
    char *p = buf;
    /* Простая ручная форматировка без kprintf для минимизации зависимостей */
    #define PUT2(v) do { *p++='0'+(v)/10; *p++='0'+(v)%10; } while(0)
    #define PUT4(v) do { *p++='0'+(v)/1000; *p++='0'+((v)/100)%10; *p++='0'+((v)/10)%10; *p++='0'+(v)%10; } while(0)
    PUT4(t->year); *p++='-'; PUT2(t->month); *p++='-'; PUT2(t->day);
    *p++=' '; PUT2(t->hour); *p++=':'; PUT2(t->min); *p++=':'; PUT2(t->sec);
    *p = '\0';
    #undef PUT2
    #undef PUT4
}
