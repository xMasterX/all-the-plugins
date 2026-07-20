#include "morse_scene.h"
#include "../morse_trainer_i.h"
#include <stdio.h>

static void teach_show_current(MorseTrainerApp* app, bool play) {
    const MorseLevel* level = curriculum_get(app->level);
    uint8_t count = strlen(level->new_chars);
    char c = level->new_chars[app->teach_index];

    TrainerModel* m = &app->tm;
    memset(m, 0, sizeof(TrainerModel));
    m->screen = TrainerScreenTeach;
    snprintf(
        m->title,
        sizeof(m->title),
        "Learn %u/%u",
        (unsigned)(app->teach_index + 1),
        (unsigned)count);
    m->prompt[0] = c;
    m->prompt_hilite = 0xFF;
    strlcpy(m->pattern, morse_code_for(c), sizeof(m->pattern));
    strlcpy(m->footer, "OK replay   > next", sizeof(m->footer));
    trainer_view_set_model(app->trainer_view, m);

    if(play) {
        char text[2] = {c, '\0'};
        morse_player_play_text(app->player, text);
    }
}

void morse_scene_teach_on_enter(void* context) {
    MorseTrainerApp* app = context;
    app->teach_index = 0;
    /* Teaching always at base speed; the quiz applies the L10+ ramp. */
    morse_player_set_dot_ms(app->player, MORSE_DOT_MS);
    teach_show_current(app, true);
    view_dispatcher_switch_to_view(app->view_dispatcher, MorseViewTrainer);
}

bool morse_scene_teach_on_event(void* context, SceneManagerEvent event) {
    MorseTrainerApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    const MorseLevel* level = curriculum_get(app->level);
    uint8_t count = strlen(level->new_chars);

    switch(event.event) {
    case MTEventTeachOk:
        teach_show_current(app, true);
        return true;
    case MTEventTeachNext:
        if(app->teach_index + 1 < count) {
            app->teach_index++;
            teach_show_current(app, true);
        } else {
            scene_manager_next_scene(app->scene_manager, MorseSceneQuiz);
        }
        return true;
    }
    return false;
}

void morse_scene_teach_on_exit(void* context) {
    MorseTrainerApp* app = context;
    morse_player_stop(app->player);
}
