#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/bpc_util.h"

#define BPC_FRAME_SECONDS 60U

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
    for(size_t i = 0; i < BPC_FRAME_SECONDS; i++) {
        out[i] = pulse_to_char(frame[i]);
    }

    out[BPC_FRAME_SECONDS] = '\0';
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

static void test_captured_jinan_example_starts_with_known_symbols(void) {
    RadioClockPulse frame[BPC_FRAME_SECONDS];
    char actual[BPC_FRAME_SECONDS + 1];

    /* Public BPC capture example: 2026-01-11 07:34:00 CST, Sunday.
       The first block after the marker is documented as:
       00 00 01 11 10 00 10 01 11 00. */
    set_bpc_timecode(frame, 34, 7, 11, 1, 26, 7);
    frame_to_string(frame, actual);

    assert(strncmp(actual, "M0013202130", 11U) == 0);
}

static void test_captured_jinan_example_full_minute_matches_repeated_blocks(void) {
    RadioClockPulse frame[BPC_FRAME_SECONDS];
    char actual[BPC_FRAME_SECONDS + 1];

    set_bpc_timecode(frame, 34, 7, 11, 1, 26, 7);
    frame_to_string(frame, actual);

    assert(
        strcmp(
            actual,
            "M0013202130023011221M1013202131023011220M2013202131023011220") == 0);
}

static void test_block_fields_and_block_index_digits(void) {
    RadioClockPulse frame[BPC_FRAME_SECONDS];

    set_bpc_timecode(frame, 52, 19, 1, 12, 31, 2);

    assert(frame[0] == RadioClockPulseMarker);
    assert(frame[20] == RadioClockPulseMarker);
    assert(frame[40] == RadioClockPulseMarker);
    assert(pulse_to_digit(frame[1]) == 0U);
    assert(pulse_to_digit(frame[21]) == 1U);
    assert(pulse_to_digit(frame[41]) == 2U);

    assert(weighted_pair_value(frame, 3U, 8U, 4U) + weighted_pair_value(frame, 4U, 2U, 1U) == 7U);
    assert(
        weighted_pair_value(frame, 5U, 32U, 16U) + weighted_pair_value(frame, 6U, 8U, 4U) +
            weighted_pair_value(frame, 7U, 2U, 1U) ==
        52U);
    assert(weighted_pair_value(frame, 8U, 0U, 4U) + weighted_pair_value(frame, 9U, 2U, 1U) == 2U);
    assert(weighted_pair_value(frame, 12U, 8U, 4U) + weighted_pair_value(frame, 13U, 2U, 1U) == 1U);
    assert(weighted_pair_value(frame, 14U, 8U, 4U) + weighted_pair_value(frame, 15U, 2U, 1U) == 12U);
    assert(
        weighted_pair_value(frame, 16U, 32U, 16U) + weighted_pair_value(frame, 17U, 8U, 4U) +
            weighted_pair_value(frame, 18U, 2U, 1U) + weighted_pair_value(frame, 19U, 64U, 0U) ==
        31U);

    assert((pulse_to_digit(frame[10]) & 0x02U) != 0U);
    assert((pulse_to_digit(frame[19]) & 0x02U) == 0U);
    assert(frame[11] == RadioClockPulsePair00);
}

static void test_parity_changes_when_payload_changes(void) {
    RadioClockPulse frame_a[BPC_FRAME_SECONDS];
    RadioClockPulse frame_b[BPC_FRAME_SECONDS];

    set_bpc_timecode(frame_a, 0, 0, 1, 1, 0, 1);
    set_bpc_timecode(frame_b, 1, 0, 1, 1, 0, 1);

    assert(frame_a[10] != frame_b[10]);
}

static void test_time_and_date_parity_groups_change_independently(void) {
    RadioClockPulse minute_a[BPC_FRAME_SECONDS];
    RadioClockPulse minute_b[BPC_FRAME_SECONDS];
    RadioClockPulse date_a[BPC_FRAME_SECONDS];
    RadioClockPulse date_b[BPC_FRAME_SECONDS];

    set_bpc_timecode(minute_a, 34, 7, 11, 1, 26, 7);
    set_bpc_timecode(minute_b, 35, 7, 11, 1, 26, 7);
    assert(minute_a[10] != minute_b[10]);
    assert(minute_a[19] != minute_b[19]);

    set_bpc_timecode(date_a, 34, 7, 11, 1, 26, 7);
    set_bpc_timecode(date_b, 34, 7, 12, 1, 26, 7);
    assert(date_a[10] == date_b[10]);
    assert(date_a[19] != date_b[19]);
}

int main(void) {
    test_captured_jinan_example_starts_with_known_symbols();
    test_captured_jinan_example_full_minute_matches_repeated_blocks();
    test_block_fields_and_block_index_digits();
    test_parity_changes_when_payload_changes();
    test_time_and_date_parity_groups_change_independently();
    puts("test_set_bpc_message: OK");
    return 0;
}
