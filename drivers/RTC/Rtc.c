#include "Rtc.h"
#include "Io.h"

#define CMOS_ADDRESS 0x70
#define CMOS_DATA    0x71

// CMOS Register numbers
#define CMOS_REG_SECONDS   0x00
#define CMOS_REG_MINUTES   0x02
#define CMOS_REG_HOURS     0x04
#define CMOS_REG_DAY       0x07
#define CMOS_REG_MONTH     0x08
#define CMOS_REG_YEAR      0x09
#define CMOS_REG_CENTURY   0x32 // Common on newer systems

#define CMOS_REG_STATUS_A  0x0A
#define CMOS_REG_STATUS_B  0x0B

static uint8_t cmos_read(uint8_t reg) {
    // Select the register, making sure NMI is disabled
    outb(CMOS_ADDRESS, (1 << 7) | reg);
    // Read the data
    return inb(CMOS_DATA);
}

static int get_update_in_progress_flag() {
    return cmos_read(CMOS_REG_STATUS_A) & 0x80;
}

static uint8_t bcd_to_bin(uint8_t bcd) {
    return (bcd & 0x0F) + ((bcd >> 4) * 10);
}

void RtcReadTime(rtc_time_t* rtc_time) {
    rtc_time_t last_time;
    uint8_t status_b;

    // The robust way to read the RTC is to read it twice and see if the
    // values match. This ensures an update didn't happen in the middle of our read.
    do {
        // Wait until no update is in progress
        while (get_update_in_progress_flag()) {}

        rtc_time->second = cmos_read(CMOS_REG_SECONDS);
        rtc_time->minute = cmos_read(CMOS_REG_MINUTES);
        rtc_time->hour   = cmos_read(CMOS_REG_HOURS);
        rtc_time->day    = cmos_read(CMOS_REG_DAY);
        rtc_time->month  = cmos_read(CMOS_REG_MONTH);
        rtc_time->year   = cmos_read(CMOS_REG_YEAR);
        rtc_time->century = cmos_read(CMOS_REG_CENTURY);
        // Make a copy of the values we just read
        last_time = *rtc_time;

        // Wait again to ensure we are past the update
        while (get_update_in_progress_flag()){}

        // Read a second time
        last_time.second = cmos_read(CMOS_REG_SECONDS);
        last_time.minute = cmos_read(CMOS_REG_MINUTES);
        last_time.hour   = cmos_read(CMOS_REG_HOURS);
        last_time.day    = cmos_read(CMOS_REG_DAY);
        last_time.month  = cmos_read(CMOS_REG_MONTH);
        last_time.year   = cmos_read(CMOS_REG_YEAR);
#ifdef VF_CONFIG_RTC_CENTURY
        last_time.century = cmos_read(CMOS_REG_CENTURY);
#endif

    } while ( (last_time.second != rtc_time->second) ||
              (last_time.minute != rtc_time->minute) ||
              (last_time.hour   != rtc_time->hour)   ||
              (last_time.day    != rtc_time->day)    ||
              (last_time.month  != rtc_time->month)  ||
              (last_time.year   != rtc_time->year)
#ifdef VF_CONFIG_RTC_CENTURY
              || (last_time.century != rtc_time->century)
#endif
              );


    // Now that we have a stable read, convert from BCD if necessary
    status_b = cmos_read(CMOS_REG_STATUS_B);

    if (!(status_b & 0x04)) { // Bit 2 clear means BCD mode
        rtc_time->second = bcd_to_bin(rtc_time->second);
        rtc_time->minute = bcd_to_bin(rtc_time->minute);
        // Handle 12/24 hour clock for the hour value
        rtc_time->hour = ((rtc_time->hour & 0x7F) + 12 * ((rtc_time->hour & 0x80) != 0)) % 24;
        rtc_time->day    = bcd_to_bin(rtc_time->day);
        rtc_time->month  = bcd_to_bin(rtc_time->month);
        rtc_time->year   = bcd_to_bin(rtc_time->year);
    }
#ifdef VF_CONFIG_RTC_CENTURY
    {
        uint16_t cval = (uint16_t)rtc_time->century;
        if (cval != 0) {
            cval = bcd_to_bin((uint8_t)cval);
            rtc_time->year += (uint16_t)(cval * 100);
        } else {
            rtc_time->year += 2000;
        }
    }
#else
    rtc_time->year += 2000;
#endif
}