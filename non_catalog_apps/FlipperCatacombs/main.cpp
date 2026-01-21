#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>

#include <stdlib.h>
#include <string.h>

#include "lib/flipper.h"
#include "lib/EEPROM.h"
#include "game/Game.h"
#include "game/Platform.h"

#define TARGET_FRAMERATE 30
#define HOLD_TIME_MS     300

FlipperState* g_state = NULL;

static FuriMessageQueue* s_input_queue = NULL;
static volatile uint32_t s_input_cb_inflight = 0;
static volatile uint32_t s_fb_cb_inflight = 0;

static inline void wait_inflight_zero(volatile uint32_t* counter) {
    while(__atomic_load_n(counter, __ATOMIC_ACQUIRE) != 0) {
        furi_delay_ms(1);
    }
}

static void framebuffer_commit_callback(
    uint8_t* data,
    size_t size,
    CanvasOrientation orientation,
    void* context) {
    __atomic_fetch_add(&s_fb_cb_inflight, 1, __ATOMIC_RELAXED);

    FlipperState* state = (FlipperState*)context;
    if(!state || !data || size < BUFFER_SIZE) {
        __atomic_fetch_sub(&s_fb_cb_inflight, 1, __ATOMIC_RELAXED);
        return;
    }
    (void)orientation;

    if(furi_mutex_acquire(state->fb_mutex, 0) != FuriStatusOk) {
        __atomic_fetch_sub(&s_fb_cb_inflight, 1, __ATOMIC_RELAXED);
        return;
    }

    const uint8_t* src = state->front_buffer;
    for(size_t i = 0; i < BUFFER_SIZE; i++) {
        data[i] = (uint8_t)(src[i] ^ 0xFF);
    }

    furi_mutex_release(state->fb_mutex);

    __atomic_fetch_sub(&s_fb_cb_inflight, 1, __ATOMIC_RELAXED);
}

static void input_events_callback(const void* value, void* ctx) {
    UNUSED(ctx);

    __atomic_fetch_add(&s_input_cb_inflight, 1, __ATOMIC_RELAXED);

    if(value) {
        FuriMessageQueue* q = __atomic_load_n(&s_input_queue, __ATOMIC_ACQUIRE);
        if(q) {
            InputEvent ev = *(const InputEvent*)value;
            (void)furi_message_queue_put(q, &ev, 0);
        }
    }

    __atomic_fetch_sub(&s_input_cb_inflight, 1, __ATOMIC_RELAXED);
}

static inline void set_flag(volatile uint8_t& state, uint8_t flag, bool down) {
    if(down)
        state |= flag;
    else
        state &= (uint8_t)~flag;
}

