#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <stdlib.h>
#include <string.h>

#include "game/Defines.h"
#include "lib/Arduboy2.h"
#include "lib/Arduino.h"

#include "lib/ArduboyFX.h"
#include "game/ArduboyTonesFX.h"
#include "game/Engine.h"
#include "game/Generated/fxdata.h"
#include "game/ArduboyPlatform.h"
#include "game/Generated/Data_Audio.h"
#include "game/Engine.h"
#include "lib/ArduboyTones.h"

FuriMessageQueue* g_arduboy_sound_queue = NULL;
FuriThread* g_arduboy_sound_thread = NULL;
volatile bool g_arduboy_sound_thread_running = false;
volatile bool g_arduboy_audio_enabled = false;

volatile bool g_arduboy_tones_playing = false;

volatile uint8_t g_arduboy_volume_mode = VOLUME_IN_TONE;
volatile bool g_arduboy_force_high = false;
volatile bool g_arduboy_force_norm = false;

#define DISPLAY_WIDTH       128
#define DISPLAY_HEIGHT      64
#define BUFFER_SIZE         (DISPLAY_WIDTH * DISPLAY_HEIGHT / 8)
#define HOLD_TO_EXIT_FRAMES 15

typedef struct {
    uint8_t screen_buffer[BUFFER_SIZE];

    Gui* gui;
    Canvas* canvas;

    FuriMutex* game_mutex;

    FuriPubSub* input_events;
    FuriPubSubSubscription* input_sub;

    volatile uint8_t input_state;
    volatile bool exit_requested;
} FlipperState;

static FlipperState* g_state = NULL;

Arduboy2Base arduboy;

uint16_t audioBuffer[32];
ArduboyTonesFX sound(audioBuffer, 32);

unsigned long lastTimingSample;

static void input_events_callback(const void* value, void* ctx) {
    if(!value || !ctx) return;
    InputEvent* e = (InputEvent*)value;
    Arduboy2Base::FlipperInputCallback(e, ctx);
}

void ArduboyPlatform::playSound(uint8_t id) {
    sound.tonesFromFX(
        (uint24_t)((uint32_t)Data_audio + (uint32_t)(pgm_read_word(&Data_AudioPatterns[id]))));
}

static void framebuffer_commit_callback(
    uint8_t* data,
    size_t size,
    CanvasOrientation orientation,
    void* context) {
    FlipperState* state = (FlipperState*)context;
    if(!state || !data) return;
    if(size < BUFFER_SIZE) return;
    (void)orientation;

    const uint8_t* src = state->screen_buffer;
    for(size_t i = 0; i < BUFFER_SIZE; i++) {
        data[i] = (uint8_t)(src[i] ^ 0xFF);
    }
}

