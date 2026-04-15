#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/msf_util.h"

static void msf_bits_to_string(const bool* bits, char* out) {
    for(size_t i = 0; i < MSF_FRAME_SECONDS; i++) {
        out[i] = bits[i] ? '1' : '0';
    }

    out[MSF_FRAME_SECONDS] = '\0';
}

static uint8_t bits_to_value(
    const bool* bits,
    const uint8_t* positions,
    const uint8_t* weights,
    size_t count) {
    uint8_t value = 0U;

    for(size_t i = 0; i < count; i++) {
        if(bits[positions[i]]) {
            value = (uint8_t)(value + weights[i]);
        }
    }

    return value;
}

static uint8_t count_ones(const bool* bits, uint8_t start, uint8_t end_inclusive) {
    uint8_t count = 0U;

    for(uint8_t index = start; index <= end_inclusive; index++) {
        if(bits[index]) {
            count++;
        }
    }

    return count;
}

static void test_npl_layout_vector_2026_01_05_1430(void) {
    bool bits_a[MSF_FRAME_SECONDS];
    bool bits_b[MSF_FRAME_SECONDS];
    char actual_a[MSF_FRAME_SECONDS + 1];
    char actual_b[MSF_FRAME_SECONDS + 1];

    set_msf_timecode(bits_a, bits_b, 30, 14, 5, 1, 26, 1, false, false, 0);
    msf_bits_to_string(bits_a, actual_a);
    msf_bits_to_string(bits_b, actual_b);

    assert(strcmp(actual_a, "100000000000000000010011000001000101001010100011000001111110") == 0);
    assert(strcmp(actual_b, "100000000000000000000000000000000000000000000000000000000100") == 0);
}

static void test_field_encoding_and_odd_parity(void) {
    static const uint8_t year_positions[] = {17, 18, 19, 20, 21, 22, 23, 24};
    static const uint8_t year_weights[] = {80, 40, 20, 10, 8, 4, 2, 1};
    static const uint8_t month_positions[] = {25, 26, 27, 28, 29};
    static const uint8_t month_weights[] = {10, 8, 4, 2, 1};
    static const uint8_t day_positions[] = {30, 31, 32, 33, 34, 35};
    static const uint8_t day_weights[] = {20, 10, 8, 4, 2, 1};
    static const uint8_t weekday_positions[] = {36, 37, 38};
    static const uint8_t weekday_weights[] = {4, 2, 1};
    static const uint8_t hour_positions[] = {39, 40, 41, 42, 43, 44};
    static const uint8_t hour_weights[] = {20, 10, 8, 4, 2, 1};
    static const uint8_t minute_positions[] = {45, 46, 47, 48, 49, 50, 51};
    static const uint8_t minute_weights[] = {40, 20, 10, 8, 4, 2, 1};
    bool bits_a[MSF_FRAME_SECONDS];
    bool bits_b[MSF_FRAME_SECONDS];

    set_msf_timecode(bits_a, bits_b, 59, 23, 31, 12, 99, 6, true, true, 0);

    assert(bits_to_value(bits_a, year_positions, year_weights, sizeof(year_positions)) == 99U);
    assert(bits_to_value(bits_a, month_positions, month_weights, sizeof(month_positions)) == 12U);
    assert(bits_to_value(bits_a, day_positions, day_weights, sizeof(day_positions)) == 31U);
    assert(bits_to_value(bits_a, weekday_positions, weekday_weights, sizeof(weekday_positions)) == 6U);
    assert(bits_to_value(bits_a, hour_positions, hour_weights, sizeof(hour_positions)) == 23U);
    assert(bits_to_value(bits_a, minute_positions, minute_weights, sizeof(minute_positions)) == 59U);

    assert(((uint8_t)(count_ones(bits_a, 17, 24) + (bits_b[54] ? 1U : 0U)) & 1U) == 1U);
    assert(((uint8_t)(count_ones(bits_a, 25, 35) + (bits_b[55] ? 1U : 0U)) & 1U) == 1U);
    assert(((uint8_t)(count_ones(bits_a, 36, 38) + (bits_b[56] ? 1U : 0U)) & 1U) == 1U);
    assert(((uint8_t)(count_ones(bits_a, 39, 51) + (bits_b[57] ? 1U : 0U)) & 1U) == 1U);

    assert(bits_b[53] == true);
    assert(bits_b[58] == true);
}

static void test_invalid_calendar_values_still_encode(void) {
    static const uint8_t month_positions[] = {25, 26, 27, 28, 29};
    static const uint8_t month_weights[] = {10, 8, 4, 2, 1};
    static const uint8_t day_positions[] = {30, 31, 32, 33, 34, 35};
    static const uint8_t day_weights[] = {20, 10, 8, 4, 2, 1};
    bool bits_a[MSF_FRAME_SECONDS];
    bool bits_b[MSF_FRAME_SECONDS];

    set_msf_timecode(bits_a, bits_b, 1, 2, 30, 13, 26, 7, false, false, 0);

    assert(bits_to_value(bits_a, month_positions, month_weights, sizeof(month_positions)) == 13U);
    assert(bits_to_value(bits_a, day_positions, day_weights, sizeof(day_positions)) == 30U);
    assert(bits_b[54] == false);
    assert(bits_b[55] == false);
}

static void test_dut1_positive_and_negative(void) {
    bool bits_a[MSF_FRAME_SECONDS];
    bool bits_b[MSF_FRAME_SECONDS];

    set_msf_timecode(bits_a, bits_b, 0, 0, 1, 1, 0, 0, false, false, 3);
    assert(bits_b[1] == true);
    assert(bits_b[2] == true);
    assert(bits_b[3] == true);
    assert(bits_b[4] == false);
    assert(bits_b[9] == false);

    set_msf_timecode(bits_a, bits_b, 0, 0, 1, 1, 0, 0, false, false, -2);
    assert(bits_b[1] == false);
    assert(bits_b[9] == true);
    assert(bits_b[10] == true);
    assert(bits_b[11] == false);
}

int main(void) {
    test_npl_layout_vector_2026_01_05_1430();
    test_field_encoding_and_odd_parity();
    test_invalid_calendar_values_still_encode();
    test_dut1_positive_and_negative();
    puts("test_set_msf_message: OK");
    return 0;
}
