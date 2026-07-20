#include "curriculum.h"
#include <furi.h>
#include <string.h>

/* Challenge word pools are code-time verified to use only letters learned in
 * earlier levels. CHALLENGE_WORDS of them are drawn at random each run, so
 * replays see different words. Tables are static const: flash, not RAM.
 *  L3  letters ETAINM
 *  L6  + SORU DKGW
 *  L9  + HVFL BCJP
 *  L13 everything incl. digits */
static const char* const l3_words[] =
    {"TEA", "MAN", "MINE", "TIN", "MAT", "NET", "AIM", "ANT", "TAN", "TIE"};
static const char* const l6_words[] =
    {"WORDS", "GAME", "DRUM", "SNOW", "ROAD", "WIND", "STAR", "DOOR", "MOON", "SOUND"};
static const char* const l9_words[] =
    {"FLASH", "PHONE", "JUICE", "BEACH", "PLANE", "HOUSE", "CLOUD", "BLACK", "FISH", "JUMP"};
static const char* const l13_words[] =
    {"QUIZ", "TA1BM", "CQ4YOU", "XRAY", "ZERO", "JAZZ", "PIZZA", "QRZ73"};

#define POOL(p) p, (uint8_t)(sizeof(p) / sizeof(p[0]))

static const MorseLevel levels[LEVEL_COUNT] = {
    {"E T", "ET", NULL, 0},
    {"A I N M", "AINM", NULL, 0},
    {"Challenge!", "", POOL(l3_words)},
    {"S O R U", "SORU", NULL, 0},
    {"D K G W", "DKGW", NULL, 0},
    {"Challenge!", "", POOL(l6_words)},
    {"H V F L", "HVFL", NULL, 0},
    {"B C J P", "BCJP", NULL, 0},
    {"Challenge!", "", POOL(l9_words)},
    {"Q X Y Z", "QXYZ", NULL, 0},
    {"0 1 2 3 4", "01234", NULL, 0},
    {"5 6 7 8 9", "56789", NULL, 0},
    {"FINAL", "", POOL(l13_words)},
};

const MorseLevel* curriculum_get(uint8_t level_index) {
    furi_assert(level_index < LEVEL_COUNT);
    return &levels[level_index];
}

bool curriculum_is_challenge(uint8_t level_index) {
    return levels[level_index].word_pool != NULL;
}

void curriculum_learned_upto(uint8_t level_index, char* buf, uint8_t buf_size) {
    furi_assert(buf_size >= MAX_LEARNED);
    buf[0] = '\0';
    for(uint8_t i = 0; i <= level_index && i < LEVEL_COUNT; i++) {
        strlcat(buf, levels[i].new_chars, buf_size);
    }
}
