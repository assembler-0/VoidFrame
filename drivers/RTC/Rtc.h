#ifndef VOIDFRAME_RTC_H
#define VOIDFRAME_RTC_H

#include <stdint.h>

// A structure to hold the date and time.
typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
} rtc_time_t;

// Reads the current date and time from the RTC into the provided struct.
void RtcReadTime(rtc_time_t* rtc_time);

#endif // VOIDFRAME_RTC_H
