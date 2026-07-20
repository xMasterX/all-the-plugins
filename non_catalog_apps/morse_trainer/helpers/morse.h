#pragma once

#include <furi.h>
#include <notification/notification_messages.h>
#include "progress.h"

/* Timing at ~15 WPM. All other element durations derive from the dot. */
#define MORSE_DOT_MS  80
#define MORSE_DASH_MS (MORSE_DOT_MS * 3)
#define MORSE_GAP_MS  MORSE_DOT_MS /* intra-letter */
#define MORSE_LGAP_MS (MORSE_DOT_MS * 3) /* between letters */
#define MORSE_WGAP_MS (MORSE_DOT_MS * 7) /* between words */
#define MORSE_TONE_HZ 600.0f
#define MORSE_GOOD_HZ 1400.0f
#define MORSE_BAD_HZ  200.0f

/* Returns ".-"-style pattern for A-Z / 0-9, or NULL. */
const char* morse_code_for(char c);

/* Reverse lookup: returns the character for a ".-" pattern, or 0. */
char morse_char_for(const char* pattern);

/* Async player: a worker thread owns the speaker during playback so the
 * GUI thread never blocks on audio. */
typedef struct MorsePlayer MorsePlayer;

/* `settings` is read live at playback time, so toggling sound/vibro takes
 * effect immediately without touching the player. */
MorsePlayer* morse_player_alloc(NotificationApp* notifications, const MorseSettings* settings);
void morse_player_free(MorsePlayer* player);

/* Interrupts anything currently playing, then plays `text` (letters/digits,
 * spaces = word gap) as beep + vibro. */
void morse_player_play_text(MorsePlayer* player, const char* text);

/* Interrupts current playback, plays a single feedback tone. */
void morse_player_play_tone(MorsePlayer* player, float freq, uint16_t ms);

/* Abort current playback. */
void morse_player_stop(MorsePlayer* player);

/* Character speed for subsequent playback (speed ramp on later levels).
 * Element durations all derive from this dot length. */
void morse_player_set_dot_ms(MorsePlayer* player, uint16_t dot_ms);
