#include "morse_scene.h"
#include "../morse_trainer_i.h"

static const char* const mode_names[MODE_COUNT] = {"Encoding", "Decoding", "Mixed"};

void morse_scene_progress_on_enter(void* context) {
    MorseTrainerApp* app = context;
    widget_reset(app->widget);

    FuriString* text = furi_string_alloc();
    furi_string_cat_printf(
        text,
        "Streak: %u day(s)\nAnswered: %lu (%lu ok)\n\n",
        (unsigned)app->progress.streak,
        (unsigned long)app->progress.total_answered,
        (unsigned long)app->progress.total_correct);
    for(uint8_t m = 0; m < MODE_COUNT; m++) {
        furi_string_cat_printf(text, "= %s =\n", mode_names[m]);
        uint8_t unlocked = app->progress.unlocked[m];
        for(uint8_t l = 0; l < LEVEL_COUNT; l++) {
            if(l < unlocked) {
                char stars[4] = "---";
                for(uint8_t s = 0; s < app->progress.stars[m][l]; s++)
                    stars[s] = '*';
                furi_string_cat_printf(text, "L%u: %s\n", (unsigned)(l + 1), stars);
            } else {
                furi_string_cat_printf(text, "L%u: locked\n", (unsigned)(l + 1));
            }
        }
        furi_string_cat_str(text, "\n");
    }
    widget_add_text_scroll_element(app->widget, 0, 0, 128, 64, furi_string_get_cstr(text));
    furi_string_free(text);

    view_dispatcher_switch_to_view(app->view_dispatcher, MorseViewWidget);
}

bool morse_scene_progress_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false; /* Back pops to the menu via SceneManager history */
}

void morse_scene_progress_on_exit(void* context) {
    MorseTrainerApp* app = context;
    widget_reset(app->widget);
}
