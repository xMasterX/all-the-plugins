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
    const bool wipe_mode = (instance->iso15693_mode == NfcMagicIso15693ModeWipe);
    // Bound the list at the cut. What that saves differs by mode, and only the CLONE has the problem it
    // was written for:
    //
    //   clone -- the bitmap holds two different things at two different addresses. Below the cut are
    //     blocks the card was asked for and refused; at and above it are blocks the back-fill recorded so
    //     the "written" figure, derived by subtracting failures from the total, could not claim they
    //     landed. Only the lower group is a fact about the card, and listing them together names the
    //     upper group as refusals -- which is the whole complaint.
    //   wipe -- there is no back-fill. Nothing above the cut is recorded at all, so the bound is a no-op
    //     and the note below is the only thing that mentions those blocks.
    //
    // The bound is applied in both modes anyway, because a rule that holds in one and is inert in the
    // other is simpler than a mode test, and the note below carries the rest either way.
    const uint16_t list_upto = instance->iso15693_result.pass_truncated ?
                                   instance->iso15693_result.cut_block :
                                   (uint16_t)(ISO15693_POLLER_BLOCK_BITMAP_SIZE * 8);
    // A partial can reach this screen with NO failed blocks -- when its only problem is the gen1 UID
    // clobber, a rejected AFI/DSFID, or a cut that happened before anything was refused. Titling an
    // empty list "Blocks not written" would be wrong, so name the screen for what it actually shows.
    //
    // Asked over the SAME range that will be printed, which is now structural rather than a promise in a
    // comment: both go through list_upto. Not from failed_count, which on a cut run includes every
    // unattempted block above the cut.
    //
    // No `+ over_capacity` term: it was dead. over_capacity survives non-zero only down the
    // failures_are_top_tail branch, which requires an uncut pass, which forces list_upto to the full
    // range -- and every one of those blocks already has its bit set. So over_capacity > 0 implies the
    // bitmap is non-empty, and the sum read as though the two were complementary when one contains the
    // other.
    const bool has_block_list =
        nfc_magic_partial_details_any_index(instance->iso15693_result.failed_bitmap, list_upto);
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
        title = wipe_mode ? "Blocks not cleared" : "Blocks not written";
    } else {
        title = wipe_mode ? "Wipe notes" : "Clone notes";
    }
    widget_add_string_element(widget, 0, 0, AlignLeft, AlignTop, FontPrimary, title);

    FuriString* message = furi_string_alloc();
    if(empty_blocks) {
        // These empty blocks are past the card's physical capacity -- no data was lost, but say why
        // they weren't written. ("Card too small" is reserved for the partial screen, where real data
        // IS lost; this clone's data all fit.)
        furi_string_cat_str(message, "Didn't fit on the card:\n");
    }
    // The bound is list_upto, never blocks_total: the wipe and gen1 paths reduce blocks_total to a
    // logical count that excludes the skipped backdoor blocks (56/57/62/63), yet failures are recorded
    // at their TRUE block index, which can exceed that reduced total. Unused bits are 0, so on an
    // uncut run scanning the whole bitmap prints only real failures, each at its true index.
    nfc_magic_partial_details_append_indices(
        message, instance->iso15693_result.failed_bitmap, list_upto, 0);
    // Separate the caveats from whatever precedes them, but don't open with a blank line when there is
    // no block list above (the notes-only case).
    if(instance->iso15693_result.pass_truncated) {
        // The blocks above the cut are the ones the list above deliberately excludes, which is exactly
        // why they need saying here. This is also the only route to that fact on the UID-changed
        // screen, whose reason code pre-empts the partial one, and the only place either summary's
        // count can be qualified.
        if(furi_string_size(message) > 0) furi_string_push_back(message, '\n');
        // The cut index, never blocks_total: this is a claim about which blocks were TRIED, and
        // blocks_total is the highest that answered. Below the cut it under-reports (the trailing run
        // the tail-drop discards was attempted -- three writes and a read each -- yet would be excluded
        // by the sentence); above it, a card claiming 200 while holding 10 read "time limit at block
        // 10" about 170 blocks that were attempted and answered nothing.
        if(wipe_mode) {
            // Which side of the claim the cut lands on changes what is true, so it changes the
            // sentence. The sweep runs past the advertised count deliberately, so "of the N this card
            // claims" is only a frame when the cut is actually inside it.
            // <= not <: at equality the sweep stopped exactly AT the claim, having attempted the claimed
            // range and nothing beyond it, so block N itself was not attempted and the first sentence is
            // the accurate one. Strict < sent that case to the "past the N this card claims" wording.
            if(instance->iso15693_result.cut_block <=
               instance->iso15693_result.blocks_advertised) {
                furi_string_cat_printf(
                    message,
                    "Sweep hit its time limit at block %u of the %u this card claims. Blocks above "
                    "that were never attempted and may still hold data.",
                    instance->iso15693_result.cut_block,
                    instance->iso15693_result.blocks_advertised);
            } else {
                furi_string_cat_printf(
                    message,
                    "Sweep hit its time limit at block %u, past the %u this card claims. Every "
                    "claimed block was attempted; anything above the cut was not.",
                    instance->iso15693_result.cut_block,
                    instance->iso15693_result.blocks_advertised);
            }
        } else {
            // A clone has no advertised count to measure against -- its denominator is the source -- so
            // it states the cut alone. What re-running can do is the shared clause below; it is the same
            // answer in both modes and was wrong to phrase as a clone-specific promise.
            furi_string_cat_printf(
                message,
                "Clone hit its time limit at block %u. Blocks from there up were never sent to the "
                "card -- they are counted as not written, but the card did not refuse them.",
                instance->iso15693_result.cut_block);
        }
        // What Retry can and cannot do, said once for both modes. The bound is a WALL CLOCK, not a
        // position, so re-running repeats the same work against the same budget: a card that is
        // consistently this slow is cut in the same place every time, and only a transient -- marginal
        // coupling forcing per-block retries -- clears on a second pass. So the wording may not promise
        // that a retry succeeds: the Retry button already implies it, and this is the only place that
        // can qualify it.
        furi_string_cat_str(
            message,
            "\nRetrying may get further, but the limit is a time budget rather than a position: if it "
            "stops at the same block, the card is too slow to finish in one pass rather than refusing.");
    }
    if(wipe_mode && !instance->iso15693_result.uid_verified) {
        // The wipe has three outcome screens and only "Wipe complete" has a spare body line for this,
        // so on the other two the check that never ran was asserted by omission. The field's own doc
        // says a caller reporting success should say so, and two of the three callers could not.
        //
        // It bites hardest on the card the check exists for. The sweep zeroes 56/57 at INDEX 56/57 --
        // long before any plausible cut -- so on a gen1 card left armed by an earlier UID write the UID
        // registers are already overwritten by the time a truncation is decided. "Wipe stopped" then
        // offers Retry without saying the identity check never reached an answer.
        if(furi_string_size(message) > 0) furi_string_push_back(message, '\n');
        furi_string_cat_str(
            message,
            "UID not re-checked: the card did not answer after the field reset, so whether the wipe "
            "moved its UID is unknown.");
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
