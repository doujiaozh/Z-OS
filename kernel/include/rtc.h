#ifndef Z_OS_RTC_H
#define Z_OS_RTC_H

#include "types.h"

struct rtc_time {
    u16 year;
    u8 month;
    u8 day;
    u8 hour;
    u8 minute;
    u8 second;
    u8 weekday;
};

void rtc_read(struct rtc_time *time);

#endif
