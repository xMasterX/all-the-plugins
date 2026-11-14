#include "composer_model.h"
#include "composer_music.h"

#include <furi.h>
#include <furi_hal.h>
#include <gui/elements.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AC_CONFIG_PATH  APP_DATA_PATH("settings_v2.txt") /* v2: eight styles */
#define AC_WRITE_MS     22U /* "writing" animation: per note */
#define AC_GAP_MS       14U /* articulation gap between notes */
#define AC_TOAST_MS     1800U
#define AC_ROWS_VISIBLE 4U

typedef enum {
    AcRowStyle,
    AcRowBlend,
    AcRowAmount,
    AcRowCreativity,
    AcRowKey,
    AcRowVolume,
    AcRowCount,
} AcRow;

typedef enum {
    AcScreenStudio,
    AcScreenPlayer,
} AcScreen;

typedef enum {
    AcPhaseIdle,
    AcPhaseWriting, /* notes appear one by one */
    AcPhasePlaying,
    AcPhaseDone,
} AcPhase;

/* Creativity steps map to sampling temperatures. */
static const float ac_temperatures[] = {0.45f, 0.65f, 0.85f, 1.05f, 1.3f};
#define AC_CREATIVITY_STEPS (sizeof(ac_temperatures) / sizeof(ac_temperatures[0]))
static const char* const ac_key_names[13] =
    {"Random", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

typedef struct {
    uint8_t style; /* main style */
    uint8_t blend; /* second style, CM_STYLES = none */
    uint8_t amount; /* blend share in 10% steps, 1..9 */
    uint8_t creativity; /* 0..AC_CREATIVITY_STEPS-1 */
    uint8_t key; /* 0 random, 1..12 = C..B */
    uint8_t volume; /* 1..4 = 25..100% */
} AcSettings;

typedef struct {
    FuriMutex* mutex;
    FuriMessageQueue* input;
    NotificationApp* notification;
    AcSettings settings;
    bool settings_dirty;

    AcScreen screen;
    uint8_t row;
    uint8_t scroll;

    /* Current melody */
    CmNote notes[CM_MAX_NOTES];
    uint8_t note_count;
    uint8_t note_start[CM_MAX_NOTES]; /* in sixteenths */
    uint8_t key; /* 0..11 actually used */
    uint16_t bpm;
    char title[32];
    uint32_t seed;

    AcPhase phase;
    uint32_t phase_tick;
    uint8_t shown; /* notes revealed while writing */
    uint8_t playing; /* index of the sounding note */
    uint32_t note_tick;
    bool speaker;

    char toast[32];
    uint32_t toast_tick;
    uint16_t saved_count;

    /* Composing scratch space, kept off the app stack. */
    ComposerState model;
    CmNote draft[CM_MAX_NOTES];
    uint8_t draft_start[CM_MAX_NOTES];
} AcApp;

static uint32_t ac_ms(uint32_t since) {
    return (uint32_t)((uint64_t)(furi_get_tick() - since) * 1000U /
                      furi_kernel_get_tick_frequency());
}

/* ------------------------------------------------------------------------ */
/* Settings                                                                  */
/* ------------------------------------------------------------------------ */

static void ac_settings_default(AcSettings* s) {
    s->style = 0;
    s->blend = CM_STYLES;
    s->amount = 3;
    s->creativity = 2;
    s->key = 0;
    s->volume = 3;
}

static void ac_settings_load(AcSettings* s) {
    ac_settings_default(s);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    char buffer[64] = {0};
    if(storage_file_open(file, AC_CONFIG_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        storage_file_read(file, buffer, sizeof(buffer) - 1);
        unsigned style, blend, amount, creativity, key, volume;
        if(sscanf(
               buffer, "%u %u %u %u %u %u", &style, &blend, &amount, &creativity, &key, &volume) ==
           6) {
            if(style < CM_STYLES) s->style = (uint8_t)style;
            if(blend <= CM_STYLES) s->blend = (uint8_t)blend;
            if(amount >= 1 && amount <= 9) s->amount = (uint8_t)amount;
            if(creativity < AC_CREATIVITY_STEPS) s->creativity = (uint8_t)creativity;
            if(key <= 12) s->key = (uint8_t)key;
            if(volume >= 1 && volume <= 4) s->volume = (uint8_t)volume;
        }
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

static void ac_settings_save(const AcSettings* s) {
    char buffer[64];
    int n = snprintf(
        buffer,
        sizeof(buffer),
        "%u %u %u %u %u %u\n",
        s->style,
        s->blend,
        s->amount,
        s->creativity,
        s->key,
        s->volume);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, APP_DATA_PATH(""));
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, AC_CONFIG_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(file, buffer, (size_t)n);
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

static void ac_style_weights(const AcSettings* s, float weights[CM_STYLES]) {
    for(int i = 0; i < CM_STYLES; ++i)
        weights[i] = 0.0f;
    if(s->blend < CM_STYLES && s->blend != s->style) {
        weights[s->blend] = (float)s->amount / 10.0f;
        weights[s->style] = 1.0f - weights[s->blend];
    } else {
        weights[s->style] = 1.0f;
    }
}

/* ------------------------------------------------------------------------ */
/* Composing and playback (main thread)                                     */
/* ------------------------------------------------------------------------ */

static float ac_rand01(void* ctx) {
    return composer_xorshift01(ctx);
}

static void ac_speaker_stop(AcApp* app) {
    if(app->speaker) {
        furi_hal_speaker_stop();
        furi_hal_speaker_release();
        app->speaker = false;
    }
}

static void ac_compose(AcApp* app) {
    ac_speaker_stop(app);
    float weights[CM_STYLES];
    ac_style_weights(&app->settings, weights);
    uint32_t seed = furi_hal_random_get();
    ComposerState* state = &app->model;
    CmNote* notes = app->draft;
    uint8_t* starts = app->draft_start;
    composer_reset(state, weights);
    uint8_t count = 0, pos = 0, pitch, duration;
    while(count < CM_MAX_NOTES && composer_next(
                                      state,
                                      ac_temperatures[app->settings.creativity],
                                      ac_rand01,
                                      &seed,
                                      &pitch,
                                      &duration)) {
        notes[count] = (CmNote){.pitch = pitch, .duration = duration};
        starts[count] = pos;
        pos += cm_duration_sixteenths[duration];
        ++count;
    }

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    memcpy(app->notes, notes, sizeof(CmNote) * count);
    memcpy(app->note_start, starts, count);
    app->note_count = count;
    app->key = app->settings.key ? (uint8_t)(app->settings.key - 1) :
                                   (uint8_t)(furi_hal_random_get() % 12U);
    app->bpm = cm_mix_bpm(weights);
    if(app->settings.blend < CM_STYLES && app->settings.blend != app->settings.style) {
        snprintf(
            app->title,
            sizeof(app->title),
            "%s+%s",
            cm_style_names[app->settings.style],
            cm_style_names[app->settings.blend]);
    } else {
        snprintf(app->title, sizeof(app->title), "%s", cm_style_names[app->settings.style]);
    }
    app->screen = AcScreenPlayer;
    app->phase = AcPhaseWriting;
    app->phase_tick = furi_get_tick();
    app->shown = 0;
    app->playing = 0;
    furi_mutex_release(app->mutex);
}

static uint32_t ac_note_ms(const AcApp* app, uint8_t index) {
    /* One sixteenth = 15000 / bpm ms. */
    return (uint32_t)cm_duration_sixteenths[app->notes[index].duration] * 15000U / app->bpm;
}

static void ac_start_note(AcApp* app, uint8_t index) {
    app->playing = index;
    app->note_tick = furi_get_tick();
    if(!app->speaker) {
        app->speaker = furi_hal_speaker_acquire(500);
        if(!app->speaker) return;
    }
    int midi = cm_note_midi(app->notes[index].pitch, app->key);
    if(midi < 0) {
        furi_hal_speaker_stop();
    } else {
        furi_hal_speaker_start(cm_midi_frequency(midi), (float)app->settings.volume / 4.0f);
    }
    /* Downbeats light the LED. */
    if(app->note_start[index] % CM_BAR == 0) {
        notification_message(app->notification, &sequence_blink_green_10);
    }
}

static void ac_start_playback(AcApp* app) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->phase = AcPhasePlaying;
    app->shown = app->note_count;
    furi_mutex_release(app->mutex);
    ac_start_note(app, 0);
}

/* Advances animation and playback; returns true while something moves. */
static bool ac_tick(AcApp* app) {
    if(app->screen != AcScreenPlayer) return false;
    if(app->phase == AcPhaseWriting) {
        uint32_t shown = ac_ms(app->phase_tick) / AC_WRITE_MS;
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        app->shown = shown >= app->note_count ? app->note_count : (uint8_t)shown;
        bool finished = app->shown >= app->note_count;
        furi_mutex_release(app->mutex);
        if(finished) ac_start_playback(app);
        return true;
    }
    if(app->phase == AcPhasePlaying) {
        uint32_t elapsed = ac_ms(app->note_tick);
        uint32_t length = ac_note_ms(app, app->playing);
        if(app->speaker && elapsed + AC_GAP_MS >= length) furi_hal_speaker_stop();
        if(elapsed >= length) {
            if(app->playing + 1U < app->note_count) {
                furi_mutex_acquire(app->mutex, FuriWaitForever);
                uint8_t next = (uint8_t)(app->playing + 1U);
                furi_mutex_release(app->mutex);
                ac_start_note(app, next);
            } else {
                ac_speaker_stop(app);
                furi_mutex_acquire(app->mutex, FuriWaitForever);
                app->phase = AcPhaseDone;
                furi_mutex_release(app->mutex);
            }
        }
        return true;
    }
    return false;
}

static void ac_save_fmf(AcApp* app) {
    char* text = malloc(2048);
    if(!text) return;
    size_t length = cm_write_fmf(app->notes, app->note_count, app->key, app->bpm, text, 2048);
    bool ok = false;
    char path[96];
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, APP_DATA_PATH(""));
    for(uint16_t n = 1; n < 1000 && length; ++n) {
        snprintf(path, sizeof(path), APP_DATA_PATH("melody_%03u.fmf"), n);
        if(storage_file_exists(storage, path)) continue;
        File* file = storage_file_alloc(storage);
        if(storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_NEW)) {
            ok = storage_file_write(file, text, length) == length;
        }
        storage_file_close(file);
        storage_file_free(file);
        if(ok) app->saved_count = n;
        break;
    }
    furi_record_close(RECORD_STORAGE);
    free(text);
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    if(ok) {
        snprintf(app->toast, sizeof(app->toast), "Saved melody_%03u", app->saved_count);
    } else {
        snprintf(app->toast, sizeof(app->toast), "Could not save");
    }
    app->toast_tick = furi_get_tick();
    furi_mutex_release(app->mutex);
}

/* ------------------------------------------------------------------------ */
/* Drawing                                                                   */
/* ------------------------------------------------------------------------ */

static const char* ac_row_label(uint8_t row) {
    static const char* const labels[AcRowCount] = {
        "Style", "Blend with", "Blend", "Creativity", "Key", "Volume"};
    return labels[row];
}

static void ac_row_value(const AcApp* app, uint8_t row, char* out, size_t size) {
    const AcSettings* s = &app->settings;
    switch(row) {
    case AcRowStyle:
        snprintf(out, size, "%s", cm_style_names[s->style]);
        break;
    case AcRowBlend:
        snprintf(
            out,
            size,
            "%s",
            s->blend < CM_STYLES && s->blend != s->style ? cm_style_names[s->blend] : "Off");
        break;
    case AcRowAmount:
        snprintf(out, size, "%u%%", s->amount * 10U);
        break;
    case AcRowKey:
        snprintf(out, size, "%s", ac_key_names[s->key]);
        break;
    default:
        out[0] = '\0';
        break;
    }
}

/* Small segmented meter, e.g. creativity or volume. */
static void ac_draw_meter(Canvas* canvas, int right, int y, uint8_t value, uint8_t steps) {
    const int w = 5, gap = 2;
    int x = right - (int)steps * (w + gap) + gap;
    for(uint8_t i = 0; i < steps; ++i) {
        if(i < value) {
            canvas_draw_box(canvas, x, y - 6, w, 6);
        } else {
            canvas_draw_frame(canvas, x, y - 6, w, 6);
        }
        x += w + gap;
    }
}

static void ac_draw_studio(Canvas* canvas, AcApp* app) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "AI Composer");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 126, 10, AlignRight, AlignBottom, "OK compose");
    canvas_draw_line(canvas, 0, 13, 127, 13);

    for(uint8_t i = 0; i < AC_ROWS_VISIBLE; ++i) {
        uint8_t row = (uint8_t)(app->scroll + i);
        if(row >= AcRowCount) break;
        int y = 16 + i * 12;
        bool selected = row == app->row;
        bool disabled = row == AcRowAmount && !(app->settings.blend < CM_STYLES &&
                                                app->settings.blend != app->settings.style);
        canvas_draw_str(canvas, 4, y + 9, ac_row_label(row));
        if(row == AcRowCreativity) {
            ac_draw_meter(canvas, 120, y + 9, app->settings.creativity + 1, AC_CREATIVITY_STEPS);
        } else if(row == AcRowVolume) {
            ac_draw_meter(canvas, 120, y + 9, app->settings.volume, 4);
        } else {
            char value[24], shown[28];
            ac_row_value(app, row, value, sizeof(value));
            if(disabled) snprintf(value, sizeof(value), "-");
            snprintf(shown, sizeof(shown), selected && !disabled ? "< %s >" : "%s", value);
            canvas_draw_str_aligned(canvas, 122, y + 9, AlignRight, AlignBottom, shown);
        }
        if(selected) {
            canvas_set_color(canvas, ColorXOR);
            canvas_draw_rbox(canvas, 1, y, 124, 12, 3);
            canvas_set_color(canvas, ColorBlack);
        }
    }
    /* Scroll indicator */
    const int track = (int)AC_ROWS_VISIBLE * 12;
    int thumb = track * (int)AC_ROWS_VISIBLE / (int)AcRowCount;
    int pos = (track - thumb) * app->scroll / (int)(AcRowCount - AC_ROWS_VISIBLE);
    canvas_draw_box(canvas, 126, 16 + pos, 2, thumb);
}

