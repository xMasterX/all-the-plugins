#ifndef DCF77_TEST_DATETIME_STUB_H
#define DCF77_TEST_DATETIME_STUB_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t day;
    uint8_t month;
    uint16_t year;
    uint8_t weekday;
} DateTime;

bool datetime_validate_datetime(DateTime* datetime);
uint32_t datetime_datetime_to_timestamp(DateTime* datetime);
void datetime_timestamp_to_datetime(uint32_t timestamp, DateTime* datetime);
uint16_t datetime_get_days_per_year(uint16_t year);
bool datetime_is_leap_year(uint16_t year);
uint8_t datetime_get_days_per_month(bool leap_year, uint8_t month);

#endif
