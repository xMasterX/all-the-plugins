#include "datetime_stub.h"

#include <assert.h>
#include <stddef.h>

#define SECONDS_PER_MINUTE 60U
#define SECONDS_PER_HOUR (SECONDS_PER_MINUTE * 60U)
#define SECONDS_PER_DAY (SECONDS_PER_HOUR * 24U)

static const uint8_t datetime_days_per_month[2][12] = {
    {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
    {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
};

bool datetime_validate_datetime(DateTime* datetime) {
    bool invalid = false;

    invalid |= (datetime->second > 59U);
    invalid |= (datetime->minute > 59U);
    invalid |= (datetime->hour > 23U);
    invalid |= (datetime->year < 2000U);
    invalid |= (datetime->year > 2099U);
    invalid |= (datetime->month == 0U);
    invalid |= (datetime->month > 12U);
    invalid |= (datetime->day == 0U);
    invalid |= (datetime->day > 31U);
    invalid |= (datetime->weekday == 0U);
    invalid |= (datetime->weekday > 7U);

    return !invalid;
}

uint32_t datetime_datetime_to_timestamp(DateTime* datetime) {
    assert(datetime != NULL);

    uint32_t timestamp = 0U;

    for(uint16_t year = 1970U; year < datetime->year; year++) {
        timestamp += (uint32_t)datetime_get_days_per_year(year) * SECONDS_PER_DAY;
    }

    for(uint8_t month = 1U; month < datetime->month; month++) {
        timestamp +=
            (uint32_t)datetime_get_days_per_month(datetime_is_leap_year(datetime->year), month) *
            SECONDS_PER_DAY;
    }

    timestamp += (uint32_t)(datetime->day - 1U) * SECONDS_PER_DAY;
    timestamp += (uint32_t)datetime->hour * SECONDS_PER_HOUR;
    timestamp += (uint32_t)datetime->minute * SECONDS_PER_MINUTE;
    timestamp += datetime->second;

    return timestamp;
}

void datetime_timestamp_to_datetime(uint32_t timestamp, DateTime* datetime) {
    assert(datetime != NULL);

    uint32_t days = timestamp / SECONDS_PER_DAY;
    const uint32_t seconds_in_day = timestamp % SECONDS_PER_DAY;

    datetime->year = 1970U;
    datetime->weekday = ((days + 3U) % 7U) + 1U;

    while(days >= datetime_get_days_per_year(datetime->year)) {
        days -= datetime_get_days_per_year(datetime->year);
        datetime->year++;
    }

    datetime->month = 1U;
    while(days >= datetime_get_days_per_month(datetime_is_leap_year(datetime->year), datetime->month)) {
        days -= datetime_get_days_per_month(datetime_is_leap_year(datetime->year), datetime->month);
        datetime->month++;
    }

    datetime->day = (uint8_t)(days + 1U);
    datetime->hour = (uint8_t)(seconds_in_day / SECONDS_PER_HOUR);
    datetime->minute = (uint8_t)((seconds_in_day % SECONDS_PER_HOUR) / SECONDS_PER_MINUTE);
    datetime->second = (uint8_t)(seconds_in_day % SECONDS_PER_MINUTE);
}

uint16_t datetime_get_days_per_year(uint16_t year) {
    return datetime_is_leap_year(year) ? 366U : 365U;
}

bool datetime_is_leap_year(uint16_t year) {
    return (((year % 4U) == 0U) && ((year % 100U) != 0U)) || ((year % 400U) == 0U);
}

uint8_t datetime_get_days_per_month(bool leap_year, uint8_t month) {
    return datetime_days_per_month[leap_year ? 1U : 0U][month - 1U];
}
