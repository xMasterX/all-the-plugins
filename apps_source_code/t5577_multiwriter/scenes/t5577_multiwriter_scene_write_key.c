#include "../t5577_mw_tag.h"

typedef enum {
    WriteStepFind, // find the tag, and settle what writing this slot would cost
    WriteStepSend, // put the three blocks on the wire
    WriteStepRefind, // find it again, which also hands back block 0
    WriteStepHigh, // read the slot's first block back
    WriteStepLow, // and its second
} WriteStep;

#define WRITE_STEPS 5

// A step either leads to another, finishes the job, or walks away from it. Finishing and
// walking away are not the same ending: a tag that was never found has a result worth showing,
// a user who answered "no" to the warning has none.
typedef enum {
    WriteOutcomeMore,
    WriteOutcomeDone,
    WriteOutcomeAbandoned,
} WriteOutcome;

// What the tag already holds decides whether this write is the one the user meant. Both
// questions are about block 0's maxblock, so both are asked here, once, before anything is sent.
static bool t5577_multiwriter_scene_write_key_confirm(LfRfid* app) {
    if(t5577_mw_tag_foreign(app)) {
        return t5577_multiwriter_ask(
            app,
            "Not a multiwriter tag",
            "Block 0 is not one of\nours. Writing replaces\nwhatever it holds",
            "Write");
    }

    // Writing slot N sets maxblock to N+1 keys, so every key after it stops being broadcast.
    // The blocks keep their contents, which is why this says hidden rather than erased.
    if(t5577_mw_tag_keys(app) > app->slot + 1) {
        return t5577_multiwriter_ask(
            app,
            "Hides later keys",
            "The tag broadcasts more\nkeys than this. They stay\nstored but go silent",
            "Write");
    }

    // The other way round: the slots before this one have never been written, so the tag would
    // broadcast whatever those blocks happen to hold ahead of the key being written now.
    if(app->slot > t5577_mw_tag_keys(app)) {
        return t5577_multiwriter_ask(
            app,
            "Earlier keys missing",
            "Slots before this one are\nempty. The tag will send\ntheir contents as keys",
            "Write");
    }

    return true;
}

static void t5577_multiwriter_scene_write_key_send(LfRfid* app) {
    LFRFIDT5577 data = {0};

    // Checked rather than ignored: a refused slot would leave the mask empty and send block 0
    // alone, announcing a key count over blocks that were never written - and the read-back
    // would then blame the tag for a write this app skipped. Both only fail on a slot number
    // that cannot come from the menu, so this is a programmer error, not a tag one.
    furi_check(add_em41xx_data(&data, app->key, em41xx_slot_block(app->slot)));

    // The configuration carries forward whatever protection bits the tag was found with. Those
    // come from the block 0 read *before* this write, which is what app->config still holds.
    furi_check(set_em41xx_config(&data, app->slot + 1, app->config.block0));

    // Kept, not recomposed at verification time: rebuilding the expected word afterwards reads
    // the protection bits back out of the very block being checked, which compares them against
    // themselves, so a lock that silently dropped would verify as correct.
    const uint8_t block = em41xx_slot_block(app->slot);
    app->write_block0 = data.block[0];
    app->write_high = data.block[block];
    app->write_low = data.block[block + 1];

    t5577_write_with_mask(&data, 0, t5577_mw_tag_locked(app) && app->password_set, app->password);
}

void t5577_multiwriter_scene_write_key_on_enter(void* context) {
    LfRfid* app = context;

    app->step = WriteStepFind;
    app->tag_written = false;
    app->write_block0 = 0;
    for(uint8_t i = 0; i < T5577_MW_WRITE_BLOCKS; i++) {
        app->write_result[i] = T5577BlockNoAnswer;
        app->write_actual[i] = 0;
    }

    t5577_mw_work_view_start(app->work_view, "Writing", WRITE_STEPS);
    t5577_mw_work_view_set_status(app->work_view, "Finding tag", 0);
    view_dispatcher_switch_to_view(app->view_dispatcher, LfRfidViewWork);

    notification_message(app->notifications, &sequence_blink_start_magenta);

    // Long enough for the screen to be up before the first detect takes the thread for a second.
    t5577_multiwriter_step_later(app, 50);
}

