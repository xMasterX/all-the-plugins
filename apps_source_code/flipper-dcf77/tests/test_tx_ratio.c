#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "tx_ratio.h"

static void test_y_values_sequence(void) {
    static const uint8_t expected[] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 12U, 15U, 16U, 20U, 24U};

    assert(dcf77_tx_ratio_y_count() == sizeof(expected) / sizeof(expected[0]));

    for(size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        assert(dcf77_tx_ratio_y_value(i) == expected[i]);
        assert(dcf77_tx_ratio_y_index_for_value(expected[i]) == i);
    }
}

static void test_unknown_y_value_falls_back_to_first_index(void) {
    assert(dcf77_tx_ratio_y_index_for_value(11U) == 0U);
}

static void test_x_is_clamped_to_y(void) {
    assert(dcf77_tx_ratio_x_clamp(5U, 3U) == 3U);
    assert(dcf77_tx_ratio_x_clamp(0U, 3U) == 1U);
}

static void test_send_ratio_patterns(void) {
    static const bool ratio_one_of_two[] = {true, false, true, false, true, false};
    static const bool ratio_two_of_three[] = {true, true, false, true, true, false};
    static const bool ratio_two_of_six[] = {
        true, false, false, true, false, false, true, false, false, true, false, false};

    for(size_t i = 0; i < sizeof(ratio_one_of_two) / sizeof(ratio_one_of_two[0]); i++) {
        assert(dcf77_tx_ratio_should_send((uint32_t)i, 1U, 2U) == ratio_one_of_two[i]);
        assert(dcf77_tx_ratio_should_send((uint32_t)i, 2U, 3U) == ratio_two_of_three[i]);
    }

    for(size_t i = 0; i < sizeof(ratio_two_of_six) / sizeof(ratio_two_of_six[0]); i++) {
        assert(dcf77_tx_ratio_should_send((uint32_t)i, 2U, 6U) == ratio_two_of_six[i]);
    }
}

static void test_one_of_one_always_sends(void) {
    for(uint32_t i = 0; i < 64U; i++) {
        assert(dcf77_tx_ratio_should_send(i, 1U, 1U));
    }
}

int main(void) {
    test_y_values_sequence();
    test_unknown_y_value_falls_back_to_first_index();
    test_x_is_clamped_to_y();
    test_send_ratio_patterns();
    test_one_of_one_always_sends();

    puts("test_tx_ratio: ok");
    return 0;
}
