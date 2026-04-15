#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/dcf77_util.h"

static bool get_bit(const uint8_t* frame, uint8_t bit_index) {
    return !!(frame[bit_index / 8] & (1u << (7 - (bit_index % 8))));
}

static uint8_t bits_to_uint(const uint8_t* frame, uint8_t start, uint8_t count) {
    uint8_t value = 0;

    for(uint8_t i = 0; i < count; i++) {
        value |= (uint8_t)get_bit(frame, start + i) << i;
    }

    return value;
}

static uint8_t parity_range(const uint8_t* frame, uint8_t start, uint8_t end_inclusive) {
    uint8_t parity = 0;

    for(uint8_t bit = start; bit <= end_inclusive; bit++) {
        parity ^= (uint8_t)get_bit(frame, bit);
    }

    return parity;
}

static void assert_bytes_eq(const uint8_t* actual, const uint8_t* expected, size_t len, const char* label) {
    if(memcmp(actual, expected, len) == 0) {
        return;
    }

    fprintf(stderr, "%s mismatch\nexpected:", label);
    for(size_t i = 0; i < len; i++) {
        fprintf(stderr, " %02x", expected[i]);
    }
    fprintf(stderr, "\nactual:  ");
    for(size_t i = 0; i < len; i++) {
        fprintf(stderr, " %02x", actual[i]);
    }
    fprintf(stderr, "\n");
    assert(false);
}

static void test_online_gold_vector_2026_01_05_1430_cet(void) {
    uint8_t frame[8] = {0};

    set_dcf_message(frame, 30, 14, 5, 1, 26, 1, false, false, false, false, 0x0000);

    /* Online cross-check:
       https://www.fotoventus.cz/tools/osc/dcf.html
       2026-01-05 14:30 CET (weekday=1, DST=0) ->
       00000000000000000010100001100001010010100010010000011001001
    */
    static const uint8_t expected[8] = {0x00, 0x00, 0x28, 0x61, 0x4a, 0x24, 0x19, 0x20};
    assert_bytes_eq(frame, expected, sizeof(expected), "gold vector 2026-01-05 14:30 CET");
}

static void test_online_gold_vector_2024_06_15_0807_cest(void) {
    uint8_t frame[8] = {0};

    set_dcf_message(frame, 7, 8, 15, 6, 24, 6, true, false, false, false, 0x0000);

    /* Online cross-check:
       https://www.fotoventus.cz/tools/osc/dcf.html
       2024-06-15 08:07 CEST (weekday=6, DST=1) ->
       00000000000000000100111100001000100110101001101100001001001
    */
    static const uint8_t expected[8] = {0x00, 0x00, 0x4f, 0x08, 0x9a, 0x9b, 0x09, 0x20};
    assert_bytes_eq(frame, expected, sizeof(expected), "gold vector 2024-06-15 08:07 CEST");
}

static void test_online_gold_vector_2024_02_29_2359_cet(void) {
    uint8_t frame[8] = {0};

    set_dcf_message(frame, 59, 23, 29, 2, 24, 4, false, false, false, false, 0x0000);

    /* Online cross-check:
       https://www.fotoventus.cz/tools/osc/dcf.html
       2024-02-29 23:59 CET (weekday=4, DST=0) ->
       00000000000000000010110011010110001110010100101000001001001
    */
    static const uint8_t expected[8] = {0x00, 0x00, 0x2c, 0xd6, 0x39, 0x4a, 0x09, 0x20};
    assert_bytes_eq(frame, expected, sizeof(expected), "gold vector 2024-02-29 23:59 CET");
}

static void test_control_bits_and_civil_warning_bits(void) {
    uint8_t frame[8] = {0};
    const uint16_t civbits = 0x1234;

    set_dcf_message(frame, 0, 0, 1, 1, 0, 1, true, true, true, true, civbits);

    for(uint8_t bit = 1; bit <= 14; bit++) {
        const bool expected = !!(civbits & (1u << (14 - bit)));
        assert(get_bit(frame, bit) == expected);
    }

    assert(get_bit(frame, 15) == true);
    assert(get_bit(frame, 16) == true);
    assert(get_bit(frame, 17) == true);
    assert(get_bit(frame, 18) == false);
    assert(get_bit(frame, 19) == true);
    assert(get_bit(frame, 20) == true);
}

static void test_bcd_layout_and_even_parity_on_boundaries(void) {
    uint8_t frame[8] = {0};

    set_dcf_message(frame, 59, 23, 31, 12, 99, 7, false, false, false, false, 0x0000);

    assert(bits_to_uint(frame, 21, 7) == 0x59);
    assert(bits_to_uint(frame, 29, 6) == 0x23);
    assert(bits_to_uint(frame, 36, 6) == 0x31);
    assert(bits_to_uint(frame, 42, 3) == 7);
    assert(bits_to_uint(frame, 45, 5) == 0x12);
    assert(bits_to_uint(frame, 50, 8) == 0x99);

    assert(parity_range(frame, 21, 28) == 0);
    assert(parity_range(frame, 29, 35) == 0);
    assert(parity_range(frame, 36, 58) == 0);
}

static void test_invalid_calendar_values_still_encode_as_raw_bcd(void) {
    uint8_t frame[8] = {0};

    set_dcf_message(frame, 58, 21, 37, 13, 26, 7, false, false, false, false, 0x0000);

    assert(bits_to_uint(frame, 21, 7) == 0x58);
    assert(bits_to_uint(frame, 29, 6) == 0x21);
    assert(bits_to_uint(frame, 36, 6) == 0x37);
    assert(bits_to_uint(frame, 45, 5) == 0x13);
    assert(bits_to_uint(frame, 50, 8) == 0x26);
    assert(parity_range(frame, 21, 28) == 0);
    assert(parity_range(frame, 29, 35) == 0);
    assert(parity_range(frame, 36, 58) == 0);
}

static void test_impossible_february_date_remains_decodable(void) {
    uint8_t frame[8] = {0};

    set_dcf_message(frame, 1, 2, 30, 2, 24, 5, false, false, false, false, 0x0000);

    assert(bits_to_uint(frame, 36, 6) == 0x30);
    assert(bits_to_uint(frame, 45, 5) == 0x02);
    assert(bits_to_uint(frame, 50, 8) == 0x24);
    assert(bits_to_uint(frame, 42, 3) == 5);
    assert(parity_range(frame, 36, 58) == 0);
}

static void test_weekday_uses_only_three_bits(void) {
    uint8_t frame[8] = {0};

    set_dcf_message(frame, 12, 3, 4, 5, 26, 15, false, false, false, false, 0x0000);

    assert(bits_to_uint(frame, 42, 3) == 7);
    assert(parity_range(frame, 36, 58) == 0);
}

int main(void) {
    test_online_gold_vector_2026_01_05_1430_cet();
    test_online_gold_vector_2024_06_15_0807_cest();
    test_online_gold_vector_2024_02_29_2359_cet();
    test_control_bits_and_civil_warning_bits();
    test_bcd_layout_and_even_parity_on_boundaries();
    test_invalid_calendar_values_still_encode_as_raw_bcd();
    test_impossible_february_date_remains_decodable();
    test_weekday_uses_only_three_bits();

    puts("test_set_dcf_message: OK");
    return 0;
}
