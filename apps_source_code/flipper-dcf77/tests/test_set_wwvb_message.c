#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/wwvb_util.h"

#define WWVB_FRAME_SECONDS 60U

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
    for(size_t i = 0; i < 60; i++) {
        out[i] = pulse_to_char(frame[i]);
    }
    out[60] = '\0';
}

static void test_nist_gold_vector_2001_day_258_1842_utc(void) {
    RadioClockPulse frame[WWVB_FRAME_SECONDS];
    char actual[61];

    set_wwvb_am_symbols(frame, 42, 18, 258, 1, true, true, false, false, -7);
    frame_to_string(frame, actual);

    /* NIST Special Publication 432, Fig. 2.6 / Table 2.3:
       decoded UTC at the start of frame is year 2001, day 258, 18:42
       with UT1 correction -0.7 s. */
    assert(wwvb_day_of_year(2001, 9, 15) == 258);
    assert(
        strcmp(
            actual,
            "M10000010M000101000M001000101M100000010M011100000M000100011M") == 0);
}

static void test_field_encoding_and_markers(void) {
    RadioClockPulse frame[WWVB_FRAME_SECONDS];

    set_wwvb_am_symbols(frame, 59, 23, 366, 24, false, false, true, true, 5);

    assert(frame[0] == RadioClockPulseMarker);
    assert(frame[9] == RadioClockPulseMarker);
    assert(frame[19] == RadioClockPulseMarker);
    assert(frame[29] == RadioClockPulseMarker);
    assert(frame[39] == RadioClockPulseMarker);
    assert(frame[49] == RadioClockPulseMarker);
    assert(frame[59] == RadioClockPulseMarker);

    assert(frame[1] == RadioClockPulseOne);
    assert(frame[2] == RadioClockPulseZero);
    assert(frame[3] == RadioClockPulseOne);
    assert(frame[5] == RadioClockPulseOne);
    assert(frame[6] == RadioClockPulseZero);
    assert(frame[7] == RadioClockPulseZero);
    assert(frame[8] == RadioClockPulseOne);

    assert(frame[12] == RadioClockPulseOne);
    assert(frame[13] == RadioClockPulseZero);
    assert(frame[15] == RadioClockPulseZero);
    assert(frame[16] == RadioClockPulseZero);
    assert(frame[17] == RadioClockPulseOne);
    assert(frame[18] == RadioClockPulseOne);

    assert(frame[36] == RadioClockPulseOne);
    assert(frame[37] == RadioClockPulseZero);
    assert(frame[38] == RadioClockPulseOne);
    assert(frame[40] == RadioClockPulseZero);
    assert(frame[41] == RadioClockPulseOne);
    assert(frame[42] == RadioClockPulseZero);
    assert(frame[43] == RadioClockPulseOne);

    assert(frame[55] == RadioClockPulseOne);
    assert(frame[56] == RadioClockPulseOne);
    assert(frame[57] == RadioClockPulseZero);
    assert(frame[58] == RadioClockPulseZero);
}

static void test_day_of_year_boundaries(void) {
    assert(wwvb_day_of_year(2025, 1, 1) == 1);
    assert(wwvb_day_of_year(2025, 12, 31) == 365);
    assert(wwvb_day_of_year(2024, 2, 29) == 60);
    assert(wwvb_day_of_year(2024, 12, 31) == 366);
}

static void test_ut1_sign_and_magnitude_bits(void) {
    RadioClockPulse frame[WWVB_FRAME_SECONDS];

    set_wwvb_am_symbols(frame, 0, 0, 1, 26, false, false, false, false, 0);
    assert(frame[36] == RadioClockPulseZero);
    assert(frame[37] == RadioClockPulseZero);
    assert(frame[38] == RadioClockPulseZero);
    assert(frame[40] == RadioClockPulseZero);
    assert(frame[41] == RadioClockPulseZero);
    assert(frame[42] == RadioClockPulseZero);
    assert(frame[43] == RadioClockPulseZero);

    set_wwvb_am_symbols(frame, 0, 0, 1, 26, false, false, false, false, 3);
    assert(frame[36] == RadioClockPulseOne);
    assert(frame[37] == RadioClockPulseZero);
    assert(frame[38] == RadioClockPulseOne);
    assert(frame[40] == RadioClockPulseZero);
    assert(frame[41] == RadioClockPulseZero);
    assert(frame[42] == RadioClockPulseOne);
    assert(frame[43] == RadioClockPulseOne);

    set_wwvb_am_symbols(frame, 0, 0, 1, 26, false, false, false, false, -6);
    assert(frame[36] == RadioClockPulseZero);
    assert(frame[37] == RadioClockPulseOne);
    assert(frame[38] == RadioClockPulseZero);
    assert(frame[40] == RadioClockPulseZero);
    assert(frame[41] == RadioClockPulseOne);
    assert(frame[42] == RadioClockPulseOne);
    assert(frame[43] == RadioClockPulseZero);
}

int main(void) {
    test_nist_gold_vector_2001_day_258_1842_utc();
    test_field_encoding_and_markers();
    test_day_of_year_boundaries();
    test_ut1_sign_and_magnitude_bits();
    puts("test_set_wwvb_message: OK");
    return 0;
}
