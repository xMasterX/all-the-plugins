#pragma once

#include <furi_hal.h>

// serial link to the ESP32 board; same USART/baud as Marauder's CLI, no rewiring
#define PWNPAL_UART_CHANNEL FuriHalSerialIdUsart
#define PWNPAL_UART_BAUD    115200

// secs between persona re-pushes to the ESP32 so its beacon reflects growing counts
#define PWNPAL_ADV_RESEND_SECS 15

// secs between persona saves to SD
#define PWNPAL_SAVE_SECS 30

// the ESP32 wifi TX silently stalls after tens of minutes (esp_wifi_80211_tx keeps returning OK
// but nothing radiates -- beacons AND attacks go dark while the firmware's counters keep ticking;
// only a full wifi re-init revives it). periodically force the board to stop+restart its scan
// (exactly what a manual app restart does) to pre-empt the stall. well under the observed
// ~30-45min death, and it also keeps captures alive (same dead radio kills those too).
#define PWNPAL_REINIT_SECS 600

// ESP32-absence: board sends PWNPAL_ADV ~2x/sec; silence for LINK_TIMEOUT after
// LINK_GRACE (covers boot + StartScan) warns the user
// the ESP goes quiet for several seconds while attackChannel() hammers a dense channel
// (assoc+deauth over 20-30 APs) — that's a BUSY board, not a dead one. keep the timeout well
// above the worst-case attack-phase gap so we only cry "no ESP32" on a real disconnect.
#define PWNPAL_LINK_TIMEOUT_SECS 15
#define PWNPAL_LINK_GRACE_SECS   10

// setup link QR'd on the "no ESP32" screen; <=53 bytes so the QR stays version-3 (29x29) at 2px/module
#define PWNPAL_SETUP_URL "https://github.com/mrq1911/pwnpal"

// serial-proto version this app expects (mirrors firmware PWNPAL_PROTO). firmware stamps
// ver=N on each PWNPAL_ADV; non-zero but below this -> board too old, warn the user
#define PWNPAL_FW_PROTO 6

// PWNPAL_EXPECTED_FW_COMMIT (the firmware build this app expects) is generated into version.h by
// gen_version.sh as the last commit touching pwnpal-marauder/ — the same value apply_pwnpal.py
// stamps into the firmware's fw=. board reports a different hash on PWNPAL_ADV -> recommend a
// reflash. it only changes when firmware sources change, so app-only releases never nag.

// FLIPPER_SCREEN_WIDTH / _HEIGHT come from pwn_constants.h (via pwnagotchi.h).
