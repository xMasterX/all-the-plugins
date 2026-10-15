#pragma once

#include <storage/storage.h>
#include <stdint.h>
#include <stdbool.h>

// WiGLE-importable wardrive log; one CSV, a row per geotagged AP/PWND, beside the persona
#define PWNPAL_WARDRIVE_DIR "/ext/apps_data/pwnpal"
#define PWNPAL_WARDRIVE_PATH PWNPAL_WARDRIVE_DIR "/wardrive.csv"

// append one WiGLE-1.4 row. lat/lon are verbatim decimal-degree strings from the firmware
// JSON, never parsed to float (Flipper printf has %f disabled), so must be non-empty.
// pre-header + column header written first on an empty file. false on error.
bool wardrive_log(
    Storage* storage,
    const char* mac, // "aa:bb:cc:dd:ee:ff"
    const char* ssid, // may be empty
    const char* auth, // AuthMode capabilities string
    int channel,
    int rssi,
    const char* lat,
    const char* lon);
