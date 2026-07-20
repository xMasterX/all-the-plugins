#include "trainer_view.h"
#include "../morse_events.h"
#include "../helpers/morse.h"
#include <furi.h>
#include <furi_hal_speaker.h>
#include <gui/elements.h>

/* Short press < 2 dot lengths = dot, otherwise dash. */
#define KEY_DASH_THRESHOLD_MS (MORSE_DOT_MS * 2)

struct TrainerView {
    View* view;
    ViewDispatcher* view_dispatcher;
    const MorseSettings* settings;
    NotificationApp* notifications;
    /* Keying state is view-internal: it only exists between an OK press and
     * its release, both delivered on the dispatcher thread. */
    uint32_t ok_press_tick;
    bool ok_down;
    bool sidetone_on;
    /* ~30 fps redraws while OK is held, so the dot/dash threshold bar
     * animates. Writes go through the locking model, so the timer thread
     * and GUI draws can't race. */
    FuriTimer* hold_timer;
};

static void draw_pattern_shapes(Canvas* canvas, const char* pattern, int32_t y) {
    /* Dots as discs, dashes as boxes, centered horizontally. */
    const int32_t dot_w = 5, dash_w = 13, gap = 4, h = 5;
    int32_t total = 0;
    for(const char* s = pattern; *s; s++) {
        total += ((*s == '.') ? dot_w : dash_w) + (s[1] ? gap : 0);
    }
    int32_t x = (128 - total) / 2;
    for(const char* s = pattern; *s; s++) {
        if(*s == '.') {
            canvas_draw_disc(canvas, x + dot_w / 2, y + h / 2, 2);
            x += dot_w + gap;
        } else {
            canvas_draw_box(canvas, x, y, dash_w, h);
            x += dash_w + gap;
        }
    }
}

static void draw_prompt(Canvas* canvas, const TrainerModel* m, int32_t y) {
    canvas_set_font(canvas, FontPrimary);
    size_t len = strlen(m->prompt);
    if(len <= 1 || m->prompt_hilite == 0xFF) {
        canvas_draw_str_aligned(canvas, 64, y, AlignCenter, AlignCenter, m->prompt);
        return;
    }
    /* Word prompt: draw per-letter so the active letter can be underlined. */
    const int32_t cell = 12;
    int32_t x = 64 - (int32_t)(len * cell) / 2;
    for(size_t i = 0; i < len; i++) {
        char s[2] = {m->prompt[i], '\0'};
        canvas_draw_str_aligned(canvas, x + cell / 2, y, AlignCenter, AlignCenter, s);
        if(i == m->prompt_hilite) {
            canvas_draw_line(canvas, x + 2, y + 7, x + cell - 3, y + 7);
        }
        x += cell;
    }
}

static void draw_hold_bar(Canvas* canvas, const TrainerModel* m) {
    /* Fill grows while OK is held; crossing the tick mark = dash. Makes the
     * dot/dash timing boundary visible instead of trial-and-error. */
    if(m->key_held_ms == 0) return;
    const int32_t x = 44, y = 49, w = 40, h = 6;
    const uint32_t full_ms = KEY_DASH_THRESHOLD_MS * 2;
    uint32_t held = m->key_held_ms;
    if(held > full_ms) held = full_ms;
    canvas_draw_frame(canvas, x, y, w, h);
    canvas_draw_box(canvas, x, y, (held * w) / full_ms, h);
    /* Threshold tick at the halfway point */
    canvas_draw_line(canvas, x + w / 2, y - 2, x + w / 2, y + h + 1);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, x - 6, y + h - 1, AlignCenter, AlignBottom, ".");
    canvas_draw_str_aligned(canvas, x + w + 6, y + h - 1, AlignCenter, AlignBottom, "-");
}

