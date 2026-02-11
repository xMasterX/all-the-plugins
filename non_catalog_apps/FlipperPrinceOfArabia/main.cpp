#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification_messages.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wredundant-decls"

#include "lib/Arduboy2.h"
#include "lib/ArduboyFX.h"
#include "src/utils/Arduboy2Ext.h"
#include "src/ArduboyTonesFX.h"
#include "src/entities/Entities.h"
#include "src/utils/Enums.h"

#define DISPLAY_WIDTH  128
#define DISPLAY_HEIGHT 64
#define FB_SIZE        (DISPLAY_WIDTH * DISPLAY_HEIGHT / 8)

FuriMessageQueue* g_arduboy_sound_queue = NULL;
FuriThread* g_arduboy_sound_thread = NULL;
volatile bool g_arduboy_sound_thread_running = false;
volatile bool g_arduboy_audio_enabled = false;
volatile bool g_arduboy_tones_playing = false;
volatile bool g_arduboy_force_high = false;
volatile bool g_arduboy_force_norm = false;
volatile uint8_t g_arduboy_volume_mode = VOLUME_IN_TONE;

void splashScreen_Init();
void splashScreen();
void title_Init();
void saveSoundState();
void title();
void setSound(SoundIndex index);
void isEnemyVisible(
    Prince& prince,
    bool swapEnemies,
    bool& isVisible,
    bool& sameLevelAsPrince,
    bool justEnteredRoom);

bool testScroll(GamePlay& gamePlay, Prince& prince, Level& level);
void pushSequence();
void processJump(uint24_t pos);
void processRunJump(Prince& prince, Level& level, bool testEnemy);
void processStandingJump(Prince& prince, Level& level);
void initFlash(Prince& prince, Level& level, FlashType flashType);
void initFlash(Enemy& enemy, Level& level, FlashType flashType);
uint8_t activateSpikes(Prince& prince, Level& level);
void activateSpikes_Helper(Item& spikes);
void pushJumpUp_Drop(Prince& prince);
bool leaveLevel(Prince& prince, Level& level);
void pushDead(Prince& entity, Level& level, GamePlay& gamePlay, bool clear, DeathType deathType);
void pushDead(Enemy& entity, bool clear);
void showSign(Prince& prince, Level& level);
void playGrab();
void fixPosition();
uint8_t getImageIndexFromStance(uint16_t stance);
void getStance_Offsets(Direction direction, Point& offset, int16_t stance);
void processRunningTurn();
void saveCookie(bool enableLEDs);
void bindRuntimeStacks();
void restoreRuntimeAfterLoad();
void handleBlades();
void handleBlade_Single(int8_t tileXIdx, int8_t tileYIdx, uint8_t princeLX, uint8_t princeRX);

void game_Init();
void game_StartLevel();
uint16_t getMenuData(uint24_t table);
void game();
void moveBackwardsWithSword(Prince& prince);
void moveBackwardsWithSword(BaseEntity entity, BaseStack stack);

void render(bool sameLevelAsPrince);
void renderMenu();
void renderNumber(uint8_t x, uint8_t y, uint8_t number);
void renderNumber_Small(uint8_t x, uint8_t y, uint8_t number);
void renderNumber_Upright(uint8_t x, uint8_t y, uint8_t number);
void renderTorches(uint8_t x1, uint8_t x2, uint8_t y);

void setup();
void loop();

#include "game/PrinceOfArabia.cpp"
#include "game/PrinceOfArabia_Game.cpp"
#include "game/PrinceOfArabia_Render.cpp"
#include "game/PrinceOfArabia_SplashScreen.cpp"
#include "game/PrinceOfArabia_Title.cpp"
#include "game/PrinceOfArabia_Utils.cpp"

typedef struct {
    uint8_t screen_buffer[FB_SIZE];
    uint8_t front_buffer[FB_SIZE];

    Gui* gui;
    Canvas* canvas;
    FuriMutex* fb_mutex;
    FuriMutex* game_mutex;

    FuriPubSub* input_events;
    FuriPubSubSubscription* input_sub;
    NotificationApp* notifications;

    volatile uint8_t input_state;
    volatile uint8_t input_press_latch;
    volatile bool exit_requested;
    volatile bool back_long_requested;
    GameState last_game_state;
    bool backlight_forced;
} FlipperState;

