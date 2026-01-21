#include <furi.h>
#include <furi_hal.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <stdlib.h>
#include <string.h>

#include "lib/flipper.h"

#define COLOUR_WHITE 0
#define COLOUR_BLACK 1

#include "game/Game.h"
#include "game/Draw.h"
#include "game/FixedMath.h"
#include "game/Platform.h"
#include "game/Defines.h"
#include "game/Sounds.h"
#include "lib/EEPROM.h"

// ---------------- SOUND ----------------

typedef struct {
    const uint16_t* pattern;
} SoundRequest;

static FuriMessageQueue* g_sound_queue = NULL;
static FuriThread* g_sound_thread = NULL;
static volatile bool g_sound_thread_running = false;
static const float kSoundVolume = 1.0f;
static const uint32_t kToneTickHz = 780;

static inline uint32_t arduboy_ticks_to_ms(uint16_t ticks) {
    return (uint32_t)((ticks * 1000u + (kToneTickHz / 2)) / kToneTickHz);
}

static int32_t sound_thread_fn(void* ctx) {
    UNUSED(ctx);
    SoundRequest req;

    while(g_sound_thread_running) {
        if(furi_message_queue_get(g_sound_queue, &req, 50) != FuriStatusOk) continue;
        if(!g_state || !g_state->audio_enabled || !req.pattern) continue;
        if(!furi_hal_speaker_acquire(50)) continue;

        const uint16_t* p = req.pattern;
        while(g_sound_thread_running && g_state && g_state->audio_enabled) {
            SoundRequest new_req;
            if(furi_message_queue_get(g_sound_queue, &new_req, 0) == FuriStatusOk) {
                if(new_req.pattern) p = new_req.pattern;
            }

            uint16_t freq = *p++;
            if(freq == TONES_END) break;

            uint16_t dur_ticks = *p++;
            uint32_t dur_ms = arduboy_ticks_to_ms(dur_ticks);
            if(dur_ms == 0) dur_ms = 1;

            if(freq == 0) {
                furi_hal_speaker_stop();
                furi_delay_ms(dur_ms);
            } else {
                furi_hal_speaker_start((float)freq, kSoundVolume);
                furi_delay_ms(dur_ms);
                furi_hal_speaker_stop();
            }
        }

        furi_hal_speaker_stop();
        furi_hal_speaker_release();
    }

    if(furi_hal_speaker_is_mine()) {
        furi_hal_speaker_stop();
        furi_hal_speaker_release();
    }
    return 0;
}

static void sound_system_init() {
    if(g_sound_queue || g_sound_thread) return;

    g_sound_queue = furi_message_queue_alloc(4, sizeof(SoundRequest));

    g_sound_thread = furi_thread_alloc();
    furi_thread_set_name(g_sound_thread, "GameSound");
    furi_thread_set_stack_size(g_sound_thread, 1024);
    furi_thread_set_priority(g_sound_thread, FuriThreadPriorityNormal);
    furi_thread_set_callback(g_sound_thread, sound_thread_fn);

    g_sound_thread_running = true;
    furi_thread_start(g_sound_thread);
}

static void sound_system_deinit() {
    if(!g_sound_thread) return;

    g_sound_thread_running = false;
    furi_thread_join(g_sound_thread);
    furi_thread_free(g_sound_thread);
    g_sound_thread = NULL;

    if(g_sound_queue) {
        furi_message_queue_free(g_sound_queue);
        g_sound_queue = NULL;
    }
}

void Platform::PlaySound(const uint16_t* audioPattern) {
    if(!g_state || !g_state->audio_enabled || !audioPattern || !g_sound_queue) return;

    SoundRequest req = {.pattern = audioPattern};
    if(furi_message_queue_put(g_sound_queue, &req, 0) != FuriStatusOk) {
        SoundRequest dummy;
        (void)furi_message_queue_get(g_sound_queue, &dummy, 0);
        (void)furi_message_queue_put(g_sound_queue, &req, 0);
    }
}

bool Platform::IsAudioEnabled() {
    return g_state && g_state->audio_enabled;
}

