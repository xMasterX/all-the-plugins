#include "../t5577_mw_tag.h"

#define TAG "T5577Mw"

// Nothing answered. Which of the walk's silences this is decides what the user has to do next,
// and the two are not close: one leaves a tag that is fine, the other leaves a tag that may now
// be protected with a word nothing has yet confirmed.
static void t5577_multiwriter_scene_protect_result_silent(LfRfid* app, bool locking) {
    if(!app->tag_written) {
        t5577_multiwriter_show_error(
            app,
            "No tag",
            locking ? "Nothing was changed.\nHold the tag still" :
                      "Nothing was changed.\nHold it still, or that\nis the wrong password");
        return;
    }

    if(locking && app->protect_armed) {
        // Block 0 was written with the password bit before the tag went quiet, so it probably
        // took. Telling the user to check the tag is not an action; telling them not to lose
        // the password is.
        t5577_multiwriter_show_error(
            app,
            "Lock unconfirmed",
            "The tag may now need\nthis password. Do not\nforget it - check the tag");
        return;
    }

    if(locking) {
        // Only block 7 was written. The password bit was never set, so the tag still takes
        // writes - but block 7 no longer holds whatever it held before.
        t5577_multiwriter_show_error(
            app,
            "Tag lost",
            "The lock was not turned\non, so the tag is still\nwritable. Block 7 changed");
        return;
    }

    t5577_multiwriter_show_error(
        app, "Tag lost", "Block 0 was sent but\nnothing read back.\nCheck the tag");
}

void t5577_multiwriter_scene_protect_result_on_enter(void* context) {
    LfRfid* app = context;

    const bool locking = app->protect_op == T5577MwProtectLock;

    if(!t5577_config_found(&app->config)) {
        t5577_multiwriter_scene_protect_result_silent(app, locking);
        return;
    }

    // The deliberate stop: block 7 did not come back holding the word, so block 0 was never
    // written and the tag still takes writes. What is NOT true is that nothing happened -
    // block 7 was written, so whatever it held before is gone either way.
    if(locking && app->protect_result[0] != T5577BlockHeld) {
        if(app->protect_result[0] == T5577BlockNoAnswer) {
            t5577_multiwriter_show_error(
                app,
                "Not locked",
                "Block 7 would not read\nback, so the lock was\nleft off. It may hold it");
        } else {
            t5577_multiwriter_show_error(
                app,
                "Not locked",
                "Block 7 holds a different\nword, so the lock was\nleft off. Try another");
        }
        return;
    }

    if(app->protect_result[1] != T5577BlockHeld) {
        t5577_multiwriter_show_error(
            app,
            locking ? "Lock refused" : "Unlock refused",
            locking ? "Block 7 holds the word\nbut block 0 would not\nturn the lock on" :
                      "Block 0 still asks for\na password. The tag\nrefused the change");
        return;
    }

    t5577_multiwriter_show_success(app, locking ? "Locked!" : "Unlocked");
}

bool t5577_multiwriter_scene_protect_result_on_event(void* context, SceneManagerEvent event) {
    return t5577_multiwriter_leave_to_start(context, event);
}

void t5577_multiwriter_scene_protect_result_on_exit(void* context) {
    LfRfid* app = context;

    popup_reset(app->popup);
    widget_reset(app->widget);
}