/* Piano roll: 1 px per sixteenth, so 8 bars fill the 128 px width exactly. */
static void ac_draw_player(Canvas* canvas, AcApp* app) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 1, 8, app->phase == AcPhaseWriting ? "Composing..." : app->title);
    char info[24];
    snprintf(info, sizeof(info), "%s %u", ac_key_names[app->key + 1], app->bpm);
    canvas_draw_str_aligned(canvas, 127, 8, AlignRight, AlignBottom, info);

    const int top = 12, bottom = 47; /* pitch -5 at bottom, +19 at top; buttons start at y 51 */
    /* Bar lines: short ticks at the top and bottom edge only. */
    for(int x = CM_BAR; x < 128; x += CM_BAR) {
        canvas_draw_line(canvas, x, top - 1, x, top + 1);
        canvas_draw_line(canvas, x, bottom - 1, x, bottom + 1);
    }
    for(uint8_t i = 0; i < app->shown && i < app->note_count; ++i) {
        const CmNote* n = &app->notes[i];
        if(n->pitch == 0) continue;
        int x = app->note_start[i];
        int w = cm_duration_sixteenths[n->duration];
        int y = bottom - (((int)n->pitch - 1) * (bottom - top)) / (CM_PITCHES - 2);
        bool now = app->phase == AcPhasePlaying && i == app->playing;
        if(now) {
            canvas_draw_rbox(canvas, x, y - 2, w > 1 ? w : 2, 5, 1);
        } else {
            canvas_draw_box(canvas, x, y - 1, w > 1 ? w - 1 : 1, 2);
        }
    }
    if(app->phase == AcPhaseWriting && app->shown < app->note_count) {
        int x = app->note_start[app->shown];
        canvas_draw_line(canvas, x, top, x, bottom);
    } else if(app->phase == AcPhasePlaying) {
        uint32_t into = ac_ms(app->note_tick) * app->bpm / 15000U;
        int x = app->note_start[app->playing] + (int)into;
        if(x > 127) x = 127;
        canvas_draw_line(canvas, x, top, x, bottom);
    }

    canvas_draw_line(canvas, 0, 50, 127, 50);
    if(app->toast[0] && ac_ms(app->toast_tick) < AC_TOAST_MS) {
        canvas_draw_str_aligned(canvas, 64, 63, AlignCenter, AlignBottom, app->toast);
    } else {
        elements_button_left(canvas, "Save");
        elements_button_center(canvas, "New");
        elements_button_right(canvas, "Replay");
    }
}

