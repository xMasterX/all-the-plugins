#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "experimental_time.h"

static const DateTime fallback_datetime = {
    .hour = 21,
    .minute = 17,
    .second = 33,
    .day = 13,
    .month = 4,
    .year = 2026,
    .weekday = 1,
};

static void test_reset_settings(void) {
    Dcf77ExperimentalTimeSettings settings;

    dcf77_experimental_time_reset_settings(&settings, &fallback_datetime);

    assert(settings.enabled == false);
    assert(settings.source == Dcf77ExperimentalTimeSourceFlipper);
    assert(settings.preset_datetime.hour == fallback_datetime.hour);
    assert(settings.preset_datetime.minute == fallback_datetime.minute);
    assert(settings.direction == Dcf77ExperimentalTimeDirectionForward);
    assert(settings.speedup == 1U);
    assert(settings.slowdown == 1U);
}

static void test_normalize_settings_invalid_preset(void) {
    Dcf77ExperimentalTimeSettings settings = {
        .enabled = true,
        .source = Dcf77ExperimentalTimeSourcePreset,
        .preset_datetime =
            {
                .hour = 29,
                .minute = 90,
                .second = 75,
                .day = 31,
                .month = 15,
                .year = 2107,
                .weekday = 0,
            },
        .direction = Dcf77ExperimentalTimeDirectionForward,
        .speedup = 9U,
        .slowdown = 8U,
    };

    dcf77_experimental_time_normalize_settings(&settings, &fallback_datetime);

    assert(settings.source == Dcf77ExperimentalTimeSourcePreset);
    assert(settings.preset_datetime.year == fallback_datetime.year);
    assert(settings.preset_datetime.month == fallback_datetime.month);
    assert(settings.preset_datetime.day == fallback_datetime.day);
    assert(settings.speedup == 5U);
    assert(settings.slowdown == 1U);
}

static void test_normalize_datetime_clamps_and_recomputes_weekday(void) {
    DateTime datetime = {
        .hour = 22,
        .minute = 9,
        .second = 14,
        .day = 31,
        .month = 2,
        .year = 2024,
        .weekday = 7,
    };

    dcf77_experimental_time_normalize_datetime(&datetime, &fallback_datetime);

    assert(datetime.year == 2024U);
    assert(datetime.month == 2U);
    assert(datetime.day == 29U);
    assert(datetime.weekday == 4U);
}

static void test_seed_runtime_uses_preset_when_enabled(void) {
    Dcf77ExperimentalTimeSettings settings;
    Dcf77ExperimentalTimeRuntime runtime;
    DateTime rtc_snapshot = fallback_datetime;

    dcf77_experimental_time_reset_settings(&settings, &fallback_datetime);
    settings.enabled = true;
    settings.source = Dcf77ExperimentalTimeSourcePreset;
    settings.preset_datetime.hour = 8;
    settings.preset_datetime.minute = 45;
    settings.preset_datetime.second = 12;

    dcf77_experimental_time_seed_runtime(&runtime, &settings, &rtc_snapshot);

    assert(runtime.frame_index == 0U);
    assert(runtime.base_datetime.hour == 8U);
    assert(runtime.base_datetime.minute == 45U);
    assert(runtime.base_datetime.second == 12U);
}

static void test_frame_datetime_normal_progression(void) {
    Dcf77ExperimentalTimeSettings settings;
    Dcf77ExperimentalTimeRuntime runtime;
    DateTime datetime;

    dcf77_experimental_time_reset_settings(&settings, &fallback_datetime);
    settings.enabled = true;
    dcf77_experimental_time_seed_runtime(&runtime, &settings, &fallback_datetime);
    dcf77_experimental_time_get_frame_datetime(&settings, &runtime, 2U, &datetime);

    assert(datetime.hour == 21U);
    assert(datetime.minute == 19U);
    assert(datetime.second == 33U);
}

static void test_frame_datetime_speedup(void) {
    Dcf77ExperimentalTimeSettings settings;
    Dcf77ExperimentalTimeRuntime runtime;
    DateTime datetime;

    dcf77_experimental_time_reset_settings(&settings, &fallback_datetime);
    settings.enabled = true;
    settings.speedup = 2U;
    dcf77_experimental_time_normalize_settings(&settings, &fallback_datetime);
    dcf77_experimental_time_seed_runtime(&runtime, &settings, &fallback_datetime);
    dcf77_experimental_time_get_frame_datetime(&settings, &runtime, 3U, &datetime);

    assert(datetime.hour == 21U);
    assert(datetime.minute == 23U);
}

static void test_frame_datetime_slowdown(void) {
    Dcf77ExperimentalTimeSettings settings;
    Dcf77ExperimentalTimeRuntime runtime;
    DateTime datetime;

    dcf77_experimental_time_reset_settings(&settings, &fallback_datetime);
    settings.enabled = true;
    settings.slowdown = 3U;
    dcf77_experimental_time_normalize_settings(&settings, &fallback_datetime);
    dcf77_experimental_time_seed_runtime(&runtime, &settings, &fallback_datetime);
    dcf77_experimental_time_get_frame_datetime(&settings, &runtime, 5U, &datetime);

    assert(datetime.hour == 21U);
    assert(datetime.minute == 18U);
}

