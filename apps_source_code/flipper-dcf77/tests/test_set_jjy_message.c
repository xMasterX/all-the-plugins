#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/jjy_util.h"

#define JJY_FRAME_SECONDS 60U

static char pulse_to_char(RadioClockPulse pulse) {
    switch(pulse) {
    case RadioClockPulseOne:
        return '1';
    case RadioClockPulseMarker:
        return 'M';
    case RadioClockPulseZero:
    default:
        return '0';
    }
}

static void frame_to_string(const RadioClockPulse* frame, char* out) {
    for(size_t i = 0; i < JJY_FRAME_SECONDS; i++) {
        out[i] = pulse_to_char(frame[i]);
    }

    out[JJY_FRAME_SECONDS] = '\0';
}

static uint16_t symbols_to_value(
    const RadioClockPulse* frame,
    const uint8_t* positions,
    const uint16_t* weights,
    size_t count) {
    uint16_t value = 0U;

    for(size_t i = 0; i < count; i++) {
        if(frame[positions[i]] == RadioClockPulseOne) {
            value = (uint16_t)(value + weights[i]);
        }
    }

    return value;
}

static void test_nict_layout_vector_2026_04_13_1234(void) {
    static const uint8_t minute_bits[] = {1, 2, 3, 5, 6, 7, 8};
    static const uint16_t minute_weights[] = {40, 20, 10, 8, 4, 2, 1};
    static const uint8_t hour_bits[] = {12, 13, 15, 16, 17, 18};
    static const uint16_t hour_weights[] = {20, 10, 8, 4, 2, 1};
    static const uint8_t doy_bits[] = {22, 23, 25, 26, 27, 28, 30, 31, 32, 33};
    static const uint16_t doy_weights[] = {200, 100, 80, 40, 20, 10, 8, 4, 2, 1};
    static const uint8_t year_bits[] = {41, 42, 43, 44, 45, 46, 47, 48};
    static const uint16_t year_weights[] = {80, 40, 20, 10, 8, 4, 2, 1};
    static const uint8_t weekday_bits[] = {50, 51, 52};
    static const uint16_t weekday_weights[] = {4, 2, 1};
    RadioClockPulse frame[JJY_FRAME_SECONDS];
    char actual[JJY_FRAME_SECONDS + 1];

    /* NICT time-code layout, using the requested "today" date in this workspace. */
    set_jjy_timecode(frame, 34, 12, 103, 26, 1, false, false, false, false);
    frame_to_string(frame, actual);

    assert(
        strcmp(
            actual,
            "M01100100M000100010M000100000M001100010M000100110M001000000M") == 0);
    assert(symbols_to_value(frame, minute_bits, minute_weights, sizeof(minute_bits) / sizeof(minute_bits[0])) == 34U);
    assert(symbols_to_value(frame, hour_bits, hour_weights, sizeof(hour_bits) / sizeof(hour_bits[0])) == 12U);
    assert(symbols_to_value(frame, doy_bits, doy_weights, sizeof(doy_bits) / sizeof(doy_bits[0])) == 103U);
    assert(symbols_to_value(frame, year_bits, year_weights, sizeof(year_bits) / sizeof(year_bits[0])) == 26U);
    assert(
        symbols_to_value(frame, weekday_bits, weekday_weights, sizeof(weekday_bits) / sizeof(weekday_bits[0])) == 1U);
}

static void test_yesterday_today_tomorrow_dates_encode_requested_days(void) {
    static const uint8_t doy_bits[] = {22, 23, 25, 26, 27, 28, 30, 31, 32, 33};
    static const uint16_t doy_weights[] = {200, 100, 80, 40, 20, 10, 8, 4, 2, 1};
    static const uint8_t weekday_bits[] = {50, 51, 52};
    static const uint16_t weekday_weights[] = {4, 2, 1};
    RadioClockPulse frame[JJY_FRAME_SECONDS];

    set_jjy_timecode(frame, 0, 0, 102, 26, 0, false, false, false, false);
    assert(symbols_to_value(frame, doy_bits, doy_weights, sizeof(doy_bits) / sizeof(doy_bits[0])) == 102U);
    assert(
        symbols_to_value(frame, weekday_bits, weekday_weights, sizeof(weekday_bits) / sizeof(weekday_bits[0])) == 0U);

    set_jjy_timecode(frame, 0, 0, 103, 26, 1, false, false, false, false);
    assert(symbols_to_value(frame, doy_bits, doy_weights, sizeof(doy_bits) / sizeof(doy_bits[0])) == 103U);
    assert(
        symbols_to_value(frame, weekday_bits, weekday_weights, sizeof(weekday_bits) / sizeof(weekday_bits[0])) == 1U);

    set_jjy_timecode(frame, 0, 0, 104, 26, 2, false, false, false, false);
    assert(symbols_to_value(frame, doy_bits, doy_weights, sizeof(doy_bits) / sizeof(doy_bits[0])) == 104U);
    assert(
        symbols_to_value(frame, weekday_bits, weekday_weights, sizeof(weekday_bits) / sizeof(weekday_bits[0])) == 2U);
}

static void test_reserved_and_leap_bits_follow_safe_defaults(void) {
    RadioClockPulse frame[JJY_FRAME_SECONDS];

    set_jjy_timecode(frame, 15, 17, 162, 16, 5, false, false, false, false);

    assert(frame[4] == RadioClockPulseZero);
    assert(frame[10] == RadioClockPulseZero);
    assert(frame[11] == RadioClockPulseZero);
    assert(frame[14] == RadioClockPulseZero);
    assert(frame[20] == RadioClockPulseZero);
    assert(frame[21] == RadioClockPulseZero);
    assert(frame[24] == RadioClockPulseZero);
    assert(frame[34] == RadioClockPulseZero);
    assert(frame[35] == RadioClockPulseZero);
    assert(frame[38] == RadioClockPulseZero);
    assert(frame[40] == RadioClockPulseZero);
    assert(frame[53] == RadioClockPulseZero);
    assert(frame[54] == RadioClockPulseZero);
    assert(frame[55] == RadioClockPulseZero);
    assert(frame[56] == RadioClockPulseZero);
    assert(frame[57] == RadioClockPulseZero);
    assert(frame[58] == RadioClockPulseZero);
}

static void test_format_b_uses_call_sign_block_at_15_and_45(void) {
    RadioClockPulse frame[JJY_FRAME_SECONDS];
    char actual[JJY_FRAME_SECONDS + 1];

    set_jjy_timecode(frame, 15, 17, 162, 16, 5, false, false, false, false);
    frame_to_string(frame, actual);
    assert(strcmp(actual + 40, "000010110M000000000M") == 0);

    set_jjy_timecode(frame, 45, 17, 162, 16, 5, false, false, false, false);
    frame_to_string(frame, actual);
    assert(strcmp(actual + 40, "000010110M000000000M") == 0);
}

int main(void) {
    test_nict_layout_vector_2026_04_13_1234();
    test_yesterday_today_tomorrow_dates_encode_requested_days();
    test_reserved_and_leap_bits_follow_safe_defaults();
    test_format_b_uses_call_sign_block_at_15_and_45();
    puts("test_set_jjy_message: OK");
    return 0;
}
