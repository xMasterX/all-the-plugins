#include "composer_music.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

const char* const cm_style_names[CM_STYLES] =
    {"Chiptune", "Blues", "Classical", "Folk", "Game", "Phonk", "Hip-Hop", "Jazz"};
const uint16_t cm_style_bpm[CM_STYLES] = {150, 96, 100, 120, 132, 136, 90, 140};

static const char* const cm_note_names[12] =
    {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

int cm_note_midi(uint8_t pitch, uint8_t key) {
    if(pitch == 0 || pitch >= CM_PITCHES) return -1;
    return CM_TONIC_MIDI + (int)(key % 12) + (int)pitch - 1 + CM_PITCH_MIN;
}

float cm_midi_frequency(int midi) {
    return 440.0f * powf(2.0f, (float)(midi - 69) / 12.0f);
}

uint16_t cm_mix_bpm(const float weights[CM_STYLES]) {
    float total = 0.0f, bpm = 0.0f;
    for(int s = 0; s < CM_STYLES; ++s) {
        float w = weights[s] > 0.0f ? weights[s] : 0.0f;
        total += w;
        bpm += w * (float)cm_style_bpm[s];
    }
    return total > 0.0f ? (uint16_t)(bpm / total + 0.5f) : cm_style_bpm[0];
}

void cm_midi_name(int midi, char* out, size_t size) {
    snprintf(out, size, "%s%d", cm_note_names[midi % 12], midi / 12 - 1);
}

/* Sixteenths -> FMF duration and dot: 1 -> 16, 3 -> 8., 12 -> 2. */
static void cm_fmf_duration(uint8_t sixteenths, int* value, int* dotted) {
    switch(sixteenths) {
    case 1:
        *value = 16, *dotted = 0;
        break;
    case 2:
        *value = 8, *dotted = 0;
        break;
    case 3:
        *value = 8, *dotted = 1;
        break;
    case 4:
        *value = 4, *dotted = 0;
        break;
    case 6:
        *value = 4, *dotted = 1;
        break;
    case 8:
        *value = 2, *dotted = 0;
        break;
    case 12:
        *value = 2, *dotted = 1;
        break;
    default:
        *value = 1, *dotted = 0;
        break;
    }
}

size_t cm_write_fmf(
    const CmNote* notes,
    size_t count,
    uint8_t key,
    uint16_t bpm,
    char* out,
    size_t size) {
    int used = snprintf(
        out,
        size,
        "Filetype: Flipper Music Format\nVersion: 0\nBPM: %u\nDuration: 4\nOctave: 5\nNotes: ",
        bpm);
    if(used < 0 || (size_t)used >= size) return 0;
    for(size_t i = 0; i < count; ++i) {
        int value, dotted;
        cm_fmf_duration(cm_duration_sixteenths[notes[i].duration], &value, &dotted);
        int midi = cm_note_midi(notes[i].pitch, key);
        char name[8];
        if(midi < 0) {
            strcpy(name, "P");
        } else {
            cm_midi_name(midi, name, sizeof(name));
        }
        int n = snprintf(
            out + used,
            size - (size_t)used,
            "%s%d%s%s",
            i ? ", " : "",
            value,
            name,
            dotted ? "." : "");
        if(n < 0 || (size_t)(used + n) >= size) return 0;
        used += n;
    }
    if((size_t)used + 2 > size) return 0;
    out[used++] = '\n';
    out[used] = '\0';
    return (size_t)used;
}