static void ac_draw(Canvas* canvas, void* context) {
    AcApp* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    canvas_clear(canvas);
    if(app->screen == AcScreenStudio) {
        ac_draw_studio(canvas, app);
    } else {
        ac_draw_player(canvas, app);
    }
    furi_mutex_release(app->mutex);
}

static void ac_input(InputEvent* event, void* context) {
    AcApp* app = context;
    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        furi_message_queue_put(app->input, event, 0);
    }
}

/* ------------------------------------------------------------------------ */
/* Input                                                                     */
/* ------------------------------------------------------------------------ */

static void ac_change(AcApp* app, int dir) {
    AcSettings* s = &app->settings;
    switch(app->row) {
    case AcRowStyle:
        s->style = (uint8_t)((s->style + CM_STYLES + dir) % CM_STYLES);
        break;
    case AcRowBlend: {
        /* Cycle through the other styles and "Off" (= CM_STYLES). */
        uint8_t b = s->blend;
        do {
            b = (uint8_t)((b + CM_STYLES + 1 + dir) % (CM_STYLES + 1));
        } while(b == s->style);
        s->blend = b;
        break;
    }
    case AcRowAmount:
        if(s->blend < CM_STYLES) {
            int a = s->amount + dir;
            s->amount = (uint8_t)(a < 1 ? 1 : a > 9 ? 9 : a);
        }
        break;
    case AcRowCreativity: {
        const int max = (int)AC_CREATIVITY_STEPS - 1;
        int c = s->creativity + dir;
        s->creativity = (uint8_t)(c < 0 ? 0 : c > max ? max : c);
        break;
    }
    case AcRowKey:
        s->key = (uint8_t)((s->key + 13 + dir) % 13);
        break;
    case AcRowVolume: {
        int v = s->volume + dir;
        s->volume = (uint8_t)(v < 1 ? 1 : v > 4 ? 4 : v);
        break;
    }
    default:
        break;
    }
    if(s->blend == s->style) s->blend = CM_STYLES;
    app->settings_dirty = true;
}

