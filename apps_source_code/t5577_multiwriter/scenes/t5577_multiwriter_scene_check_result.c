#include "../t5577_mw_tag.h"

#define TAG "T5577Mw"

// Append one finding to the footer, comma-separated, truncating at the end of the buffer. A
// helper so that no caller has to repeat the bound check and the separator.
//
// snprintf reports the length the text would have had, not what it wrote, so the cursor is
// clamped: left unclamped, one truncated append puts `used` past the buffer and the next
// caller's `size - *used` underflows into a huge size_t.
static void
    t5577_multiwriter_footer_add(char* footer, size_t size, size_t* used, const char* text) {
    if(*used >= size) return;

    const int written = snprintf(footer + *used, size - *used, "%s%s", *used ? ", " : "", text);
    if(written < 0) return;

    *used += (size_t)written;
    if(*used > size) *used = size;
}

static void t5577_multiwriter_scene_check_result_nothing(LfRfid* app) {
    Widget* widget = app->widget;

    widget_add_string_element(widget, 64, 4, AlignCenter, AlignTop, FontPrimary, "No tag found");

    // Three causes, not two. The third is a tag that is present and answering but configured
    // for a modulation or rate this decoder cannot follow - an HID or Indala clone written onto
    // a T5577, say - and for that one neither holding it still nor a password will ever work.
    // The detect fails before there is any configuration word to test, so the app cannot tell
    // the three apart; naming all three is the most it can honestly do.
    widget_add_string_multiline_element(
        widget,
        64,
        20,
        AlignCenter,
        AlignTop,
        FontSecondary,
        "Hold it still, or it needs\na password - or it is not\nan ASK/Manchester tag");

    widget_add_button_element(
        widget, GuiButtonTypeLeft, "Retry", t5577_multiwriter_widget_callback, app);
    widget_add_button_element(
        widget, GuiButtonTypeRight, "Password", t5577_multiwriter_widget_callback, app);
}

// How far the walk got, read from the slots themselves: t5577_mw_tag_read_slot() leaves a slot
// Unread until it has an answer, and the walk stops at the first slot that will not read, so
// the first Unread index is exactly where it stopped.
static uint8_t t5577_multiwriter_slots_read(const LfRfid* app) {
    uint8_t read = 0;

    while(read < EM41XX_MAX_KEYS && app->slot_state[read] != T5577MwSlotUnread)
        read++;

    return read;
}

// Whether every slot answered. Asked of the states themselves rather than of the stop index,
// so it does not depend on the walk stopping at the first slot that refuses.
static bool t5577_multiwriter_slots_whole(const LfRfid* app) {
    for(uint8_t slot = 0; slot < EM41XX_MAX_KEYS; slot++) {
        if(app->slot_state[slot] == T5577MwSlotUnread) return false;
    }

    return true;
}

// Keys the tag actually holds, counted across every slot rather than only the broadcast ones.
// A key stored past what block 0 sends is still a key the tag holds, and a slot that is sent
// while holding nothing is still not one.
//
// After a read that stopped early this is a lower bound, which is why the header prints it with
// a trailing + and the footer says where the read stopped.
static uint8_t t5577_multiwriter_slots_held(const LfRfid* app) {
    uint8_t held = 0;

    for(uint8_t slot = 0; slot < EM41XX_MAX_KEYS; slot++) {
        if(app->slot_state[slot] == T5577MwSlotKey) held++;
    }

    return held;
}

// Whether this row is starred: it holds a key, past what block 0 broadcasts. Both the star and
// the legend that explains it come from here, so the legend cannot appear over rows that have
// no star - which it did when it was gated on "does the tag send fewer than three keys", a
// question a one-key tag written from blank answers yes while starring nothing.
static bool t5577_multiwriter_slot_starred(const LfRfid* app, uint8_t slot) {
    return slot >= t5577_mw_tag_keys(app) && app->slot_state[slot] == T5577MwSlotKey;
}

static bool t5577_multiwriter_slots_starred(const LfRfid* app) {
    for(uint8_t slot = 0; slot < EM41XX_MAX_KEYS; slot++) {
        if(t5577_multiwriter_slot_starred(app, slot)) return true;
    }

    return false;
}

