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
    const bool nothing_wiped = (reason == NfcMagicIso15693WriteFailReasonNothingWiped);
    const bool empty_source = (reason == NfcMagicIso15693WriteFailReasonEmptySource);
    const bool nothing_cloned = (reason == NfcMagicIso15693WriteFailReasonNothingCloned);
    const bool wipe_mode = (instance->iso15693_mode == NfcMagicIso15693ModeWipe);

    // Over-capacity is a clean success (nothing was lost) -> success tone. Partial means something
    // didn't write, and card-lost / not-magic / nothing-wiped / empty-source are outright failures ->
    // error tone.
    notification_message(
        instance->notifications, over_capacity ? &sequence_success : &sequence_error);

    if(over_capacity) {
        // Clean success: every source block was written, the card just advertises more blocks than it
        // physically holds (the extra source blocks were empty, so nothing was lost). Concise summary
        // here, gen2-style; the exact empty top blocks are behind "Details". We confirm the writes
        // were accepted, not a byte-for-byte read-back, so the wording says "written", not "matches".
        const uint16_t advertised = instance->iso15693_result.blocks_total;
        const uint16_t extra = instance->iso15693_result.over_capacity;
        // The over-capacity gate guarantees >=1 block wrote, so extra < advertised.
        const uint16_t physical = (uint16_t)(advertised - extra);
        widget_add_string_element(
            widget, 64, 0, AlignCenter, AlignTop, FontPrimary, "Clone finished");
        FuriString* text = furi_string_alloc();
        furi_string_printf(
            text,
            "All data written.\nHolds %u/%u blocks.\nTop %u were empty.",
            physical,
            advertised,
            extra);
        widget_add_string_multiline_element(
            widget, 4, 20, AlignLeft, AlignTop, FontSecondary, furi_string_get_cstr(text));
        furi_string_free(text);
    } else if(partial) {
        // Summary only -- counts here, the per-block list behind "Details" -- mirroring the Gen2 /
        // USCUID-UL partial screens. Partial means some blocks wouldn't write (real source data lost,
        // or empty failures that weren't a clean capacity tail), or a clone fell back to gen1.
        const uint16_t total = instance->iso15693_result.blocks_total;
        // Everything that didn't write: real-data losses plus any empty blocks past capacity.
        const uint16_t not_written =
            instance->iso15693_result.failed_count + instance->iso15693_result.over_capacity;
        const uint16_t ok = (total >= not_written) ? (uint16_t)(total - not_written) : 0;
        widget_add_string_element(
            widget,
            64,
            0,
            AlignCenter,
            AlignTop,
            FontPrimary,
            wipe_mode ? "Wipe partial" : "Clone partial");
        FuriString* text = furi_string_alloc();
        furi_string_printf(
            text,
            wipe_mode ? "Wiped %u/%u blocks\nNot cleared: %u" :
                        "Cloned %u/%u blocks\nNot written: %u",
            ok,
            total,
            not_written);
        // The body sits at y=20 with the button row below it, so only three FontSecondary lines fit:
        // the two count lines plus ONE qualifier. Show the most significant (real data loss > gen1 UID
        // clobber > AFI/DSFID). A lower-priority caveat is dropped from THIS screen only -- "Details"
        // below is offered whenever any caveat applies and lists all of them, so nothing is unreachable.
        if(instance->iso15693_result.capacity_confirmed &&
           instance->iso15693_result.failed_count > 0) {
            // Real data was lost because those blocks are a persistent, contiguous run at the top of
            // the card -> the card is physically smaller than the source. (An empty top tail loses
            // nothing and is reported as an over-capacity success, not here.)
            furi_string_cat_str(text, "\nCard too small");
        } else if(instance->iso15693_result.used_gen1) {
            // gen1 fallback stamped the UID/commit into blocks 56/57/62/63, so they differ.
            furi_string_cat_str(text, "\ngen1: 56/57/62/63 differ");
        } else if(instance->iso15693_result.identity_failed) {
            // All data blocks took, but the card rejected the AFI/DSFID write.
            furi_string_cat_str(text, "\nAFI/DSFID not set");
        }
        widget_add_string_multiline_element(
            widget, 4, 20, AlignLeft, AlignTop, FontSecondary, furi_string_get_cstr(text));
        furi_string_free(text);
    } else if(nothing_wiped) {
        // A wipe that cleared nothing: the card accepted no zero-write (read-only / no usable
        // geometry). The UID was never touched -- say so, since the generic "not a magic tag / UID
        // write" message would be wrong for a wipe.
        widget_add_string_element(
            widget, 64, 0, AlignCenter, AlignTop, FontPrimary, "Wipe failed");
        widget_add_string_multiline_element(
            widget,
            0,
            13,
            AlignLeft,
            AlignTop,
            FontSecondary,
            "No blocks could be\ncleared. The card\nrejected all writes.\nIts UID is unchanged.");
    } else if(nothing_cloned) {
        // The gen2/gen1 UID write took, but every data block was rejected. Say what the card now holds:
        // it answers with the source's UID, so a UID-only reader accepts it while anything that reads
        // memory does not. No block list -- it would name every block.
        widget_add_string_element(
            widget, 64, 0, AlignCenter, AlignTop, FontPrimary, "Clone failed");
        widget_add_string_multiline_element(
            widget,
            0,
            13,
            AlignLeft,
            AlignTop,
            FontSecondary,
            "The UID was written\nbut no data block\nwould take. The card\nhas the UID only.");
    } else if(empty_source) {
        // A clone whose source image has no data blocks: nothing was written (not even the UID), so
        // the card is untouched. Distinct from a non-magic card.
        widget_add_string_element(
            widget, 64, 0, AlignCenter, AlignTop, FontPrimary, "Nothing to clone");
        widget_add_string_multiline_element(
            widget,
            0,
            13,
            AlignLeft,
            AlignTop,
            FontSecondary,
            "The saved file has\nno data blocks to\nclone. Nothing was\nwritten to the card.");
    } else {
        const char* message = card_lost ?
                                  "Card removed\nbefore the write\ncould finish." :
                                  "Not a magic tag.\nThis card doesn't\nsupport UID write.";
        widget_add_icon_element(widget, 83, 22, &I_WarningDolphinFlip_45x42);
        widget_add_string_element(
            widget, 64, 0, AlignCenter, AlignTop, FontPrimary, "Write failed");
        widget_add_string_multiline_element(
            widget, 0, 13, AlignLeft, AlignTop, FontSecondary, message);
    }

    if(card_lost) {
        // Card removed mid-write -> retryable. Retry re-runs the write; Exit leaves. Matches the
        // generic write-fail screen (Retry left, Exit right).
        widget_add_button_element(
            widget,
            GuiButtonTypeLeft,
            "Retry",
            nfc_magic_scene_iso15693_write_fail_widget_callback,
            instance);
        widget_add_button_element(
            widget,
            GuiButtonTypeRight,
            "Exit",
            nfc_magic_scene_iso15693_write_fail_widget_callback,
            instance);
    } else {
        // over-capacity / partial are (qualified) successes -> "Finish"; not-magic is a failure ->
        // "Back". The primary exit sits on the left, like the Gen2/USCUID/gen4 result screens.
        widget_add_button_element(
            widget,
            GuiButtonTypeLeft,
            (over_capacity || partial) ? "Finish" : "Back",
            nfc_magic_scene_iso15693_write_fail_widget_callback,
            instance);
        // "Details" (forward) carries everything that didn't fit the summary, like Gen2 / USCUID: a
        // partial's failed blocks, an over-capacity's empty top blocks, and the gen1 / AFI-DSFID
        // caveats. Offer it whenever there is ANY of those -- not just failed blocks. Without the two
        // caveat terms, a partial whose only problem is the gen1 UID clobber or a rejected AFI/DSFID
        // (both possible with zero failed blocks) had no Details button, and since the summary shows
        // only its single highest-priority qualifier, the lower one was then reachable nowhere at all.
        if(over_capacity || (partial && (instance->iso15693_result.failed_count > 0 ||
                                         instance->iso15693_result.used_gen1 ||
                                         instance->iso15693_result.identity_failed))) {
            widget_add_button_element(
                widget,
                GuiButtonTypeRight,
                "Details",
                nfc_magic_scene_iso15693_write_fail_widget_callback,
                instance);
        }
    }

    view_dispatcher_switch_to_view(instance->view_dispatcher, NfcMagicAppViewWidget);
}