/* Returns false to exit. */
static bool ac_handle_key(AcApp* app, InputEvent* event) {
    if(app->screen == AcScreenStudio) {
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        switch(event->key) {
        case InputKeyUp:
            if(app->row > 0) --app->row;
            break;
        case InputKeyDown:
            if(app->row + 1U < AcRowCount) ++app->row;
            break;
        case InputKeyLeft:
            ac_change(app, -1);
            break;
        case InputKeyRight:
            ac_change(app, 1);
            break;
        default:
            break;
        }
        if(app->row < app->scroll) app->scroll = app->row;
        if(app->row >= app->scroll + AC_ROWS_VISIBLE) {
            app->scroll = (uint8_t)(app->row - AC_ROWS_VISIBLE + 1U);
        }
        furi_mutex_release(app->mutex);
        if(event->key == InputKeyOk && event->type == InputTypeShort) {
            if(app->settings_dirty) {
                ac_settings_save(&app->settings);
                app->settings_dirty = false;
            }
            ac_compose(app);
        }
        return !(event->key == InputKeyBack && event->type == InputTypeShort);
    }

    /* Player */
    if(event->type != InputTypeShort) return true;
    switch(event->key) {
    case InputKeyOk:
        ac_compose(app);
        break;
    case InputKeyRight:
        if(app->phase != AcPhaseWriting) {
            ac_speaker_stop(app);
            ac_start_playback(app);
        }
        break;
    case InputKeyLeft:
        if(app->phase != AcPhaseWriting) ac_save_fmf(app);
        break;
    case InputKeyBack:
        ac_speaker_stop(app);
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        app->screen = AcScreenStudio;
        app->phase = AcPhaseIdle;
        furi_mutex_release(app->mutex);
        break;
    default:
        break;
    }
    return true;
}