static FlipperState* g_state = NULL;

static bool can_exit_from_current_state() {
    switch(gamePlay.gameState) {
    case GameState::SplashScreen_Init:
    case GameState::SplashScreen:
    case GameState::Title_Init:
    case GameState::Title:
        return true;
    default:
        return false;
    }
}

static bool should_hold_backlight(GameState state) {
    return (state != GameState::SplashScreen_Init) && (state != GameState::SplashScreen);
}

static void update_display_policy(FlipperState* state) {
    if(!state || !state->notifications) return;

    const bool hold = should_hold_backlight(gamePlay.gameState);
    if(hold && !state->backlight_forced) {
        notification_message(state->notifications, &sequence_display_backlight_enforce_on);
        state->backlight_forced = true;
    } else if(!hold && state->backlight_forced) {
        notification_message(state->notifications, &sequence_display_backlight_enforce_auto);
        state->backlight_forced = false;
    }
}

static void input_events_callback(const void* value, void* ctx) {
    if(!value || !ctx) return;

    const InputEvent* event = (const InputEvent*)value;
    FlipperState* state = (FlipperState*)ctx;
    if(!state) return;

    uint8_t bit = 0;
    switch(event->key) {
    case InputKeyUp:
        bit = INPUT_UP;
        break;
    case InputKeyDown:
        bit = INPUT_DOWN;
        break;
    case InputKeyLeft:
        bit = INPUT_LEFT;
        break;
    case InputKeyRight:
        bit = INPUT_RIGHT;
        break;
    case InputKeyOk:
        bit = INPUT_A;
        break;
    case InputKeyBack:
        bit = INPUT_B;
        break;
    default:
        return;
    }

    if(event->type == InputTypePress || event->type == InputTypeRepeat) {
        (void)__atomic_fetch_or((uint8_t*)&state->input_state, bit, __ATOMIC_RELAXED);
        (void)__atomic_fetch_or((uint8_t*)&state->input_press_latch, bit, __ATOMIC_RELAXED);
    } else if(event->type == InputTypeRelease) {
        (void)__atomic_fetch_and((uint8_t*)&state->input_state, (uint8_t)~bit, __ATOMIC_RELAXED);
        if(event->key == InputKeyBack) {
            state->back_long_requested = false;
        }
    } else if(event->type == InputTypeLong && event->key == InputKeyBack) {
        if(can_exit_from_current_state()) {
            state->back_long_requested = true;
        }
    }
}

static void framebuffer_commit_callback(
    uint8_t* data,
    size_t size,
    CanvasOrientation orientation,
    void* context) {
    UNUSED(orientation);

    FlipperState* state = (FlipperState*)context;
    if(!state || !data || size < FB_SIZE) return;

    if(furi_mutex_acquire(state->fb_mutex, 0) != FuriStatusOk) return;
    const uint8_t* src = state->front_buffer;
    for(size_t i = 0; i < FB_SIZE; i++) {
        data[i] = (uint8_t)(src[i] ^ 0xFF);
    }
    furi_mutex_release(state->fb_mutex);
}

