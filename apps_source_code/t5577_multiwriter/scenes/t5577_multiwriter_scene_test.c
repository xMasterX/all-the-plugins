#include "../t5577_multiwriter_i.h"

#define TAG "T5577Mw"

/** Frames one run collects.
 *
 * The listener counts every frame the decoder yields, so with three keys in the stream each
 * one comes up about a third of the time and the odds of a given key never appearing in n
 * frames are (2/3)^n: about one in three thousand by twenty, one in two hundred thousand by
 * thirty. Fifty settles which keys are there several times over and leaves enough margin to
 * see one of them coming back weakly, which is the other thing worth knowing.
 *
 * A key takes two blocks of 32 bits at RF/64, 512 us a bit, so a frame lands every 33 ms, a
 * given key comes round about every 98 ms, and fifty frames take a second or two.
 *
 * It is a minimum rather than the count a run ends on - see t5577_multiwriter_test_target().
 */
#define TEST_TARGET 50

/** How often the screen catches up with the listener. Frames arrive far faster than this. */
#define TEST_POLL_MS 150

/** How long without a frame before the run is treated as over.
 *
 * Not only at the start. The end condition needs one *particular* further frame - the one that
 * lands on a cycle boundary - so "frames have been arriving" does not mean "this run will
 * finish". A three key tag lifted at 50 of 51 stops one frame short, and without this the
 * screen would sit on "Listening 50/51" for ever with its counts stranded behind Back.
 */
#define TEST_IDLE_MS 3000

static void t5577_multiwriter_scene_test_again_callback(void* context) {
    LfRfid* app = context;

    view_dispatcher_send_custom_event(app->view_dispatcher, LfRfidEventNext);
}

static void t5577_multiwriter_scene_test_start(LfRfid* app) {
    // Resets the whole model, done and idle included.
    t5577_mw_tally_view_reset(app->tally_view, TEST_TARGET);

    t5577_mw_listener_start(app->listener, TEST_TARGET);
    notification_message(app->notifications, &sequence_blink_start_cyan);

    t5577_multiwriter_step_later(app, TEST_POLL_MS);
}

static void t5577_multiwriter_scene_test_finish(LfRfid* app) {
    t5577_multiwriter_step_stop(app);
    t5577_mw_listener_stop(app->listener);
    notification_message(app->notifications, &sequence_blink_stop);
}

void t5577_multiwriter_scene_test_on_enter(void* context) {
    LfRfid* app = context;

    t5577_mw_tally_view_set_callback(
        app->tally_view, t5577_multiwriter_scene_test_again_callback, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, LfRfidViewTally);

    t5577_multiwriter_scene_test_start(app);
}

// Every number the screen showed, so a run can be read back from the log - which is the only
// record of it, input being uninjectable into the GUI on this firmware. The per-key counts are
// listed rather than averaged: an average asserts the turns were equal, which is the one thing
// a reader needs to check rather than be told.
static void t5577_multiwriter_scene_test_log(const T5577MwTally* tally) {
    FURI_LOG_I(
        TAG,
        "test: %u frames, %u keys, %u crowded, %lu dropped, quiet %lu ticks",
        tally->reads,
        tally->keys,
        tally->crowded,
        tally->dropped,
        tally->quiet_ticks);

    for(uint8_t i = 0; i < tally->keys; i++) {
        char key[EM41XX_KEY_STR_SIZE];
        em41xx_key_format(tally->key[i], key, sizeof(key));
        FURI_LOG_I(TAG, "test: key %s seen %u", key, tally->count[i]);
    }
}

static void t5577_multiwriter_scene_test_poll(LfRfid* app) {
    // Read before the tally, not after. Once this is true the listener has already stopped, so
    // the snapshot below is the count the run ended on. Sampled the other way round, a frame
    // landing between the two reads leaves the screen one short of the boundary - showing
    // 17/17/16 for the very run whose stopping rule exists to make it 17/17/17.
    const bool complete = t5577_mw_listener_complete(app->listener);

    T5577MwTally tally;
    t5577_mw_listener_tally(app->listener, &tally);

    t5577_mw_tally_view_update(
        app->tally_view, &tally, t5577_mw_listener_target(TEST_TARGET, tally.keys));

    // The listener decides when a run is complete, not this poll: it sees each frame and stops
    // on the exact one that lands on a cycle boundary. A poll at this rate sees frames four or
    // five at a time and lands past it - a three key run that should end at 51 ended at 54.
    //
    // A run that has stopped collecting is over too, boundary or not. Both endings stop the
    // listener and offer Again; only one of them claims the counts are even.
    const bool quiet = tally.quiet_ticks >= furi_ms_to_ticks(TEST_IDLE_MS);

    if(!complete && !quiet) {
        t5577_multiwriter_step_later(app, TEST_POLL_MS);
        return;
    }

    t5577_multiwriter_scene_test_finish(app);
    t5577_multiwriter_scene_test_log(&tally);

    t5577_mw_tally_view_finish(app->tally_view, complete);

    // The chime says the run ended, not that the tag is good. What the tag is doing is on the
    // screen, where a single key counted fifty times is plainly visible - and that is a failure
    // the user would otherwise be told to celebrate.
    notification_message(app->notifications, &sequence_blink_blue_100);
}

bool t5577_multiwriter_scene_test_on_event(void* context, SceneManagerEvent event) {
    LfRfid* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;

    switch(event.event) {
    case LfRfidEventStep:
        t5577_multiwriter_scene_test_poll(app);
        return true;

    case LfRfidEventNext:
        t5577_multiwriter_scene_test_start(app);
        return true;

    default:
        return false;
    }
}

void t5577_multiwriter_scene_test_on_exit(void* context) {
    LfRfid* app = context;

    t5577_multiwriter_scene_test_finish(app);
    t5577_mw_tally_view_set_callback(app->tally_view, NULL, NULL);
}
