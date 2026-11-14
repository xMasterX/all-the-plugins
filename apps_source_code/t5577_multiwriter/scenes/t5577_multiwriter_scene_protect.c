#include "../t5577_mw_tag.h"

/* Turning protection on and off are the same walk with different blocks in it, so they share a
 * scene and differ by app->protect_op.
 *
 * Locking writes block 7 before block 0, and checks it in between. That order is the whole of
 * doing this safely: block 0's password bit is what makes the chip ask, and a chip asking for a
 * word that never reached block 7 is a tag nothing can write again. So the walk stops at the
 * check rather than turning protection on over an unproven password.
 *
 * Neither of the *locking* writes carries a password. Until block 0 lands, the chip is not
 * protected and does not want one - and after block 7 has changed, the word it would ask for is
 * the new one anyway. Unlocking is the other way about: its single write must be addressed with
 * the password, because the chip is protected at that point by definition.
 */

typedef enum {
    LockFind, // find the tag; it must not already be locked
    LockWritePassword, // block 7 takes the word
    LockRefind, // the write cycled the field, so find it again
    LockCheckPassword, // and prove block 7 holds it before anything depends on that
    LockWriteConfig, // block 0 turns protection on
    LockRefindLocked, // which means the tag now answers only to the password
    LockCheckConfig,
    LockSteps,
} LockStep;

typedef enum {
    UnlockFind, // find the tag, which takes the password to do
    UnlockWriteConfig, // block 0 drops the password bit
    UnlockRefind, // so the tag answers without one again
    UnlockCheckConfig,
    UnlockSteps,
} UnlockStep;

typedef enum {
    ProtectOutcomeMore,
    ProtectOutcomeDone,
    ProtectOutcomeAbandoned,
} ProtectOutcome;

// One label per step, so the screen names what is happening rather than guessing from whether
// anything has been written yet. The two walks are different lengths, hence two tables.
static const char* const t5577_multiwriter_lock_labels[LockSteps] = {
    "Finding tag",
    "Writing password",
    "Finding tag",
    "Checking password",
    "Turning lock on",
    "Finding tag",
    "Checking lock",
};

static const char* const t5577_multiwriter_unlock_labels[UnlockSteps] = {
    "Finding tag",
    "Turning lock off",
    "Finding tag",
    "Checking lock",
};

static void t5577_multiwriter_scene_protect_say(LfRfid* app) {
    const bool locking = app->protect_op == T5577MwProtectLock;
    const uint8_t steps = locking ? LockSteps : UnlockSteps;

    if(app->step >= steps) return;

    t5577_mw_work_view_set_status(
        app->work_view,
        locking ? t5577_multiwriter_lock_labels[app->step] :
                  t5577_multiwriter_unlock_labels[app->step],
        app->step);
}

static ProtectOutcome t5577_multiwriter_scene_protect_lock(LfRfid* app) {
    switch(app->step) {
    case LockFind:
        // Confirmed only. Locking an already-locked tag is refused below anyway, so there is
        // never a reason for this step to send a word the tag has not already asked for.
        if(t5577_mw_tag_detect(app, T5577MwDetectConfirmed) == T5577MwTagSilent) {
            return ProtectOutcomeDone;
        }
        if(t5577_mw_tag_locked(app)) {
            t5577_multiwriter_say(
                app, "Already locked", "Unlock it first, or it\nwould take two words\nto write");
            return ProtectOutcomeAbandoned;
        }
        break;

    case LockWritePassword:
        app->tag_written = true;
        t5577_write_block(0, LFRFID_T5577_PASSWORD_BLOCK, app->password, false, 0);
        break;

    case LockRefind:
        // Plain: the tag was open a moment ago and block 0 has not been touched yet, so it is
        // still open. A password frame here could only reach a tag that wants none.
        if(t5577_mw_tag_detect(app, T5577MwDetectPlain) == T5577MwTagSilent) {
            return ProtectOutcomeDone;
        }
        break;

    case LockCheckPassword: {
        // Its own local: write_actual belongs to the write walk, and storing block 7 there
        // would overwrite that walk's block 0 value with something nothing here reads.
        uint32_t actual = 0;
        app->protect_result[0] =
            t5577_mw_tag_check_block(app, LFRFID_T5577_PASSWORD_BLOCK, app->password, &actual);
        // The one place the walk refuses to go on. Turning protection on now would leave the
        // chip asking for a word it does not hold.
        if(app->protect_result[0] != T5577BlockHeld) return ProtectOutcomeDone;
        break;
    }

    case LockWriteConfig:
        // The tag's own block 0 with the password bit added, rather than a word composed here:
        // locking must not also change what the tag broadcasts.
        app->protect_armed = true;
        t5577_write_block(0, 0, app->config.block0 | LFRFID_T5577_PWD, false, 0);
        break;

    case LockRefindLocked:
        // The one step that must be allowed to send an untried word, because a tag that locked
        // correctly cannot be read any other way. It is earned rather than assumed: this app
        // wrote that exact word into block 7 and read it back two steps ago, and the plain
        // detect inside t5577_mw_tag_detect() runs first - so a tag that is still open answers
        // that and is reported as a refused lock before any password frame is sent.
        if(t5577_mw_tag_detect(app, T5577MwDetectUnconfirmed) == T5577MwTagSilent) {
            return ProtectOutcomeDone;
        }
        break;

    case LockCheckConfig:
        app->protect_result[1] = t5577_mw_tag_locked(app) ? T5577BlockHeld : T5577BlockMismatch;
        return ProtectOutcomeDone;

    default:
        return ProtectOutcomeDone;
    }

    app->step++;

    return ProtectOutcomeMore;
}

