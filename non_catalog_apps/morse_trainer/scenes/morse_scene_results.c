#include "morse_scene.h"
#include "../morse_trainer_i.h"
#include <furi_hal_rtc.h>
#include <stdio.h>

static void results_button_callback(GuiButtonType result, InputType type, void* context) {
    MorseTrainerApp* app = context;
    if(result == GuiButtonTypeCenter && type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->view_dispatcher, MTEventResultsOk);
    }
}

void morse_scene_results_on_enter(void* context) {
    MorseTrainerApp* app = context;
    QuizState* q = &app->quiz;

    /* Stars are only ever raised; passing the newest level unlocks the next. */
    if(q->passed) {
        if(q->stars > app->progress.stars[app->mode][app->level]) {
            app->progress.stars[app->mode][app->level] = q->stars;
        }
        if(app->progress.unlocked[app->mode] == app->level + 1 &&
           app->progress.unlocked[app->mode] < LEVEL_COUNT) {
            app->progress.unlocked[app->mode]++;
        }
    }
    /* Daily streak: any completed run (pass or fail) counts as practice. */
    uint32_t today = furi_hal_rtc_get_timestamp() / 86400;
    if(app->progress.last_epoch_day != today) {
        app->progress.streak =
            (today == app->progress.last_epoch_day + 1) ? app->progress.streak + 1 : 1;
        app->progress.last_epoch_day = today;
    }
    /* Save unconditionally: stats and miss counts changed even on a fail. */
    morse_progress_save(&app->progress);

    widget_reset(app->widget);
    char line[40];
    if(q->passed) {
        widget_add_string_element(
            app->widget, 64, 8, AlignCenter, AlignTop, FontPrimary, "Level passed!");
        char stars[8] = "";
        for(uint8_t s = 0; s < q->stars; s++)
            strlcat(stars, "* ", sizeof(stars));
        widget_add_string_element(app->widget, 64, 22, AlignCenter, AlignTop, FontPrimary, stars);
    } else {
        widget_add_string_element(
            app->widget, 64, 8, AlignCenter, AlignTop, FontPrimary, "Failed - try again!");
    }
    if(q->challenge) {
        snprintf(line, sizeof(line), "Mistakes: %u", (unsigned)q->mistakes);
    } else {
        snprintf(
            line,
            sizeof(line),
            "Score: %u/%u (pass: %u)",
            (unsigned)q->correct,
            (unsigned)q->total_questions,
            (unsigned)q->pass_score);
    }
    widget_add_string_element(app->widget, 64, 36, AlignCenter, AlignTop, FontSecondary, line);
    widget_add_button_element(
        app->widget, GuiButtonTypeCenter, "OK", results_button_callback, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, MorseViewWidget);
}

bool morse_scene_results_on_event(void* context, SceneManagerEvent event) {
    MorseTrainerApp* app = context;
    bool leave = false;
    if(event.type == SceneManagerEventTypeBack) leave = true;
    if(event.type == SceneManagerEventTypeCustom && event.event == MTEventResultsOk) leave = true;
    if(leave) {
        scene_manager_search_and_switch_to_previous_scene(
            app->scene_manager, MorseSceneLevelSelect);
        return true;
    }
    return false;
}

void morse_scene_results_on_exit(void* context) {
    MorseTrainerApp* app = context;
    widget_reset(app->widget);
}
