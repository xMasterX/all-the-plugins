#include "../nfc_magic_app_i.h"

void nfc_magic_scene_iso15693_write_fail_widget_callback(
    GuiButtonType result,
    InputType type,
    void* context) {
    NfcMagicApp* instance = context;

    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(instance->view_dispatcher, result);
    }
}

void nfc_magic_scene_iso15693_write_fail_on_enter(void* context) {
    NfcMagicApp* instance = context;
    Widget* widget = instance->widget;

    const uint32_t reason =
        scene_manager_get_scene_state(instance->scene_manager, NfcMagicSceneIso15693WriteFail);
    const bool card_lost = (reason == NfcMagicIso15693WriteFailReasonCardLost);
    const bool partial = (reason == NfcMagicIso15693WriteFailReasonPartial);
    const bool over_capacity = (reason == NfcMagicIso15693WriteFailReasonOverCapacity);
    const bool nothing_written = (reason == NfcMagicIso15693WriteFailReasonNothingWritten);
    const bool wipe = instance->iso15693_is_wipe_mode;
    const Iso15693PollerResult* result = &instance->iso15693_result;

    // Partial and over-capacity are soft (successful) outcomes; the rest are errors.
    notification_message(
        instance->notifications, (partial || over_capacity) ? &sequence_success : &sequence_error);

    if(over_capacity) {
        // The clone matched the source, and the only blocks the card refused were empty ones in a
        // contiguous run at the top -- so nothing was lost, but the card now advertises more blocks
        // than it physically holds. Worth flagging: a reader probing the top blocks sees them
        // error/zero, and real data can't be stored there.
        //
        // Only the tail case reaches this screen, so subtracting it from the total is a real
        // measurement rather than a guess (see iso15693_poller_excuse_empty_tail).
        FuriString* text = furi_string_alloc();
        furi_string_cat_printf(
            text,
            "Every block with data was copied. The card took %u of %u blocks; the top %u are empty "
            "and would not write, so it advertises more than it physically has.",
            result->blocks_written,
            result->blocks_total,
            result->over_capacity);
        widget_add_string_element(
            widget, 3, 0, AlignLeft, AlignTop, FontPrimary, "Clone complete");
        // Scrolling element (not a fixed text box) so the note isn't silently clipped past the height.
        widget_add_text_scroll_element(widget, 0, 14, 128, 38, furi_string_get_cstr(text));
        furi_string_free(text);
    } else if(partial) {
        // Some blocks were placed and some were refused. Lead with the count so "partial" is never
        // read as "complete", and do not name a cause we cannot prove: a refusal can be a locked
        // block on the target, a transient error, or the card's real capacity, and this code has no
        // way to tell them apart. (Empty blocks refused in a run at the top are excused as capacity
        // by the poller and reach the over-capacity screen instead.)
        FuriString* text = furi_string_alloc();
        furi_string_cat_printf(
            text,
            wipe ? "Cleared %u of %u blocks.\n" : "Wrote %u of %u blocks, plus the UID.\n",
            result->blocks_written,
            result->blocks_total);
        if(result->failed_count > 0) {
            furi_string_cat_printf(
                text,
                wipe ? "%u would not clear (locked): " :
                       "%u refused (locked, or past the card's real capacity): ",
                result->failed_count);
            for(uint16_t block = 0; block < ISO15693_POLLER_MAX_BLOCKS; block++) {
                if(result->failed_bitmap[block / 8] & (1u << (block % 8))) {
                    furi_string_cat_printf(text, "%u ", block);
                }
            }
            furi_string_push_back(text, '\n');
        }
        if(result->afi_failed || result->dsfid_failed) {
            // No read-back covers these, so name them explicitly: a clone that looks right but has
            // the wrong AFI is invisible to an AFI-filtered reader.
            furi_string_cat_printf(
                text,
                "The card would not take the source's %s, so a reader that filters on it won't see "
                "this copy.\n",
                (result->afi_failed && result->dsfid_failed) ? "AFI or DSFID" :
                result->afi_failed                           ? "AFI" :
                                                               "DSFID");
        }
        if(result->used_gen1) {
            // The clone fell back to the gen1 method, which stamps the UID (56/57) plus unlock/commit
            // (62/63) into those data blocks -- so they no longer match the source.
            furi_string_cat_str(
                text,
                "gen1 method: blocks 56/57/62/63 hold UID + unlock/commit, not your file's data.");
        }
        widget_add_string_element(
            widget,
            3,
            0,
            AlignLeft,
            AlignTop,
            FontPrimary,
            wipe ? "Wipe partial" : "Clone partial");
        // Scrolling, not a fixed box: the block list has no useful upper bound, and a text box
        // silently drops whatever runs past its height.
        widget_add_text_scroll_element(widget, 0, 14, 128, 38, furi_string_get_cstr(text));
        furi_string_free(text);
    } else if(nothing_written) {
        // The card answered but would not take a single block. That is a write-protected memory, and
        // it says nothing about whether the UID can be changed -- so this must not read as "not a
        // magic tag", which would have the user discard a card whose UID write may work fine.
        FuriString* text = furi_string_alloc();
        if(result->pass == Iso15693BlockPassNoGeometry) {
            furi_string_cat_str(
                text,
                "This card did not report a usable memory layout, so no block could be addressed.");
        } else {
            furi_string_cat_printf(
                text,
                "The card refused all %u blocks, so its memory is write-protected. Its UID may "
                "still be writable.",
                result->blocks_total);
        }
        widget_add_string_element(
            widget,
            3,
            0,
            AlignLeft,
            AlignTop,
            FontPrimary,
            wipe ? "Nothing wiped" : "Nothing written");
        widget_add_text_scroll_element(widget, 0, 14, 128, 38, furi_string_get_cstr(text));
        furi_string_free(text);
    } else {
        // The block pass runs before the UID verify, so by the time a card goes missing it may
        // already hold some of the new data. Say so -- "removed before it could finish" on its own
        // reads as "nothing happened", and the user would not think to re-check the card.
        const char* message;
        if(card_lost) {
            message = (result->blocks_written > 0) ?
                          "Card removed\nmid-write. Some\nblocks were\nalready changed." :
                          "Card removed\nbefore the write\ncould finish.";
        } else {
            message = "Not a magic tag.\nThis card doesn't\nsupport UID write.";
        }
        widget_add_icon_element(widget, 83, 22, &I_WarningDolphinFlip_45x42);
        widget_add_string_element(
            widget, 64, 0, AlignCenter, AlignTop, FontPrimary, "Write failed");
        widget_add_string_multiline_element(
            widget, 0, 13, AlignLeft, AlignTop, FontSecondary, message);
    }

    // Only a lost card is worth retrying; a rejected/partial write would just repeat.
    if(card_lost) {
        widget_add_button_element(
            widget,
            GuiButtonTypeLeft,
            "Retry",
            nfc_magic_scene_iso15693_write_fail_widget_callback,
            instance);
    }
    widget_add_button_element(
        widget,
        GuiButtonTypeRight,
        "OK",
        nfc_magic_scene_iso15693_write_fail_widget_callback,
        instance);

    view_dispatcher_switch_to_view(instance->view_dispatcher, NfcMagicAppViewWidget);
}

bool nfc_magic_scene_iso15693_write_fail_on_event(void* context, SceneManagerEvent event) {
    NfcMagicApp* instance = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == GuiButtonTypeLeft) {
            // Retry: back to the write scene, which re-runs the write on enter.
            consumed = scene_manager_previous_scene(instance->scene_manager);
        } else if(event.event == GuiButtonTypeRight) {
            consumed = scene_manager_search_and_switch_to_previous_scene(
                instance->scene_manager, NfcMagicSceneIso15693);
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        consumed = scene_manager_search_and_switch_to_previous_scene(
            instance->scene_manager, NfcMagicSceneIso15693);
    }
    return consumed;
}

void nfc_magic_scene_iso15693_write_fail_on_exit(void* context) {
    NfcMagicApp* instance = context;

    // Reset to the default reason so a later failure isn't mislabelled.
    scene_manager_set_scene_state(
        instance->scene_manager,
        NfcMagicSceneIso15693WriteFail,
        NfcMagicIso15693WriteFailReasonNotMagic);
    widget_reset(instance->widget);
}
