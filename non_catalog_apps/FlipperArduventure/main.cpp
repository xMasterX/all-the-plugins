#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <stdlib.h>
#include <string.h>

#define COLOUR_WHITE 1
#define COLOUR_BLACK 0
#define TONES_END    0x8000

// ====== НАСТРОЙКИ ======
#define TARGET_FRAMERATE         60
#define GHOST_COMPENSATION_LEVEL 0

#ifndef HOLD_TO_EXIT_FRAMES
#define HOLD_TO_EXIT_FRAMES 30
#endif

#include "lib/Arduboy2.h"
#include "lib/Arduino.h"
#include "lib/ATMlib.h"

#define DISPLAY_WIDTH  128
#define DISPLAY_HEIGHT 64
#define BUFFER_SIZE    (DISPLAY_WIDTH * DISPLAY_HEIGHT / 8)

typedef struct {
    uint8_t screen_buffer[BUFFER_SIZE];
    uint8_t front_buffer[BUFFER_SIZE];

#if (GHOST_COMPENSATION_LEVEL >= 1)
    uint8_t prev_buffer[BUFFER_SIZE];
#endif
#if (GHOST_COMPENSATION_LEVEL >= 2)
    uint8_t prev2_buffer[BUFFER_SIZE];
#endif

    Gui* gui;
    Canvas* canvas;
    FuriTimer* timer;
    FuriMutex* fb_mutex;
    FuriMutex* game_mutex;
    FuriPubSub* input_events;
    FuriPubSubSubscription* input_sub;
    volatile uint8_t input_state;
    volatile bool exit_requested;
    volatile bool in_frame;
    volatile bool invert_frame;
} FlipperState;

static FlipperState* g_state = NULL;

static void input_events_callback(const void* value, void* ctx) {
    if(!value || !ctx) return;
    InputEvent* e = (InputEvent*)value;
    Arduboy2Base::FlipperInputCallback(e, ctx);
}

#define GAME_ID 46

#include "game/globals.h"
#include "game/songs.h"
#include "game/menu.h"
#include "game/game.h"
#include "game/inputs.h"
#include "game/text.h"
#include "game/inventory.h"
#include "game/items.h"
#include "game/player.h"
#include "game/enemies.h"
#include "game/battles.h"

Arduboy2Base* arduboy_ptr = nullptr;
Sprites* sprites_ptr = nullptr;

alignas(Arduboy2Base) static uint8_t arduboy_storage[sizeof(Arduboy2Base)];
alignas(Sprites) static uint8_t sprites_storage[sizeof(Sprites)];


typedef void (*FunctionPointer) ();
const FunctionPointer mainGameLoop[] = {
  stateMenuIntro,
  stateMenuMain,
  stateMenuContinue,
  stateMenuNew,
  stateMenuSound,
  stateMenuCredits,
  stateGamePlaying,
  stateGameInventory,
  stateGameEquip,
  stateGameStats,
  stateGameMap,
  stateGameOver,
  showSubMenuStuff,
  showSubMenuStuff,
  showSubMenuStuff,
  showSubMenuStuff,
  walkingThroughDoor,
  stateGameBattle,
  stateGameBoss,
  stateGameIntro,
  stateGameNew,
  stateGameSaveSoundEnd,
  stateGameSaveSoundEnd,
  stateGameSaveSoundEnd,
  stateGameObjects,
  stateGameShop,
  stateGameInn,
  battleGiveRewards,
  stateMenuReboot,
};

static void game_setup() {
  atm_system_init();
  arduboy.boot();
  arduboy.audio.begin();
  ATM.play(titleSong);
  arduboy.setFrameRate(60);                                 // set the frame rate of the game at 60 fps
}


static void game_loop_tick() {
  if (!(arduboy.nextFrame())) return;
  //arduboy.fillScreen(1);
  arduboy.pollButtons();
  //arduboy.clear();
  drawTiles();
  updateEyes();
  
  mainGameLoop[gameState]();

  checkInputs();
  if (question) drawQuestion();
  if (yesNo) drawYesNo();
  if (flashBlack) flashScreen(BLACK);     // Set in battleStart
  else if (flashWhite) flashScreen(WHITE);
  //Serial.write(arduboy.getBuffer(), 128 * 64 / 8);
  arduboy.display();
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

    if(furi_mutex_acquire(state->fb_mutex, 0) != FuriStatusOk) return;

    const uint8_t* src = state->front_buffer;

    for(size_t i = 0; i < BUFFER_SIZE; i++) {
        if(gameState < 6) {
            data[i] = (uint8_t)(src[i] ^ 0x00);
        } else {
            data[i] = (uint8_t)(src[i] ^ 0xFF);
        }
    }

    furi_mutex_release(state->fb_mutex);
}

