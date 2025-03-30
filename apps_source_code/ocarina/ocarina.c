#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdlib.h>

#define NOTE_UP 587.33f
#define NOTE_LEFT 493.88f
#define NOTE_RIGHT 440.00f
#define NOTE_DOWN 349.23f
#define NOTE_OK 293.66f
#define SONGS_PER_PAGE 3
#define LONG_PRESS_DURATION 1000
#define INPUT_KEY_COUNT 5  // Define the total number of keys we care about

typedef struct {
    const char* name;
    const char* sequence;
    const char* symbols;
} OcarinaSong;

OcarinaSong songs[] = {
    {"Zelda's Lullaby", "Left Up Right Left Up Right", "< ^ > < ^ >"},
    {"Epona's Song", "Up Left Right Up Left Right", "^ < > ^ < >"},
    {"Saria's Song", "Down Right Left Down Right Left", "v > < v > <"},
    {"Sun's Song", "Right Down Up Right Down Up", "> v ^ > v ^"},
    {"Song of Time", "Right A Down Right A Down", "> A v > A v"},
    {"Song of Storms", "A Down Up A Down Up", "A v ^ A v ^"},
    // Adding Warp Songs from Ocarina of Time
    {"Song of Soaring", "Down Right Up Down Right Up", "v > ^ v > ^"},
    {"Prelude of Light", "Up Left Right Up Left Right", "^ < > ^ < >"},
    {"Minuet of Forest", "Left Down Right Left Down Right", "< v > < v >"},
    {"Bolero of Fire", "Right Down Left Right Down Left", "> v < > v <"},
    {"Serenade of Water", "Down Left Right Down Left Right", "v < > v < >"},
    {"Requiem of Spirit", "Right Left Left Right Up Down", "> < < > ^ v"},
    {"Nocturne of Shadow", "Up Right Down Up Right Down", "^ > v ^ > v"},
    // Adding some songs from Majora's Mask
    {"Song of Healing", "Down Right Up Down Right Up", "v > ^ v > ^"},
    {"Elegy of Emptiness", "Left Down Right Left Down Right", "< v > < v >"},
    {"Oath to Order", "Right Up Left Right Up Left", "> ^ < > ^ <"},
    // Adding additional Majora's Mask Songs
    {"Sonata of Awakening", "Right Left Down Right Left Down", "> < v > < v >"},
    {"Goron Lullaby", "Left Right Left Right Down Up", "< > < > v ^"},
    {"New Wave Bossa Nova", "Right Left Up Down Left Right", "> < ^ v < >"}
};

const int song_count = sizeof(songs) / sizeof(OcarinaSong);

typedef struct {
    FuriMutex* model_mutex;
    FuriMessageQueue* event_queue;
    ViewPort* view_port;
    Gui* gui;
    int start_index;
    uint32_t back_press_time; // Track back press time for long press detection
    bool key_down[INPUT_KEY_COUNT]; // Track the state of each key
} Ocarina;

void draw_callback(Canvas* canvas, void* ctx) {
    Ocarina* ocarina = ctx;
    furi_check(furi_mutex_acquire(ocarina->model_mutex, FuriWaitForever) == FuriStatusOk);

    canvas_draw_frame(canvas, 0, 0, 128, 64);
    canvas_draw_str(canvas, 30, 8, "Ocarina Songs");
    for (int i = 0; i < SONGS_PER_PAGE; i++) {
        int song_index = ocarina->start_index + i;
        if (song_index < song_count) {
            canvas_draw_str(canvas, 5, 20 + (i * 16), songs[song_index].name);
            canvas_draw_str(canvas, 10, 30 + (i * 16), songs[song_index].symbols);
        }
    }
    if (ocarina->start_index + SONGS_PER_PAGE < song_count)
        canvas_draw_str(canvas, 55, 58, "▼");
    if (ocarina->start_index > 0)
        canvas_draw_str(canvas, 55, 5, "▲");

    furi_mutex_release(ocarina->model_mutex);
}

void play_note(float frequency) {
    if(furi_hal_speaker_is_mine() || furi_hal_speaker_acquire(1000)) {
        furi_hal_speaker_start(frequency, 1.0f);
    }
}

