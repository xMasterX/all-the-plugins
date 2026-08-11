#include "../nfc_magic_app_i.h"
#include "nfc_magic_scene_partial_details_common.h"

// The per-block "which blocks didn't write/clear" list for an ISO15693 partial clone/wipe, reached
// via "Details" on the partial summary -- mirrors the Gen2 / USCUID-UL partial-details screens.
void nfc_magic_scene_iso15693_partial_details_on_enter(void* context) {
    NfcMagicApp* instance = context;
    Widget* widget = instance->widget;

    // Reached from the write-fail summary (still on the stack): its reason picks the title. For an
    // over-capacity success these are the empty blocks the card can't physically hold (not a failure),
    // so soften the wording; for a partial they're the blocks that wouldn't write / clear.
    const uint32_t reason =
        scene_manager_get_scene_state(instance->scene_manager, NfcMagicSceneIso15693WriteFail);
    const bool over_capacity = (reason == NfcMagicIso15693WriteFailReasonOverCapacity);
    // A partial can reach this screen with NO failed blocks -- when its only problem is the gen1 UID
    // clobber or a rejected AFI/DSFID. Titling an empty list "Blocks not written" would be wrong, so
    // name the screen for what it actually shows.
    const bool has_block_list =
        (instance->iso15693_result.failed_count + instance->iso15693_result.over_capacity) > 0;
    // Whether the listed blocks are merely EMPTY ones past the card's physical capacity (nothing lost)
    // has to be decided from the capacity facts, not from the reason code: a Partial can consist purely
    // of an empty capacity tail plus a gen1 / AFI-DSFID caveat, and in that state over_capacity
    // only survived because the failures were a confirmed contiguous top tail. Calling those blocks
    // "not written" with no softening would overstate the damage on a clone that lost nothing.
    const bool only_empty_tail = (instance->iso15693_result.failed_count == 0) &&
                                 (instance->iso15693_result.over_capacity > 0);
    const bool empty_blocks = over_capacity || only_empty_tail;
    const char* title;
    if(empty_blocks) {
        title = "Empty top blocks";
    } else if(has_block_list) {
        title = (instance->iso15693_mode == NfcMagicIso15693ModeWipe) ? "Blocks not cleared" :
                                                                        "Blocks not written";
    } else {
        title = (instance->iso15693_mode == NfcMagicIso15693ModeWipe) ? "Wipe notes" :
                                                                        "Clone notes";
    }
    widget_add_string_element(widget, 0, 0, AlignLeft, AlignTop, FontPrimary, title);

    FuriString* message = furi_string_alloc();
    if(empty_blocks) {
        // These empty blocks are past the card's physical capacity -- no data was lost, but say why
        // they weren't written. ("Card too small" is reserved for the partial screen, where real data
        // IS lost; this clone's data all fit.)
        furi_string_cat_str(message, "Didn't fit on the card:\n");
    }
    // Scan the whole bitmap, not blocks_total: the wipe and gen1 paths reduce blocks_total
    // to a logical count that excludes the skipped backdoor blocks (56/57/62/63), yet failures are
    // recorded at their TRUE block index, which can exceed that reduced total. Unused bits are 0, so
    // only real failures print, each at its true index -- keeping this list consistent with the
    // "Not written/cleared: N" summary count.
    nfc_magic_partial_details_append_indices(
        message, instance->iso15693_result.failed_bitmap, ISO15693_POLLER_BLOCK_BITMAP_SIZE * 8, 0);
    // Separate the caveats from whatever precedes them, but don't open with a blank line when there is
    // no block list above (the notes-only case).
    if(instance->iso15693_result.sweep_truncated) {
        // The blocks above the cut are not in the bitmap -- they were never attempted, so there is
        // nothing to list -- which is exactly why they need saying here. This is the only route to that
        // fact on the UID-changed screen, whose reason code pre-empts the partial one, and the only
        // place the partial screen's own count can be qualified.
        if(furi_string_size(message) > 0) furi_string_push_back(message, '\n');
        furi_string_cat_printf(
            message,
            "Sweep hit its time limit at block %u of the %u this card claims. Blocks above that were "
            "never attempted and may still hold data.",
            instance->iso15693_result.blocks_total,
            instance->iso15693_result.blocks_advertised);
    }
    if(instance->iso15693_result.used_gen1) {
        // The gen1 fallback stamped the UID/commit into blocks 56/57/62/63, so they differ from the
        // source regardless of the write results above.
        if(furi_string_size(message) > 0) furi_string_push_back(message, '\n');
        furi_string_cat_str(message, "gen1: 56/57/62/63 hold UID + unlock/commit, not file data.");
    }
    if(instance->iso15693_result.identity_failed) {
        // The card rejected the standard WRITE AFI / WRITE DSFID, so those identity fields may not
        // match the source.
        if(furi_string_size(message) > 0) furi_string_push_back(message, '\n');
        furi_string_cat_str(message, "AFI/DSFID: card rejected the write.");
    }
    widget_add_text_scroll_element(widget, 0, 13, 128, 51, furi_string_get_cstr(message));
    furi_string_free(message);

    view_dispatcher_switch_to_view(instance->view_dispatcher, NfcMagicAppViewWidget);
}

bool nfc_magic_scene_iso15693_partial_details_on_event(void* context, SceneManagerEvent event) {
    NfcMagicApp* instance = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeBack) {
        consumed = scene_manager_previous_scene(instance->scene_manager);
    }
    return consumed;
}

void nfc_magic_scene_iso15693_partial_details_on_exit(void* context) {
    NfcMagicApp* instance = context;
    widget_reset(instance->widget);
}