static uint8_t exit_hold_frames = 0;
static bool exit_hold_cancelled = false;
extern "C" int32_t arduboy_app(void* p) {
    UNUSED(p);

    g_state = (FlipperState*)malloc(sizeof(FlipperState));
    if(!g_state) return -1;
    memset(g_state, 0, sizeof(FlipperState));

    g_state->game_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    if(!g_state->game_mutex) {
        free(g_state);
        g_state = NULL;
        return -1;
    }

    g_state->exit_requested = false;
    g_state->input_state = 0;
    memset(g_state->screen_buffer, 0x00, BUFFER_SIZE);

    arduboy.begin(
        g_state->screen_buffer,
        &g_state->input_state,
        g_state->game_mutex,
        &g_state->exit_requested);

    Sprites::setArduboy(&arduboy);

    g_state->gui = (Gui*)furi_record_open(RECORD_GUI);
    gui_add_framebuffer_callback(g_state->gui, framebuffer_commit_callback, g_state);
    g_state->canvas = gui_direct_draw_acquire(g_state->gui);

    g_state->input_events = (FuriPubSub*)furi_record_open(RECORD_INPUT_EVENTS);
    g_state->input_sub = furi_pubsub_subscribe(
        g_state->input_events, input_events_callback, arduboy.inputContext());

    furi_mutex_acquire(g_state->game_mutex, FuriWaitForever);

    arduboy.boot();
    arduboy.setFrameRate(TARGET_FRAMERATE);
    arduboy.audio.begin();

    (void)FX::begin(0, 0);

    engine.init();

    lastTimingSample = millis();
    {
        engine.update();
        engine.draw();
        arduboy.setRGBled(0, 0, 0);
        if(g_state->canvas) canvas_commit(g_state->canvas);
    }

    furi_mutex_release(g_state->game_mutex);

    int16_t tickAccum = 0;
    const int16_t frameDuration = (int16_t)(1000 / TARGET_FRAMERATE);

    while(!g_state->exit_requested) {
        if(furi_mutex_acquire(g_state->game_mutex, 0) == FuriStatusOk) {
            const unsigned long now = millis();
            tickAccum += (int16_t)(now - lastTimingSample);
            lastTimingSample = now;

            if(arduboy.nextFrame()) {
                Platform.update();
                const uint8_t in = Platform.readInput();
                const bool inGame = engine.inGame();
                const bool backHeld = (in & Input_Btn_A);
                const bool strafeHeld = inGame && backHeld &&
                                        ((in & Input_Dpad_Left) || (in & Input_Dpad_Right));

                if(!backHeld) {
                    exit_hold_frames = 0;
                    exit_hold_cancelled = false;
                } else if(strafeHeld) {
                    // Cancel hold-to-exit once user starts strafing while holding Back.
                    exit_hold_frames = 0;
                    exit_hold_cancelled = true;
                } else {
                    if(!exit_hold_cancelled) {
                        if(exit_hold_frames < HOLD_TO_EXIT_FRAMES) exit_hold_frames++;
                        if(exit_hold_frames >= HOLD_TO_EXIT_FRAMES) {
                            exit_hold_frames = 0;
                            if(inGame) {
                                engine.exitToMenu();
                                // Require releasing Back before starting a new hold action in menu.
                                exit_hold_cancelled = true;
                            } else {
                                arduboy.exitToBootloader();
                            }
                        }
                    }
                }

                while(tickAccum > frameDuration) {
                    engine.update();
                    tickAccum -= frameDuration;
                }

                engine.draw();

                if(engine.gameState == GameState_Playing && engine.renderer.damageIndicator < 0) {
                    uint8_t b = (uint8_t)(-engine.renderer.damageIndicator * 5);
                    arduboy.setRGBled(b, b, b);
                } else if(
                    engine.gameState == GameState_Playing && engine.renderer.damageIndicator > 0) {
                    uint8_t b = (uint8_t)(engine.renderer.damageIndicator * 51);
                    arduboy.setRGBled(b, 0, 0);
                } else {
                    arduboy.setRGBled(0, 0, 0);
                }

                if(g_state->canvas) canvas_commit(g_state->canvas);
            }

            furi_mutex_release(g_state->game_mutex);
        }
        furi_delay_ms(1);
    }

    arduboy.audio.saveOnOff();
    arduboy_tone_sound_system_deinit();
    FX::commit();
    FX::end();

    if(g_state->input_sub) {
        furi_pubsub_unsubscribe(g_state->input_events, g_state->input_sub);
        g_state->input_sub = NULL;
    }

    if(g_state->input_events) {
        furi_record_close(RECORD_INPUT_EVENTS);
        g_state->input_events = NULL;
    }

    if(g_state->gui) {
        gui_direct_draw_release(g_state->gui);
        gui_remove_framebuffer_callback(g_state->gui, framebuffer_commit_callback, g_state);
        furi_record_close(RECORD_GUI);
        g_state->gui = NULL;
        g_state->canvas = NULL;
    }

    if(g_state->game_mutex) {
        furi_mutex_free(g_state->game_mutex);
        g_state->game_mutex = NULL;
    }

    free(g_state);
    g_state = NULL;

    return 0;
}
