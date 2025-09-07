#include "RTC/Rtc.h"
#include "Io.h"
#include "stdbool.h"

#define RTC_CMOS_ADDRESS 0x70
#define RTC_CMOS_DATA 0x71

#define RTC_SECONDS 0x00
#define RTC_MINUTES 0x02
#define RTC_HOURS 0x04
#define RTC_DAY_OF_WEEK 0x06
#define RTC_DAY_OF_MONTH 0x07
#define RTC_MONTH 0x08
#define RTC_YEAR 0x09
#define RTC_CENTURY 0x32 // Most common century register
#define RTC_STATUS_A 0x0A
#define RTC_STATUS_B 0x0B
#define RTC_STATUS_C 0x0C

typedef rtc_time_t RtcDateTime;

static uint8_t Rtc_ReadRegister(uint8_t reg) {
    outb(RTC_CMOS_ADDRESS, reg);
    return inb(RTC_CMOS_DATA);
}

static void Rtc_WriteRegister(uint8_t reg, uint8_t value) {
    outb(RTC_CMOS_ADDRESS, reg);
    outb(RTC_CMOS_DATA, value);
}

int Rtc_BcdToBinary(uint8_t bcd) {
    return (bcd & 0x0F) + ((bcd / 16) * 10);
}

uint8_t Rtc_BinaryToBcd(uint8_t binary) {
    return ((binary / 10) << 4) | (binary % 10);
}

static bool Rtc_IsUpdating() {
    Rtc_WriteRegister(RTC_STATUS_A, 0x0A); // Select register A
    return (Rtc_ReadRegister(RTC_STATUS_A) & 0x80); // Check update in progress bit
}

void RtcReadTime(RtcDateTime *dateTime) {
    uint8_t second, minute, hour, day, month, year, century;
    uint8_t statusB;

    // Wait until the update in progress bit is 0
    while (Rtc_IsUpdating());

    // Read RTC registers
    second = Rtc_ReadRegister(RTC_SECONDS);
    minute = Rtc_ReadRegister(RTC_MINUTES);
    hour = Rtc_ReadRegister(RTC_HOURS);
    day = Rtc_ReadRegister(RTC_DAY_OF_MONTH);
    month = Rtc_ReadRegister(RTC_MONTH);
    year = Rtc_ReadRegister(RTC_YEAR);
    century = Rtc_ReadRegister(RTC_CENTURY); // Read century byte

    statusB = Rtc_ReadRegister(RTC_STATUS_B);

    if (!(statusB & 0x04)) { // If BCD mode
        dateTime->second = Rtc_BcdToBinary(second);
        dateTime->minute = Rtc_BcdToBinary(minute);
        dateTime->hour = Rtc_BcdToBinary(hour);
        dateTime->day = Rtc_BcdToBinary(day);
        dateTime->month = Rtc_BcdToBinary(month);
        dateTime->year = Rtc_BcdToBinary(year);
        dateTime->century = Rtc_BcdToBinary(century);
    } else { // Binary mode
        dateTime->second = second;
        dateTime->minute = minute;
        dateTime->hour = hour;
        dateTime->day = day;
        dateTime->month = month;
        dateTime->year = year;
        dateTime->century = century;
    }

    if (!(statusB & 0x02) && (dateTime->hour & 0x80)) { // 12-hour format and PM
        dateTime->hour = ((dateTime->hour & 0x7F) + 12) % 24;
    }

    // Calculate full year
    dateTime->year += dateTime->century * 100;
}

void RtcSetTime(const RtcDateTime *dateTime) {
    uint8_t statusB;
    uint8_t second, minute, hour, day, month, year, century;

    // Read current status B to preserve settings
    statusB = Rtc_ReadRegister(RTC_STATUS_B);

    // Disable NMI and updates while setting time
    Rtc_WriteRegister(RTC_CMOS_ADDRESS, RTC_STATUS_B | 0x80); // Disable NMI
    Rtc_WriteRegister(RTC_STATUS_B, statusB | 0x80); // Disable updates

    // Convert to BCD if necessary
    if (!(statusB & 0x04)) { // If BCD mode
        second = Rtc_BinaryToBcd(dateTime->second);
        minute = Rtc_BinaryToBcd(dateTime->minute);
        hour = Rtc_BinaryToBcd(dateTime->hour);
        day = Rtc_BinaryToBcd(dateTime->day);
        month = Rtc_BinaryToBcd(dateTime->month);
        year = Rtc_BinaryToBcd(dateTime->year % 100);
        century = Rtc_BinaryToBcd(dateTime->year / 100);
    } else { // Binary mode
        second = dateTime->second;
        minute = dateTime->minute;
        hour = dateTime->hour;
        day = dateTime->day;
        month = dateTime->month;
        year = dateTime->year % 100;
        century = dateTime->year / 100;
    }

    // Write to RTC registers
    Rtc_WriteRegister(RTC_SECONDS, second);
    Rtc_WriteRegister(RTC_MINUTES, minute);
    Rtc_WriteRegister(RTC_HOURS, hour);
    Rtc_WriteRegister(RTC_DAY_OF_MONTH, day);
    Rtc_WriteRegister(RTC_MONTH, month);
    Rtc_WriteRegister(RTC_YEAR, year);
    Rtc_WriteRegister(RTC_CENTURY, century);

    // Re-enable updates and NMI
    Rtc_WriteRegister(RTC_STATUS_B, statusB);
    Rtc_WriteRegister(RTC_CMOS_ADDRESS, RTC_STATUS_B); // Re-enable NMI
}