extern "C" int32_t arduboy_app(void* p) {
    UNUSED(p);

    g_state = (FlipperState*)malloc(sizeof(FlipperState));
    if(!g_state) return -1;
    memset(g_state, 0, sizeof(FlipperState));

    g_state->game_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    g_state->fb_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    if(!g_state->game_mutex || !g_state->fb_mutex) {
        if(g_state->game_mutex) furi_mutex_free(g_state->game_mutex);
        if(g_state->fb_mutex) furi_mutex_free(g_state->fb_mutex);
        free(g_state);
        g_state = NULL;
        return -1;
    }

    g_state->exit_requested = false;
    g_state->back_long_requested = false;
    g_state->last_game_state = gamePlay.gameState;
    g_state->notifications = NULL;
    g_state->backlight_forced = false;
    g_state->input_state = 0;
    g_state->input_press_latch = 0;
    memset(g_state->screen_buffer, 0x00, FB_SIZE);
    memset(g_state->front_buffer, 0x00, FB_SIZE);

    arduboy.begin(
        g_state->screen_buffer,
        &g_state->input_state,
        &g_state->input_press_latch,
        g_state->game_mutex,
        &g_state->exit_requested);
    Sprites::setArduboy(&arduboy);

    EEPROM.begin(APP_DATA_PATH("eeprom.bin"));

    g_state->gui = (Gui*)furi_record_open(RECORD_GUI);
    if(g_state->gui) {
        gui_add_framebuffer_callback(g_state->gui, framebuffer_commit_callback, g_state);
        g_state->canvas = gui_direct_draw_acquire(g_state->gui);
    }

    g_state->notifications = (NotificationApp*)furi_record_open(RECORD_NOTIFICATION);

    g_state->input_events = (FuriPubSub*)furi_record_open(RECORD_INPUT_EVENTS);
    if(g_state->input_events) {
        g_state->input_sub =
            furi_pubsub_subscribe(g_state->input_events, input_events_callback, g_state);
    }

    if(furi_mutex_acquire(g_state->game_mutex, FuriWaitForever) == FuriStatusOk) {
        const uint32_t frame_before = arduboy.frameCount();
        setup();
        loop();
        update_display_policy(g_state);
        if(g_state->back_long_requested) {
            g_state->back_long_requested = false;
            if(can_exit_from_current_state()) g_state->exit_requested = true;
        }
        const uint32_t frame_after = arduboy.frameCount();
        if(frame_after != frame_before) {
            const GameState now_state = gamePlay.gameState;
            if(now_state != g_state->last_game_state) {
                g_state->last_game_state = now_state;
                g_state->input_state = 0;
                g_state->input_press_latch = 0;
                g_state->back_long_requested = false;
                arduboy.clearButtonState();
            }
            if(furi_mutex_acquire(g_state->fb_mutex, FuriWaitForever) == FuriStatusOk) {
                memcpy(g_state->front_buffer, g_state->screen_buffer, FB_SIZE);
                furi_mutex_release(g_state->fb_mutex);
            }
            arduboy.applyDeferredDisplayOps();
        }
        furi_mutex_release(g_state->game_mutex);
    }
    if(g_state->canvas) canvas_commit(g_state->canvas);

    while(!g_state->exit_requested) {
        if(furi_mutex_acquire(g_state->game_mutex, 0) == FuriStatusOk) {
            const uint32_t frame_before = arduboy.frameCount();
            loop();
            update_display_policy(g_state);
            if(g_state->back_long_requested) {
                g_state->back_long_requested = false;
                if(can_exit_from_current_state()) g_state->exit_requested = true;
            }
            const uint32_t frame_after = arduboy.frameCount();
            if(frame_after != frame_before) {
                const GameState now_state = gamePlay.gameState;
                if(now_state != g_state->last_game_state) {
                    g_state->last_game_state = now_state;
                    g_state->input_state = 0;
                    g_state->input_press_latch = 0;
                    g_state->back_long_requested = false;
                    arduboy.clearButtonState();
                }
                if(furi_mutex_acquire(g_state->fb_mutex, 0) == FuriStatusOk) {
                    memcpy(g_state->front_buffer, g_state->screen_buffer, FB_SIZE);
                    furi_mutex_release(g_state->fb_mutex);
                }
                arduboy.applyDeferredDisplayOps();
            }
            furi_mutex_release(g_state->game_mutex);
        }
        if(g_state->canvas) canvas_commit(g_state->canvas);
        furi_delay_ms(1);
    }

    arduboy.audio.saveOnOff();
    EEPROM.commit();
    FX::commit();
    FX::end();
    arduboy_tone_sound_system_deinit();
    if(g_state->notifications) {
        if(g_state->backlight_forced) {
            notification_message(g_state->notifications, &sequence_display_backlight_enforce_auto);
            g_state->backlight_forced = false;
        }
        furi_record_close(RECORD_NOTIFICATION);
        g_state->notifications = NULL;
    }

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
    if(g_state->fb_mutex) {
        furi_mutex_free(g_state->fb_mutex);
        g_state->fb_mutex = NULL;
    }

    free(g_state);
    g_state = NULL;

    return 0;
}

#pragma GCC diagnostic pop
