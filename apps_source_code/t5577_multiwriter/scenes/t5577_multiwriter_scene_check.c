#include "../t5577_mw_tag.h"

// Step 0 finds the tag; step 1 + n reads slot n. The step is the position, as in the write and
// protect walks - which also keeps app->slot meaning only what it says it means, the slot a
// write targets, rather than doubling as this walk's cursor.
#define CHECK_STEPS (1 + EM41XX_MAX_KEYS)

void t5577_multiwriter_scene_check_on_enter(void* context) {
    LfRfid* app = context;

    app->step = 0;
    memset(app->slot_key, 0, sizeof(app->slot_key));
    memset(app->slot_state, 0, sizeof(app->slot_state)); // T5577MwSlotUnread

    t5577_mw_work_view_start(app->work_view, "Checking", CHECK_STEPS);
    t5577_mw_work_view_set_status(app->work_view, "Finding tag", 0);
    view_dispatcher_switch_to_view(app->view_dispatcher, LfRfidViewWork);

    notification_message(app->notifications, &sequence_blink_start_cyan);

    t5577_multiwriter_step_later(app, 50);
}

// Returns false once there is nothing left to read.
static bool t5577_multiwriter_scene_check_step(LfRfid* app) {
    if(app->step == 0) {
        // Spent on the one detect it was entered for, whether or not the tag answers: a word
        // the user authorised against this tag is not authorisation for the next one.
        const bool try_password = app->password_try;
        app->password_try = false;

        const T5577MwDetectMode mode = try_password ? T5577MwDetectUnconfirmed :
                                                      T5577MwDetectConfirmed;

        if(t5577_mw_tag_detect(app, mode) == T5577MwTagSilent) return false;
    } else {
        // Every slot is read, not just the ones block 0 broadcasts: a tag rewritten for fewer
        // keys keeps the rest of its blocks, and "stored but silent" is the finding that
        // explains a tag which stopped presenting a key it was definitely given.
        if(!t5577_mw_tag_read_slot(app, app->step - 1)) return false;
    }

    app->step++;

    if(app->step >= CHECK_STEPS) return false;

    char status[24];
    snprintf(status, sizeof(status), "Reading key %u", app->step);
    t5577_mw_work_view_set_status(app->work_view, status, app->step);

    return true;
}

bool t5577_multiwriter_scene_check_on_event(void* context, SceneManagerEvent event) {
    LfRfid* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event != LfRfidEventStep) return false;

    if(!t5577_multiwriter_scene_check_step(app)) {
        t5577_mw_work_view_finish(app->work_view, "Done");
        scene_manager_next_scene(app->scene_manager, LfRfidSceneCheckResult);
        return true;
    }

    t5577_multiwriter_step_later(app, 10);

    return true;
}

void t5577_multiwriter_scene_check_on_exit(void* context) {
    LfRfid* app = context;

    t5577_multiwriter_step_stop(app);
    notification_message(app->notifications, &sequence_blink_stop);
}
