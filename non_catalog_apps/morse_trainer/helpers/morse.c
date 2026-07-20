#include "morse.h"
#include <furi_hal_speaker.h>

/* Code table lives in flash (static const), not RAM. */
static const char* const morse_letters[26] = {
    ".-",   "-...", "-.-.", "-..",  ".",   "..-.", "--.",  "....", "..",
    ".---", "-.-",  ".-..", "--",   "-.",  "---",  ".--.", "--.-", ".-.",
    "...",  "-",    "..-",  "...-", ".--", "-..-", "-.--", "--..",
};

static const char* const morse_digits[10] = {
    "-----",
    ".----",
    "..---",
    "...--",
    "....-",
    ".....",
    "-....",
    "--...",
    "---..",
    "----.",
};

const char* morse_code_for(char c) {
    if(c >= 'a' && c <= 'z') c -= 32;
    if(c >= 'A' && c <= 'Z') return morse_letters[c - 'A'];
    if(c >= '0' && c <= '9') return morse_digits[c - '0'];
    return NULL;
}

char morse_char_for(const char* pattern) {
    for(uint8_t i = 0; i < 26; i++) {
        if(strcmp(morse_letters[i], pattern) == 0) return 'A' + i;
    }
    for(uint8_t i = 0; i < 10; i++) {
        if(strcmp(morse_digits[i], pattern) == 0) return '0' + i;
    }
    return 0;
}

typedef enum {
    PlayerMsgText,
    PlayerMsgTone,
    PlayerMsgQuit,
} PlayerMsgType;

typedef struct {
    PlayerMsgType type;
    char text[14];
    float freq;
    uint16_t ms;
} PlayerMsg;

struct MorsePlayer {
    FuriThread* thread;
    FuriMessageQueue* queue;
    NotificationApp* notifications;
    const MorseSettings* settings;
    volatile uint16_t dot_ms;
    volatile bool aborted;
};

static void player_element(MorsePlayer* player, bool have_speaker, uint32_t duration_ms) {
    bool vibro = player->settings->vibro;
    if(have_speaker) furi_hal_speaker_start(MORSE_TONE_HZ, 1.0f);
    if(vibro) notification_message(player->notifications, &sequence_set_vibro_on);
    furi_delay_ms(duration_ms);
    if(have_speaker) furi_hal_speaker_stop();
    if(vibro) notification_message(player->notifications, &sequence_reset_vibro);
}

static int32_t player_worker(void* context) {
    MorsePlayer* player = context;
    PlayerMsg msg;
    for(;;) {
        furi_message_queue_get(player->queue, &msg, FuriWaitForever);
        if(msg.type == PlayerMsgQuit) break;
        player->aborted = false;
        /* Acquire once per message, release before going idle: holding the
         * speaker while idle would starve system sounds. */
        bool have_speaker = player->settings->sound && furi_hal_speaker_acquire(500);
        if(msg.type == PlayerMsgTone) {
            if(have_speaker) {
                furi_hal_speaker_start(msg.freq, 1.0f);
                furi_delay_ms(msg.ms);
                furi_hal_speaker_stop();
            } else {
                furi_delay_ms(msg.ms);
            }
        } else {
            /* Farnsworth: elements stay at character speed, but the gaps
             * BETWEEN letters/words stretch 3x — the standard way to give
             * beginners thinking time without slowing letter recognition. */
            uint32_t dot = player->dot_ms;
            uint32_t gap_scale = player->settings->farnsworth ? 3 : 1;
            for(const char* c = msg.text; *c && !player->aborted; c++) {
                if(*c == ' ') {
                    furi_delay_ms(dot * 7 * gap_scale);
                    continue;
                }
                const char* pattern = morse_code_for(*c);
                if(!pattern) continue;
                for(const char* s = pattern; *s && !player->aborted; s++) {
                    player_element(player, have_speaker, (*s == '.') ? dot : dot * 3);
                    if(s[1]) furi_delay_ms(dot);
                }
                if(c[1] && !player->aborted) furi_delay_ms(dot * 3 * gap_scale);
            }
        }
        if(have_speaker) furi_hal_speaker_release();
    }
    return 0;
}

MorsePlayer* morse_player_alloc(NotificationApp* notifications, const MorseSettings* settings) {
    MorsePlayer* player = malloc(sizeof(MorsePlayer));
    player->notifications = notifications;
    player->settings = settings;
    player->dot_ms = MORSE_DOT_MS;
    player->aborted = false;
    player->queue = furi_message_queue_alloc(2, sizeof(PlayerMsg));
    player->thread = furi_thread_alloc_ex("MorsePlayer", 1024, player_worker, player);
    furi_thread_start(player->thread);
    return player;
}

void morse_player_free(MorsePlayer* player) {
    player->aborted = true;
    PlayerMsg msg = {.type = PlayerMsgQuit};
    furi_message_queue_put(player->queue, &msg, FuriWaitForever);
    furi_thread_join(player->thread);
    furi_thread_free(player->thread);
    furi_message_queue_free(player->queue);
    free(player);
}

static void player_submit(MorsePlayer* player, const PlayerMsg* msg) {
    /* Interrupt whatever is playing and drop stale queued requests so the
     * new sound starts promptly. */
    player->aborted = true;
    furi_message_queue_reset(player->queue);
    furi_message_queue_put(player->queue, msg, 0);
}

void morse_player_play_text(MorsePlayer* player, const char* text) {
    PlayerMsg msg = {.type = PlayerMsgText};
    strlcpy(msg.text, text, sizeof(msg.text));
    player_submit(player, &msg);
}

void morse_player_play_tone(MorsePlayer* player, float freq, uint16_t ms) {
    PlayerMsg msg = {.type = PlayerMsgTone, .freq = freq, .ms = ms};
    player_submit(player, &msg);
}

void morse_player_stop(MorsePlayer* player) {
    player->aborted = true;
    furi_message_queue_reset(player->queue);
}

void morse_player_set_dot_ms(MorsePlayer* player, uint16_t dot_ms) {
    player->dot_ms = dot_ms;
}
