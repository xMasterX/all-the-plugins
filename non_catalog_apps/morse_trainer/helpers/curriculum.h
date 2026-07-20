#pragma once

#include <stdint.h>
#include <stdbool.h>

#define LEVEL_COUNT     13
#define CHALLENGE_WORDS 3
#define MAX_WORD_LEN    6
#define MAX_LEARNED     37 /* 26 letters + 10 digits + NUL */

typedef struct {
    const char* label; /* shown in level select, e.g. "E T" */
    const char* new_chars; /* letters introduced; "" for challenge levels */
    const char* const* word_pool; /* challenge vocabulary; NULL for regular levels */
    uint8_t word_count; /* pool size; CHALLENGE_WORDS are drawn per run */
} MorseLevel;

const MorseLevel* curriculum_get(uint8_t level_index);
bool curriculum_is_challenge(uint8_t level_index);

/* Fills buf with every char learned in levels 0..level_index inclusive. */
void curriculum_learned_upto(uint8_t level_index, char* buf, uint8_t buf_size);
