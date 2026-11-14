#pragma once

#include <stddef.h>
#include <stdint.h>

#include "composer_model.h"

/* Music helpers shared by the app and host tests (portable C). */

#define CM_MAX_NOTES  CM_TOTAL /* at most one note per sixteenth */
#define CM_TONIC_MIDI 72 /* C5: the Flipper speaker sounds best from here up */

typedef struct {
    uint8_t pitch; /* model index: 0 rest, 1..25 */
    uint8_t duration; /* index into cm_duration_sixteenths */
} CmNote;

extern const char* const cm_style_names[CM_STYLES];
extern const uint16_t cm_style_bpm[CM_STYLES];

/* MIDI number of a model pitch index in the given key (0 = C ... 11 = B); -1 for rests. */
int cm_note_midi(uint8_t pitch, uint8_t key);
float cm_midi_frequency(int midi);
/* Tempo for a style mix (weights need not be normalised). */
uint16_t cm_mix_bpm(const float weights[CM_STYLES]);
/* Note name like "C#5" for a MIDI number. */
void cm_midi_name(int midi, char* out, size_t size);

/* Writes the melody as a Flipper Music Format file body. Returns the length
 * written, or 0 if it did not fit into out. */
size_t cm_write_fmf(
    const CmNote* notes,
    size_t count,
    uint8_t key,
    uint16_t bpm,
    char* out,
    size_t size);
