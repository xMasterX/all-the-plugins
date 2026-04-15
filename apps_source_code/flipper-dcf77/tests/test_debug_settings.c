#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/debug_settings.h"

static void test_baseband_pin_options_and_formatting(void) {
    char text[16];

    assert(dcf77_debug_gpio_baseband_pin_count() == 8U);
    assert(dcf77_debug_gpio_baseband_default_pin() == 7U);
    assert(dcf77_debug_gpio_baseband_pin_at(0U) == 3U);
    assert(dcf77_debug_gpio_baseband_pin_at(7U) == 0U);
    assert(dcf77_debug_gpio_baseband_pin_valid(0U));
    assert(dcf77_debug_gpio_baseband_pin_valid(7U));
    assert(dcf77_debug_gpio_baseband_pin_valid(16U));
    assert(!dcf77_debug_gpio_baseband_pin_valid(2U));
    assert(!dcf77_debug_gpio_baseband_pin_valid(17U));
    assert(!dcf77_debug_gpio_baseband_pin_valid(13U));

    dcf77_debug_format_pin_text(text, sizeof(text), 0U, true);
    assert(strcmp(text, "None") == 0);
    dcf77_debug_format_pin_text(text, sizeof(text), 7U, false);
    assert(strcmp(text, "P7") == 0);

    assert(dcf77_debug_gpio_baseband_step(7U, 1, 0U) == 15U);
    assert(dcf77_debug_gpio_baseband_step(16U, 1, 0U) == 0U);
    assert(dcf77_debug_gpio_baseband_step(7U, 1, 15U) == 16U);
    assert(dcf77_debug_gpio_baseband_step(16U, -1, 0U) == 15U);
}

static void test_gpio_rf_options_and_formatting(void) {
    char text[16];

    assert(dcf77_debug_gpio_rf_pin_count() == 2U);
    assert(dcf77_debug_gpio_rf_default_pin() == 0U);
    assert(dcf77_debug_gpio_rf_pin_valid(0U));
    assert(dcf77_debug_gpio_rf_pin_valid(2U));
    assert(!dcf77_debug_gpio_rf_pin_valid(4U));

    dcf77_debug_format_pin_text(text, sizeof(text), 0U, true);
    assert(strcmp(text, "None") == 0);
    dcf77_debug_format_pin_text(text, sizeof(text), 2U, true);
    assert(strcmp(text, "P2") == 0);

    assert(dcf77_debug_gpio_rf_step(0U, 1) == 2U);
    assert(dcf77_debug_gpio_rf_step(2U, 1) == 0U);
    assert(dcf77_debug_gpio_rf_step(0U, -1) == 2U);
}

static void test_gpio_rf_duty_helpers(void) {
    char text[16];

    assert(dcf77_debug_gpio_rf_duty_count() == 17U);
    assert(dcf77_debug_gpio_rf_duty_at(0U) == 10U);
    assert(dcf77_debug_gpio_rf_duty_at(16U) == 90U);
    assert(dcf77_debug_gpio_rf_duty_normalize(0U) == 10U);
    assert(dcf77_debug_gpio_rf_duty_normalize(53U) == 50U);
    assert(dcf77_debug_gpio_rf_duty_normalize(99U) == 90U);
    assert(dcf77_debug_gpio_rf_duty_step(50U, 1) == 55U);
    assert(dcf77_debug_gpio_rf_duty_step(50U, -1) == 45U);
    assert(dcf77_debug_gpio_rf_duty_index(50U) == 8U);

    dcf77_debug_format_gpio_rf_duty_text(text, sizeof(text), 53U);
    assert(strcmp(text, "50%") == 0);
}

static void test_led_and_screen_labels(void) {
    assert(dcf77_debug_led_color_count() == 11U);
    assert(strcmp(dcf77_debug_led_color_label(0U), "None") == 0);
    assert(strcmp(dcf77_debug_led_color_label(1U), "Orange") == 0);
    assert(strcmp(dcf77_debug_led_color_label(2U), "Red") == 0);
    assert(strcmp(dcf77_debug_led_color_label(7U), "Violet") == 0);
    assert(strcmp(dcf77_debug_led_color_label(8U), "Magenta") == 0);
    assert(strcmp(dcf77_debug_led_color_label(9U), "Pink") == 0);
    assert(strcmp(dcf77_debug_led_color_label(10U), "White") == 0);
    assert(dcf77_debug_led_color_at(0U) == 0U);
    assert(dcf77_debug_led_color_at(1U) == 2U);
    assert(dcf77_debug_led_color_at(2U) == 1U);
    assert(dcf77_debug_led_color_at(7U) == 9U);
    assert(dcf77_debug_led_color_at(8U) == 7U);
    assert(dcf77_debug_led_color_at(10U) == 8U);
    assert(dcf77_debug_led_color_index(1U) == 2U);
    assert(dcf77_debug_led_color_index(2U) == 1U);
    assert(dcf77_debug_led_color_index(9U) == 7U);
    assert(dcf77_debug_led_color_index(7U) == 8U);
    assert(dcf77_debug_led_color_index(8U) == 10U);

    assert(dcf77_debug_screen_mode_count() == 2U);
    assert(strcmp(dcf77_debug_screen_mode_label(0U), "Debug") == 0);
    assert(strcmp(dcf77_debug_screen_mode_label(1U), "Basic") == 0);
}

int main(void) {
    test_baseband_pin_options_and_formatting();
    test_gpio_rf_options_and_formatting();
    test_gpio_rf_duty_helpers();
    test_led_and_screen_labels();
    puts("test_debug_settings: OK");
    return 0;
}
