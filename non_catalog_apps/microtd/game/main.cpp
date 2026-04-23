#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../lib/Arduboy2.h"
#include "../lib/Arduino.h"
#include "../lib/EEPROM.h"

#define DISPLAY_WIDTH  128
#define DISPLAY_HEIGHT 64
#define BUFFER_SIZE    (DISPLAY_WIDTH * DISPLAY_HEIGHT / 8)

typedef struct {
    uint8_t screen_buffer[BUFFER_SIZE];
    uint8_t front_buffer[BUFFER_SIZE];

    Gui* gui;
    Canvas* canvas;
    FuriMutex* fb_mutex;
    FuriMutex* game_mutex;
    FuriPubSub* input_events;
    FuriPubSubSubscription* input_sub;

    volatile uint8_t input_state;
    volatile bool exit_requested;
    volatile bool back_long_requested;
    volatile bool back_long_suppressed;
    volatile uint32_t input_cb_inflight;
} FlipperState;

extern void setup();
extern void loop();
extern bool microtd_handle_back_long();
extern Arduboy2 arduboy;

static FlipperState* g_state = NULL;

static void wait_input_callbacks_idle(FlipperState* state) {
    if(!state) return;
    while(__atomic_load_n((uint32_t*)&state->input_cb_inflight, __ATOMIC_ACQUIRE) != 0) {
        furi_delay_ms(1);
    }
}

static void framebuffer_commit_callback(
    uint8_t* data,
    size_t size,
    CanvasOrientation orientation,
    void* context) {
    FlipperState* state = (FlipperState*)context;
    if(!state || !data || size < BUFFER_SIZE) return;
    (void)orientation;

    if(furi_mutex_acquire(state->fb_mutex, 0) != FuriStatusOk) return;
    const uint8_t* src = state->front_buffer;
    for(size_t i = 0; i < BUFFER_SIZE; i++) {
        data[i] = (uint8_t)(src[i] ^ 0xFF);
    }
    furi_mutex_release(state->fb_mutex);
}

static void input_events_callback(const void* value, void* ctx) {
    if(!value || !ctx) return;

    FlipperState* state = (FlipperState*)ctx;
    const InputEvent* event = (const InputEvent*)value;
    Arduboy2Base::InputContext* input_ctx = arduboy.inputContext();

    (void)__atomic_fetch_add((uint32_t*)&state->input_cb_inflight, 1, __ATOMIC_RELAXED);

    if(event->key == InputKeyBack) {
        if(state->back_long_suppressed) {
            if(event->type == InputTypeRelease) {
                state->back_long_suppressed = false;
            }
            arduboy.clearInputMask(INPUT_B);
            goto exit_callback;
        }

        if(event->type == InputTypeLong) {
            state->back_long_requested = true;
            state->back_long_suppressed = true;
            arduboy.clearInputMask(INPUT_B);
            goto exit_callback;
        }
    }

    Arduboy2Base::FlipperInputCallback(event, input_ctx);

exit_callback:
    (void)__atomic_fetch_sub((uint32_t*)&state->input_cb_inflight, 1, __ATOMIC_RELAXED);
}

extern "C" int32_t arduboy_app(void* p) {
    UNUSED(p);

    bool fb_callback_added = false;
    int32_t rc = -1;

    g_state = (FlipperState*)malloc(sizeof(FlipperState));
    if(!g_state) return -1;
    memset(g_state, 0, sizeof(FlipperState));

    do {
        g_state->game_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
        g_state->fb_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
        if(!g_state->game_mutex || !g_state->fb_mutex) break;

        g_state->exit_requested = false;
        g_state->back_long_requested = false;
        g_state->back_long_suppressed = false;
        g_state->input_cb_inflight = 0;
        g_state->input_state = 0;
        memset(g_state->screen_buffer, 0x00, BUFFER_SIZE);
        memset(g_state->front_buffer, 0x00, BUFFER_SIZE);

        arduboy.begin(
            g_state->screen_buffer,
            &g_state->input_state,
            g_state->game_mutex,
            &g_state->exit_requested);
        Sprites::setArduboy(&arduboy);

        g_state->gui = (Gui*)furi_record_open(RECORD_GUI);
        if(!g_state->gui) break;

        gui_add_framebuffer_callback(g_state->gui, framebuffer_commit_callback, g_state);
        fb_callback_added = true;

        g_state->canvas = gui_direct_draw_acquire(g_state->gui);
        if(!g_state->canvas) break;

        g_state->input_events = (FuriPubSub*)furi_record_open(RECORD_INPUT_EVENTS);
        if(!g_state->input_events) break;

        g_state->input_sub =
            furi_pubsub_subscribe(g_state->input_events, input_events_callback, g_state);
        if(!g_state->input_sub) break;

        if(furi_mutex_acquire(g_state->game_mutex, FuriWaitForever) != FuriStatusOk) break;
        arduboy.audio.begin();
        EEPROM.begin();
        setup();

        if(furi_mutex_acquire(g_state->fb_mutex, FuriWaitForever) == FuriStatusOk) {
            memcpy(g_state->front_buffer, g_state->screen_buffer, BUFFER_SIZE);
            furi_mutex_release(g_state->fb_mutex);
        }
        furi_mutex_release(g_state->game_mutex);

        if(g_state->canvas) canvas_commit(g_state->canvas);

        while(!g_state->exit_requested) {
            if(furi_mutex_acquire(g_state->game_mutex, 0) == FuriStatusOk) {
                if(g_state->back_long_requested) {
                    g_state->back_long_requested = false;
                    const bool should_exit = microtd_handle_back_long();
                    arduboy.resetInputState();

                    if(should_exit) {
                        g_state->exit_requested = true;
                    }
                }

                const uint32_t frame_before = arduboy.frameCount;
                if(!g_state->exit_requested) {
                    loop();
                }
                const uint32_t frame_after = arduboy.frameCount;

                if(frame_after != frame_before) {
                    if(furi_mutex_acquire(g_state->fb_mutex, 0) == FuriStatusOk) {
                        memcpy(g_state->front_buffer, g_state->screen_buffer, BUFFER_SIZE);
                        furi_mutex_release(g_state->fb_mutex);
                    }
                }

                furi_mutex_release(g_state->game_mutex);
            }

            if(g_state->canvas) canvas_commit(g_state->canvas);
            furi_delay_ms(1);
        }

        rc = 0;
    } while(false);

    arduboy.audio.saveOnOff();
    EEPROM.commit();
    arduboy_tone_sound_system_deinit();

    if(g_state->input_sub && g_state->input_events) {
        furi_pubsub_unsubscribe(g_state->input_events, g_state->input_sub);
        g_state->input_sub = NULL;
    }

    wait_input_callbacks_idle(g_state);

    if(g_state->input_events) {
        furi_record_close(RECORD_INPUT_EVENTS);
        g_state->input_events = NULL;
    }

    if(g_state->gui) {
        if(g_state->canvas) {
            gui_direct_draw_release(g_state->gui);
            g_state->canvas = NULL;
        }
        if(fb_callback_added) {
            gui_remove_framebuffer_callback(g_state->gui, framebuffer_commit_callback, g_state);
        }
        furi_record_close(RECORD_GUI);
        g_state->gui = NULL;
    }

    if(g_state->game_mutex) {
        furi_mutex_free(g_state->game_mutex);
        g_state->game_mutex = NULL;
    }
    if(g_state->fb_mutex) {
        furi_mutex_free(g_state->fb_mutex);
        g_state->fb_mutex = NULL;
    }

    free(g_state);
    g_state = NULL;

    return rc;
}
