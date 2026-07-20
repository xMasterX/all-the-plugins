#pragma once

#include <stdint.h>
#include "curriculum.h"

#define MODE_COUNT 3

typedef enum {
    TrainModeEncoding = 0,
    TrainModeDecoding = 1,
    TrainModeMixed = 2,
} TrainMode;

#define MORSE_CHAR_COUNT 36 /* A-Z + 0-9 */

/* Field order matters: the v1 file body (unlocked + stars) must stay a
 * byte-exact prefix of v2 so old save files migrate by a short read.
 * Packed so the on-disk layout is the in-memory layout. */
typedef struct __attribute__((packed)) {
    uint8_t unlocked[MODE_COUNT]; /* count of unlocked levels, 1..LEVEL_COUNT */
    uint8_t stars[MODE_COUNT][LEVEL_COUNT]; /* 0..3 */
    /* v2 fields: */
    uint8_t misses[MORSE_CHAR_COUNT]; /* per-char error counts (capped) */
    uint32_t total_answered;
    uint32_t total_correct;
    uint16_t streak; /* consecutive days with a completed level */
    uint32_t last_epoch_day; /* furi_hal_rtc_get_timestamp() / 86400 */
} MorseProgress;

typedef struct __attribute__((packed)) {
    uint8_t sound; /* 0/1 */
    uint8_t vibro; /* 0/1 */
    uint8_t farnsworth; /* 0/1: stretch gaps between letters (v2) */
    uint8_t led; /* 0/1: notification LED blinks (v3) */
    uint8_t visual; /* 0=off, 1=auto (L1-L3 only), 2=always: show ./- patterns (v4) */
} MorseSettings;

/* A-Z -> 0..25, 0-9 -> 26..35, anything else -> -1. */
static inline int morse_char_index(char c) {
    if(c >= 'A' && c <= 'Z') return c - 'A';
    if(c >= '0' && c <= '9') return 26 + (c - '0');
    return -1;
}

/* Loads from SD; any missing/corrupt file yields fresh defaults. */
void morse_progress_load(MorseProgress* progress);
void morse_progress_save(const MorseProgress* progress);

/* Settings live in their own file so a format change in either never
 * invalidates the other. */
void morse_settings_load(MorseSettings* settings);
void morse_settings_save(const MorseSettings* settings);
