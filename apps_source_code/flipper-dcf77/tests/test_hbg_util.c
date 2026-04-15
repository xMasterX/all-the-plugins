#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "../src/hbg_util.h"

static uint32_t waveform_total_ticks(const RadioClockSecondWaveform* waveform) {
    uint32_t total = 0U;

    for(uint8_t i = 0; i < waveform->segment_count; i++) {
        total += waveform->segments[i].ticks;
    }

    return total;
}

static void test_start_pattern_selection(void) {
    assert(hbg_get_start_pattern(11, 37) == HbgStartPatternMinute);
    assert(hbg_get_start_pattern(7, 0) == HbgStartPatternHour);
    assert(hbg_get_start_pattern(0, 0) == HbgStartPatternNoon);
    assert(hbg_get_start_pattern(12, 0) == HbgStartPatternNoon);
}

static void test_minute_start_waveform(void) {
    RadioClockSecondWaveform waveform;

    hbg_build_start_waveform(&waveform, HbgStartPatternMinute);

    assert(waveform.segment_count == 4U);
    assert(waveform.segments[0].level == false);
    assert(waveform.segments[0].ticks == 3277U);
    assert(waveform.segments[1].level == true);
    assert(waveform.segments[1].ticks == 3277U);
    assert(waveform.segments[2].level == false);
    assert(waveform.segments[2].ticks == 3277U);
    assert(waveform.segments[3].level == true);
    assert(waveform_total_ticks(&waveform) == 32768U);
}

static void test_hour_start_waveform(void) {
    RadioClockSecondWaveform waveform;

    hbg_build_start_waveform(&waveform, HbgStartPatternHour);

    assert(waveform.segment_count == 6U);
    assert(waveform.segments[0].level == false);
    assert(waveform.segments[1].level == true);
    assert(waveform.segments[2].level == false);
    assert(waveform.segments[3].level == true);
    assert(waveform.segments[4].level == false);
    assert(waveform.segments[5].level == true);
    assert(waveform_total_ticks(&waveform) == 32768U);
}

static void test_noon_start_waveform(void) {
    RadioClockSecondWaveform waveform;

    hbg_build_start_waveform(&waveform, HbgStartPatternNoon);

    assert(waveform.segment_count == 8U);
    for(uint8_t i = 0; i < 7U; i++) {
        assert(waveform.segments[i].ticks == 3277U);
    }
    assert(waveform.segments[7].level == true);
    assert(waveform_total_ticks(&waveform) == 32768U);
}

int main(void) {
    test_start_pattern_selection();
    test_minute_start_waveform();
    test_hour_start_waveform();
    test_noon_start_waveform();
    puts("test_hbg_util: OK");
    return 0;
}
