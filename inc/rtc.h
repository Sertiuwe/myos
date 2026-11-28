#ifndef RTC_H
#define RTC_H
#include <stdint.h>
#include <stddef.h>
typedef struct {
    uint8_t sec, min, hour, day, month;
    uint16_t year;
} rtc_time_t;
int rtc_read(rtc_time_t *t);
void rtc_format(const rtc_time_t *t, char *buf, size_t sz);
#endif
