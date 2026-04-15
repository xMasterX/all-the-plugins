#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/bsf_util.h"

#define BSF_FRAME_SECONDS 60U

static char pulse_to_char(RadioClockPulse pulse) {
    switch(pulse) {
    case RadioClockPulsePair01:
        return '1';
    case RadioClockPulsePair10:
        return '2';
    case RadioClockPulsePair11:
        return '3';
    case RadioClockPulseMarker:
        return 'M';
    case RadioClockPulsePair00:
    default:
        return '0';
    }
}

static void frame_to_string(const RadioClockPulse* frame, char* out) {
    for(size_t i = 0; i < BSF_FRAME_SECONDS; i++) {
        out[i] = pulse_to_char(frame[i]);
    }

    out[BSF_FRAME_SECONDS] = '\0';
}

static uint8_t pulse_to_digit(RadioClockPulse pulse) {
    switch(pulse) {
    case RadioClockPulsePair01:
        return 1U;
    case RadioClockPulsePair10:
        return 2U;
    case RadioClockPulsePair11:
        return 3U;
    case RadioClockPulsePair00:
    default:
        return 0U;
    }
}

static bool pulse_high_bit(RadioClockPulse pulse) {
    return (pulse_to_digit(pulse) & 0x02U) != 0U;
}

static bool pulse_low_bit(RadioClockPulse pulse) {
    return (pulse_to_digit(pulse) & 0x01U) != 0U;
}

static uint16_t weighted_pair_value(const RadioClockPulse* frame, uint8_t second, uint16_t high_weight, uint16_t low_weight) {
    uint16_t value = 0U;

    if(pulse_high_bit(frame[second])) {
        value = (uint16_t)(value + high_weight);
    }
    if(pulse_low_bit(frame[second])) {
        value = (uint16_t)(value + low_weight);
    }

    return value;
}

static void test_taiwan_example_2009_09_28_0916(void) {
    RadioClockPulse frame[BSF_FRAME_SECONDS];
    char actual[BSF_FRAME_SECONDS + 1];

    set_bsf_timecode(frame, 16, 9, 28, 9, 9, 1, false, false);
    frame_to_string(frame, actual);

    /* Taiwan LF time-signal project example: 09:16 AM, Monday, 2009/09/28. */
    assert(strcmp(actual, "000000000000000000000000000000000000000M1100102320121010300M") == 0);
}

static void test_time_and_date_fields_decode_back(void) {
    RadioClockPulse frame[BSF_FRAME_SECONDS];

    set_bsf_timecode(frame, 58, 23, 31, 12, 26, 6, true, false);

    assert(frame[0] == RadioClockPulsePair00);
    assert(frame[39] == RadioClockPulseMarker);
    assert(frame[59] == RadioClockPulseMarker);
    assert(frame[40] == RadioClockPulsePair01);

    assert(
        weighted_pair_value(frame, 41U, 32U, 16U) + weighted_pair_value(frame, 42U, 8U, 4U) +
            weighted_pair_value(frame, 43U, 2U, 1U) ==
        58U);
    assert(
        weighted_pair_value(frame, 44U, 16U, 8U) + weighted_pair_value(frame, 45U, 4U, 2U) +
            (pulse_high_bit(frame[46U]) ? 1U : 0U) ==
        23U);
    assert(
        weighted_pair_value(frame, 47U, 16U, 8U) + weighted_pair_value(frame, 48U, 4U, 2U) +
            (pulse_high_bit(frame[49U]) ? 1U : 0U) ==
        31U);
    assert((pulse_low_bit(frame[49U]) ? 4U : 0U) + weighted_pair_value(frame, 50U, 2U, 1U) == 6U);
    assert(weighted_pair_value(frame, 51U, 8U, 4U) + weighted_pair_value(frame, 52U, 2U, 1U) == 12U);
    assert(
        weighted_pair_value(frame, 53U, 64U, 32U) + weighted_pair_value(frame, 54U, 16U, 8U) +
            weighted_pair_value(frame, 55U, 4U, 2U) + (pulse_high_bit(frame[56U]) ? 1U : 0U) ==
        26U);
    assert(pulse_low_bit(frame[57U]) == true);
    assert(pulse_low_bit(frame[58U]) == false);
}

static void test_bsf_parity_changes_with_payload(void) {
    RadioClockPulse frame_a[BSF_FRAME_SECONDS];
    RadioClockPulse frame_b[BSF_FRAME_SECONDS];

    set_bsf_timecode(frame_a, 16, 9, 28, 9, 9, 1, false, false);
    set_bsf_timecode(frame_b, 17, 9, 28, 9, 9, 1, false, false);

    assert(frame_a[46] != frame_b[46]);
}

static void test_sunday_weekday_wraps_to_zero(void) {
    RadioClockPulse frame[BSF_FRAME_SECONDS];

    set_bsf_timecode(frame, 0, 0, 6, 4, 26, 7, false, false);

    assert(pulse_low_bit(frame[49U]) == false);
    assert(weighted_pair_value(frame, 50U, 2U, 1U) == 0U);
}

static void test_time_and_date_parity_groups_change_independently(void) {
    RadioClockPulse time_a[BSF_FRAME_SECONDS];
    RadioClockPulse time_b[BSF_FRAME_SECONDS];
    RadioClockPulse date_a[BSF_FRAME_SECONDS];
    RadioClockPulse date_b[BSF_FRAME_SECONDS];

    set_bsf_timecode(time_a, 16, 9, 28, 9, 9, 1, false, false);
    set_bsf_timecode(time_b, 17, 9, 28, 9, 9, 1, false, false);
    assert(time_a[46] != time_b[46]);
    assert(time_a[56] == time_b[56]);

    set_bsf_timecode(date_a, 16, 9, 28, 9, 9, 1, false, false);
    set_bsf_timecode(date_b, 16, 9, 29, 9, 9, 1, false, false);
    assert(date_a[46] == date_b[46]);
    assert(date_a[56] != date_b[56]);
}

int main(void) {
    test_taiwan_example_2009_09_28_0916();
    test_time_and_date_fields_decode_back();
    test_bsf_parity_changes_with_payload();
    test_sunday_weekday_wraps_to_zero();
    test_time_and_date_parity_groups_change_independently();
    puts("test_set_bsf_message: OK");
    return 0;
}