static void draw_choices(Canvas* canvas, const TrainerModel* m) {
    const int32_t cell_w = 18, cell_h = 13, y = 46;
    int32_t x = 64 - (m->choice_count * (cell_w + 2) - 2) / 2;
    canvas_set_font(canvas, FontPrimary);
    for(uint8_t i = 0; i < m->choice_count; i++) {
        char s[2] = {m->choices[i], '\0'};
        if(i == m->choice_sel) {
            canvas_draw_box(canvas, x, y, cell_w, cell_h);
            canvas_set_color(canvas, ColorWhite);
            canvas_draw_str_aligned(
                canvas, x + cell_w / 2, y + cell_h / 2, AlignCenter, AlignCenter, s);
            canvas_set_color(canvas, ColorBlack);
        } else {
            canvas_draw_frame(canvas, x, y, cell_w, cell_h);
            canvas_draw_str_aligned(
                canvas, x + cell_w / 2, y + cell_h / 2, AlignCenter, AlignCenter, s);
        }
        x += cell_w + 2;
    }
}

static void trainer_view_draw(Canvas* canvas, void* model) {
    const TrainerModel* m = model;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 8, m->title);

    switch(m->screen) {
    case TrainerScreenTeach:
        draw_prompt(canvas, m, 24);
        if(m->pattern[0]) draw_pattern_shapes(canvas, m->pattern, 34);
        break;
    case TrainerScreenEncode:
        draw_prompt(canvas, m, 24);
        /* Live keyed symbols */
        if(m->pattern[0]) draw_pattern_shapes(canvas, m->pattern, 38);
        draw_hold_bar(canvas, m);
        break;
    case TrainerScreenPractice:
        draw_prompt(canvas, m, 22);
        if(m->pattern[0]) draw_pattern_shapes(canvas, m->pattern, 34);
        draw_hold_bar(canvas, m);
        break;
    case TrainerScreenDecode:
        draw_prompt(canvas, m, 22);
        if(m->show_pattern && m->pattern[0]) draw_pattern_shapes(canvas, m->pattern, 32);
        draw_choices(canvas, m);
        break;
    }

    if(m->hint1[0] && !m->feedback[0]) {
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, 0, 37, 128, 27);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_frame(canvas, 0, 37, 128, 27);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 46, AlignCenter, AlignCenter, m->hint1);
        if(m->hint2[0]) {
            canvas_draw_str_aligned(canvas, 64, 57, AlignCenter, AlignCenter, m->hint2);
        }
    }

    if(m->feedback[0]) {
        canvas_draw_box(canvas, 0, 51, 128, 13);
        canvas_set_color(canvas, ColorWhite);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 57, AlignCenter, AlignCenter, m->feedback);
        canvas_set_color(canvas, ColorBlack);
    } else if(
        m->footer[0] && m->screen != TrainerScreenDecode && m->key_held_ms == 0 &&
        m->hint1[0] == '\0') {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 63, AlignCenter, AlignBottom, m->footer);
    }
}

static void sidetone_start(TrainerView* tv) {
    if(!tv->settings->sound) return;
    /* Skip the tone if the player thread holds the speaker (feedback beep in
     * flight) — the keyed symbol still registers either way. */
    if(furi_hal_speaker_acquire(20)) {
        furi_hal_speaker_start(MORSE_TONE_HZ, 1.0f);
        tv->sidetone_on = true;
    }
}

static void sidetone_stop(TrainerView* tv) {
    if(tv->sidetone_on) {
        furi_hal_speaker_stop();
        furi_hal_speaker_release();
        tv->sidetone_on = false;
    }
}

static void hold_timer_callback(void* context) {
    TrainerView* tv = context;
    uint32_t held = furi_get_tick() - tv->ok_press_tick;
    if(held == 0) held = 1;
    if(held > 999) held = 999;
    with_view_model(tv->view, TrainerModel * m, { m->key_held_ms = held; }, true);
}

static void key_press_begin(TrainerView* tv) {
    tv->ok_down = true;
    tv->ok_press_tick = furi_get_tick();
    sidetone_start(tv);
    /* Real-time visual sidetone: LED holds blue exactly as long as the key
     * is down, mirroring the audio tone. */
    if(tv->settings->led) {
        notification_message(tv->notifications, &sequence_set_only_blue_255);
    }
    furi_timer_start(tv->hold_timer, furi_ms_to_ticks(33));
}