bool nfc_magic_scene_iso15693_write_fail_on_event(void* context, SceneManagerEvent event) {
    NfcMagicApp* instance = context;
    bool consumed = false;

    const uint32_t reason =
        scene_manager_get_scene_state(instance->scene_manager, NfcMagicSceneIso15693WriteFail);
    const bool card_lost = (reason == NfcMagicIso15693WriteFailReasonCardLost);
    const bool partial = (reason == NfcMagicIso15693WriteFailReasonPartial);
    const bool over_capacity = (reason == NfcMagicIso15693WriteFailReasonOverCapacity);

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == GuiButtonTypeLeft) {
            if(card_lost) {
                // Retry: back to the write scene, which re-runs the write on enter.
                consumed = scene_manager_previous_scene(instance->scene_manager);
            } else {
                // Finish / Back -> the ISO15693 menu.
                consumed = scene_manager_search_and_switch_to_previous_scene(
                    instance->scene_manager, NfcMagicSceneIso15693);
            }
        } else if(event.event == GuiButtonTypeRight) {
            if(partial || over_capacity) {
                // Details -> the affected-block list (failed blocks, or the empty top blocks).
                scene_manager_next_scene(
                    instance->scene_manager, NfcMagicSceneIso15693PartialDetails);
                consumed = true;
            } else {
                // Card-lost "Exit" -> the ISO15693 menu.
                consumed = scene_manager_search_and_switch_to_previous_scene(
                    instance->scene_manager, NfcMagicSceneIso15693);
            }
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        consumed = scene_manager_search_and_switch_to_previous_scene(
            instance->scene_manager, NfcMagicSceneIso15693);
    }
    return consumed;
}

void nfc_magic_scene_iso15693_write_fail_on_exit(void* context) {
    NfcMagicApp* instance = context;

    // NOTE: do not reset the scene state here. The write scene always sets the reason before entering
    // this scene, and the "Details" round-trip re-enters this scene, which must re-read the same
    // reason -- clearing it here would rebuild the wrong screen on return from Details.
    widget_reset(instance->widget);
}
