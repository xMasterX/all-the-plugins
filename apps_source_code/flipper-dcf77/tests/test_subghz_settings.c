#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/subghz_settings.h"

static void test_note_table_boundaries_and_text(void) {
    char text[16];

    assert(dcf77_subghz_note_count() == 33U);
    assert(dcf77_subghz_note_freq_millihz(0) == 110000U);
    assert(dcf77_subghz_note_freq_millihz(dcf77_subghz_note_count() - 1U) == 698456U);
    assert(dcf77_subghz_note_half_period_us(0) == 4545U);
    assert(dcf77_subghz_note_half_period_us(12) == 2273U);
    assert(dcf77_subghz_note_half_period_us(dcf77_subghz_note_count() - 1U) == 716U);

    dcf77_subghz_format_note_text(text, sizeof(text), 0);
    assert(strcmp(text, "110") == 0);

    dcf77_subghz_format_note_text(text, sizeof(text), dcf77_subghz_note_count() - 1U);
    assert(strcmp(text, "698") == 0);
}

static void test_band_and_frequency_formatting(void) {
    char text[16];

    dcf77_subghz_format_band_text(text, sizeof(text), 304100000U);
    assert(strcmp(text, "304 MHz") == 0);

    dcf77_subghz_format_frequency_text(text, sizeof(text), 305277000U);
    assert(strcmp(text, "305.277") == 0);
    dcf77_subghz_format_frequency_khz_text(text, sizeof(text), 305277000U);
    assert(strcmp(text, "305277") == 0);
}

static void test_frequency_step_rounding_and_clamping(void) {
    const Dcf77SubGhzBand band = {
        .start_hz = 304100000U,
        .end_hz = 321950000U,
    };

    assert(dcf77_subghz_step_frequency(305277000U, &band, 1) == 305280000U);
    assert(dcf77_subghz_step_frequency(305277000U, &band, -1) == 305270000U);
    assert(dcf77_subghz_step_frequency(305280000U, &band, 1) == 305290000U);
    assert(dcf77_subghz_step_frequency(305280000U, &band, -1) == 305270000U);
    assert(dcf77_subghz_step_frequency(304100000U, &band, -1) == 304100000U);
    assert(dcf77_subghz_step_frequency(321949000U, &band, 1) == 321950000U);
    assert(dcf77_subghz_clamp_frequency(303000000U, &band) == 304100000U);
    assert(dcf77_subghz_clamp_frequency(330000000U, &band) == 321950000U);
}

static void test_tx_timeout_choices(void) {
    assert(dcf77_subghz_tx_timeout_count() == 11U);
    assert(dcf77_subghz_tx_timeout_default_index() == 8U);
    assert(dcf77_subghz_tx_timeout_seconds(0U) == 1U);
    assert(dcf77_subghz_tx_timeout_seconds(8U) == 300U);
    assert(dcf77_subghz_tx_timeout_seconds(dcf77_subghz_tx_timeout_count() - 1U) == 1800U);
    assert(dcf77_subghz_tx_timeout_index_for_seconds(300U) == 8U);
    assert(strcmp(dcf77_subghz_tx_timeout_label(8U), "5min") == 0);
}

int main(void) {
    test_note_table_boundaries_and_text();
    test_band_and_frequency_formatting();
    test_frequency_step_rounding_and_clamping();
    test_tx_timeout_choices();
    puts("test_subghz_settings: OK");
    return 0;
}