extern "C" int32_t arduboy3d_app(void* p) {
    UNUSED(p);

    Gui* gui = NULL;
    Canvas* canvas = NULL;
    FuriPubSub* input_events = NULL;
    FuriPubSubSubscription* input_sub = NULL;
    FuriMessageQueue* q_local = NULL;

    FlipperState* st = (FlipperState*)malloc(sizeof(FlipperState));
    if(!st) return -1;
    memset(st, 0, sizeof(FlipperState));
    g_state = st;

    do {
        st->fb_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
        if(!st->fb_mutex) break;

        memset(st->back_buffer, 0x00, BUFFER_SIZE);
        memset(st->front_buffer, 0x00, BUFFER_SIZE);

        q_local = furi_message_queue_alloc(32, sizeof(InputEvent));
        if(!q_local) break;
        __atomic_store_n(&s_input_queue, q_local, __ATOMIC_RELEASE);

        EEPROM.begin();
        furi_delay_ms(50);
        Platform::SetAudioEnabled(EEPROM.read(2) != 0);
        Game::menu.ReadSave();

        gui = (Gui*)furi_record_open(RECORD_GUI);
        if(!gui) break;
        st->gui = gui;

        gui_add_framebuffer_callback(gui, framebuffer_commit_callback, st);

        canvas = gui_direct_draw_acquire(gui);
        if(!canvas) break;
        st->canvas = canvas;

        input_events = (FuriPubSub*)furi_record_open(RECORD_INPUT_EVENTS);
        if(!input_events) break;
        st->input_events = input_events;

        input_sub = furi_pubsub_subscribe(input_events, input_events_callback, st);
        if(!input_sub) break;
        st->input_sub = input_sub;

        const uint32_t tick_hz = furi_kernel_get_tick_frequency();
        uint32_t period_ticks = (tick_hz + (TARGET_FRAMERATE / 2)) / TARGET_FRAMERATE;
        if(period_ticks == 0) period_ticks = 1;
        const uint32_t hold_ticks = (uint32_t)((HOLD_TIME_MS * tick_hz + 999u) / 1000u);

        uint32_t next_tick = furi_get_tick();

        bool back_pressed = false;
        bool back_hold_fired = false;
        uint32_t back_press_tick = 0;

        while(!st->exit_requested) {
            uint32_t now = furi_get_tick();

            // frame pacing
            if((int32_t)(now - next_tick) < 0) {
                uint32_t dt_ticks = next_tick - now;
                uint32_t dt_ms = (dt_ticks * 1000u) / tick_hz;
                furi_delay_ms(dt_ms ? dt_ms : 1);
                continue;
            }

            if((int32_t)(now - next_tick) > (int32_t)(period_ticks * 2)) {
                next_tick = now;
            }
            next_tick += period_ticks;

            // drain input queue (main thread)
            InputEvent ev;
            while(q_local && (furi_message_queue_get(q_local, &ev, 0) == FuriStatusOk)) {
                const bool down = (ev.type == InputTypePress);
                const bool up = (ev.type == InputTypeRelease);

                switch(ev.key) {
                case InputKeyUp:
                    if(down)
                        set_flag(st->input_state, INPUT_UP, true);
                    else if(up)
                        set_flag(st->input_state, INPUT_UP, false);
                    break;
                case InputKeyDown:
                    if(down)
                        set_flag(st->input_state, INPUT_DOWN, true);
                    else if(up)
                        set_flag(st->input_state, INPUT_DOWN, false);
                    break;
                case InputKeyLeft:
                    if(down)
                        set_flag(st->input_state, INPUT_LEFT, true);
                    else if(up)
                        set_flag(st->input_state, INPUT_LEFT, false);
                    break;
                case InputKeyRight:
                    if(down)
                        set_flag(st->input_state, INPUT_RIGHT, true);
                    else if(up)
                        set_flag(st->input_state, INPUT_RIGHT, false);
                    break;
                case InputKeyOk:
                    if(down)
                        set_flag(st->input_state, INPUT_B, true);
                    else if(up)
                        set_flag(st->input_state, INPUT_B, false);
                    break;
                case InputKeyBack:
                    if(down) {
                        back_pressed = true;
                        back_hold_fired = false;
                        back_press_tick = now;
                    } else if(up) {
                        back_pressed = false;
                        back_hold_fired = false;
                    }
                    break;
                default:
                    break;
                }
            }

            // BACK hold logic
            if(back_pressed && !back_hold_fired) {
                if((uint32_t)(now - back_press_tick) >= hold_ticks) {
                    back_hold_fired = true;
                    if(Game::InMenu())
                        st->exit_requested = true;
                    else
                        Game::GoToMenu();
                }
            }

            if(st->exit_requested) break;

            Game::Tick();
            Game::Draw();

            // swap for framebuffer callback
            furi_mutex_acquire(st->fb_mutex, FuriWaitForever);
            memcpy(st->front_buffer, st->back_buffer, BUFFER_SIZE);
            furi_mutex_release(st->fb_mutex);

            canvas_commit(canvas);
        }
    } while(false);

    Game::menu.WriteSave();

    __atomic_store_n(&s_input_queue, (FuriMessageQueue*)NULL, __ATOMIC_RELEASE);

    if(input_sub && input_events) {
        furi_pubsub_unsubscribe(input_events, input_sub);
        input_sub = NULL;
    }

    wait_inflight_zero(&s_input_cb_inflight);

    if(input_events) {
        furi_record_close(RECORD_INPUT_EVENTS);
        input_events = NULL;
    }

    if(q_local) {
        furi_message_queue_free(q_local);
        q_local = NULL;
    }

    if(gui) {
        gui_remove_framebuffer_callback(gui, framebuffer_commit_callback, st);
    }

    wait_inflight_zero(&s_fb_cb_inflight);

    if(g_state->input_events) {
        furi_record_close(RECORD_INPUT_EVENTS);
        g_state->input_events = NULL;
    }

    if(g_state->gui) {
        gui_direct_draw_release(g_state->gui);
        furi_record_close(RECORD_GUI);
        g_state->gui = NULL;
        g_state->canvas = NULL;
    }

    if(g_state->fb_mutex) {
        furi_mutex_free(g_state->fb_mutex);
        g_state->fb_mutex = NULL;
    }

    Platform::SetAudioEnabled(false);

    return 0;
}
