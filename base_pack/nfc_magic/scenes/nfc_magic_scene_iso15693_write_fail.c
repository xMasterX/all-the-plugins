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

// Does this outcome have anything behind "Details"? One answer, for both the button in on_enter and the
// routing in on_event.
//
// Why it is not simply failed_count: a partial whose only problem is the gen1 UID clobber or a rejected
// AFI/DSFID has zero failed blocks, and the summary shows only its highest-priority qualifier, so the
// lower one would be reachable nowhere. UID-changed is here because it PRE-EMPTS the partial reason
// code -- without it a wipe that both moved the UID and left blocks uncleared names them nowhere.
// Is re-running the write the right next action? A sweep the clock cut may have left real data above
// the cut, which is the privacy failure the sweep exists to remove -- as good a claim on Retry as a
// card that left mid-write. Both the buttons in on_enter and the left-button handler in on_event need
// this answer.
static bool
    nfc_magic_scene_iso15693_write_fail_is_retryable(NfcMagicApp* instance, uint32_t reason) {
    UNUSED(instance);
    return reason == NfcMagicIso15693WriteFailReasonCardLost ||
           reason == NfcMagicIso15693WriteFailReasonWipeStopped;
}

static bool
    nfc_magic_scene_iso15693_write_fail_has_details(NfcMagicApp* instance, uint32_t reason) {
    const Iso15693PollerResult* result = &instance->iso15693_result;
    switch(reason) {
    case NfcMagicIso15693WriteFailReasonOverCapacity:
        return true;
    case NfcMagicIso15693WriteFailReasonPartial:
        return result->failed_count > 0 || result->used_gen1 || result->identity_failed;
    case NfcMagicIso15693WriteFailReasonWipeStopped:
        // Always: the blocks above the cut were never attempted, so they carry no bitmap bits and the
        // scroll view is the only place that fact can be stated.
        return true;
    case NfcMagicIso15693WriteFailReasonWipeUidChanged:
        return result->failed_count > 0 || result->sweep_truncated;
    default:
        return false;
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
    const bool uid_unexpected = (reason == NfcMagicIso15693WriteFailReasonUidUnexpected);
    const bool gen1_failed = (reason == NfcMagicIso15693WriteFailReasonGen1Failed);
    const bool uid_unverifiable = (reason == NfcMagicIso15693WriteFailReasonUidUnverifiable);
    const bool wipe_uid_changed = (reason == NfcMagicIso15693WriteFailReasonWipeUidChanged);
    const bool wipe_complete = (reason == NfcMagicIso15693WriteFailReasonWipeComplete);
    const bool wipe_mode = (instance->iso15693_mode == NfcMagicIso15693ModeWipe);

    const bool wipe_stopped = (reason == NfcMagicIso15693WriteFailReasonWipeStopped);

    // Over-capacity and a wipe that ran to the card's top are clean successes -> success tone.
    // Everything else did not deliver what was asked for -> error tone, the cut sweep included: the
    // poller reports that as Partial, and the tone has to agree with the event rather than contradict
    // it.
    notification_message(
        instance->notifications,
        (over_capacity || wipe_complete) ? &sequence_success : &sequence_error);

    if(wipe_complete) {
        // A clean wipe, reporting what it actually covered. This screen exists because the sweep's
        // length is measured, not assumed: it stops at the highest block the card answered for, which
        // can be short of the card's claim or past it (a card cloned from a smaller source advertises
        // the smaller count while still holding everything above). On the bare Success popup those
        // render identically.
        //
        // Both figures, no verdict. blocks_total < advertised is NOT flagged as an error: a card
        // advertising 66 blocks against 64 physical is a normal, undamaged card, and calling its two
        // dropped phantom blocks a failure is the false report the tail-drop rule exists to prevent.
        widget_add_string_element(
            widget, 64, 0, AlignCenter, AlignTop, FontPrimary, "Wipe complete");
        FuriString* text = furi_string_alloc();
        furi_string_printf(
            text,
            "Cleared %u blocks.\nCard claims %u.",
            instance->iso15693_result.blocks_total,
            instance->iso15693_result.blocks_advertised);
        // Third and last line the body has room for. The wipe itself finished; only the identity check
        // did not, and saying so is what stops this screen asserting a check that never ran.
        if(!instance->iso15693_result.uid_verified) {
            furi_string_cat_str(text, "\nUID not re-checked.");
        }
        widget_add_string_multiline_element(
            widget, 0, 13, AlignLeft, AlignTop, FontSecondary, furi_string_get_cstr(text));
        furi_string_free(text);
    } else if(wipe_stopped) {
        // The clock stopped the sweep with blocks the card still claims unattempted. Those blocks have
        // no bitmap bits -- nothing tried them -- so these counts are the whole on-screen story and
        // Details carries the rest.
        widget_add_string_element(
            widget, 64, 0, AlignCenter, AlignTop, FontPrimary, "Wipe stopped");
        const uint16_t reached = instance->iso15693_result.blocks_total;
        const uint16_t failed = instance->iso15693_result.failed_count;
        FuriString* text = furi_string_alloc();
        furi_string_printf(
            text,
            "Cleared %u blocks.\nStopped at %u of %u.",
            (reached >= failed) ? (uint16_t)(reached - failed) : 0,
            reached,
            instance->iso15693_result.blocks_advertised);
        if(failed > 0) furi_string_cat_printf(text, "\nNot cleared: %u", failed);
        widget_add_string_multiline_element(
            widget, 0, 13, AlignLeft, AlignTop, FontSecondary, furi_string_get_cstr(text));
        furi_string_free(text);
    } else if(over_capacity) {
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
        // clobber > AFI/DSFID). A lower one is dropped from THIS screen only -- "Details" below is
        // offered whenever any caveat applies and lists all of them, so nothing is unreachable.
        // A cut sweep is not among them: it has its own reason code and screen.
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
        // A card that refuses every write while still answering reads is exactly the card the sweep's
        // time limit exists for, and it is also the one that ends here: nothing accepted, so the wipe
        // short-circuits to this screen before any of the truncation reporting. Three lines is all the
        // body has, so the cut replaces the prose rather than adding to it.
        FuriString* text = furi_string_alloc();
        if(instance->iso15693_result.sweep_truncated) {
            furi_string_printf(
                text,
                "No block accepted the\nzero-write.\nStopped at %u of %u.",
                instance->iso15693_result.blocks_total,
                instance->iso15693_result.blocks_advertised);
        } else {
            furi_string_set_str(
                text, "No blocks could be\ncleared -- the card\naccepted no zero-write.");
        }
        widget_add_string_multiline_element(
            widget, 0, 13, AlignLeft, AlignTop, FontSecondary, furi_string_get_cstr(text));
        furi_string_free(text);
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
            "The UID was written,\nbut no data block took.\nThe card has UID only.");
    } else if(wipe_uid_changed) {
        // The wipe cleared its blocks and the card came back answering a different UID. On gen2 that
        // cannot happen -- the wipe sends no UID command and the gen2 UID lives in a separate register
        // space -- so in practice this is a gen1 card that an earlier gen1 UID write left armed, with
        // the wipe's zeros landing in blocks 56/57, which on gen1 ARE the UID registers. Print what it
        // answers to now: without that the card is simply lost, since it no longer responds to the UID
        // the user knows it by.
        widget_add_string_element(
            widget, 64, 0, AlignCenter, AlignTop, FontPrimary, "UID changed");
        // success_or_partial ORs uid_changed with failed_count, so BOTH can hold: a wipe can move the UID
        // AND leave blocks uncleared. Lead with the counts rather than asserting "Data cleared", which
        // would be false in exactly that case -- and it costs nothing, since the counts replace a prose
        // line. The UID then lands on line 3, which matters: FontSecondary advances 11px per line, so a
        // body at y=13 puts line tops at 13/24/35/46 and a 4th line's lower rows fall inside the button
        // box at rows 52-63. This UID is the only way back to a card that has stopped answering to the
        // one the user knows, and a clipped hex digit is a mis-readable UID.
        const uint16_t wiped_total = instance->iso15693_result.blocks_total;
        const uint16_t wiped_bad = instance->iso15693_result.failed_count;
        FuriString* text = furi_string_alloc();
        furi_string_printf(
            text,
            "Wiped %u/%u. The card's\nUID moved. Now reads:\n",
            (wiped_total >= wiped_bad) ? (uint16_t)(wiped_total - wiped_bad) : 0,
            wiped_total);
        for(size_t i = 0; i < ISO15693_3_UID_SIZE; i++) {
            if(i == 4) furi_string_push_back(text, ' ');
            furi_string_cat_printf(text, "%02X", instance->iso15693_result.uid_readback[i]);
        }
        widget_add_string_multiline_element(
            widget, 0, 13, AlignLeft, AlignTop, FontSecondary, furi_string_get_cstr(text));
        furi_string_free(text);
    } else if(uid_unexpected) {
        // The gen2 backdoor moved the UID to neither the original nor the target. Everything else that
        // lands on "Not a magic tag" is a card that did nothing; this one demonstrably responded to a
        // magic command, so saying it isn't magic would be exactly backwards. The card is now answering
        // to a UID nobody asked for, and printing it is the only way the user can find the card again.
        widget_add_string_element(
            widget, 64, 0, AlignCenter, AlignTop, FontPrimary, "Unexpected UID");
        // Prose kept to two lines so the UID lands on line 3: a 4th line at y=13 starts at row 46 and
        // runs into the button box at rows 52-63, and a clipped hex digit is a mis-readable UID.
        FuriString* text = furi_string_alloc();
        furi_string_set_str(text, "Card is magic, but the\nUID it took isn't yours:\n");
        for(size_t i = 0; i < ISO15693_3_UID_SIZE; i++) {
            // Two 4-byte groups, unspaced: the spaced form the Info screen uses is 23 characters and
            // overruns the 128px line here.
            if(i == 4) furi_string_push_back(text, ' ');
            furi_string_cat_printf(text, "%02X", instance->iso15693_result.uid_readback[i]);
        }
        widget_add_string_multiline_element(
            widget, 0, 13, AlignLeft, AlignTop, FontSecondary, furi_string_get_cstr(text));
        furi_string_free(text);
    } else if(gen1_failed) {
        // The opt-in gen1 UID sequence didn't verify. It is sent before anything is checked, as four
        // ordinary WRITE BLOCKs that any writable tag accepts, so on the tag this most likely is --
        // an ordinary one -- those four blocks are gone. The user consented to that risk; they still
        // need to be told it was spent, and on which blocks, to restore them from a backup.
        widget_add_string_element(
            widget, 64, 0, AlignCenter, AlignTop, FontPrimary, "gen1 failed");
        widget_add_string_multiline_element(
            widget,
            0,
            13,
            AlignLeft,
            AlignTop,
            FontSecondary,
            "UID didn't take, so not\na gen1 card. 56/57/62/63\nmay be overwritten.");
    } else if(uid_unverifiable) {
        // Write UID was asked for the UID the card already has -- the editor pre-seeds itself from the
        // last Info read, so this is two menu taps away. Nothing was sent: a read-back against a UID
        // the card already carries is passed by any tag at all, so the Success it would have earned
        // would have said nothing about the card. Tell the user how to get an answer instead.
        widget_add_string_element(
            widget, 64, 0, AlignCenter, AlignTop, FontPrimary, "UID unchanged");
        widget_add_string_multiline_element(
            widget,
            0,
            13,
            AlignLeft,
            AlignTop,
            FontSecondary,
            "Already this card's UID,\nso nothing was written.\nTry a different one.");
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
            "The saved file has no\ndata blocks to clone.\nNothing was written.");
    } else {
        // A card lost during an opt-in gen1 run has already had the four backdoor registers written at,
        // so say so -- this screen is the only report that run produces, and "Card removed" alone would
        // hide damage the user consented to but was never told had been spent.
        const bool gen1_spent = card_lost && instance->iso15693_result.gen1_attempted &&
                                !instance->iso15693_result.used_gen1;
        const char* message =
            gen1_spent ? "Card removed mid-write.\ngen1 was attempted:\n56/57/62/63 may differ." :
            card_lost  ? "Card removed\nbefore the write\ncould finish." :
                         "Not a magic tag.\nThis card doesn't\nsupport UID write.";
        widget_add_icon_element(widget, 83, 22, &I_WarningDolphinFlip_45x42);
        widget_add_string_element(
            widget, 64, 0, AlignCenter, AlignTop, FontPrimary, "Write failed");
        widget_add_string_multiline_element(
            widget, 0, 13, AlignLeft, AlignTop, FontSecondary, message);
    }

    if(nfc_magic_scene_iso15693_write_fail_is_retryable(instance, reason)) {
        // Retry re-runs the write; Exit leaves. Matches the generic write-fail screen (Retry left,
        // Exit right).
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
        // over-capacity / partial / a completed wipe are (qualified) successes -> "Finish"; not-magic is
        // a failure -> "Back". The primary exit sits on the left, like the Gen2/USCUID/gen4 screens.
        widget_add_button_element(
            widget,
            GuiButtonTypeLeft,
            (over_capacity || partial || wipe_complete) ? "Finish" : "Back",
            nfc_magic_scene_iso15693_write_fail_widget_callback,
            instance);
        if(nfc_magic_scene_iso15693_write_fail_has_details(instance, reason)) {
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

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == GuiButtonTypeLeft) {
            if(nfc_magic_scene_iso15693_write_fail_is_retryable(instance, reason)) {
                // Retry: back to the write scene, which re-runs the write on enter.
                consumed = scene_manager_previous_scene(instance->scene_manager);
            } else {
                // Finish / Back -> the ISO15693 menu.
                consumed = scene_manager_search_and_switch_to_previous_scene(
                    instance->scene_manager, NfcMagicSceneIso15693);
            }
        } else if(event.event == GuiButtonTypeRight) {
            if(nfc_magic_scene_iso15693_write_fail_has_details(instance, reason)) {
                // Details -> the affected-block list (failed blocks, or the empty top blocks).
                scene_manager_next_scene(
                    instance->scene_manager, NfcMagicSceneIso15693PartialDetails);
                consumed = true;
            } else {
                // The retryable outcomes' "Exit" -> the ISO15693 menu.
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
