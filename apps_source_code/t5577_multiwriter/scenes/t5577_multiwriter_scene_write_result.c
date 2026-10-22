#include "../t5577_mw_tag.h"
#include <dolphin/dolphin.h>

#define TAG "T5577Mw"

// Which block each write_result entry is about, for naming the one that did not take.
static uint8_t t5577_multiwriter_result_block(const LfRfid* app, uint8_t index) {
    return (index == 0) ? 0 : em41xx_slot_block(app->slot) + (index - 1);
}

static void t5577_multiwriter_scene_write_result_ok(LfRfid* app) {
    FURI_LOG_I(TAG, "key %u verified", app->slot + 1);
    t5577_multiwriter_show_success(app, "Written!");
    dolphin_deed(DolphinDeedRfidEmulate);
}

void t5577_multiwriter_scene_write_result_on_enter(void* context) {
    LfRfid* app = context;

    if(!t5577_config_found(&app->config)) {
        if(app->tag_written) {
            // The blocks went out and the tag was gone before any of them could be read back,
            // so what it holds now is genuinely unknown - which is worth saying rather than
            // reporting a write that may well have landed as a failure.
            t5577_multiwriter_show_error(
                app, "Tag lost", "Blocks were sent but\nnone read back.\nCheck the tag");
        } else {
            t5577_multiwriter_show_error(
                app, "No tag", "Nothing was written.\nHold it still, or it\nneeds a password");
        }
        return;
    }

    bool quiet = false;
    uint8_t refused = T5577_MW_WRITE_BLOCKS;
    for(uint8_t i = 0; i < T5577_MW_WRITE_BLOCKS; i++) {
        if(app->write_result[i] == T5577BlockNoAnswer) quiet = true;
        if(app->write_result[i] == T5577BlockMismatch && refused == T5577_MW_WRITE_BLOCKS) {
            refused = i;
        }
    }

    // A refusal is reported ahead of a block that went quiet, and names both. A tag the user
    // simply has to hold still invites a retry; a tag that refused a block has changed, and
    // retrying without knowing that is how a half-written tag gets carried away.
    if(refused < T5577_MW_WRITE_BLOCKS) {
        char text[96];
        const uint8_t block = t5577_multiwriter_result_block(app, refused);

        // What the block holds is known, so the two cases are told apart rather than guessed
        // at: a block still holding the old key leaves the tag usable, a block holding neither
        // word leaves the slot broadcasting data that is not a key.
        if(refused == 0) {
            snprintf(
                text,
                sizeof(text),
                "Block 0 came back as\n%08lX, not %08lX.\nKey count may be wrong",
                app->write_actual[0],
                app->write_block0);
        } else {
            snprintf(
                text,
                sizeof(text),
                "Block %u holds neither\nkey. That slot is now\nunreadable data",
                block);
        }

        t5577_multiwriter_show_error(app, quiet ? "Refused, tag moved" : "Write refused", text);
        return;
    }

    if(quiet) {
        t5577_multiwriter_show_error(
            app, "Tag went quiet", "Blocks were sent; some\nwould not read back.\nCheck the tag");
        return;
    }

    t5577_multiwriter_scene_write_result_ok(app);
}

bool t5577_multiwriter_scene_write_result_on_event(void* context, SceneManagerEvent event) {
    return t5577_multiwriter_leave_to_start(context, event);
}

void t5577_multiwriter_scene_write_result_on_exit(void* context) {
    LfRfid* app = context;

    popup_reset(app->popup);
    widget_reset(app->widget);
}