static ProtectOutcome t5577_multiwriter_scene_protect_unlock(LfRfid* app) {
    switch(app->step) {
    case UnlockFind:
        // Unconfirmed is allowed here only because the menu has already shown the warning: a
        // locked tag cannot be found any other way, and the word may well be one the user has
        // just typed and no tag has answered to yet.
        if(t5577_mw_tag_detect(app, T5577MwDetectUnconfirmed) == T5577MwTagSilent) {
            return ProtectOutcomeDone;
        }
        if(!t5577_mw_tag_locked(app)) {
            t5577_multiwriter_say(
                app, "Not locked", "This tag already takes\nwrites without a\npassword");
            return ProtectOutcomeAbandoned;
        }
        break;

    case UnlockWriteConfig:
        app->tag_written = true;
        // Answer-on-request goes too. This app never sets it, and a tag carrying it stays silent
        // to an ordinary reader - which is the one thing a multi-key tag must not be.
        t5577_write_block(0, 0, app->config.block0 & ~EM41XX_CONFIG_TAG_BITS, true, app->password);
        break;

    case UnlockRefind:
        // The plain detect inside this is what proves the unlock: answering without a password
        // is exactly what unlocked means. The password fallback is for the other answer - a tag
        // that still only responds to the word has refused the change and is sitting right
        // there, which must not be reported as a tag that vanished.
        if(t5577_mw_tag_detect(app, T5577MwDetectUnconfirmed) == T5577MwTagSilent) {
            return ProtectOutcomeDone;
        }
        break;

    case UnlockCheckConfig:
        app->protect_result[1] = t5577_mw_tag_locked(app) ? T5577BlockMismatch : T5577BlockHeld;
        return ProtectOutcomeDone;

    default:
        return ProtectOutcomeDone;
    }

    app->step++;

    return ProtectOutcomeMore;
}

void t5577_multiwriter_scene_protect_on_enter(void* context) {
    LfRfid* app = context;

    const bool locking = app->protect_op == T5577MwProtectLock;

    app->step = 0;
    app->tag_written = false;
    app->protect_armed = false;
    app->protect_result[0] = T5577BlockHeld; // only locking checks block 7
    app->protect_result[1] = T5577BlockNoAnswer;

    t5577_mw_work_view_start(
        app->work_view, locking ? "Locking" : "Unlocking", locking ? LockSteps : UnlockSteps);
    t5577_multiwriter_scene_protect_say(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, LfRfidViewWork);

    notification_message(app->notifications, &sequence_blink_start_magenta);

    t5577_multiwriter_step_later(app, 50);
}

bool t5577_multiwriter_scene_protect_on_event(void* context, SceneManagerEvent event) {
    LfRfid* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event != LfRfidEventStep) return false;

    const ProtectOutcome outcome = (app->protect_op == T5577MwProtectLock) ?
                                       t5577_multiwriter_scene_protect_lock(app) :
                                       t5577_multiwriter_scene_protect_unlock(app);

    switch(outcome) {
    case ProtectOutcomeMore:
        t5577_multiwriter_scene_protect_say(app);
        t5577_multiwriter_step_later(app, 10);
        break;
    case ProtectOutcomeDone:
        t5577_mw_work_view_finish(app->work_view, "Done");
        scene_manager_next_scene(app->scene_manager, LfRfidSceneProtectResult);
        break;
    case ProtectOutcomeAbandoned:
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, LfRfidSceneStart);
        break;
    }

    return true;
}

void t5577_multiwriter_scene_protect_on_exit(void* context) {
    LfRfid* app = context;

    t5577_multiwriter_step_stop(app);
    notification_message(app->notifications, &sequence_blink_stop);
}
