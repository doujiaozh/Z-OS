#include "rtc.h"
#include "io.h"

#define CMOS_ADDRESS 0x70
#define CMOS_DATA 0x71

static u8 cmos_read(u8 register_index) {
    outb(CMOS_ADDRESS, register_index);
    io_wait();
    return inb(CMOS_DATA);
}

static bool update_in_progress(void) {
    return (cmos_read(0x0A) & 0x80) != 0;
}

static u8 bcd_to_binary(u8 value) {
    return (u8)((value & 0x0F) + ((value >> 4) * 10));
}

static void read_time(struct rtc_time *time) {
    while (update_in_progress()) {
    }
    u8 second = cmos_read(0x00);
    u8 minute = cmos_read(0x02);
    u8 hour = cmos_read(0x04);
    u8 weekday = cmos_read(0x06);
    u8 day = cmos_read(0x07);
    u8 month = cmos_read(0x08);
    u8 year = cmos_read(0x09);
    u8 century = cmos_read(0x32);
    u8 status_b = cmos_read(0x0B);
    bool is_bcd = (status_b & 0x04) == 0;
    bool is_12_hour = (status_b & 0x02) == 0;
    if (is_bcd) {
        second = bcd_to_binary(second);
        minute = bcd_to_binary(minute);
        u8 hour_pm = hour & 0x80;
        hour = (u8)(hour & 0x7F);
        hour = bcd_to_binary(hour);
        day = bcd_to_binary(day);
        month = bcd_to_binary(month);
        year = bcd_to_binary(year);
        if (century != 0) {
            century = bcd_to_binary(century);
        }
        if (is_12_hour) {
            if (hour_pm) {
                if (hour != 12) {
                    hour = (u8)(hour + 12);
                }
            } else if (hour == 12) {
                hour = 0;
            }
        }
    } else if (is_12_hour) {
        u8 hour_pm = hour & 0x80;
        hour = (u8)(hour & 0x7F);
        if (hour_pm && hour != 12) {
            hour = (u8)(hour + 12);
        } else if (!hour_pm && hour == 12) {
            hour = 0;
        }
    }
    time->second = second;
    time->minute = minute;
    time->hour = hour;
    time->weekday = weekday;
    time->day = day;
    time->month = month;
    time->year = century != 0 ? (u16)(century * 100 + year) : (u16)(2000 + year);
}

void rtc_read(struct rtc_time *time) {
    for (u32 attempt = 0; attempt < 8; attempt++) {
        struct rtc_time first;
        struct rtc_time second;
        read_time(&first);
        read_time(&second);
        if (first.year == second.year && first.month == second.month && first.day == second.day &&
            first.hour == second.hour && first.minute == second.minute && first.second == second.second) {
            *time = second;
            return;
        }
    }
    *time = (struct rtc_time){0};
}