static WriteOutcome t5577_multiwriter_scene_write_key_step(LfRfid* app) {
    const uint8_t block = em41xx_slot_block(app->slot);

    switch(app->step) {
    case WriteStepFind:
        // Confirmed only: a write is not the place to try a guess, and the word is only sent
        // at all if a plain detect has already found nothing.
        if(t5577_mw_tag_detect(app, T5577MwDetectConfirmed) == T5577MwTagSilent) {
            return WriteOutcomeDone;
        }
        // Normally unreachable - a tag that asks for a password only answers a detect addressed
        // with it, so getting here means the app holds one. A clone that answers a plain read
        // while still refusing writes would land here, and a doomed write is worse than saying
        // why. Answer-on-request lands here too, hence "needs" rather than "is locked".
        if(t5577_mw_tag_locked(app) && !app->password_set) {
            t5577_multiwriter_say(
                app, "Tag needs a password", "Enter it first, or this\nwrite will be refused");
            return WriteOutcomeAbandoned;
        }
        if(!t5577_multiwriter_scene_write_key_confirm(app)) return WriteOutcomeAbandoned;
        t5577_mw_work_view_set_status(app->work_view, "Writing blocks", 1);
        break;

    case WriteStepSend:
        app->tag_written = true;
        t5577_multiwriter_scene_write_key_send(app);
        t5577_mw_work_view_set_status(app->work_view, "Reading back", 2);
        break;

    case WriteStepRefind: {
        // The write cycled the field, so the alignment the first detect found is spent and the
        // tag has to be found again. It hands back block 0 as it now stands, which is the whole
        // of checking the configuration - no read of its own.
        //
        // Plain, with no fallback: the tag this write went to was found without a password a
        // moment ago, so a password frame here could only reach a tag that wants none.
        const T5577MwDetectMode mode = t5577_mw_tag_locked(app) ? T5577MwDetectConfirmed :
                                                                  T5577MwDetectPlain;

        if(t5577_mw_tag_detect(app, mode) == T5577MwTagSilent) return WriteOutcomeDone;

        app->write_actual[0] = app->config.block0;
        app->write_result[0] = (app->config.block0 == app->write_block0) ? T5577BlockHeld :
                                                                           T5577BlockMismatch;
        t5577_mw_work_view_set_status(app->work_view, "Checking block 1", 3);
        break;
    }

    case WriteStepHigh:
        app->write_result[1] =
            t5577_mw_tag_check_block(app, block, app->write_high, &app->write_actual[1]);
        t5577_mw_work_view_set_status(app->work_view, "Checking block 2", 4);
        break;

    case WriteStepLow:
        app->write_result[2] =
            t5577_mw_tag_check_block(app, block + 1, app->write_low, &app->write_actual[2]);
        return WriteOutcomeDone;

    default:
        return WriteOutcomeDone;
    }

    app->step++;

    return WriteOutcomeMore;
}

bool t5577_multiwriter_scene_write_key_on_event(void* context, SceneManagerEvent event) {
    LfRfid* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event != LfRfidEventStep) return false;

    switch(t5577_multiwriter_scene_write_key_step(app)) {
    case WriteOutcomeMore:
        t5577_multiwriter_step_later(app, 10);
        break;
    case WriteOutcomeDone:
        t5577_mw_work_view_finish(app->work_view, "Done");
        scene_manager_next_scene(app->scene_manager, LfRfidSceneWriteResult);
        break;
    case WriteOutcomeAbandoned:
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, LfRfidSceneStart);
        break;
    }

    return true;
}

void t5577_multiwriter_scene_write_key_on_exit(void* context) {
    LfRfid* app = context;

    t5577_multiwriter_step_stop(app);
    notification_message(app->notifications, &sequence_blink_stop);
}