static uint32_t key_press_end(TrainerView* tv) {
    tv->ok_down = false;
    sidetone_stop(tv);
    notification_message(tv->notifications, &sequence_reset_rgb);
    furi_timer_stop(tv->hold_timer);
    with_view_model(tv->view, TrainerModel * m, { m->key_held_ms = 0; }, true);
    return furi_get_tick() - tv->ok_press_tick;
}

static bool trainer_view_input(InputEvent* event, void* context) {
    TrainerView* tv = context;
    if(event->key == InputKeyBack) {
        if(tv->ok_down) key_press_end(tv);
        sidetone_stop(tv);
        return false; /* let SceneManager handle navigation */
    }

    TrainerScreen screen = TrainerScreenTeach;
    bool frozen = false;
    with_view_model(
        tv->view,
        TrainerModel * m,
        {
            screen = m->screen;
            frozen = (m->feedback[0] != '\0');
        },
        false);

    if(frozen) return true; /* input locked while feedback shows */

    uint32_t send = 0;
    switch(screen) {
    case TrainerScreenTeach:
        if(event->type == InputTypeShort && event->key == InputKeyOk) send = MTEventTeachOk;
        if(event->type == InputTypeShort && event->key == InputKeyRight) send = MTEventTeachNext;
        break;
    case TrainerScreenEncode:
    case TrainerScreenPractice:
        if(event->key == InputKeyOk) {
            if(event->type == InputTypePress && !tv->ok_down) {
                key_press_begin(tv);
            } else if(event->type == InputTypeRelease && tv->ok_down) {
                uint32_t held = key_press_end(tv);
                send = (held < KEY_DASH_THRESHOLD_MS) ? MTEventKeyDot : MTEventKeyDash;
            }
        } else if(event->type == InputTypeShort && event->key == InputKeyLeft) {
            send = MTEventKeyClear;
        } else if(event->type == InputTypeShort && event->key == InputKeyUp) {
            /* Practice: replay what you wrote. Quiz encode: letter hint. */
            send = (screen == TrainerScreenPractice) ? MTEventReplay : MTEventHint;
        }
        break;
    case TrainerScreenDecode:
        if(event->type == InputTypeShort) {
            if(event->key == InputKeyLeft) send = MTEventChoicePrev;
            if(event->key == InputKeyRight) send = MTEventChoiceNext;
            if(event->key == InputKeyOk) send = MTEventChoiceConfirm;
            if(event->key == InputKeyUp) send = MTEventReplay;
            if(event->key == InputKeyDown) send = MTEventHint;
        }
        break;
    }

    if(send) view_dispatcher_send_custom_event(tv->view_dispatcher, send);
    return true;
}

TrainerView* trainer_view_alloc(
    ViewDispatcher* view_dispatcher,
    const MorseSettings* settings,
    NotificationApp* notifications) {
    TrainerView* tv = malloc(sizeof(TrainerView));
    tv->view_dispatcher = view_dispatcher;
    tv->settings = settings;
    tv->notifications = notifications;
    tv->ok_down = false;
    tv->sidetone_on = false;
    tv->view = view_alloc();
    view_set_context(tv->view, tv);
    view_allocate_model(tv->view, ViewModelTypeLocking, sizeof(TrainerModel));
    view_set_draw_callback(tv->view, trainer_view_draw);
    view_set_input_callback(tv->view, trainer_view_input);
    tv->hold_timer = furi_timer_alloc(hold_timer_callback, FuriTimerTypePeriodic, tv);
    return tv;
}

void trainer_view_free(TrainerView* tv) {
    furi_timer_stop(tv->hold_timer);
    furi_timer_free(tv->hold_timer);
    sidetone_stop(tv);
    view_free(tv->view); /* frees the model too */
    free(tv);
}

View* trainer_view_get_view(TrainerView* tv) {
    return tv->view;
}

void trainer_view_set_model(TrainerView* tv, const TrainerModel* src) {
    with_view_model(tv->view, TrainerModel * m, { memcpy(m, src, sizeof(TrainerModel)); }, true);
}
