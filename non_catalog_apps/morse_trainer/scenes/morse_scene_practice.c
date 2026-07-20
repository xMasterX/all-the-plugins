#include "morse_scene.h"
#include "../morse_trainer_i.h"

/* Free practice: key anything with OK, the app decodes it live. No score,
 * no pressure — a sandbox next to the graded levels. */

static void practice_render(MorseTrainerApp* app) {
    TrainerModel* m = &app->tm;
    memset(m, 0, sizeof(TrainerModel));
    m->screen = TrainerScreenPractice;
    strlcpy(m->title, "Practice", sizeof(m->title));
    /* Show the tail that fits the prompt field. */
    size_t len = strlen(app->practice_text);
    size_t max = sizeof(m->prompt) - 1;
    const char* tail = (len > max) ? app->practice_text + (len - max) : app->practice_text;
    strlcpy(m->prompt, tail, sizeof(m->prompt));
    m->prompt_hilite = 0xFF;
    strlcpy(m->pattern, app->practice_keyed, sizeof(m->pattern));
    strlcpy(m->footer, "OK:key  <:del  ^:hear", sizeof(m->footer));
    trainer_view_set_model(app->trainer_view, m);
}

void morse_scene_practice_on_enter(void* context) {
    MorseTrainerApp* app = context;
    app->practice_text[0] = '\0';
    app->practice_keyed[0] = '\0';
    morse_player_set_dot_ms(app->player, MORSE_DOT_MS);
    practice_render(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, MorseViewTrainer);
}

bool morse_scene_practice_on_event(void* context, SceneManagerEvent event) {
    MorseTrainerApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    switch(event.event) {
    case MTEventKeyDot:
    case MTEventKeyDash: {
        size_t len = strlen(app->practice_keyed);
        if(len < sizeof(app->practice_keyed) - 1) {
            app->practice_keyed[len] = (event.event == MTEventKeyDot) ? '.' : '-';
            app->practice_keyed[len + 1] = '\0';
        }
        furi_timer_restart(app->commit_timer, furi_ms_to_ticks(COMMIT_PAUSE_MS));
        practice_render(app);
        return true;
    }
    case MTEventKeyClear: {
        /* Left: wipe the current attempt, or delete the last decoded char. */
        size_t len = strlen(app->practice_text);
        if(app->practice_keyed[0]) {
            app->practice_keyed[0] = '\0';
            furi_timer_stop(app->commit_timer);
        } else if(len) {
            app->practice_text[len - 1] = '\0';
        }
        practice_render(app);
        return true;
    }
    case MTEventCommit: {
        if(app->practice_keyed[0] == '\0') return true;
        char c = morse_char_for(app->practice_keyed);
        if(c == 0) c = '?';
        size_t len = strlen(app->practice_text);
        if(len >= sizeof(app->practice_text) - 1) {
            /* Rolling window: drop the oldest character. */
            memmove(app->practice_text, app->practice_text + 1, len);
            len--;
        }
        app->practice_text[len] = c;
        app->practice_text[len + 1] = '\0';
        app->practice_keyed[0] = '\0';
        practice_render(app);
        return true;
    }
    case MTEventReplay:
        if(app->practice_text[0]) morse_player_play_text(app->player, app->practice_text);
        return true;
    }
    return false;
}

void morse_scene_practice_on_exit(void* context) {
    MorseTrainerApp* app = context;
    furi_timer_stop(app->commit_timer);
    morse_player_stop(app->player);
}
