#pragma once

#include <stdbool.h>
#include <stdint.h>

/* On-device inference for the tiny melody model (portable C, no furi).
 * The network writes one note at a time: a GRU step, then the pitch head,
 * then the duration head conditioned on the chosen pitch. Weights are int8
 * with one float scale per row (see composer_weights.c, made by export.py). */

#define CM_STYLES    8
#define CM_E         32
#define CM_H         80
#define CM_EP        16
#define CM_PITCHES   26 /* 0 = rest, 1..25 = -5..+19 semitones from the tonic */
#define CM_DURATIONS 8
#define CM_BAR       16 /* sixteenths per bar */
#define CM_BARS      8
#define CM_PITCH_MIN (-5)
#define CM_TOTAL     (CM_BAR * CM_BARS)

extern const uint8_t cm_duration_sixteenths[CM_DURATIONS];

typedef struct {
    float h[CM_H];
    float style[CM_E];
    uint8_t prev_pitch; /* token indices; CM_PITCHES / CM_DURATIONS = start token */
    uint8_t prev_dur;
    uint8_t pos; /* sixteenths written so far */
} ComposerState;

/* weights[i] mixes style i (0 chiptune, 1 blues, 2 classical, 3 folk, 4 game,
 * 5 phonk, 6 hip-hop, 7 jazz);
 * they are normalised internally. */
void composer_reset(ComposerState* state, const float weights[CM_STYLES]);

/* Writes the next note. temperature > 0: lower = safer, higher = wilder.
 * rand01 returns uniform numbers in [0, 1). Returns false once 8 bars are full. */
bool composer_next(
    ComposerState* state,
    float temperature,
    float (*rand01)(void* ctx),
    void* rand_ctx,
    uint8_t* pitch,
    uint8_t* duration);

/* Exposed for tests: logits of the last step (after composer_next). */
extern float composer_last_pitch_logits[CM_PITCHES];
extern float composer_last_dur_logits[CM_DURATIONS];

/* Deterministic xorshift32 used by tests and the app. */
float composer_xorshift01(void* ctx);