void Platform::SetAudioEnabled(bool enabled) {
    if(!g_state) return;
    bool was_enabled = g_state->audio_enabled;
    g_state->audio_enabled = enabled;
    if(enabled && !was_enabled)
        sound_system_init();
    else if(!enabled && was_enabled)
        sound_system_deinit();
}

// ---------------- INPUT ----------------

uint8_t Platform::GetInput() {
    return g_state ? g_state->input_state : 0;
}

// ---------------- DRAW ----------------

static inline void set_pixel(int16_t x, int16_t y, bool color) {
    if(!g_state) return;
    if(x < 0 || y < 0 || x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT) return;

    uint8_t* buf = g_state->back_buffer;
    uint16_t idx = (uint16_t)(x + (y >> 3) * DISPLAY_WIDTH);
    uint8_t mask = (uint8_t)(1u << (y & 7));

    if(color)
        buf[idx] |= mask;
    else
        buf[idx] &= (uint8_t)~mask;
}

void Platform::PutPixel(uint8_t x, uint8_t y, uint8_t color) {
    set_pixel(x, y, color);
}

void Platform::FillScreen(uint8_t color) {
    if(!g_state) return;
    memset(g_state->back_buffer, color ? 0xFF : 0x00, BUFFER_SIZE);
}

uint8_t* Platform::GetScreenBuffer() {
    return g_state ? g_state->back_buffer : NULL;
}

void Platform::DrawVLine(uint8_t x, int8_t y0, int8_t y1, uint8_t pattern) {
    if(y0 > y1) {
        int8_t t = y0;
        y0 = y1;
        y1 = t;
    }
    for(int y = y0; y <= y1; y++) {
        if(pattern & (1 << (y & 7))) set_pixel(x, y, 1);
    }
}

void Platform::DrawBitmap(int16_t x, int16_t y, const uint8_t* bmp) {
    if(!bmp) return;
    uint8_t w = bmp[0], h = bmp[1];
    const uint8_t* data = bmp + 2;
    uint8_t pages = (h + 7) >> 3;

    for(uint8_t page = 0; page < pages; page++) {
        for(uint8_t i = 0; i < w; i++) {
            uint8_t byte = data[i + page * w];
            for(uint8_t b = 0; b < 8; b++) {
                uint8_t py = (uint8_t)(page * 8 + b);
                if(py >= h) break;
                if(byte & (1 << b)) set_pixel(x + i, y + py, COLOUR_WHITE);
            }
        }
    }
}

void Platform::DrawSolidBitmap(int16_t x, int16_t y, const uint8_t* bmp) {
    if(!bmp) return;
    uint8_t w = bmp[0], h = bmp[1];
    const uint8_t* data = bmp + 2;
    uint8_t pages = (h + 7) >> 3;

    for(uint8_t page = 0; page < pages; page++) {
        for(uint8_t i = 0; i < w; i++) {
            uint8_t byte = data[i + page * w];
            for(uint8_t b = 0; b < 8; b++) {
                uint8_t py = (uint8_t)(page * 8 + b);
                if(py >= h) break;
                bool pixel = (byte & (1 << b)) != 0;
                set_pixel(x + i, y + py, pixel ? COLOUR_WHITE : COLOUR_BLACK);
            }
        }
    }
}

void Platform::DrawSprite(int16_t x, int16_t y, const uint8_t* bmp, uint8_t frame) {
    if(!bmp) return;

    uint8_t w = bmp[0];
    uint8_t h = bmp[1];
    uint8_t pages = (h + 7) >> 3;
    uint16_t frame_size = (uint16_t)(w * pages);

    const uint8_t* data = bmp + 2 + (uint32_t)frame * frame_size * 2;

    for(uint8_t page = 0; page < pages; page++) {
        for(uint8_t i = 0; i < w; i++) {
            uint16_t idx = (uint16_t)((i + page * w) * 2);
            uint8_t s = data[idx + 0];
            uint8_t m = data[idx + 1];

            for(uint8_t b = 0; b < 8; b++) {
                uint8_t py = (uint8_t)(page * 8 + b);
                if(py >= h) break;

                uint8_t bit = (uint8_t)(1u << b);
                if(m & bit) {
                    set_pixel(x + i, y + py, (s & bit) ? COLOUR_BLACK : COLOUR_WHITE);
                }
            }
        }
    }
}
