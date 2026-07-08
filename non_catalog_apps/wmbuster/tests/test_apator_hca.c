#include "../drivers/engine/driver.h"

#include <stdio.h>
#include <string.h>

extern const WmbusDriver wmbus_drv_apator_hca;

const WmbusDriver* const wmbus_engine_registry[] = { &wmbus_drv_apator_hca };
const size_t wmbus_engine_registry_len = 1;

#define MANUF(a, b, c) \
    ((uint16_t)((((uint16_t)((a) - '@')) << 10) | \
                (((uint16_t)((b) - '@')) << 5) | \
                ((uint16_t)((c) - '@'))))

static int expect_contains(const char* name, const char* out, const char* expected) {
    if(strstr(out, expected)) return 0;
    fprintf(stderr, "%s: missing '%s' in:\n%s\n", name, expected, out);
    return 1;
}

static int run_sample(void) {
    const uint8_t apdu[] = {
        0x21, 0x01, 0x00, 0x87, 0x19, 0x00, 0x00, 0x04,
        0x20, 0xE7, 0x34, 0x88, 0x16, 0x08, 0x19,
    };
    char out[256];
    int fails = 0;

    wmbus_engine_decode(MANUF('A', 'P', 'A'), 0x04, 0x08,
                        apdu, sizeof(apdu), out, sizeof(out));

    fails += expect_contains("sample", out, "Apator HCA\n");
    fails += expect_contains("sample", out, "Units 6535\n");
    fails += expect_contains("sample", out, "RefUnits 8196\n");
    fails += expect_contains("sample", out, "TempC 22\n");
    fails += expect_contains("sample", out, "Marker E734\n");
    fails += expect_contains("sample", out, "Flags 0000880819\n");

    return fails;
}

static int run_bad_length(void) {
    const uint8_t apdu[] = {0x21, 0x01, 0x00};
    char out[256];
    int fails = 0;

    wmbus_engine_decode(MANUF('A', 'P', 'A'), 0x04, 0x08,
                        apdu, sizeof(apdu), out, sizeof(out));

    fails += expect_contains("bad_length", out, "Apator HCA\n");
    fails += expect_contains("bad_length", out, "Bytes 3\n");
    fails += expect_contains("bad_length", out, "Status bad length\n");
    fails += expect_contains("bad_length", out, "Hex00 210100\n");

    return fails;
}

int main(void) {
    int fails = 0;

    fails += run_sample();
    fails += run_bad_length();

    if(fails) return 1;
    printf("All Apator HCA tests passed.\n");
    return 0;
}