int32_t ai_composer_main(void* argument) {
    UNUSED(argument);
    AcApp* app = calloc(1, sizeof(AcApp));
    if(!app) return -1;
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->input = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->notification = furi_record_open(RECORD_NOTIFICATION);
    ac_settings_load(&app->settings);

    Gui* gui = furi_record_open(RECORD_GUI);
    ViewPort* viewport = view_port_alloc();
    view_port_draw_callback_set(viewport, ac_draw, app);
    view_port_input_callback_set(viewport, ac_input, app);
    gui_add_view_port(gui, viewport, GuiLayerFullscreen);

    bool running = true;
    while(running) {
        bool busy = ac_tick(app);
        view_port_update(viewport);
        InputEvent event;
        /* 5 ms steps keep note timing tight while playing. */
        if(furi_message_queue_get(app->input, &event, busy ? 5 : 100) != FuriStatusOk) continue;
        running = ac_handle_key(app, &event);
    }

    ac_speaker_stop(app);
    if(app->settings_dirty) ac_settings_save(&app->settings);
    view_port_enabled_set(viewport, false);
    gui_remove_view_port(gui, viewport);
    view_port_free(viewport);
    furi_record_close(RECORD_GUI);
    notification_message_block(app->notification, &sequence_reset_rgb);
    furi_record_close(RECORD_NOTIFICATION);
    furi_message_queue_free(app->input);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