static void apply_ghost_compensation(FlipperState* state) {
#if (GHOST_COMPENSATION_LEVEL == 0)
    memcpy(state->front_buffer, state->screen_buffer, BUFFER_SIZE);

#elif (GHOST_COMPENSATION_LEVEL == 1)
    // front = current OR prev  (объект держится 2 кадра по сути: текущий + предыдущий)
    for(size_t i = 0; i < BUFFER_SIZE; i++) {
        state->front_buffer[i] = (uint8_t)(state->screen_buffer[i] | state->prev_buffer[i]);
    }
    // сдвигаем историю
    memcpy(state->prev_buffer, state->screen_buffer, BUFFER_SIZE);

#elif (GHOST_COMPENSATION_LEVEL >= 2)
    // front = current OR prev OR prev2 (ещё сильнее удержание)
    for(size_t i = 0; i < BUFFER_SIZE; i++) {
        state->front_buffer[i] =
            (uint8_t)(state->screen_buffer[i] | state->prev_buffer[i] | state->prev2_buffer[i]);
    }
    memcpy(state->prev2_buffer, state->prev_buffer, BUFFER_SIZE);
    memcpy(state->prev_buffer, state->screen_buffer, BUFFER_SIZE);
#endif
}

static void timer_callback(void* ctx) {
    FlipperState* state = (FlipperState*)ctx;
    if(!state) return;

    if(state->in_frame) return;
    state->in_frame = true;

    if(furi_mutex_acquire(state->game_mutex, 0) != FuriStatusOk) {
        state->in_frame = false;
        return;
    }

    game_loop_tick();

    state->invert_frame = false;

    // Критично: защита fb + применение компенсации
    furi_mutex_acquire(state->fb_mutex, FuriWaitForever);
    apply_ghost_compensation(state);
    furi_mutex_release(state->fb_mutex);

    if(g_state->canvas) canvas_commit(g_state->canvas);


    furi_mutex_release(state->game_mutex);
    state->in_frame = false;
}

extern "C" int32_t arduboy_app(void* p) {
    UNUSED(p);

    g_state = (FlipperState*)malloc(sizeof(FlipperState));
    if(!g_state) return -1;
    memset(g_state, 0, sizeof(FlipperState));
    arduboy_ptr = new(arduboy_storage) Arduboy2Base();
sprites_ptr = new(sprites_storage) Sprites();


    g_state->fb_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    g_state->game_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    if(!g_state->fb_mutex || !g_state->game_mutex) {
        if(g_state->fb_mutex) furi_mutex_free(g_state->fb_mutex);
        if(g_state->game_mutex) furi_mutex_free(g_state->game_mutex);
        free(g_state);
        g_state = NULL;
        return -1;
    }

    g_state->exit_requested = false;
    g_state->invert_frame = false;

    memset(g_state->screen_buffer, 0x00, BUFFER_SIZE);
    memset(g_state->front_buffer, 0x00, BUFFER_SIZE);

#if (GHOST_COMPENSATION_LEVEL >= 1)
    memset(g_state->prev_buffer, 0x00, BUFFER_SIZE);
#endif
#if (GHOST_COMPENSATION_LEVEL >= 2)
    memset(g_state->prev2_buffer, 0x00, BUFFER_SIZE);
#endif

    arduboy.begin(g_state->screen_buffer, &g_state->input_state, g_state->game_mutex, &g_state->exit_requested);
    Sprites::setArduboy(&arduboy);

    g_state->gui = (Gui*)furi_record_open(RECORD_GUI);
    gui_add_framebuffer_callback(g_state->gui, framebuffer_commit_callback, g_state);
    g_state->canvas = gui_direct_draw_acquire(g_state->gui);

    g_state->input_events = (FuriPubSub*)furi_record_open(RECORD_INPUT_EVENTS);
    g_state->input_sub = furi_pubsub_subscribe(
        g_state->input_events, input_events_callback, arduboy.inputContext());

    // Setup game and do one initial draw
    furi_mutex_acquire(g_state->game_mutex, FuriWaitForever);
    game_setup();

    furi_mutex_acquire(g_state->fb_mutex, FuriWaitForever);
    apply_ghost_compensation(g_state);
    furi_mutex_release(g_state->fb_mutex);

    if(g_state->canvas) canvas_commit(g_state->canvas);
    furi_mutex_release(g_state->game_mutex);

    // Таймер на 30 FPS
    g_state->timer = furi_timer_alloc(timer_callback, FuriTimerTypePeriodic, g_state);
    const uint32_t tick_hz = furi_kernel_get_tick_frequency();
    uint32_t period = (tick_hz + (TARGET_FRAMERATE / 2)) / TARGET_FRAMERATE;
    if(period == 0) period = 1;
    furi_timer_start(g_state->timer, period);

    while(!g_state->exit_requested) {
        atm_set_enabled(arduboy.audio.enabled());
        furi_delay_ms(50);
    }
    
    atm_system_deinit();
    arduboy_tone_sound_system_deinit();

    if(g_state->timer) {
        furi_timer_stop(g_state->timer);
        furi_timer_free(g_state->timer);
        g_state->timer = NULL;
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

    if(g_state->fb_mutex) {
        furi_mutex_free(g_state->fb_mutex);
        g_state->fb_mutex = NULL;
    }
    if(g_state->game_mutex) {
        furi_mutex_free(g_state->game_mutex);
        g_state->game_mutex = NULL;
    }

    free(g_state);
    g_state = NULL;

    return 0;
}
