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

    const NfcMagicIso15693WriteFailReason reason =
        scene_manager_get_scene_state(instance->scene_manager, NfcMagicSceneIso15693WriteFail);
    const bool card_lost = (reason == NfcMagicIso15693WriteFailReasonCardLost);
    const bool soft = (reason == NfcMagicIso15693WriteFailReasonPartial) ||
                      (reason == NfcMagicIso15693WriteFailReasonOverCapacity);
    const bool wipe = instance->iso15693_is_wipe_mode;
    const Iso15693PollerResult* result = &instance->iso15693_result;

    // Partial and over-capacity are soft (successful) outcomes; the rest are errors.
    notification_message(instance->notifications, soft ? &sequence_success : &sequence_error);

    // Each reason produces a title and a body; a NULL title means the compact icon layout below.
    // Switching on the enum with no default gets -Wswitch coverage, so a new reason cannot be added
    // without deciding what it says.
    const char* title = NULL;
    FuriString* body = furi_string_alloc();

    switch(reason) {
    case NfcMagicIso15693WriteFailReasonOverCapacity:
        // Every block holding data was copied; the only refusals were empty ones in a contiguous run
        // at the top, so nothing was lost -- but the card now advertises more blocks than it
        // physically holds, and a reader probing the top sees them error/zero. Because only the tail
        // case reaches here, these counts are measured rather than guessed (see
        // iso15693_poller_excuse_empty_tail).
        title = "Clone complete";
        furi_string_printf(
            body,
            "Every block with data was copied. The card took %u of %u blocks; the top %u are empty "
            "and would not write, so it advertises more than it physically has.",
            result->blocks_written,
            result->blocks_total,
            result->over_capacity);
        break;

    case NfcMagicIso15693WriteFailReasonPartial:
        // Lead with the count so "partial" is never read as "complete", and do not name a cause we
        // cannot prove: a refusal can be a locked block on the target, a transient error, or the
        // card's real capacity, and nothing here can tell them apart.
        title = wipe ? "Wipe partial" : "Clone partial";
        furi_string_printf(
            body,
            wipe ? "Cleared %u of %u blocks.\n" : "Wrote %u of %u blocks, plus the UID.\n",
            result->blocks_written,
            result->blocks_total);
        if(result->failed_count > 0) {
            furi_string_cat_printf(
                body,
                wipe ? "%u would not clear (locked): " :
                       "%u refused (locked, or past the card's real capacity): ",
                result->failed_count);
            for(uint16_t block = 0; block < result->blocks_total; block++) {
                if(result->failed_bitmap[block / 8] & (1u << (block % 8))) {
                    furi_string_cat_printf(body, "%u ", block);
                }
            }
            furi_string_push_back(body, '\n');
        }
        if(result->over_capacity > 0) {
            // Excused blocks are absent from failed_count, so without this they would simply be
            // missing from the arithmetic on screen.
            furi_string_cat_printf(
                body,
                "%u empty block(s) past the card's real capacity were skipped; no data was lost "
                "there.\n",
                result->over_capacity);
        }
        if(result->afi_failed || result->dsfid_failed) {
            // No read-back covers these, so name them explicitly: a clone that looks right but has
            // the wrong AFI is invisible to an AFI-filtered reader.
            const char* field = (result->afi_failed && result->dsfid_failed) ? "AFI or DSFID" :
                                result->afi_failed                           ? "AFI" :
                                                                               "DSFID";
            furi_string_cat_printf(
                body,
                "The card would not take the source's %s, so a reader that filters on it won't see "
                "this copy.\n",
                field);
        }
        if(result->used_gen1) {
            // gen1 stamps the UID (56/57) plus unlock/commit (62/63) into those data blocks, so they
            // no longer match the source.
            furi_string_cat_str(
                body,
                "gen1 method: blocks 56/57/62/63 hold UID + unlock/commit, not your file's data.");
        }
        break;

    case NfcMagicIso15693WriteFailReasonNothingWritten:
        // The card answered but took nothing. That is a write-protected memory, and it says nothing
        // about whether the UID can be changed -- so it must not read as "not a magic tag", which
        // would have the user discard a card whose UID write may work fine.
        title = wipe ? "Nothing wiped" : "Nothing written";
        if(result->pass == Iso15693BlockPassNoGeometry) {
            furi_string_set_str(
                body,
                "This card did not report a usable memory layout, so no block could be addressed.");
        } else {
            furi_string_printf(
                body,
                "The card refused all %u blocks, so its memory is write-protected. Its UID may "
                "still be writable.",
                result->blocks_total);
        }
        break;

    case NfcMagicIso15693WriteFailReasonCardLost:
    case NfcMagicIso15693WriteFailReasonNotMagic:
        break; // compact icon layout, handled below
    }

    if(title != NULL) {
        widget_add_string_element(widget, 3, 0, AlignLeft, AlignTop, FontPrimary, title);
        // Scrolling, not a fixed text box: the block list has no useful upper bound, and a text box
        // silently drops whatever runs past its height.
        widget_add_text_scroll_element(widget, 0, 14, 128, 38, furi_string_get_cstr(body));
    } else {
        // A card can go missing after some blocks are already down, so say that rather than just
        // "removed before it could finish", which reads as "nothing happened".
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
    furi_string_free(body);

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