void input_callback(InputEvent* input, void* ctx) {
    Ocarina* ocarina = ctx;
    furi_message_queue_put(ocarina->event_queue, input, FuriWaitForever);
}

Ocarina* ocarina_alloc() {
    Ocarina* instance = malloc(sizeof(Ocarina));
    instance->model_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    instance->event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    instance->view_port = view_port_alloc();
    view_port_draw_callback_set(instance->view_port, draw_callback, instance);
    view_port_input_callback_set(instance->view_port, input_callback, instance);
    instance->gui = furi_record_open("gui");
    gui_add_view_port(instance->gui, instance->view_port, GuiLayerFullscreen);
    instance->start_index = 0;
    memset(instance->key_down, 0, sizeof(instance->key_down));  // Initialize the key states to false
    return instance;
}

void ocarina_free(Ocarina* instance) {
    view_port_enabled_set(instance->view_port, false);
    gui_remove_view_port(instance->gui, instance->view_port);
    furi_record_close("gui");
    view_port_free(instance->view_port);
    furi_message_queue_free(instance->event_queue);
    furi_mutex_free(instance->model_mutex);
    if(furi_hal_speaker_is_mine()) {
        furi_hal_speaker_stop();
        furi_hal_speaker_release();
    }
    free(instance);
}

int32_t ocarina_app(void* p) {
    UNUSED(p);
    Ocarina* ocarina = ocarina_alloc();
    InputEvent event;
    bool processing = true;
    while (processing) {
        FuriStatus status = furi_message_queue_get(ocarina->event_queue, &event, 100);
        furi_check(furi_mutex_acquire(ocarina->model_mutex, FuriWaitForever) == FuriStatusOk);

        if (status == FuriStatusOk) {
            if (event.type == InputTypePress || event.type == InputTypeLong || event.type == InputTypeRepeat) {
                switch (event.key) {
                    case InputKeyUp:
                        if (!ocarina->key_down[InputKeyUp]) {
                            ocarina->key_down[InputKeyUp] = true;
                            play_note(NOTE_UP);
                        }
                        break;
                    case InputKeyDown:
                        if (!ocarina->key_down[InputKeyDown]) {
                            ocarina->key_down[InputKeyDown] = true;
                            play_note(NOTE_DOWN);
                        }
                        break;
                    case InputKeyLeft:
                        if (!ocarina->key_down[InputKeyLeft]) {
                            ocarina->key_down[InputKeyLeft] = true;
                            play_note(NOTE_LEFT);
                        }
                        break;
                    case InputKeyRight:
                        if (!ocarina->key_down[InputKeyRight]) {
                            ocarina->key_down[InputKeyRight] = true;
                            play_note(NOTE_RIGHT);
                        }
                        break;
                    case InputKeyOk:
                        if (!ocarina->key_down[InputKeyOk]) {
                            ocarina->key_down[InputKeyOk] = true;
                            play_note(NOTE_OK);
                        }
                        break;
                    case InputKeyBack:
                        if (event.type == InputTypeLong) {
                            processing = false; // Exit app on long press of back button
                        } else if (event.type == InputTypePress) {
                            // Back button scroll functionality
                            if (ocarina->start_index < song_count - SONGS_PER_PAGE) {
                                ocarina->start_index++;
                            } else {
                                ocarina->start_index = 0;  // Cycle back to the top
                            }
                        }
                        break;
                    default:
                        break;
                }
            } else if (event.type == InputTypeRelease) {
                switch (event.key) {
                    case InputKeyUp:
                        ocarina->key_down[InputKeyUp] = false;
                        break;
                    case InputKeyDown:
                        ocarina->key_down[InputKeyDown] = false;
                        break;
                    case InputKeyLeft:
                        ocarina->key_down[InputKeyLeft] = false;
                        break;
                    case InputKeyRight:
                        ocarina->key_down[InputKeyRight] = false;
                        break;
                    case InputKeyOk:
                        ocarina->key_down[InputKeyOk] = false;
                        break;
                    default:
                        break;
                }
                if (furi_hal_speaker_is_mine()) {
                    furi_hal_speaker_stop();
                    furi_hal_speaker_release();
                }
            }
        }

        furi_mutex_release(ocarina->model_mutex);
        view_port_update(ocarina->view_port);
    }

    ocarina_free(ocarina);
    return 0;
}
