#include "t5577_mw_tag.h"

#define TAG "T5577Mw"

T5577MwTagAnswer t5577_mw_tag_detect(LfRfid* app, T5577MwDetectMode mode) {
    furi_assert(app);

    if(t5577_detect(&app->config)) {
        // Set *and cleared* from the tag's own answer. The detect drops a password the tag
        // turned out not to want, so with_pass is the chip speaking rather than the user
        // guessing - and a flag that only ever went up would let one locked tag authorise a
        // password frame at every tag put on the pad afterwards.
        app->password_confirmed = false;
        return T5577MwTagOpen;
    }

    bool may_send = app->password_set;
    if(mode == T5577MwDetectPlain) {
        may_send = false;
    } else if(mode == T5577MwDetectConfirmed) {
        may_send = may_send && app->password_confirmed;
    }

    if(may_send) {
        FURI_LOG_I(TAG, "plain detect found nothing, trying the password");

        if(t5577_detect_with_pass(&app->config, app->password)) {
            app->password_confirmed = app->config.with_pass;

            if(app->config.with_pass) {
                app->password = app->config.password;
                app->password_set = true;
            }

            return T5577MwTagUnlocked;
        }
    }

    // t5577_detect() zeroes the config when it fails, and every accessor reads the config, so
    // there is nothing to clear: t5577_config_found() and the rest all answer for a tag that
    // was not there.
    FURI_LOG_W(TAG, "no tag answered");

    return T5577MwTagSilent;
}

bool t5577_mw_tag_read_slot(LfRfid* app, uint8_t slot) {
    furi_assert(app);
    furi_assert(slot < EM41XX_MAX_KEYS);

    const uint8_t block = em41xx_slot_block(slot);
    uint32_t high = 0;
    uint32_t low = 0;

    app->slot_state[slot] = T5577MwSlotUnread;
    app->slot_key[slot] = 0;

    if(!t5577_block_value(&app->config, 0, block, &high)) return false;

    // The nine header bits all live in the first block, so a block that does not carry them
    // cannot decode whatever the second holds. Only a first block of zero still needs the
    // second, to tell a slot that was never written from one holding somebody else's data -
    // and skipping the read saves a second and a half per garbage slot.
    if(!em41xx_first_block_plausible(high) && high != 0) {
        app->slot_state[slot] = T5577MwSlotUnknown;
        FURI_LOG_W(TAG, "slot %u starts %08lX, not a key", slot + 1, high);
        return true;
    }

    if(!t5577_block_value(&app->config, 0, block + 1, &low)) return false;

    const uint64_t frame = ((uint64_t)high << 32) | low;

    if(em41xx_decode(frame, &app->slot_key[slot])) {
        app->slot_state[slot] = T5577MwSlotKey;
    } else if(frame == 0) {
        app->slot_state[slot] = T5577MwSlotBlank;
    } else {
        // Blocks that read back perfectly well and are not a key. Worth its own state: if
        // block 0 still broadcasts this slot, the tag is sending that word to every reader -
        // which is the opposite of the slot being empty.
        app->slot_state[slot] = T5577MwSlotUnknown;
        FURI_LOG_W(TAG, "slot %u holds %08lX %08lX, not a key", slot + 1, high, low);
    }

    return true;
}

T5577BlockResult
    t5577_mw_tag_check_block(LfRfid* app, uint8_t block, uint32_t expected, uint32_t* actual) {
    furi_assert(app);
    furi_assert(actual);

    uint32_t value = 0;
    if(!t5577_block_value(&app->config, 0, block, &value)) {
        FURI_LOG_W(TAG, "block %u did not read back", block);
        return T5577BlockNoAnswer;
    }

    *actual = value;

    if(value == expected) return T5577BlockHeld;

    FURI_LOG_W(TAG, "block %u holds %08lX, sent %08lX", block, value, expected);

    return T5577BlockMismatch;
}
