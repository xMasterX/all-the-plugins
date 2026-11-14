#include "composer_model.h"

#include "composer_weights.h"

#include <math.h>
#include <string.h>

const uint8_t cm_duration_sixteenths[CM_DURATIONS] = {1, 2, 3, 4, 6, 8, 12, 16};

float composer_last_pitch_logits[CM_PITCHES];

/* Scratch buffers live here instead of on the caller's stack (about 2.4 KB);
 * composing runs on one thread only. */
static float cm_gi[3 * CM_H];
static float cm_gh[3 * CM_H];
static float cm_hd[CM_H + CM_EP];
float composer_last_dur_logits[CM_DURATIONS];

/* out[r] = bias[r] + scale[r] * sum_c w[r][c] * x[c] for an int8 row-major matrix. */
static void cm_matvec(
    const int8_t* w,
    const float* scale,
    const float* bias,
    const float* x,
    int rows,
    int cols,
    float* out) {
    for(int r = 0; r < rows; ++r) {
        const int8_t* row = w + r * cols;
        float acc = 0.0f;
        for(int c = 0; c < cols; ++c)
            acc += (float)row[c] * x[c];
        out[r] = acc * scale[r] + (bias ? bias[r] : 0.0f);
    }
}

static void cm_add_embedding(float* x, const int8_t* table, const float* scale, int row, int dim) {
    const int8_t* v = table + row * dim;
    for(int i = 0; i < dim; ++i)
        x[i] += (float)v[i] * scale[row];
}

static float cm_sigmoid(float v) {
    return 1.0f / (1.0f + expf(-v));
}

/* Samples from softmax(logits / temperature) over entries where allowed[i]. */
static int cm_sample(
    const float* logits,
    const bool* allowed,
    int n,
    float temperature,
    float (*rand01)(void*),
    void* ctx) {
    float max = -1e30f;
    for(int i = 0; i < n; ++i)
        if((!allowed || allowed[i]) && logits[i] > max) max = logits[i];
    float probs[CM_PITCHES];
    float sum = 0.0f;
    for(int i = 0; i < n; ++i) {
        probs[i] = (!allowed || allowed[i]) ? expf((logits[i] - max) / temperature) : 0.0f;
        sum += probs[i];
    }
    float r = rand01(ctx) * sum;
    int last = 0;
    for(int i = 0; i < n; ++i) {
        if(probs[i] <= 0.0f) continue;
        last = i;
        r -= probs[i];
        if(r < 0.0f) return i;
    }
    return last;
}

void composer_reset(ComposerState* state, const float weights[CM_STYLES]) {
    memset(state, 0, sizeof(*state));
    float total = 0.0f;
    for(int s = 0; s < CM_STYLES; ++s)
        total += weights[s] > 0.0f ? weights[s] : 0.0f;
    if(total <= 0.0f) total = 1.0f;
    for(int s = 0; s < CM_STYLES; ++s) {
        float w = (weights[s] > 0.0f ? weights[s] : 0.0f) / total;
        const int8_t* v = cm_emb_style + s * CM_E;
        for(int i = 0; i < CM_E; ++i)
            state->style[i] += w * (float)v[i] * cm_emb_style_scale[s];
    }
    state->prev_pitch = CM_PITCHES;
    state->prev_dur = CM_DURATIONS;
}

bool composer_next(
    ComposerState* state,
    float temperature,
    float (*rand01)(void* ctx),
    void* rand_ctx,
    uint8_t* pitch,
    uint8_t* duration) {
    if(state->pos >= CM_TOTAL) return false;
    if(temperature < 0.05f) temperature = 0.05f;
    const int beat = state->pos % CM_BAR;
    const int bar = state->pos / CM_BAR;

    /* Input: previous note + position + style. */
    float x[CM_E];
    memcpy(x, state->style, sizeof(x));
    cm_add_embedding(x, cm_emb_pitch, cm_emb_pitch_scale, state->prev_pitch, CM_E);
    cm_add_embedding(x, cm_emb_dur, cm_emb_dur_scale, state->prev_dur, CM_E);
    cm_add_embedding(x, cm_emb_beat, cm_emb_beat_scale, beat, CM_E);
    cm_add_embedding(x, cm_emb_bar, cm_emb_bar_scale, bar, CM_E);

    /* GRU cell, PyTorch gate order r, z, n:
     * r = s(Wir x + bir + Whr h + bhr), z = s(...), n = tanh(Win x + bin + r * (Whn h + bhn)),
     * h' = (1 - z) * n + z * h */
    cm_matvec(cm_gru_w_ih, cm_gru_w_ih_scale, cm_gru_b_ih, x, 3 * CM_H, CM_E, cm_gi);
    cm_matvec(cm_gru_w_hh, cm_gru_w_hh_scale, cm_gru_b_hh, state->h, 3 * CM_H, CM_H, cm_gh);
    for(int i = 0; i < CM_H; ++i) {
        float r = cm_sigmoid(cm_gi[i] + cm_gh[i]);
        float z = cm_sigmoid(cm_gi[CM_H + i] + cm_gh[CM_H + i]);
        float n = tanhf(cm_gi[2 * CM_H + i] + r * cm_gh[2 * CM_H + i]);
        state->h[i] = (1.0f - z) * n + z * state->h[i];
    }

    /* Pitch, then a duration that fits into the bar. */
    cm_matvec(
        cm_head_pitch_w,
        cm_head_pitch_w_scale,
        cm_head_pitch_b,
        state->h,
        CM_PITCHES,
        CM_H,
        composer_last_pitch_logits);
    const int p =
        cm_sample(composer_last_pitch_logits, NULL, CM_PITCHES, temperature, rand01, rand_ctx);

    memcpy(cm_hd, state->h, sizeof(state->h));
    for(int i = 0; i < CM_EP; ++i)
        cm_hd[CM_H + i] = (float)cm_emb_pitch2[p * CM_EP + i] * cm_emb_pitch2_scale[p];
    cm_matvec(
        cm_head_dur_w,
        cm_head_dur_w_scale,
        cm_head_dur_b,
        cm_hd,
        CM_DURATIONS,
        CM_H + CM_EP,
        composer_last_dur_logits);
    bool fits[CM_DURATIONS];
    for(int d = 0; d < CM_DURATIONS; ++d)
        fits[d] = beat + cm_duration_sixteenths[d] <= CM_BAR;
    /* Rhythm stays a little calmer than the melody at high creativity. */
    const float dur_temperature = temperature > 1.0f ? 1.0f + (temperature - 1.0f) * 0.5f :
                                                       temperature;
    const int d =
        cm_sample(composer_last_dur_logits, fits, CM_DURATIONS, dur_temperature, rand01, rand_ctx);

    /* The last note always resolves to the tonic nearest the previous note
     * (index 6 = tonic, 18 = tonic an octave up). */
    int out_pitch = p;
    if(state->pos + cm_duration_sixteenths[d] >= CM_TOTAL) {
        out_pitch = state->prev_pitch >= 12 && state->prev_pitch < CM_PITCHES ? 18 : 6;
    }

    state->prev_pitch = (uint8_t)out_pitch;
    state->prev_dur = (uint8_t)d;
    state->pos += cm_duration_sixteenths[d];
    *pitch = (uint8_t)out_pitch;
    *duration = (uint8_t)d;
    return true;
}

float composer_xorshift01(void* ctx) {
    uint32_t* s = ctx;
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return (float)(x >> 8) * (1.0f / 16777216.0f);
}
