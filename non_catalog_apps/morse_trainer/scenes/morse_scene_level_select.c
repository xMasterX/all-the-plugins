#include "morse_scene.h"
#include "../morse_trainer_i.h"
#include <stdio.h>

static const char* const mode_names[MODE_COUNT] = {"Encoding", "Decoding", "Mixed"};

static void level_callback(void* context, uint32_t index) {
    MorseTrainerApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void morse_scene_level_select_on_enter(void* context) {
    MorseTrainerApp* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, mode_names[app->mode]);
    uint8_t unlocked = app->progress.unlocked[app->mode];
    for(uint8_t i = 0; i < unlocked && i < LEVEL_COUNT; i++) {
        const MorseLevel* level = curriculum_get(i);
        uint8_t stars = app->progress.stars[app->mode][i];
        char label[32];
        char star_str[4] = "   ";
        for(uint8_t s = 0; s < stars && s < 3; s++)
            star_str[s] = '*';
        snprintf(label, sizeof(label), "L%u %s %s", (unsigned)(i + 1), star_str, level->label);
        submenu_add_item(app->submenu, label, i, level_callback, app);
    }
    submenu_set_selected_item(app->submenu, unlocked > 0 ? unlocked - 1 : 0);
    view_dispatcher_switch_to_view(app->view_dispatcher, MorseViewSubmenu);
}

bool morse_scene_level_select_on_event(void* context, SceneManagerEvent event) {
    MorseTrainerApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event >= LEVEL_COUNT) return false;
    app->level = event.event;
    /* Challenge levels have no new letters to teach, so skip the teach step. */
    if(curriculum_is_challenge(app->level)) {
        scene_manager_next_scene(app->scene_manager, MorseSceneQuiz);
    } else {
        scene_manager_next_scene(app->scene_manager, MorseSceneTeach);
    }
    return true;
}

void morse_scene_level_select_on_exit(void* context) {
    MorseTrainerApp* app = context;
    submenu_reset(app->submenu);
}