static void t5577_multiwriter_scene_check_result_found(LfRfid* app) {
    Widget* widget = app->widget;

    const uint8_t read = t5577_multiwriter_slots_read(app);
    const bool whole = t5577_multiwriter_slots_whole(app);
    const uint8_t sends = t5577_mw_tag_keys(app);

    // Two separate numbers, because block 0 and the key blocks can disagree in both directions
    // and one number cannot say which happened. "Sends" is what block 0 broadcasts, which is
    // all a reader ever sees; "holds" is what the slots were actually found to contain. Writing
    // key 3 to a blank tag gives "Sends 3, holds 1"; rewriting key 1 on a full tag gives
    // "Sends 1, holds 3". Reporting either as "N of 3 keys" called both of those three keys.
    //
    // A read that stopped early never looked at the remaining slots, so its held count is a
    // floor rather than a total and is printed as "holds 1+".
    char header[24];
    snprintf(
        header,
        sizeof(header),
        "Sends %u, holds %u%s",
        sends,
        t5577_multiwriter_slots_held(app),
        whole ? "" : "+");
    widget_add_string_element(widget, 2, 0, AlignLeft, AlignTop, FontPrimary, header);

    if(t5577_mw_tag_locked(app)) {
        widget_add_string_element(widget, 126, 2, AlignRight, AlignTop, FontSecondary, "LOCK");
    }

    for(uint8_t slot = 0; slot < EM41XX_MAX_KEYS; slot++) {
        char row[26];
        const bool sent = slot < sends;
        // What the tag puts on the wire for a slot holding no key. Empty for the slots block 0
        // does not broadcast, where nothing goes out at all.
        const char* const on_air = sent ? " - ON AIR" : "";

        switch(app->slot_state[slot]) {
        case T5577MwSlotKey: {
            char key[EM41XX_KEY_STR_SIZE];
            em41xx_key_format(app->slot_key[slot], key, sizeof(key));
            // A slot past what block 0 broadcasts still holds its key; the star is the whole
            // difference between a key that is gone and one a reader simply never hears.
            snprintf(
                row,
                sizeof(row),
                "%u%s %s",
                slot + 1,
                t5577_multiwriter_slot_starred(app, slot) ? "*" : " ",
                key);
            break;
        }
        case T5577MwSlotBlank:
            // A slot block 0 broadcasts but nothing was ever written to sends 64 zero bits to
            // every reader, ahead of the real key. A plain "empty" is for the slots block 0
            // does not broadcast, where nothing goes out at all.
            snprintf(row, sizeof(row), "%u  empty%s", slot + 1, on_air);
            break;
        case T5577MwSlotUnknown:
            // Read back fine and is not a key - somebody's data rather than zeroes, so unlike
            // the blank above there may be something here worth recovering.
            snprintf(row, sizeof(row), "%u  not a key%s", slot + 1, on_air);
            break;
        default:
            snprintf(row, sizeof(row), "%u  not read", slot + 1);
            break;
        }

        widget_add_string_element(
            widget, 2, 11 + slot * 10, AlignLeft, AlignTop, FontSecondary, row);
    }

    // Additive, all three of them: an incomplete read, a foreign block 0 and keys the tag
    // stores without sending are separate findings, and a chain of else-ifs lets a later one
    // hide an earlier while the rows above it go unexplained - starred rows with no legend, or
    // every row reading "not read" with nothing to say why. The worst case is three findings,
    // 35 characters plus two separators.
    // 40 bytes needed; 48 to leave room for another finding.
    char footer[48] = {0};
    size_t used = 0;

    if(!whole) {
        // Built whole rather than appended in two calls, which would put the separator between
        // the words and the number they belong to.
        char quiet[16];
        snprintf(quiet, sizeof(quiet), "quiet at %u", read + 1);
        t5577_multiwriter_footer_add(footer, sizeof(footer), &used, quiet);
    }
    if(t5577_mw_tag_foreign(app)) {
        t5577_multiwriter_footer_add(footer, sizeof(footer), &used, "foreign block 0");
    }
    if(t5577_multiwriter_slots_starred(app)) {
        t5577_multiwriter_footer_add(footer, sizeof(footer), &used, "* not sent");
    }

    if(footer[0] != '\0') {
        // A text box rather than a string element. widget_add_string_element() does not wrap,
        // and a centred line wider than the screen is clipped at both ends - so the finding
        // written first, that the read stopped early, is the one that would be lost.
        //
        // The box is the whole screen below the rows, and its height has to be: elements_text_box()
        // takes the aligned path only while two lines of leading_default (11 each) fit under it,
        // and below that it falls into a branch that leaves the second line's y relative,
        // drawing it back up among the rows. 23 clears 22; 18 does not. Any two findings fit one
        // line, so only all three ever reach the second.
        //
        // Vertically centred, not top: AlignTop inks the first line at 41-47, one blank row
        // under the last row's descenders at 38-39, which reads as a collision. Centred, a
        // single line inks 47-53 on a baseline of 54, with clear air above it, and two lines
        // still fill the box.
        widget_add_text_box_element(
            widget, 0, 41, 128, 23, AlignCenter, AlignCenter, footer, false);
    }
}