static void test_frame_datetime_stopped(void) {
    Dcf77ExperimentalTimeSettings settings;
    Dcf77ExperimentalTimeRuntime runtime;
    DateTime datetime;

    dcf77_experimental_time_reset_settings(&settings, &fallback_datetime);
    settings.enabled = true;
    settings.direction = Dcf77ExperimentalTimeDirectionStopped;
    settings.speedup = 5U;
    dcf77_experimental_time_normalize_settings(&settings, &fallback_datetime);
    dcf77_experimental_time_seed_runtime(&runtime, &settings, &fallback_datetime);
    dcf77_experimental_time_get_frame_datetime(&settings, &runtime, 99U, &datetime);

    assert(datetime.hour == fallback_datetime.hour);
    assert(datetime.minute == fallback_datetime.minute);
    assert(datetime.second == fallback_datetime.second);
}

static void test_frame_datetime_backwards(void) {
    Dcf77ExperimentalTimeSettings settings;
    Dcf77ExperimentalTimeRuntime runtime;
    DateTime datetime;

    dcf77_experimental_time_reset_settings(&settings, &fallback_datetime);
    settings.enabled = true;
    settings.direction = Dcf77ExperimentalTimeDirectionBackwards;
    dcf77_experimental_time_seed_runtime(&runtime, &settings, &fallback_datetime);
    dcf77_experimental_time_get_frame_datetime(&settings, &runtime, 2U, &datetime);

    assert(datetime.hour == 21U);
    assert(datetime.minute == 15U);
    assert(datetime.second == 33U);
}

static void test_frame_datetime_backwards_speedup_and_slowdown(void) {
    Dcf77ExperimentalTimeSettings settings;
    Dcf77ExperimentalTimeRuntime runtime;
    DateTime datetime;

    dcf77_experimental_time_reset_settings(&settings, &fallback_datetime);
    settings.enabled = true;
    settings.direction = Dcf77ExperimentalTimeDirectionBackwards;
    settings.speedup = 2U;
    dcf77_experimental_time_normalize_settings(&settings, &fallback_datetime);
    dcf77_experimental_time_seed_runtime(&runtime, &settings, &fallback_datetime);
    dcf77_experimental_time_get_frame_datetime(&settings, &runtime, 3U, &datetime);

    assert(datetime.hour == 21U);
    assert(datetime.minute == 11U);

    settings.speedup = 1U;
    settings.slowdown = 3U;
    dcf77_experimental_time_normalize_settings(&settings, &fallback_datetime);
    dcf77_experimental_time_seed_runtime(&runtime, &settings, &fallback_datetime);
    dcf77_experimental_time_get_frame_datetime(&settings, &runtime, 5U, &datetime);

    assert(datetime.hour == 21U);
    assert(datetime.minute == 16U);
}

static void test_frame_datetime_rollover(void) {
    Dcf77ExperimentalTimeSettings settings;
    Dcf77ExperimentalTimeRuntime runtime;
    DateTime seed = {
        .hour = 23,
        .minute = 58,
        .second = 42,
        .day = 31,
        .month = 12,
        .year = 2026,
        .weekday = 4,
    };
    DateTime datetime;

    dcf77_experimental_time_reset_settings(&settings, &seed);
    settings.enabled = true;
    settings.speedup = 2U;
    dcf77_experimental_time_normalize_settings(&settings, &seed);
    dcf77_experimental_time_seed_runtime(&runtime, &settings, &seed);
    dcf77_experimental_time_get_frame_datetime(&settings, &runtime, 2U, &datetime);

    assert(datetime.year == 2027U);
    assert(datetime.month == 1U);
    assert(datetime.day == 1U);
    assert(datetime.hour == 0U);
    assert(datetime.minute == 2U);
}

static void test_current_frame_minute_zeroes_seconds(void) {
    Dcf77ExperimentalTimeSettings settings;
    Dcf77ExperimentalTimeRuntime runtime;
    DateTime datetime;

    dcf77_experimental_time_reset_settings(&settings, &fallback_datetime);
    settings.enabled = true;
    dcf77_experimental_time_seed_runtime(&runtime, &settings, &fallback_datetime);
    dcf77_experimental_time_get_current_frame_minute(&settings, &runtime, &datetime);

    assert(datetime.hour == fallback_datetime.hour);
    assert(datetime.minute == fallback_datetime.minute);
    assert(datetime.second == 0U);
}

static void test_preset_second_alignment(void) {
    Dcf77ExperimentalTimeSettings settings;
    Dcf77ExperimentalTimeRuntime runtime;

    dcf77_experimental_time_reset_settings(&settings, &fallback_datetime);
    settings.enabled = true;
    settings.source = Dcf77ExperimentalTimeSourcePreset;
    settings.preset_datetime = (DateTime){
        .hour = 6,
        .minute = 7,
        .second = 58,
        .day = 2,
        .month = 4,
        .year = 2026,
        .weekday = 4,
    };
    dcf77_experimental_time_normalize_settings(&settings, &fallback_datetime);
    dcf77_experimental_time_seed_runtime(&runtime, &settings, &fallback_datetime);

    assert(dcf77_experimental_time_get_start_second(&runtime) == 58U);
    dcf77_experimental_time_advance_runtime(&runtime);
    assert(runtime.frame_index == 1U);
}

int main(void) {
    test_reset_settings();
    test_normalize_settings_invalid_preset();
    test_normalize_datetime_clamps_and_recomputes_weekday();
    test_seed_runtime_uses_preset_when_enabled();
    test_frame_datetime_normal_progression();
    test_frame_datetime_speedup();
    test_frame_datetime_slowdown();
    test_frame_datetime_stopped();
    test_frame_datetime_backwards();
    test_frame_datetime_backwards_speedup_and_slowdown();
    test_frame_datetime_rollover();
    test_current_frame_minute_zeroes_seconds();
    test_preset_second_alignment();

    puts("test_experimental_time: ok");
    return 0;
}