// The log is the only record of what this screen showed, since input cannot be injected into
// the GUI on this firmware - so it carries every quantity the screen draws, the key values
// included. Slots are logged one per line, over EM41XX_MAX_KEYS, so this cannot silently fall
// behind the screen if the slot count ever changes.
static void t5577_multiwriter_scene_check_result_log(const LfRfid* app) {
    const uint8_t sends = t5577_mw_tag_keys(app);

    FURI_LOG_I(
        TAG,
        "result: block0 %08lX, sends %u holds %u, read %u, %s%s",
        app->config.block0,
        sends,
        t5577_multiwriter_slots_held(app),
        t5577_multiwriter_slots_read(app),
        t5577_mw_tag_locked(app) ? "locked" : "open",
        t5577_mw_tag_foreign(app) ? ", foreign" : "");

    for(uint8_t slot = 0; slot < EM41XX_MAX_KEYS; slot++) {
        char key[EM41XX_KEY_STR_SIZE] = "-";

        if(app->slot_state[slot] == T5577MwSlotKey) {
            em41xx_key_format(app->slot_key[slot], key, sizeof(key));
        }

        FURI_LOG_I(
            TAG,
            "result: slot %u state %u%s key %s",
            slot + 1,
            app->slot_state[slot],
            slot < sends ? " sent" : "",
            key);
    }
}

void t5577_multiwriter_scene_check_result_on_enter(void* context) {
    LfRfid* app = context;

    widget_reset(app->widget);

    if(!t5577_config_found(&app->config)) {
        FURI_LOG_W(TAG, "result: no tag");
        t5577_multiwriter_scene_check_result_nothing(app);
        notification_message(app->notifications, &sequence_error);
    } else {
        t5577_multiwriter_scene_check_result_found(app);
        t5577_multiwriter_scene_check_result_log(app);

        // The chime answers "did the check finish", not "was a tag there". With a tag held
        // against the antenna by hand the sound is what the user acts on, and a read that
        // stopped at the first slot must not sound like one that read every slot.
        //
        // Both are audible and both light the backlight. An LED-only signal for the partial
        // read would leave the one outcome that needs a second look as the only silent one,
        // while a tag that was never there chimed.
        notification_message(
            app->notifications,
            t5577_multiwriter_slots_whole(app) ? &sequence_success : &sequence_semi_success);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, LfRfidViewWidget);
}

bool t5577_multiwriter_scene_check_result_on_event(void* context, SceneManagerEvent event) {
    LfRfid* app = context;

    // Back goes past the check rather than into it, so leaving the result does not start
    // another read of the tag.
    if(event.type == SceneManagerEventTypeBack) {
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, LfRfidSceneStart);
        return true;
    }

    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == GuiButtonTypeLeft) {
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, LfRfidSceneCheck);
        return true;
    }

    if(event.event == GuiButtonTypeRight) {
        // The password screen comes back here through the check, so a word entered for this
        // tag is tried on it straight away.
        scene_manager_set_scene_state(app->scene_manager, LfRfidScenePasswordInput, 1);
        scene_manager_next_scene(app->scene_manager, LfRfidScenePasswordInput);
        return true;
    }

    return false;
}

void t5577_multiwriter_scene_check_result_on_exit(void* context) {
    LfRfid* app = context;

    widget_reset(app->widget);
}
