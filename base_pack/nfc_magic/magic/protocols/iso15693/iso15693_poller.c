#include "iso15693_poller.h"
#include <furi.h>
#include <nfc/nfc_poller.h>
#include <lib/nfc/protocols/iso15693_3/iso15693_3_poller.h>
#include <toolbox/bit_buffer.h>

#define TAG "Iso15693Poller"

// ISO15693_3_FDT_WRITE_POLL_FC is the WRITE-frame response timeout (271200 carrier cycles ~= 20ms,
// long enough for the tag to program its EEPROM before it answers). Older SDKs predate this macro,
// so keep a fallback with the same value the current SDK defines.
#ifndef ISO15693_3_FDT_WRITE_POLL_FC
#define ISO15693_3_FDT_WRITE_POLL_FC (271200U)
#endif

// Magic ISO15693 ("Chinese magic") backdoor UID write, ported from proxmark3 (GPLv3)
// SetTag15693Uid / SetTag15693Uid_v2 (armsrc/iso15693.c). Unaddressed frames are sent to
// hidden backdoor blocks; the CRC is appended by iso15693_3_poller_send_frame. Two card
// generations exist: the write always tries gen2 first, and offers gen1 -- which is destructive on a
// non-magic tag -- only as an explicit user opt-in after gen2 leaves the UID unchanged.
#define ISO15693_MAGIC_FLAGS (0x02U) // high data rate, unaddressed (ISO15_REQ_DATARATE_HIGH)

// gen1: WRITE BLOCK (0x21) to backdoor blocks; 4 data bytes each. The UID blocks are named by the
// UID bytes they carry (uid[0] is the MSB, so uid[7..4] is the numerically low half of the UID).
#define ISO15693_MAGIC_CMD_WRITE    (0x21U) // ISO15693 WRITE BLOCK
// The unlock/commit reading is OUR INFERENCE from proxmark's send order, not a documented contract:
// SetTag15693Uid sends 0x3E, 0x3F then the UID blocks with no explanatory comment, and doc/
// magic_cards_notes.md's ISO15693-magic section is a TODO. Nothing states what the registers do.
#define ISO15693_MAGIC_BLK_UNLOCK   (0x3EU) // written as 0; inferred: unlock
#define ISO15693_MAGIC_BLK_COMMIT   (0x3FU) // written as 0x6996; inferred: arms the UID change
#define ISO15693_MAGIC_BLK_UID_7654 (0x38U) // uid[7..4]
#define ISO15693_MAGIC_BLK_UID_3210 (0x39U) // uid[3..0]

// The same four as one list, because four places ask "is this a backdoor block?": the clone loop skips
// them, its back-fill skips them again, the reported total deducts them, and the source inspection
// reads them. Each used to spell the set out, so the set existed four times and could disagree with
// itself in four ways. Membership only -- the gen1 write SEQUENCE is ordered and stays written out at
// its call site, where the order is the point.
static const uint8_t iso15693_poller_backdoor_blocks[] = {
    ISO15693_MAGIC_BLK_UID_7654,
    ISO15693_MAGIC_BLK_UID_3210,
    ISO15693_MAGIC_BLK_UNLOCK,
    ISO15693_MAGIC_BLK_COMMIT,
};

static bool iso15693_poller_is_backdoor_block(uint16_t block) {
    for(size_t i = 0; i < sizeof(iso15693_poller_backdoor_blocks); i++) {
        if(block == iso15693_poller_backdoor_blocks[i]) return true;
    }
    return false;
}

// Standard ISO15693 identity writes, used to make a clone match the source's AFI / DSFID.
#define ISO15693_MAGIC_CMD_WRITE_AFI   (0x27U) // ISO15693 WRITE AFI
#define ISO15693_MAGIC_CMD_WRITE_DSFID (0x29U) // ISO15693 WRITE DSFID

// gen2: magic write command (0xE0) with a 0x09 subcommand and a block reference; 4 data
// bytes each. Frame layout: 02 E0 09 <ref> d0 d1 d2 d3 (+CRC).
#define ISO15693_MAGIC_CMD_WRITE_V2     (0xE0U) // ISO15693_MAGIC_WRITE
#define ISO15693_MAGIC_V2_SUB           (0x09U)
#define ISO15693_MAGIC_V2_BLK_CFG       (0x47U) // system-info config: max block / block size / IC ref
#define ISO15693_MAGIC_V2_BLK_CFG2      (0x52U) // written as 0
#define ISO15693_MAGIC_V2_BLK_UID_7654  (0x40U) // uid[7..4]
#define ISO15693_MAGIC_V2_BLK_UID_3210  (0x41U) // uid[3..0]
// Fixed config payload for the CFG block, verbatim from proxmark's gen2 sequence (matches a
// 64-block / 4-byte-block / IC-ref-0x8B card; these values are constant in proxmark too).
#define ISO15693_MAGIC_V2_CFG_MAXBLOCK  (0x3FU)
#define ISO15693_MAGIC_V2_CFG_BLOCKSIZE (0x03U)
#define ISO15693_MAGIC_V2_CFG_IC_REF    (0x8BU)

#define ISO15693_POLLER_BUF_SIZE (32U)

// ISO15693 Get System Info stores (block size - 1) in a 5-bit field, so a block is at most 32 bytes.
#define ISO15693_MAX_BLOCK_SIZE (32U)

// Give up after this many consecutive activation failures so neither the detect popup nor the write
// popup can hang forever with no card. iso15693_3_poller_run delays exactly 100ms after each failed
// activation, so this is roughly a 4 second timeout plus each attempt's own airtime.
#define ISO15693_POLLER_MAX_ACTIVATION_ERRORS (40U)

// ...except after the wipe's power-cycle, where most of that budget buys nothing. That wait has no
// "user hasn't presented the card yet" phase to sit through -- the card was in the field a moment ago
// -- and the sweep already has a result to report, so the only question is whether the card comes back
// to be checked. Spending the full budget there freezes the popup for four seconds on the common case
// of the user lifting the card the instant the wipe finishes.
//
// Not cut to the minimum, though. Giving up early on a card that IS coming back would skip the UID
// check on precisely the card that check exists for, which is the failure this state was added to
// remove; a slow wait costs the user seconds, a short one costs the check. ~1.5s is well past what a
// present card should need -- the gen2 and gen1 verifies re-activate on the first attempt in practice
// -- while staying under "the app has hung". If a bench wipe ever logs "card did not return after the
// field reset" with the card still on the case back, this number is the thing to raise.
#define ISO15693_POLLER_WIPE_VERIFY_ACTIVATIONS (15U)

// The verify read-back runs right after an RF field power-cycle, so retry the inventory a few times:
// a card that is momentarily slow to answer must not be misreported as removed (a false CardLost on
// an otherwise-successful write).
#define ISO15693_POLLER_VERIFY_ATTEMPTS (3U)
#define ISO15693_POLLER_VERIFY_RETRY_MS (5U)

// Retry a failed clone WRITE BLOCK this many times before treating the block as genuinely unwritable.
// On these cards writes are gated by physical memory, so a block that fails EVERY attempt is past the
// card's real capacity; retrying rides out a transient RF error that would otherwise look like one.
#define ISO15693_POLLER_WRITE_ATTEMPTS (3U)

// How many progress updates a block pass may emit, in total.
//
// This is a hard safety bound, not a tuning knob. The block loops below run to completion inside a
// single poller callback on the Nfc worker thread, and a progress event ends up in
// view_dispatcher_send_custom_event, which blocks with FuriWaitForever on a queue of
// VIEW_DISPATCHER_QUEUE_LEN (16) entries. A Back press during a write puts the GUI thread inside
// nfc_poller_stop -> furi_thread_join waiting for this very worker, so it stops draining that queue.
// Emit more events than the queue can hold and the worker blocks forever, the join never returns,
// and the device hangs with the write popup on screen.
//
// Four of those names are not greppable from the shipped SDK headers -- VIEW_DISPATCHER_QUEUE_LEN, the
// FuriWaitForever in the send, iso15693_3_poller_filter_error and write_block_response_parse. They were
// read in the firmware source (applications/services/gui/view_dispatcher.c and
// lib/nfc/protocols/iso15693_3/), which the app builds against but does not ship. Stated so that
// finding nothing is not mistaken for the argument being stale: the bound is load-bearing.
//
// Staying well under 16 keeps that impossible. Emitting per block does NOT -- if you want that, the
// loop has to yield to the Nfc worker between blocks (return NfcCommandContinue and resume from a
// cursor) the way uscuid_ul_poller.c does, and only then is per-block safe.
#define ISO15693_POLLER_PROGRESS_STEPS (8U)

// The wipe sweeps ABOVE the card's advertised block count, because that count is not the card's
// capacity -- the gen2 CFG frame PROGRAMS what the card advertises. Cloning a 28-block source onto a
// 64-block card leaves it advertising 28, and a wipe bounded by that clears 28 of 64 and calls it
// Success. Measured on hardware 2026-08-04: seed all 64 blocks with a marker, clone a 28-block source
// over it, wipe (screen said Success), then read with a proxmark -- block 20 was zeroed, blocks 28, 40
// and 63 still returned the marker. 36 of 64 blocks survived a "successful" wipe.
//
// Only a WRITE settles whether a block exists. A high block that has never been written may not read
// either (see the read-back note in the loop), so a read-based capacity probe under-detects. Hence:
// write upward, and stop once a run of blocks looks absent.
//
// 256 is the ISO15693 block-number space, the failure bitmap's capacity, and the same bound proxmark's
// own `hf 15 wipe` loop carries (`for(i = 0; i < 0x100; i++)`, cmdhf15.c) -- though note proxmark breaks
// at the FIRST refused write, so it never reaches that bound; the tolerance below is ours, not its
// precedent. It is a ceiling, not a cost: the sweep stops at the card's real top plus
// ISO15693_POLLER_WIPE_ABSENT_RUN probes and one re-probe of the run.
//
// Writing above physical capacity is inert on this silicon rather than destructive: the probe suite's
// `edgepages` test found phantom writes rejected, phantom reads failing, and block 0 unchanged
// (`aliased: false`) across four runs. A card that DID alias would only receive the zeros a wipe is
// writing anyway.
#define ISO15693_POLLER_WIPE_MAX_BLOCKS (ISO15693_POLLER_BLOCK_BITMAP_SIZE * 8U)

// Wall-clock bound on the sweep, because the block ceiling above is not a tight one.
//
// The absent-run check ends the sweep at the card's top, but only for a card that stops ANSWERING
// there. A card that refuses the write and still serves a read at every address never accumulates a
// run, so it walks all 256 blocks at the 40-70ms a refused-write-plus-read costs: 10-18 seconds inside
// a single poller callback, emitting no progress past the advertised count.
//
// This is a BACKSTOP, not a tuning knob, and the two errors it sits between are wildly asymmetric:
//   - cutting a legitimate sweep early leaves real data unwiped above the cut. That is the privacy
//     failure this whole sweep exists to remove, arrived at from the other direction.
//   - letting a pathological card run long makes the user wait, un-abortably, for a wipe they asked for.
// So it is set generously, well clear of any sweep a real card can ask for, and NOT tuned down for the
// second case -- the block ceiling already caps that at roughly the 18s above.
//
// 10 seconds. Working from the ~1s a 64-block wipe takes (see ISO15693_POLLER_WIPE_ABSENT_RUN): a
// refused block costs 3 writes plus 3 waits plus a read -- the retry loop has no break before its
// last delay -- so an accepted one is a fraction of the 40-70ms, and the largest sweep any card can ask for -- 256 blocks all accepting -- lands somewhere
// around 3-4s. Every figure here is an estimate from bench runs that were not instrumented for timing,
// which is itself an argument for the wide margin. Note the true worst case is this bound PLUS one
// re-probe of the trailing run, which is not deadline-checked (it would have to abandon the run
// half-classified); that is bounded by the run length.
//
// A run cut here reports what it covered and says it was cut (pass_truncated), rather than passing
// off a partial range as the card's extent -- the same distinction the advertised-count floor draws.
//
// BOTH passes use this, and the figures above are a WIPE's: a 4-byte zero write, the same payload
// every time. A clone's data pass costs more per block -- its payload is the source's block size, up
// to 32 bytes, so up to 8x the frame -- and since the probe went onto every persistent failure rather
// than only the empty ones, more again. So a clone reaches this bound sooner than the arithmetic here
// implies, and the shared name is deliberate: whatever this is set to has to be defensible for the
// more expensive of the two, not the cheaper. See the capacity test in write_source_blocks for the
// one place that costs something.
#define ISO15693_POLLER_PASS_MAX_MS (10000U)

// How many CONSECUTIVE absent-looking blocks end the sweep. One is not evidence -- the same reasoning
// as the per-block write retries -- and this is the sole guard against the sweep mistaking a momentary
// RF dropout for the top of the card, so it is set for that, not for speed.
//
// The hazard is a coupling wobble that recovers: a card shifting on the case back for tens of
// milliseconds is too brief for the card-present inventory below to notice anything wrong, but long
// enough to kill a handful of consecutive blocks. Read as the top of the card, that ends the sweep
// early and reports a clean Success over blocks that were never cleared -- the same silent residue this
// sweep exists to remove. (An actual removal is a different case and is already handled: a hand takes
// 200ms+, so the inventory finds the card gone and the wipe reports CardLost.)
//
// Writing one block off costs three refused writes, three 5ms waits and one refused read. A refused
// write returns quickly rather than burning the full FDT timeout -- the card answers, with an in-band
// error (Iso15693_3ErrorInternal, which the SDK produces from a well-formed error response, not from a
// malformed frame or silence) -- so the waits are a large share of it. Bench runs put the whole per-block
// cost somewhere in the 40-70ms range; the two runs disagree by about 2x and neither was instrumented for
// this, so treat the figure as an estimate, not a measurement.
//
// At 8 blocks that is a few hundred milliseconds of tolerance, and about the same again spent past the
// card's real top on every wipe. Against a ~1s wipe that is a real cost, and it is the reason not to keep
// raising it. A dropout shorter than the run resolves itself -- the next block that answers zeroes the
// counter and those blocks are reported as unwiped rather than dropped -- and a dropout that DOES fill
// the run is caught by the re-probe at the trip, which is what makes the run length a tolerance rather
// than a blind spot.
//
// The run length is deliberately NOT the only guard, and it never ends the sweep below the advertised
// count -- the card claims those blocks, so the sweep attempts all of them however they answer. Above
// that, the re-probe at the trip is what makes a filled run recoverable, so this number sets how much
// dropout is absorbed silently rather than how much is caught.
// What remains: a card whose memory is present but answers neither a write nor a read across a whole run,
// even on re-probe, is indistinguishable from one that ends there by any means available here. Note the
// counting rule that keeps the fake-flash case honest -- a trailing run is judged by whether it answers,
// never by the advertised count, so a card claiming 66 blocks against 64 physical still drops 64/65
// rather than reporting them as "not cleared".
#define ISO15693_POLLER_WIPE_ABSENT_RUN (8U)

// Write-mode state machine. Each verify runs after a NfcCommandReset field power-cycle.
typedef enum {
    Iso15693WriteStateStart, // note the current UID, send the backdoor UID (gen2, or gen1 on an
        // opt-in gen1 run), request a field reset
    Iso15693WriteStateVerifyGen2, // verify gen2: on a match write the payload; if the UID is untouched
        // report NotGen2 and STOP so the scene can offer the gen1 opt-in (it is not sent from here)
    Iso15693WriteStateVerifyGen1, // verify the opt-in gen1 UID; on a match write the payload
    Iso15693WriteStateVerifyWipe, // the sweep is done: check the wipe didn't move the card's UID
} Iso15693WriteState;

struct Iso15693Poller {
    NfcPoller* poller;
    Iso15693_3Data* data; // last read result (Info mode), kept for the info scene after poller free
    Iso15693PollerMode mode;
    uint8_t target_uid[ISO15693_3_UID_SIZE];
    uint8_t original_uid[ISO15693_3_UID_SIZE]; // UID before the write, to gate the gen1 fallback
    Iso15693WriteState write_state;
    // This run is the opt-in gen1 attempt (write the gen1 UID, verify it, then the payload), entered
    // from the "not gen2 magic" screen. Set for a clone AND for a bare Write-UID. A normal run leaves
    // this false and tries gen2 first, offering gen1 only if gen2 leaves the UID unchanged.
    bool attempt_gen1;
    uint32_t activation_errors; // consecutive activation failures (no card) -> timeout
    // Clone mode: the source image (kept separate from `data` so start_internal's reset can't wipe
    // it) and per-block write results.
    Iso15693_3Data* clone_source;
    uint16_t clone_blocks_total; // clone: blocks on the source image, less 56/57/62/63 on a gen1 run
        // (they carry the UID, not source data). wipe: the advertised count while the sweep runs, so the
        // progress popup has a denominator, then the count the card PROVED it holds once it ends
    // Clone mode: blocks that failed to write and count as a real problem: either they held source
    // data (lost), or they were empty failures that did NOT form a clean capacity tail (so we can't
    // call them over-capacity). Drives Partial. Wipe mode: reused as the count of blocks that still
    // held data after a failed zero-write (i.e. all wipe failures).
    uint16_t clone_failed_count;
    // Clone mode: empty source blocks past the card's real capacity -- a contiguous run above the last
    // block that did write, so nothing was lost. Drives the "clone complete, with a note" Success.
    // Wipe mode: unused (stays 0).
    uint16_t clone_over_capacity;
    // Wipe mode: the block count the card advertised, kept alongside the measured figure that replaces
    // it in clone_blocks_total once the sweep ends. Clone mode: unused (stays 0).
    uint16_t wipe_advertised;
    // Wipe mode: the sweep hit ISO15693_POLLER_PASS_MAX_MS and stopped short, so its range is a cut
    // rather than the card's extent and the report has to say so. Clone mode: unused (stays false).
    bool pass_truncated;
    // Where the clock cut the pass: the first block index NOT attempted. Only meaningful alongside the
    // truncation flag above. Kept as its own figure because nothing else in the result carries it --
    // clone_blocks_total is the source's count, and after a wipe it is the highest block that ANSWERED,
    // which sits at or below the cut and can sit far below it. A sweep that walks past the advertised
    // count before the clock fires has a cut index above that count and a total below it, so the two
    // are not interchangeable in either direction.
    uint16_t pass_cut_block;
    // Wipe mode: the post-power-cycle UID check reached an answer. False means it never ran to one, so
    // uid_changed being false is an absent observation rather than a clean result.
    bool uid_verified;
    uint8_t clone_failed_bitmap[ISO15693_POLLER_BLOCK_BITMAP_SIZE];
    // Set when the gen1 fallback (not gen2) actually set the UID. gen1 stamps the UID/commit into
    // data blocks 56/57/62/63, so a clone that fell back to gen1 can't be byte-identical there.
    // NOTE: the gen1 path is NOT hardware-validated -- we only have a gen2 test card. See the PR note.
    bool clone_used_gen1;
    // Set when the blocks that couldn't be written are a persistent, contiguous run at the very top
    // of the card -- the signature of "source larger than the card's physical capacity". Gates the
    // "Card too small" message; a scattered/anomalous failure leaves it false (generic report).
    bool clone_capacity_confirmed;
    // Clone mode: the source reported an AFI / DSFID, but reading it back with GET SYSTEM INFO did not
    // return that field carrying the source's value, so the copy does not advertise it. Decided by
    // read-back rather than by the write's return value, so neither an in-band refusal nor a silent
    // accept is misread (see iso15693_poller_write_identity). Downgrades the clone to Partial with a
    // note; it never fails the clone -- the UID and data blocks are the real payload.
    bool clone_afi_failed;
    bool clone_dsfid_failed;
    uint16_t clone_blocks_done; // blocks attempted so far, for the progress popup
    uint8_t progress_step; // last progress band emitted this pass (see PROGRESS_STEPS)
    // The gen2 backdoor moved the UID to neither the original nor the target. That is the one outcome
    // that PROVES the card is magic -- an inert tag cannot change its UID -- so it must not be reported
    // as "not a magic tag". uid_readback holds what the card actually answered with.
    bool uid_unexpected;
    uint8_t uid_readback[ISO15693_3_UID_SIZE];
    // The destructive gen1 UID sequence was sent this run, so blocks 56/57/62/63 have been overwritten
    // whatever the outcome. Reported so a gen1 failure can name them instead of saying only "not a
    // magic tag". Equal to attempt_gen1 -- kept as its own result field so the scene doesn't have to
    // know when the gen1 frames are sent. Note "unconditionally" would be too strong: two paths return
    // from Start before the send (a Write-UID asking for the card's own UID, and an empty clone source),
    // leaving this true with nothing transmitted. Neither is reachable from the opt-in screen today,
    // but start_clone_gen1 / start_write_uid_gen1 are public entry points.
    bool gen1_attempted;
    // Write-UID only: the requested UID is the one the card already has, so reading it back afterwards
    // proves nothing about the card -- a plain tag passes the check having ignored every frame. The run
    // stops before writing and reports this instead of a Success it cannot justify.
    bool uid_unverifiable;
    // Wipe only: the UID read back after the wipe is not the one the card presented before it. Only ever
    // set from a POSITIVE observation -- see Iso15693WriteStateVerifyWipe.
    bool uid_changed;
    Iso15693PollerCallback callback;
    void* context;
    bool running;
};

// gen1 frame: 02 21 <block> d0 d1 d2 d3 (+CRC).
static void iso15693_poller_build_gen1_frame(
    BitBuffer* tx,
    uint8_t block,
    uint8_t d0,
    uint8_t d1,
    uint8_t d2,
    uint8_t d3) {
    bit_buffer_reset(tx);
    bit_buffer_append_byte(tx, ISO15693_MAGIC_FLAGS);
    bit_buffer_append_byte(tx, ISO15693_MAGIC_CMD_WRITE);
    bit_buffer_append_byte(tx, block);
    bit_buffer_append_byte(tx, d0);
    bit_buffer_append_byte(tx, d1);
    bit_buffer_append_byte(tx, d2);
    bit_buffer_append_byte(tx, d3);
}

// gen2 frame: 02 E0 09 <ref> d0 d1 d2 d3 (+CRC).
static void iso15693_poller_build_gen2_frame(
    BitBuffer* tx,
    uint8_t ref,
    uint8_t d0,
    uint8_t d1,
    uint8_t d2,
    uint8_t d3) {
    bit_buffer_reset(tx);
    bit_buffer_append_byte(tx, ISO15693_MAGIC_FLAGS);
    bit_buffer_append_byte(tx, ISO15693_MAGIC_CMD_WRITE_V2);
    bit_buffer_append_byte(tx, ISO15693_MAGIC_V2_SUB);
    bit_buffer_append_byte(tx, ref);
    bit_buffer_append_byte(tx, d0);
    bit_buffer_append_byte(tx, d1);
    bit_buffer_append_byte(tx, d2);
    bit_buffer_append_byte(tx, d3);
}

// Magic cards may not answer these writes, so per-frame transceive results are intentionally
// ignored. (The UID read-back is the real check.)
static void
    iso15693_poller_send_backdoor_uid_gen1(Iso15693_3Poller* iso_poller, const uint8_t* uid) {
    BitBuffer* tx = bit_buffer_alloc(ISO15693_POLLER_BUF_SIZE);
    BitBuffer* rx = bit_buffer_alloc(ISO15693_POLLER_BUF_SIZE);

    iso15693_poller_build_gen1_frame(tx, ISO15693_MAGIC_BLK_UNLOCK, 0x00, 0x00, 0x00, 0x00);
    iso15693_3_poller_send_frame(iso_poller, tx, rx, ISO15693_3_FDT_WRITE_POLL_FC);

    iso15693_poller_build_gen1_frame(tx, ISO15693_MAGIC_BLK_COMMIT, 0x69, 0x96, 0x00, 0x00);
    iso15693_3_poller_send_frame(iso_poller, tx, rx, ISO15693_3_FDT_WRITE_POLL_FC);

    iso15693_poller_build_gen1_frame(
        tx, ISO15693_MAGIC_BLK_UID_7654, uid[7], uid[6], uid[5], uid[4]);
    iso15693_3_poller_send_frame(iso_poller, tx, rx, ISO15693_3_FDT_WRITE_POLL_FC);

    iso15693_poller_build_gen1_frame(
        tx, ISO15693_MAGIC_BLK_UID_3210, uid[3], uid[2], uid[1], uid[0]);
    iso15693_3_poller_send_frame(iso_poller, tx, rx, ISO15693_3_FDT_WRITE_POLL_FC);

    bit_buffer_free(tx);
    bit_buffer_free(rx);
}

// The gen2 CFG block also programs what the card *reports* for system-info geometry / IC ref. For a
// clone these are the source's values (so the copy advertises the same chip identity); otherwise the
// fixed magic defaults.
static void iso15693_poller_send_backdoor_uid_gen2(
    Iso15693_3Poller* iso_poller,
    const uint8_t* uid,
    uint8_t cfg_maxblock,
    uint8_t cfg_blocksize,
    uint8_t cfg_icref) {
    BitBuffer* tx = bit_buffer_alloc(ISO15693_POLLER_BUF_SIZE);
    BitBuffer* rx = bit_buffer_alloc(ISO15693_POLLER_BUF_SIZE);

    iso15693_poller_build_gen2_frame(
        tx, ISO15693_MAGIC_V2_BLK_CFG, cfg_maxblock, cfg_blocksize, cfg_icref, 0x00);
    iso15693_3_poller_send_frame(iso_poller, tx, rx, ISO15693_3_FDT_WRITE_POLL_FC);

    iso15693_poller_build_gen2_frame(tx, ISO15693_MAGIC_V2_BLK_CFG2, 0x00, 0x00, 0x00, 0x00);
    iso15693_3_poller_send_frame(iso_poller, tx, rx, ISO15693_3_FDT_WRITE_POLL_FC);

    iso15693_poller_build_gen2_frame(
        tx, ISO15693_MAGIC_V2_BLK_UID_7654, uid[7], uid[6], uid[5], uid[4]);
    iso15693_3_poller_send_frame(iso_poller, tx, rx, ISO15693_3_FDT_WRITE_POLL_FC);

    iso15693_poller_build_gen2_frame(
        tx, ISO15693_MAGIC_V2_BLK_UID_3210, uid[3], uid[2], uid[1], uid[0]);
    iso15693_3_poller_send_frame(iso_poller, tx, rx, ISO15693_3_FDT_WRITE_POLL_FC);

    bit_buffer_free(tx);
    bit_buffer_free(rx);
}

// Make the clone match the source's AFI / DSFID via the standard ISO15693 WRITE AFI / WRITE DSFID
// commands (only for fields the source actually reported). Frames: 02 27 <afi> and 02 29 <dsfid>
// (+CRC). Each field is then READ BACK with GET SYSTEM INFO and compared; a field that doesn't match
// is recorded (clone_afi_failed / clone_dsfid_failed), which downgrades the clone to Partial but never
// fails it, since these are identity extras, not the core UID/data payload.
static void
    iso15693_poller_write_identity(Iso15693Poller* instance, Iso15693_3Poller* iso_poller) {
    const Iso15693_3SystemInfo* sys = &instance->clone_source->system_info;
    const bool want_dsfid = (sys->flags & ISO15693_3_SYSINFO_FLAG_DSFID) != 0;
    const bool want_afi = (sys->flags & ISO15693_3_SYSINFO_FLAG_AFI) != 0;
    if(!want_dsfid && !want_afi) return; // source reported neither field: nothing to reproduce

    BitBuffer* tx = bit_buffer_alloc(ISO15693_POLLER_BUF_SIZE);
    BitBuffer* rx = bit_buffer_alloc(ISO15693_POLLER_BUF_SIZE);

    // A field is only "written" once we have READ IT BACK and it matches, exactly like the UID. The
    // send return can't be trusted on its own: a tag refuses in-band, answering with the error flag set
    // in the response's flags byte plus an error code -- a well-formed, CRC-valid frame -- so
    // iso15693_3_poller_send_frame returns None and the refusal is invisible. (The SDK's own
    // write_block catches that by following send_frame with iso15693_3_write_block_response_parse;
    // there is no equivalent for WRITE AFI / WRITE DSFID.) The converse is just as wrong: a tag that
    // applies the write without answering looks like a failure. GET SYSTEM INFO is the only way to
    // read AFI/DSFID back, and it returns both, so one call verifies both fields.
    //
    // Retry the write+verify pair so a transient RF error isn't mistaken for a refusal -- the same
    // reasoning as the block loop's retries.
    bool dsfid_ok = !want_dsfid;
    bool afi_ok = !want_afi;
    for(uint32_t attempt = 0; attempt < ISO15693_POLLER_WRITE_ATTEMPTS && (!dsfid_ok || !afi_ok);
        attempt++) {
        if(!dsfid_ok) {
            bit_buffer_reset(tx);
            bit_buffer_append_byte(tx, ISO15693_MAGIC_FLAGS);
            bit_buffer_append_byte(tx, ISO15693_MAGIC_CMD_WRITE_DSFID);
            bit_buffer_append_byte(tx, sys->dsfid);
            iso15693_3_poller_send_frame(iso_poller, tx, rx, ISO15693_3_FDT_WRITE_POLL_FC);
        }
        if(!afi_ok) {
            bit_buffer_reset(tx);
            bit_buffer_append_byte(tx, ISO15693_MAGIC_FLAGS);
            bit_buffer_append_byte(tx, ISO15693_MAGIC_CMD_WRITE_AFI);
            bit_buffer_append_byte(tx, sys->afi);
            iso15693_3_poller_send_frame(iso_poller, tx, rx, ISO15693_3_FDT_WRITE_POLL_FC);
        }

        Iso15693_3SystemInfo readback = {0};
        if(iso15693_3_poller_get_system_info(iso_poller, &readback) == Iso15693_3ErrorNone) {
            // Require the target to ADVERTISE the field as well as hold the right value: a copy that
            // no longer reports the AFI/DSFID the source reported isn't a faithful clone either.
            if(!dsfid_ok && (readback.flags & ISO15693_3_SYSINFO_FLAG_DSFID) &&
               readback.dsfid == sys->dsfid) {
                dsfid_ok = true;
            }
            if(!afi_ok && (readback.flags & ISO15693_3_SYSINFO_FLAG_AFI) &&
               readback.afi == sys->afi) {
                afi_ok = true;
            }
        }
        if(dsfid_ok && afi_ok) break;
        furi_delay_ms(ISO15693_POLLER_VERIFY_RETRY_MS);
    }

    if(!dsfid_ok) FURI_LOG_W(TAG, "DSFID did not read back as written");
    if(!afi_ok) FURI_LOG_W(TAG, "AFI did not read back as written");
    instance->clone_dsfid_failed = !dsfid_ok;
    instance->clone_afi_failed = !afi_ok;

    bit_buffer_free(tx);
    bit_buffer_free(rx);
}

static void iso15693_poller_report(Iso15693Poller* instance, Iso15693PollerEvent event) {
    if(instance->callback) {
        instance->callback(event, instance->context);
    }
}

// Publish how far the block pass has got, at most ISO15693_POLLER_PROGRESS_STEPS times per pass.
// The bound is what keeps a Back press from deadlocking the app -- read the comment on that macro
// before changing anything here.
static void
    iso15693_poller_report_progress(Iso15693Poller* instance, uint16_t done, uint16_t total) {
    instance->clone_blocks_done = done;
    if(total == 0) return;

    // Emit only when we cross into a new 1/STEPS band. `step` is monotone in `done` and capped at
    // STEPS, so this can fire at most STEPS+1 times per pass -- the safety bound is structural
    // rather than something a second guard has to enforce.
    const uint8_t step = (uint8_t)(((uint32_t)done * ISO15693_POLLER_PROGRESS_STEPS) / total);
    if(step == instance->progress_step) return;
    instance->progress_step = step;

    iso15693_poller_report(instance, Iso15693PollerEventWriteProgress);
}

// The one write both block passes make. A block that fails EVERY attempt is genuinely unwritable;
// retrying rides out a transient RF error that would otherwise look like one. Retries only ever run
// on a failure, so a card that takes its writes pays nothing for them. There is deliberately no break
// before the last delay, which is where ISO15693_POLLER_PASS_MAX_MS gets its per-refused-block figure.
static Iso15693_3Error iso15693_poller_write_block_retried(
    Iso15693_3Poller* iso_poller,
    const uint8_t* data,
    uint8_t block,
    uint8_t size) {
    Iso15693_3Error error = Iso15693_3ErrorNone;
    for(uint32_t attempt = 0; attempt < ISO15693_POLLER_WRITE_ATTEMPTS; attempt++) {
        error = iso15693_3_poller_write_block(iso_poller, data, block, size);
        if(error == Iso15693_3ErrorNone) break;
        furi_delay_ms(ISO15693_POLLER_VERIFY_RETRY_MS);
    }
    return error;
}

static bool iso15693_poller_block_is_empty(const uint8_t* block, uint8_t size) {
    for(uint8_t i = 0; i < size; i++) {
        if(block[i] != 0) return false;
    }
    return true;
}

// Clone mode: write every data block from the source image with the standard ISO15693 WRITE BLOCK.
// Real write errors are counted into the failure bitmap for Partial reporting. Runs synchronously on
// the Nfc worker thread. When `skip_backdoor` is set (the gen1 path), blocks 56/57/62/63 are left
// untouched -- gen1 stores the UID/unlock/commit there, so writing source data over them would clobber
// the UID -- and they are excluded from the reported total, so "Cloned X/Y" counts only the blocks
// gen1 can carry. (gen2 passes false: its UID lives in a separate register space, so 56/57/62/63 are
// ordinary data blocks there.)
// Defined below, next to the inventory helper it wraps.
static bool iso15693_poller_card_still_present(Iso15693_3Poller* iso_poller);

// Returns false if the card left the field during the loop (the caller reports CardLost instead of a
// write result); true otherwise, with the counters/bitmap describing what happened.
static bool iso15693_poller_write_source_blocks(
    Iso15693Poller* instance,
    Iso15693_3Poller* iso_poller,
    bool skip_backdoor) {
    const Iso15693_3Data* source = instance->clone_source;
    uint16_t source_count = iso15693_3_get_block_count(source);
    // Straight out of a loaded .nfc, so hand-editable and unbounded by anything this app controls,
    // while the read-probe below fills a fixed 32-byte stack buffer. The wipe clamps the same value for
    // the same reason. Note this is the SOURCE's geometry: on gen2 the CFG frame makes the target match
    // it, but a gen1 target keeps its own block size, so a mismatch there makes every empty failure read
    // as absent and fabricates an over-capacity "Holds X/Y".
    const uint8_t source_block_size = iso15693_3_get_block_size(source);
    const uint8_t block_size = source_block_size > ISO15693_MAX_BLOCK_SIZE ?
                                   (uint8_t)ISO15693_MAX_BLOCK_SIZE :
                                   source_block_size;

    // A block number is a uint8_t on the wire and the failure bitmap holds this many bits, so only the
    // first 256 blocks can be attempted or accounted for. Real ISO15693 tags never exceed this; clamp
    // defensively so a corrupt/hand-edited source can't make clone_blocks_total overstate what was
    // actually written (which would skew the over-capacity "holds X of Y" report).
    //
    // Clamped ONCE, here, and never reassigned -- which is what lets both loops below bound themselves
    // on source_count alone. Re-testing the bitmap size in their headers restated this in two more
    // places without adding a guarantee.
    if(source_count > ISO15693_POLLER_BLOCK_BITMAP_SIZE * 8) {
        source_count = ISO15693_POLLER_BLOCK_BITMAP_SIZE * 8;
    }

    // Report the count of blocks we actually attempt: for gen1, exclude the 4 backdoor registers we
    // skip below so the "Cloned X/Y" total isn't inflated by blocks that only ever hold the UID.
    uint16_t total = source_count;
    if(skip_backdoor) {
        for(size_t i = 0; i < sizeof(iso15693_poller_backdoor_blocks); i++) {
            if(iso15693_poller_backdoor_blocks[i] < source_count) total--;
        }
    }
    instance->clone_blocks_total = total;
    instance->clone_failed_count = 0;
    instance->clone_over_capacity = 0;
    instance->clone_capacity_confirmed = false;
    memset(instance->clone_failed_bitmap, 0, sizeof(instance->clone_failed_bitmap));

    if(source_count == 0 || block_size == 0) return true;

    // Do our best to reproduce the card EXACTLY: attempt every source block. We deliberately do NOT
    // cap at the target's advertised block count. On these magic cards WRITE BLOCK is gated by
    // physical memory, not by the reported count (verified on hardware: writes succeed well past the
    // advertised count). Capping would also leave stale data in the reachable gap when re-cloning onto
    // a card that currently advertises fewer blocks. The only reliable capacity test is to write.
    //
    // Since magic cards ignore their own lock bits, a block that survives the retries is past the
    // card's physical capacity. Classify each persistent failure by whether the source had data there
    // (non-empty -> data lost; empty -> nothing lost), and track position so we can confirm below that
    // the failures are a contiguous run at the TOP of the card -- the capacity signature. A
    // scattered/interior failure is anomalous and won't be called capacity.
    //
    // Do NOT skip blocks locked in the SOURCE image: the source's lock bits describe the ORIGINAL
    // card, not the magic target (which is writable regardless), and locked blocks are exactly where
    // real tags keep provisioned data. Attempt every block.
    bool wrote_any = false; // at least one block accepted a write
    bool wrote_above_failure = false; // a block wrote ABOVE one that failed -> not a capacity tail
    bool any_failure_answered =
        false; // a failed block answered a read -> it exists, so no capacity edge
    uint16_t done = 0; // blocks attempted, the denominator the progress popup shows
    // Same wall-clock bound the wipe sweep carries, for the same reason and now a sharper one: Back is
    // swallowed for the whole ISO15693 write, so this loop is time the user cannot escape. It is
    // bounded only by the source's block count, and its card-present check is after the loop rather
    // than inside it. A card slipping off one block in costs the full write timeout on every remaining
    // block -- about 19s at the 256-block ceiling.
    //
    // Usually this fires on a card that has gone, where its effect is to reach the card-present check
    // below sooner and report CardLost. But it CAN fire with the card still present -- marginal coupling
    // makes blocks succeed only after retries, and enough of those on a large source spends the budget
    // while every write lands -- which is why the cut has to be remembered rather than just logged.
    const uint32_t pass_start = furi_get_tick();
    const uint32_t pass_budget = furi_ms_to_ticks(ISO15693_POLLER_PASS_MAX_MS);
    uint16_t block = 0;
    for(; block < source_count; block++) {
        if(furi_get_tick() - pass_start > pass_budget) {
            FURI_LOG_W(TAG, "clone: time limit reached at block %u of %u", block, source_count);
            // On the instance, not a local: the report needs this as much as the capacity test below
            // does. Kept local, it made the clone claim the card had REFUSED every block above the cut
            // -- named, counted and offered no Retry -- which is the same fabrication the capacity
            // guard exists to stop, one layer further out.
            instance->pass_truncated = true;
            instance->pass_cut_block = block;
            break;
        }
        if(skip_backdoor && iso15693_poller_is_backdoor_block(block)) {
            continue; // gen1 owns these; the gen1 UID sequence already wrote them
        }
        // Before the write, so a clean run reports progress too -- the success path below continues
        // straight to the next block.
        iso15693_poller_report_progress(instance, done++, total);
        const uint8_t* block_data = iso15693_3_get_block_data(source, block);
        const Iso15693_3Error error = iso15693_poller_write_block_retried(
            iso_poller, block_data, (uint8_t)block, block_size);
        if(error == Iso15693_3ErrorNone) {
            // A success ABOVE an earlier failure means the failures are not a run at the top, so
            // they cannot be the card's capacity edge.
            if(instance->clone_failed_count + instance->clone_over_capacity > 0) {
                wrote_above_failure = true;
            }
            wrote_any = true;
            continue;
        }
        FURI_LOG_W(TAG, "clone: block %u refused (err %d)", block, error);
        // Record every failed block in the bitmap so a result screen can name it, whichever bucket
        // it ends up in.
        instance->clone_failed_bitmap[block / 8] |= (uint8_t)(1u << (block % 8));

        const bool non_empty = !iso15693_poller_block_is_empty(block_data, block_size);

        // Ask the card whether the refused block is even there, rather than inferring it from the
        // source being empty plus the failures' position. The same question iso15693_poller_wipe_blocks
        // asks, for a different purpose: the wipe asks "does this block still hold data", the clone
        // asks "does this block exist at all".
        //
        // It discriminates because a block past physical capacity refuses reads as well as writes
        // (measured: writes come back Iso15693_3ErrorInternal, reads fail outright). So a block that
        // ANSWERS a read exists, which means its write failure was something else -- a transient that
        // outlasted every retry, or a block the card genuinely refuses -- and calling that the card's
        // capacity edge would be a fabricated claim about the user's hardware. Without this, a transient
        // near the end of the pass produces a perfect contiguous empty tail and the app reports
        // "All data written. Holds 60/64 blocks." about a card that holds 64.
        //
        // Paid on EVERY persistent failure, not only the empty ones. Emptiness decides which BUCKET a
        // failure lands in -- an empty one can be excused as past capacity, a non-empty one is lost
        // data either way -- but it does not decide whether the probe is worth its cost, because
        // capacity_confirmed is the strongest factual claim this feature makes about the user's
        // hardware ("Card too small") and a run containing ANY block that answers is not a capacity
        // edge, whatever its shape or its contents.
        uint8_t probe[ISO15693_MAX_BLOCK_SIZE] = {0};
        const bool block_absent =
            iso15693_3_poller_read_block(iso_poller, probe, (uint8_t)block, block_size) !=
            Iso15693_3ErrorNone;
        if(!block_absent) {
            FURI_LOG_W(
                TAG, "clone: block %u refused the write but answers reads", (unsigned)block);
            any_failure_answered = true;
        }

        if(non_empty || !block_absent) {
            instance->clone_failed_count++;
        } else {
            instance->clone_over_capacity++;
        }
    }
    // Blocks the clock cut off were never attempted, so nothing has recorded them. Left alone they
    // would be counted as written -- the partial screen derives its "cloned" figure by subtracting the
    // failures from the total -- so a clone stopped at block 10 of 256 would claim all 256 landed.
    // Record them as failures, which is what they are, and the bitmap then names them in Details.
    // Only reachable when the card is still present; a cut caused by the card leaving is reported as
    // CardLost below and these counters are discarded.
    for(; block < source_count; block++) {
        if(skip_backdoor && iso15693_poller_is_backdoor_block(block)) {
            continue;
        }
        instance->clone_failed_bitmap[block / 8] |= (uint8_t)(1u << (block % 8));
        instance->clone_failed_count++;
    }

    // `done` counts blocks ATTEMPTED, so this lands on 100% for a pass that ran to the end and on
    // wherever the clock stopped it for one that did not. Deliberate: the popup's last frame is the
    // last honest thing it can say, and a cut pass jumping to 100% would contradict the screen that
    // follows it.
    iso15693_poller_report_progress(instance, done, total);

    // Second half of the capacity test, and both halves are needed because they answer different
    // questions. The per-block read-back above establishes that each excused block is ABSENT; this
    // establishes that the absent blocks form a contiguous run at the very top of the card, with at
    // least one block below them having written -- the shape of "source bigger than the card". A hole
    // in the middle of otherwise-writable memory would pass the first test and must not pass this one.
    //
    // Tracked as "did anything write above a failure" rather than by comparing block indices: on the
    // gen1 path blocks 56/57/62/63 are skipped, so a capacity edge landing near them leaves a gap
    // between the last success and the first failure, and an index comparison reads that gap as a
    // scattered failure and downgrades a lossless clone to Partial.
    const bool any_failure = (instance->clone_failed_count + instance->clone_over_capacity) > 0;

    // A card lifted mid-loop makes every remaining block fail, which is EXACTLY the shape of the card's
    // physical capacity ending there -- and if the source's tail happens to be empty it would be
    // classified as over-capacity and reported as a clean "All data written." So before making any
    // capacity claim, check the card is still there. Only on the failure path, so a clean write pays
    // nothing. (Bail before classifying: the caller reports CardLost and ignores these counters.)
    if(any_failure && !iso15693_poller_card_still_present(iso_poller)) return false;

    // Position AND evidence: a contiguous run at the top is the shape of a capacity edge, but only a
    // run whose members all refused reads as well as writes is one.
    //
    // And only if they were ASKED. A pass the clock cut records its unreached blocks as failures, which
    // is the same top-tail shape with none of the meaning -- those blocks refused nothing, they were
    // never attempted. Left in, a slow-but-healthy card gets "Card too small", which is the fabricated
    // hardware claim the read-probe above exists to prevent, reached from the other direction.
    //
    // KNOWN OVERLAP, and it lands on the card this apparatus was least meant to miss. The cost of this
    // pass is dominated by FAILING blocks -- 3 writes, 3 retry waits and a read probe each, 40-70ms --
    // so ISO15693_POLLER_PASS_MAX_MS buys roughly 150-250 of them. And a card that is genuinely too
    // small presents precisely one long contiguous run of failing blocks. So a source about 150+ blocks
    // larger than the target spends the budget inside that run, truncates, and loses the "Card too
    // small" line to this very guard. Retrying does not help: the second run is cut in the same place,
    // so nothing ever names the cause.
    //
    // Left as it is anyway, and the reason is the direction of the error. Dropping the guard would let
    // a cut claim capacity from partial evidence -- the run below the cut looks like a top tail whether
    // or not memory resumes above it, and asserting the user's card is too small on evidence that stops
    // mid-run is the same fabrication in a new costume. Under-claiming leaves the user a correct report
    // and a Retry they can act on; over-claiming gives them a verdict about their hardware that the
    // pass never finished testing. A longer budget for the clone alone would close it, at the cost of
    // more seconds during which Back is swallowed -- see #252. Neither trade is worth taking silently,
    // so it is written down instead.
    const bool failures_are_top_tail = any_failure && wrote_any && !wrote_above_failure &&
                                       !any_failure_answered && !instance->pass_truncated;
    if(failures_are_top_tail) {
        instance->clone_capacity_confirmed = true;
    } else if(instance->clone_over_capacity > 0) {
        // Not a clean capacity tail (scattered/anomalous) -> don't report an over-capacity "success";
        // fold the empty failures into the plain failure count (the bitmap already carries them).
        instance->clone_failed_count += instance->clone_over_capacity;
        instance->clone_over_capacity = 0;
    }
    return true;
}

// Did this block hold data at activation? Only TRUE means anything: a non-zero byte can only have come
// from a real read, so it proves the block existed and held data. FALSE proves nothing -- see the
// read-back note in the sweep for why a zeroed entry is a failed read and an empty block alike.
// The range check is not defensive; iso15693_3_get_block_data furi_check()s its index.
static bool
    iso15693_poller_block_held_data(const Iso15693_3Data* data, uint16_t block, uint8_t size) {
    if(block >= iso15693_3_get_block_count(data)) return false;
    const uint8_t* cached = iso15693_3_get_block_data(data, (uint8_t)block);
    return !iso15693_poller_block_is_empty(cached, size);
}

// Resolves a whole run at once, which the tail-drop at the end of the sweep does NOT -- it decides
// block by block and can drop a block below one this kept. See the reconciliation there; the two rules
// only agree because the activation cache is a prefix.
//
// A block just proved it exists. Every absence still open below it is therefore interior memory that
// answered nothing -- a fault, not the space above the card -- so those count (their bits are already
// set) and the run closes. This block becomes the top the report is measured against.
static void iso15693_poller_wipe_note_present(
    Iso15693Poller* instance,
    uint16_t block,
    uint16_t* absent_run,
    bool* any_present,
    uint16_t* highest_present) {
    instance->clone_failed_count += *absent_run;
    *absent_run = 0;
    *any_present = true;
    if(block > *highest_present) *highest_present = block;
}

// Wipe mode: write zeros to every data block the card PHYSICALLY holds -- no UID command is sent.
// The card's advertised block count is only the starting point, not the bound: see
// ISO15693_POLLER_WIPE_MAX_BLOCKS for why, and for the hardware measurement that settled it. The sweep
// runs upward until a run of ISO15693_POLLER_WIPE_ABSENT_RUN blocks answers neither a write nor a read.
// We attempt every block rather than pre-skipping the target's locked ones: a magic card often ignores
// its own lock bits and accepts the write. Each refused write is then classified by a FRESH read-back
// (not the copy taken at activation -- see below), which answers a question the write cannot:
//   read OK, block non-zero -> write-protected and still holding data. A real failure; the wipe's
//                              promise wasn't kept there. Counted, and named in the bitmap.
//   read OK, block zero     -> already clear. Nothing was lost, so not a failure.
//   read FAILS              -> the block answers nothing at all, so it probably isn't there. Provisional:
//                              position decides. A later block that DOES answer proves this one interior
//                              and unreadable, which is not something to write off -- counted, fail
//                              closed. A run that the sweep ends on is the space above the card's real
//                              top, which was never the card's to clear -- dropped entirely.
// That last distinction is what stops a card advertising more blocks than it holds from reporting a
// partial wipe for blocks that do not exist.
// The gen1 backdoor registers (blocks 56/57/62/63) live in this same block-number space and ARE
// cleared, deliberately. Skipping them would spare a gen1 card's UID registers at the cost of leaving
// four blocks of real user data behind on every gen2 card, where they are ordinary memory -- a certain
// loss on the card we actually have, to hedge a hazard only gen1 has. Note the narrow form of the
// argument: a wipe cannot ARM a gen1 UID change by itself, since arming needs 0x6996 in the commit
// block and a wipe writes zero -- but that says nothing about a card that is armed ALREADY. See the
// OPEN QUESTION in the loop below.
// Returns the number of blocks that actually accepted the zero-write, so the caller can tell a
// genuine wipe from one where nothing could be cleared.
static uint16_t iso15693_poller_wipe_blocks(
    Iso15693Poller* instance,
    Iso15693_3Poller* iso_poller,
    bool* card_lost) {
    const Iso15693_3Data* target = nfc_poller_get_data(instance->poller);
    const uint16_t advertised = iso15693_3_get_block_count(target);
    const uint8_t block_size = iso15693_3_get_block_size(target);

    // The advertised count, as a LIVE denominator for the progress popup: the scene reads blocks_total
    // on every WriteProgress event, so leaving it 0 until the sweep ends renders "Wiping 33 / 0" for the
    // whole pass. Overwritten at the end with what the card actually proved it holds, before any terminal
    // event -- so the popup counts against the claim and the result reports against the measurement.
    instance->clone_blocks_total = advertised;
    instance->wipe_advertised = advertised;
    instance->clone_failed_count = 0;
    instance->clone_over_capacity = 0; // clone-only: a wipe never reports an over-capacity success
    memset(instance->clone_failed_bitmap, 0, sizeof(instance->clone_failed_bitmap));

    // No usable geometry to start from: report nothing wiped rather than sweeping a card that has told
    // us it has no blocks (and block_size is what the zero-write needs).
    if(advertised == 0 || block_size == 0) return 0;

    // 32-byte zero buffer covers every valid geometry; the clamp is belt-and-braces.
    uint8_t zeros[ISO15693_MAX_BLOCK_SIZE] = {0};
    const uint8_t size = block_size > sizeof(zeros) ? (uint8_t)sizeof(zeros) : block_size;
    uint16_t wiped = 0;

    // OPEN QUESTION, gen1 only: this loop writes the gen1 UID registers (56/57) before it reaches
    // unlock/commit (62/63). The gen1 arm sequence is unlock=0 then commit=0x6996 then the UID
    // blocks, and nothing ever clears commit again -- not this app, and not proxmark's
    // SetTag15693Uid -- so a card that has had a gen1 UID written may still be armed, and zeroing
    // 56/57 while it is would be the arm sequence with a zero payload.
    //
    // Do NOT try to de-arm by pre-writing the commit block. Writing commit before unlock is the
    // reverse of the only order anyone has observed the hardware accept, so it is either rejected
    // outright or -- worse -- leaves unlock freshly zeroed, one step INTO the arm sequence, immediately
    // before this loop touches the UID registers. No blind ordering is safe, because the only route to
    // the latch is through the sequence that sets it.
    //
    // The write ORDER is therefore left alone, matching proxmark's `hf 15 wipe`, which also makes no
    // attempt to de-arm. What IS done is the grounded half: the caller re-reads the UID once the sweep
    // finishes and reports a mismatch instead of promising the UID is unchanged. That check runs in
    // Iso15693WriteStateVerifyWipe, behind a field power-cycle -- which is what lets it see a gen1 latch
    // at all, since a card latches a UID written into 56/57 only on the next power-up and answers the
    // old one until then. It converts a silent identity change into a reported one; it does not prevent
    // the change.
    //
    // Note what this argument does NOT rest on. The unlock/commit reading above is our inference from
    // one implementation's send order, and if it is wrong the arming model is wrong with it -- but the
    // conclusion survives either way, because "no blind ordering is safe" follows from not knowing what
    // those registers do rather than from the interpretation being right. Settling the question needs a
    // gen1 card to test against, and nobody on this PR has one.
    bool any_present = false; // has any block answered at all?
    uint16_t highest_present = 0; // top block proven to exist -> the reported total
    // Consecutive absent-looking blocks. Doubles as the count of absences not yet resolved as
    // interior-or-tail, because the two are the same number by construction: a block that answers
    // resolves every absence below it and zeroes the counter, so unresolved absences are always
    // exactly the current run.
    uint16_t absent_run = 0;
    uint16_t block = 0;
    const uint32_t sweep_start = furi_get_tick();
    const uint32_t sweep_budget = furi_ms_to_ticks(ISO15693_POLLER_PASS_MAX_MS);
    for(; block < ISO15693_POLLER_WIPE_MAX_BLOCKS; block++) {
        // Time bound, checked before the block is attempted so `block` stays the exclusive end of the
        // attempted range for the tail arithmetic below. Compared as elapsed-against-budget rather
        // than against an absolute deadline, which would not survive a tick wraparound. See
        // ISO15693_POLLER_PASS_MAX_MS.
        if(furi_get_tick() - sweep_start > sweep_budget) {
            FURI_LOG_W(
                TAG, "wipe: time limit reached at block %u (advertised %u)", block, advertised);
            instance->pass_truncated = true;
            instance->pass_cut_block = block;
            break;
        }

        // Progress only while inside the advertised count. The sweep's real length isn't known until
        // it ends, so there is no honest denominator past that point, and the tail is a handful of
        // blocks. Bounding the emitting range this way also keeps the PROGRESS_STEPS safety bound
        // structural rather than something this loop has to re-argue.
        if(block < advertised) iso15693_poller_report_progress(instance, block + 1, advertised);

        // Retried at every block, not only above the advertised count: a transient below it reports a
        // false "wouldn't clear" AND leaves a block unwiped that a second attempt would have cleared.
        const Iso15693_3Error error =
            iso15693_poller_write_block_retried(iso_poller, zeros, (uint8_t)block, size);

        if(error == Iso15693_3ErrorNone) {
            // Took the zero-write, so it exists and is now clear. Anything absent below it therefore
            // wasn't the top of the card: those blocks are interior and unreadable, which we do NOT
            // write off -- fail closed and count them (their bits are already set).
            wiped++;
            iso15693_poller_wipe_note_present(
                instance, block, &absent_run, &any_present, &highest_present);
            continue;
        }

        // Re-read rather than trusting the copy taken at activation. That copy is not trustworthy:
        // iso15693_3_poller_activate passes its read_blocks result through
        // iso15693_3_poller_filter_error, which maps Timeout and NotSupported to None, so activation
        // can report success having stopped reading at the FIRST failed block, leaving every later
        // block at its zeroed allocation value. Believing that cache would score a block that still
        // holds data as empty and report a hollow "wipe complete" -- and the blocks most likely to
        // fail a read are exactly the high ones that don't read until first written.
        //
        // "Zeroed" is a fact, not an assumption: simple_array_init does a bare malloc with no
        // per-element init for a byte array, but Flipper's pvPortMalloc tail-calls memset(p, 0, size).
        // So an unread entry is genuinely zeros rather than heap residue -- which is what makes
        // block_held_data deterministic instead of dependent on what the heap last held, and is
        // therefore load-bearing for the tail-drop's keep branch below.
        uint8_t remaining[ISO15693_MAX_BLOCK_SIZE] = {0};
        if(iso15693_3_poller_read_block(iso_poller, remaining, (uint8_t)block, size) ==
           Iso15693_3ErrorNone) {
            // It answered a read, so it is there: the write was refused, not addressed to nothing.
            iso15693_poller_wipe_note_present(
                instance, block, &absent_run, &any_present, &highest_present);

            if(!iso15693_poller_block_is_empty(remaining, size)) {
                FURI_LOG_W(TAG, "wipe: block %u refused and still holds data", block);
                instance->clone_failed_count++;
                instance->clone_failed_bitmap[block / 8] |= (uint8_t)(1u << (block % 8));
            }
            continue;
        }

        // Answered neither a write nor a read, so it probably isn't there. Provisional: set the bit and
        // let position decide (a later success folds it into the failures above; a run the sweep ends
        // on is dropped below).
        instance->clone_failed_bitmap[block / 8] |= (uint8_t)(1u << (block % 8));
        if(++absent_run < ISO15693_POLLER_WIPE_ABSENT_RUN) continue;

        // Below the advertised count a long run is not a capacity signal at all: the card itself says
        // those blocks exist, and the bounded loop this sweep replaced always attempted every one of
        // them. Stopping here would abandon the rest and report a bare Success over them -- a 64-block
        // card with a coupling wobble at block 15 would report "wiped 15 of 15" while 15..63 still held
        // the previous card's data. So keep sweeping. The absences stay provisional and the ordinary
        // rules resolve them: a later block that answers folds them in as interior faults, and a run
        // still open when the sweep ends is dropped as the space above the card. This changes only where
        // the sweep may STOP; the tail-drop rule is untouched, so a card claiming 66 blocks against 64
        // physical still drops 64/65 rather than reporting them as "not cleared".
        //
        // The card-present check is still worth making while this holds -- a lifted card looks exactly
        // like a dead stretch -- but only once per run length, so a mostly-dead card doesn't pay an
        // inventory per block.
        const bool claimed_range_attempted = (block + 1 >= advertised);
        if(!claimed_range_attempted) {
            if(absent_run % ISO15693_POLLER_WIPE_ABSENT_RUN == 0 &&
               !iso15693_poller_card_still_present(iso_poller)) {
                *card_lost = true;
                block++; // this block was attempted; keep `block` the attempted count, as every
                break; // other exit does
            }
            continue;
        }

        // The run may now be the card's top. The clone loop's ambiguity first: a card lifted mid-sweep
        // makes every remaining write AND read fail, which is exactly what running out of card looks
        // like. Ask before concluding anything.
        if(!iso15693_poller_card_still_present(iso_poller)) {
            *card_lost = true;
            block++; // attempted, so it counts -- see the note at the other card-lost exit
            break;
        }

        // The card answered, but that does NOT make this run the top of it, and the tail-drop below is
        // about to delete the whole run from the report. Two ways that would be wrong:
        //   - the run is a dropout, not an edge. A wobble long enough to fill the run leaves the sweep
        //     concluding the card ended where the wobble started, silently abandoning everything above
        //     it. Past the advertised count there is no other guard against that.
        //   - the run's lowest members are real and only its top is absent. A single transient on the
        //     block just below the physical top gets padded out to a full run by the nonexistent blocks
        //     above it, and the drop then discards a block that still holds data.
        // So re-probe the run now that the card is known to be answering. Any member that reads is there
        // after all: it did not clear, and nothing below it in the run can be the top either, so both
        // fail closed. Then re-derive the trailing absence; if the run no longer reaches the threshold,
        // this was not the edge and the sweep carries on.
        // Bounded by the run length, and only ever spent when a run trips -- once, on a healthy card.
        const uint16_t run_start = (uint16_t)(block + 1 - absent_run);
        uint16_t still_absent = 0;
        for(uint16_t probe = run_start; probe <= block; probe++) {
            uint8_t recheck[ISO15693_MAX_BLOCK_SIZE] = {0};
            if(iso15693_3_poller_read_block(iso_poller, recheck, (uint8_t)probe, size) !=
               Iso15693_3ErrorNone) {
                still_absent++;
                continue;
            }
            FURI_LOG_W(TAG, "wipe: block %u answered on re-probe -- not the card's top", probe);
            // Same rule, with still_absent standing in for the run while the re-probe re-derives it.
            iso15693_poller_wipe_note_present(
                instance, probe, &still_absent, &any_present, &highest_present);
            // Classify this one by CONTENT, the way the main loop does. A glitch that swallowed both a
            // write ACK and its read-back, on a block whose zero-write actually landed, answers here as
            // present-and-empty: reporting that as "not cleared" manufactures a failure. Only a block
            // that answers AND still holds data failed to clear.
            if(!iso15693_poller_block_is_empty(recheck, size)) {
                instance->clone_failed_count++;
            } else {
                instance->clone_failed_bitmap[probe / 8] &= (uint8_t) ~(1u << (probe % 8));
            }
        }
        absent_run = still_absent;
        if(absent_run < ISO15693_POLLER_WIPE_ABSENT_RUN) continue;

        FURI_LOG_I(
            TAG, "wipe: card ends at block %u (advertised %u)", highest_present, advertised);
        block++; // count this block into the tail arithmetic below
        break;
    }

    // Whatever absence is still unresolved is the run the sweep ended on, contiguous by construction.
    // Above the advertised count it is the space beyond the card's real top -- never the card's to
    // clear, and dropping it is what stops a card advertising 66 blocks against 64 physical reporting
    // "Wiped 64/66, not cleared: 2".
    //
    // Below that count the premise inverts and collides with the floor, which keeps sweeping there
    // precisely because the card's claim is evidence the blocks exist. Unqualified, the drop wins and a
    // card whose blocks stop answering mid-range reports a clean Success over data it never cleared.
    // block_held_data is the discriminator: proof of existence keeps a block, absence of proof drops
    // it, so a fake-flash card's phantoms still go.
    //
    // Not closed: a block already dead when the card was presented is indistinguishable from a phantom
    // here and drops with them. Different symptom -- degraded before the wipe rather than during it.
    //
    // One premise this cannot establish: when the clock ends the sweep, the run it ends on is wherever
    // the budget ran out rather than the card's top, so dropping the part of it above the advertised
    // count is unverified. pass_truncated is what carries that -- the outcome is reported as Partial
    // and names where the sweep stopped, so the drop is not passing those blocks off as absent memory.
    //
    // WHY THIS LOOP MAY DISAGREE WITH wipe_note_present, AND WHY THAT IS SAFE. note_present resolves a
    // whole run at once: a block proved present, therefore every absence still open BELOW it is
    // interior memory, so all of them count. This loop decides block by block and drops each unproven
    // one -- including ones below a proven-present member of the same run. On a run 56..63 where only
    // block 60 reads non-zero, 56-59 are dropped and 60 is kept. Stated as two rules those contradict.
    //
    // What reconciles them is a property of the CACHE, not of either rule:
    // iso15693_3_poller_read_blocks returns at the first block that fails, so the activation copy is a
    // PREFIX. A block proven present therefore implies every block below it was also read. So a block
    // dropped below a proven one is not an unexamined absence -- it was read, and it read back as
    // zeros. It held nothing, and dropping it conceals nothing.
    //
    // That single property is load-bearing three times over: it bounds this divergence, it is why the
    // "Not closed" note above is a correct disposition rather than a placeholder, and it is what makes
    // the zeroed-entry argument at the read-back note deterministic. It is worth not re-deriving.
    //
    // No i < advertised guard: block_held_data range-checks against the same object, so the two say the
    // same thing and one of them would drift.
    for(uint16_t i = block - absent_run; i < block; i++) {
        if(iso15693_poller_block_held_data(target, i, size)) {
            // Proven present, so it also has to count toward the measured total -- otherwise
            // failed_count can exceed blocks_total and the partial screen renders "Wiped 0/20,
            // not cleared: 44".
            instance->clone_failed_count++;
            any_present = true;
            if(i > highest_present) highest_present = i;
            continue;
        }
        instance->clone_failed_bitmap[i / 8] &= (uint8_t) ~(1u << (i % 8));
    }

    // Report against what the card proved it holds, not what it advertises.
    instance->clone_blocks_total = any_present ? (uint16_t)(highest_present + 1) : 0;

    // One line covering every exit -- the card's top, the block ceiling, the time limit, and a card
    // lifted mid-sweep, which is why those two paths break rather than return: the lifted card is the
    // case where the time spent before noticing matters most. The wall-clock cost of a sweep is then
    // measurable on any card rather than estimated, which ISO15693_POLLER_PASS_MAX_MS needs -- it is
    // set from figures nobody has instrumented. (The caller discards the counters on a card-lost exit,
    // so the tail arithmetic above running for it is harmless.)
    FURI_LOG_I(
        TAG,
        "wipe: %u blocks attempted, %u cleared, %lums (advertised %u)",
        block,
        wiped,
        (unsigned long)((furi_get_tick() - sweep_start) * 1000UL /
                        furi_kernel_get_tick_frequency()),
        advertised);

    // A card lifted mid-wipe can also surface as a pile of blocks that "wouldn't clear" -- its blocks
    // still read their old data right up until it leaves the field -- without ever tripping the
    // absent-run check above. Only asked when something failed, so a clean wipe pays nothing.
    if(instance->clone_failed_count > 0 && !iso15693_poller_card_still_present(iso_poller)) {
        *card_lost = true;
    }
    return wiped;
}

// The terminal outcome once a write finishes:
//  - a NON-EMPTY block we couldn't write means real data was lost -> Partial. An empty block that
//    wouldn't take is treated as past the card's real capacity ONLY when the empty failures form a
//    contiguous tail above the last block that wrote -> clean Success: the source held nothing
//    there, so there was nothing to lose. That is NOT a claim that the clone behaves identically --
//    the source reads those blocks back as zeros, whereas past a card's real capacity the read
//    itself fails (measured on hardware; it may differ by card). That difference is why this
//    outcome carries a note on screen rather than being reported as an unqualified success. Any
//    other empty failure is folded into clone_failed_count -> Partial (see
//    iso15693_poller_write_source_blocks).
//  - a CLONE that fell back to gen1 -> Partial: gen1 stamps the UID/commit into data blocks
//    56/57/62/63, so those no longer match the source. (A bare Write-UID has no source data to
//    disturb, so gen1 there is still a clean Success.)
//  - a CLONE whose AFI/DSFID write was rejected -> Partial (that identity field may not be set).
static Iso15693PollerEvent iso15693_poller_success_or_partial(Iso15693Poller* instance) {
    const bool clone = (instance->mode == Iso15693PollerModeClone);
    const bool gen1_clone = clone && instance->clone_used_gen1;
    const bool identity_failed = clone &&
                                 (instance->clone_afi_failed || instance->clone_dsfid_failed);
    // Terminal guard, the clone-side counterpart of the wipe's "did anything accept a write": a clone
    // whose UID took but whose every data block was rejected has written no data at all. Calling that
    // Partial would put a Finish button under "Cloned 0/28 blocks", and the card would be carrying the
    // source's UID with none of its data -- which reads correct to a UID-only reader and fails anything
    // that reads memory. That is a failure, not a qualified success. Wipe is unaffected: it reaches
    // here only when at least one block accepted, and its failed/accepted sets are disjoint.
    //
    // A CUT run is excluded, and the counts are exactly why it has to be. The back-fill records every
    // block above the cut as a failure, so on a clone that accepted nothing failed_count reaches
    // blocks_total whether the card refused those blocks or nothing was ever sent to them. Judged on
    // counts alone this branch then reports "no data block took" about a source the radio never
    // addressed -- the same fabrication the truncation flag exists to prevent, one branch earlier in
    // this same function, and on the outcome with the MOST blocks above the cut. The guard's own
    // purpose survives: a cut run falls through to Partial, which names the clock and offers Retry
    // rather than a Finish button under "Cloned 0/N".
    if(clone && !instance->pass_truncated && instance->clone_blocks_total > 0 &&
       instance->clone_failed_count >= instance->clone_blocks_total) {
        return Iso15693PollerEventFail;
    }
    // A wipe that cleared its blocks but moved the card's UID is not a clean success, whatever the
    // block counts say -- the card's identity changed under an operation that doesn't claim to touch it.
    // Nor is one the clock cut short: blocks the card claims were never attempted, so the operation's
    // own job is left undone, which is what Partial means. The UID check failing to reach an answer is
    // deliberately NOT in this list -- that check is best-effort, the wipe itself finished, and making
    // it Partial would downgrade every wipe where the user lifts the card as it completes.
    if(instance->clone_failed_count > 0 || gen1_clone || identity_failed ||
       instance->uid_changed || instance->pass_truncated) {
        return Iso15693PollerEventPartial;
    }
    return Iso15693PollerEventSuccess;
}

// Read the UID back for verification, retrying a few times so a momentary miss right after the field
// power-cycle isn't mistaken for a removed card. Runs on the Nfc worker thread (furi_delay_ms is the
// same primitive the SDK poller uses between activation attempts).
static Iso15693_3Error
    iso15693_poller_verify_inventory(Iso15693_3Poller* iso_poller, uint8_t* uid) {
    Iso15693_3Error error = Iso15693_3ErrorNone;
    for(uint32_t attempt = 0; attempt < ISO15693_POLLER_VERIFY_ATTEMPTS; attempt++) {
        error = iso15693_3_poller_inventory(iso_poller, uid);
        if(error == Iso15693_3ErrorNone) break;
        furi_delay_ms(ISO15693_POLLER_VERIFY_RETRY_MS);
    }
    return error;
}

// Is the card still in the field? Used by the write loops to tell a mid-loop removal (every remaining
// block fails) from the card's real capacity ending there -- the two are indistinguishable from the
// write results alone. Uses the same retried inventory as the UID verify, so a momentary miss isn't
// mistaken for a removal.
static bool iso15693_poller_card_still_present(Iso15693_3Poller* iso_poller) {
    uint8_t uid[ISO15693_3_UID_SIZE] = {0};
    return iso15693_poller_verify_inventory(iso_poller, uid) == Iso15693_3ErrorNone;
}

// Drives one write-mode step. Runs on the Nfc worker thread with the field active. Returns the
// NfcCommand for the poller: Reset power-cycles the field (so the next Ready verifies a freshly
// re-powered card), Stop ends the operation.
static NfcCommand
    iso15693_poller_write_step(Iso15693Poller* instance, Iso15693_3Poller* iso_poller) {
    uint8_t readback[ISO15693_3_UID_SIZE] = {0};

    switch(instance->write_state) {
    case Iso15693WriteStateStart: {
        // Wipe zeros the card's own blocks and sends no UID command, so it's a single pass with no
        // backdoor write or field reset.
        if(instance->mode == Iso15693PollerModeWipe) {
            // Note the UID the card presented, to compare against in VerifyWipe. See that state for why
            // a wipe needs the comparison at all. (nfc_poller_get_data returns void*, so it has to land
            // in a typed pointer before being dereferenced.)
            const Iso15693_3Data* wipe_target = nfc_poller_get_data(instance->poller);
            memcpy(instance->original_uid, wipe_target->uid, ISO15693_3_UID_SIZE);

            bool card_lost = false;
            const uint16_t wiped = iso15693_poller_wipe_blocks(instance, iso_poller, &card_lost);
            if(card_lost) {
                // The card left mid-wipe: don't blame it for blocks that never got the chance.
                iso15693_poller_report(instance, Iso15693PollerEventCardLost);
                return NfcCommandStop;
            }

            // If not a single block accepted the zero-write, nothing was wiped: the card reported no
            // usable geometry, or every block is read-only / write-protected. Report Fail (the UID was
            // never touched) rather than a hollow Success -- and skip the power-cycle, since no write
            // landed that could have moved the UID.
            if(wiped == 0) {
                iso15693_poller_report(instance, Iso15693PollerEventFail);
                return NfcCommandStop;
            }

            // Blocks were cleared, so the UID check below is worth making. Power-cycle the field first,
            // like every other UID verify in this file.
            instance->write_state = Iso15693WriteStateVerifyWipe;
            return NfcCommandReset;
        }
        // Remember the current UID so we can tell whether the gen2 write changed anything. The poller
        // read the UID into its data during activation.
        const Iso15693_3Data* poller_data = nfc_poller_get_data(instance->poller);
        memcpy(instance->original_uid, poller_data->uid, ISO15693_3_UID_SIZE);

        // A UID read-back only proves the card is magic when the UID it is compared against is one the
        // card did not already have. Write UID pre-seeds its editor with the UID a previous Info read
        // returned, so "Info -> Write UID -> Save" asks to write the card's own UID back onto it -- and
        // then ANY tag passes the verify, having ignored every backdoor frame, and the app would report
        // a Success that says nothing about the card. Stop before writing and say so. (Not applied to a
        // clone: its target is the source's UID, so cloning an image back onto the card it came from
        // hits the same ambiguity, but there the data blocks are the payload and the user gets exactly
        // what they asked for either way.)
        if(instance->mode == Iso15693PollerModeWriteUid &&
           memcmp(instance->target_uid, instance->original_uid, ISO15693_3_UID_SIZE) == 0) {
            instance->uid_unverifiable = true;
            iso15693_poller_report(instance, Iso15693PollerEventFail);
            return NfcCommandStop;
        }

        // A clone with no writable geometry has nothing to clone -- refuse before touching the card so
        // we don't stamp a UID and report a misleading "clone complete". (Use Write UID for a UID-only
        // change.) clone_blocks_total stays 0 so the scene picks the "empty source" reason.
        if(instance->mode == Iso15693PollerModeClone &&
           (iso15693_3_get_block_count(instance->clone_source) == 0 ||
            iso15693_3_get_block_size(instance->clone_source) == 0)) {
            instance->clone_blocks_total = 0;
            iso15693_poller_report(instance, Iso15693PollerEventFail);
            return NfcCommandStop;
        }

        // Record the source block count now, before any write. write_source_blocks (its normal setter)
        // runs only on the paths that reach the data-write stage, so a failure before then -- e.g. a
        // gen2 backdoor that changes the UID to neither original nor target -- would otherwise leave
        // this 0 and be misreported as an empty source. A genuine empty source already returned above.
        if(instance->mode == Iso15693PollerModeClone) {
            instance->clone_blocks_total = iso15693_3_get_block_count(instance->clone_source);
        }

        if(instance->attempt_gen1) {
            // Opt-in gen1 run (the user accepted it on the "not gen2 magic" screen) -- a clone or a
            // bare Write-UID; both re-enter here with attempt_gen1 set. Mirror the gen2 flow: write
            // ONLY the gen1 UID sequence now, verify it in VerifyGen1, and write the data blocks there
            // only if the UID took -- so a non-magic tag that can't do gen1 loses at most the four
            // backdoor registers, not all its data.
            iso15693_poller_send_backdoor_uid_gen1(iso_poller, instance->target_uid);
            instance->write_state = Iso15693WriteStateVerifyGen1;
            return NfcCommandReset;
        }

        // gen2 first: send only the UID + geometry (NO data yet). For a clone the data blocks are
        // written afterwards, once VerifyGen2 confirms the card actually took the gen2 UID -- so a tag
        // that does not take that UID is never clobbered by a doomed clone. The CFG block programs
        // what the card reports for geometry / IC ref: the source's values for a clone (same chip
        // identity), the fixed magic default otherwise. (gen1 has no geometry block.)
        uint8_t cfg_maxblock = ISO15693_MAGIC_V2_CFG_MAXBLOCK;
        uint8_t cfg_blocksize = ISO15693_MAGIC_V2_CFG_BLOCKSIZE;
        uint8_t cfg_icref = ISO15693_MAGIC_V2_CFG_IC_REF;
        if(instance->mode == Iso15693PollerModeClone) {
            const Iso15693_3SystemInfo* sys = &instance->clone_source->system_info;
            if(sys->flags & ISO15693_3_SYSINFO_FLAG_MEMORY) {
                if(sys->block_count > 0) cfg_maxblock = (uint8_t)(sys->block_count - 1);
                if(sys->block_size > 0) cfg_blocksize = (uint8_t)(sys->block_size - 1);
            }
            if(sys->flags & ISO15693_3_SYSINFO_FLAG_IC_REF) cfg_icref = sys->ic_ref;
        }

        iso15693_poller_send_backdoor_uid_gen2(
            iso_poller, instance->target_uid, cfg_maxblock, cfg_blocksize, cfg_icref);
        instance->write_state = Iso15693WriteStateVerifyGen2;
        return NfcCommandReset;
    }

    case Iso15693WriteStateVerifyGen2: {
        if(iso15693_poller_verify_inventory(iso_poller, readback) != Iso15693_3ErrorNone) {
            iso15693_poller_report(instance, Iso15693PollerEventCardLost);
            return NfcCommandStop;
        }
        if(memcmp(readback, instance->target_uid, ISO15693_3_UID_SIZE) == 0) {
            // The UID now reads back as the target, so the card accepted a gen2 magic command: write
            // the clone payload (AFI/DSFID + data blocks). The gen2 UID lives in a separate backdoor
            // register space, so data-block writes can't disturb it. A bare Write-UID has no payload.
            // Strictly this proves "the UID is now the target", not "the card is magic": if the tag
            // presented already had that UID the comparison passes without the write having done
            // anything. In practice that tag is the one the source was read from, so the payload it
            // then receives is the data it already holds.
            if(instance->mode == Iso15693PollerModeClone) {
                iso15693_poller_write_identity(instance, iso_poller);
                if(!iso15693_poller_write_source_blocks(instance, iso_poller, false)) {
                    iso15693_poller_report(instance, Iso15693PollerEventCardLost);
                    return NfcCommandStop;
                }
            }
            iso15693_poller_report(instance, iso15693_poller_success_or_partial(instance));
            return NfcCommandStop;
        }
        if(memcmp(readback, instance->original_uid, ISO15693_3_UID_SIZE) == 0) {
            // gen2 changed nothing: a gen1 card, or a non-magic tag. Nothing has been written yet, so
            // the card is untouched. Stop here and let the scene offer the opt-in gen1 retry. This
            // applies to a bare Write-UID as well as a clone: gen1 sets the UID with ordinary WRITE
            // BLOCK into blocks 56/57/62/63, which ANY writable tag accepts, so on a non-magic tag it
            // destroys four blocks of user data. That is destructive and not hardware-tested, so it
            // needs the same explicit consent in both flows.
            iso15693_poller_report(instance, Iso15693PollerEventNotGen2);
            return NfcCommandStop;
        }
        // gen2 changed the UID but not to the target: stop rather than compound it with gen1. Record
        // what came back. This is the ONE branch that proves the card is magic -- an inert tag cannot
        // change its UID at all -- so it must not fall through to "not a magic tag", and the UID the
        // card is now answering with is the only way the user can find it again.
        instance->uid_unexpected = true;
        memcpy(instance->uid_readback, readback, ISO15693_3_UID_SIZE);
        iso15693_poller_report(instance, Iso15693PollerEventFail);
        return NfcCommandStop;
    }

    case Iso15693WriteStateVerifyWipe: {
        // Did the wipe move the card's UID? On gen2 it cannot -- the wipe sends no UID command and the
        // gen2 UID lives in a separate register space -- so on the only hardware this PR has, this is a
        // regression test that should never fire. On gen1 it is the point: blocks 56/57 ARE the UID
        // registers, the gen1 arm sequence leaves commit = 0x6996 and nothing ever clears it, so an
        // already-armed card can have its UID rewritten by a wipe zeroing those blocks. See the OPEN
        // QUESTION in iso15693_poller_wipe_blocks: reordering blind writes cannot fix that, but
        // reporting it can, and the screens no longer promise the UID is left alone.
        //
        // This is a state of its own, entered after NfcCommandReset, for the reason iso15693_poller.h
        // gives for the gen2 and gen1 UID verifies: a card that only latches a written UID on the next
        // power-up answers the OLD one until then. Read inline at the end of the sweep, the check would
        // pass having observed nothing -- on precisely the card it exists for, since zeroing 56/57 on an
        // armed gen1 card IS a gen1 UID write.
        //
        // Reported ONLY from a positive observation of a different UID. An inventory that fails tells us
        // nothing, and treating it as a failure would turn "user lifted the card the instant the wipe
        // finished" into an error on every gen2 wipe, for the sake of a gen1 case nobody can test. So it
        // is logged and left alone -- which does mean a card bricked so thoroughly that it no longer
        // inventories at all goes unreported. The card not coming back from the reset at all is the same
        // case; see the activation-error path in iso15693_poller_nfc_callback.
        if(iso15693_poller_verify_inventory(iso_poller, readback) != Iso15693_3ErrorNone) {
            FURI_LOG_W(TAG, "wipe: card did not answer the UID read-back");
        } else {
            instance->uid_verified = true;
            if(memcmp(readback, instance->original_uid, ISO15693_3_UID_SIZE) != 0) {
                FURI_LOG_E(TAG, "wipe: the UID CHANGED");
                instance->uid_changed = true;
                memcpy(instance->uid_readback, readback, ISO15693_3_UID_SIZE);
            }
        }
        iso15693_poller_report(instance, iso15693_poller_success_or_partial(instance));
        return NfcCommandStop;
    }

    case Iso15693WriteStateVerifyGen1:
    default: {
        if(iso15693_poller_verify_inventory(iso_poller, readback) != Iso15693_3ErrorNone) {
            iso15693_poller_report(instance, Iso15693PollerEventCardLost);
            return NfcCommandStop;
        }
        if(memcmp(readback, instance->target_uid, ISO15693_3_UID_SIZE) != 0) {
            // gen1 UID didn't take -- not a gen1 card either. No data blocks were written, but the
            // gen1 UID sequence itself already went out as four ordinary WRITE BLOCKs into 56/57/62/63,
            // which any writable tag accepts. So on the tag this most likely is -- an ordinary one --
            // four blocks of user data are now gone. gen1_attempted carries that to the result screen;
            // reporting only "not a magic tag" here would hide the damage the user consented to.
            iso15693_poller_report(instance, Iso15693PollerEventFail);
            return NfcCommandStop;
        }
        // gen1 set the UID (NOTE: gen1 path is not hardware-validated). Record it so a clone reports
        // Partial and flags that blocks 56/57/62/63 now hold UID/commit bytes, not the source's data.
        instance->clone_used_gen1 = true;
        // UID took -> now write the payload. For a clone: AFI/DSFID + every data block EXCEPT the gen1
        // backdoor registers 56/57/62/63 (writing those would clobber the UID we just set). A bare
        // Write-UID has no payload.
        if(instance->mode == Iso15693PollerModeClone) {
            iso15693_poller_write_identity(instance, iso_poller);
            if(!iso15693_poller_write_source_blocks(instance, iso_poller, true)) {
                iso15693_poller_report(instance, Iso15693PollerEventCardLost);
                return NfcCommandStop;
            }
        }
        iso15693_poller_report(instance, iso15693_poller_success_or_partial(instance));
        return NfcCommandStop;
    }
    }
}

// Runs on the Nfc worker thread. Returns NfcCommand to control the poller.
static NfcCommand iso15693_poller_nfc_callback(NfcGenericEvent event, void* context) {
    Iso15693Poller* instance = context;
    furi_assert(instance);

    if(event.protocol != NfcProtocolIso15693_3) {
        return NfcCommandContinue;
    }

    Iso15693_3PollerEvent* iso_event = event.event_data;

    // Activation error => no card in the field (or removed). Retry a bounded number of times so the
    // popup can't hang forever, then report CardLost.
    if(iso_event->type == Iso15693_3PollerEventTypeError) {
        const bool verifying_wipe = (instance->write_state == Iso15693WriteStateVerifyWipe);
        const uint32_t budget = verifying_wipe ? ISO15693_POLLER_WIPE_VERIFY_ACTIVATIONS :
                                                 ISO15693_POLLER_MAX_ACTIVATION_ERRORS;
        if(++instance->activation_errors >= budget) {
            if(verifying_wipe) {
                // The wipe already ran; only its UID check is outstanding, and that check reports
                // nothing unless it POSITIVELY observes a different UID (see VerifyWipe). A card that
                // doesn't come back from the power-cycle is the same non-observation as an inventory
                // that fails, so report the wipe rather than throwing its result away for a card the
                // user has most likely just picked up.
                FURI_LOG_W(TAG, "wipe: card did not return after the field reset");
                iso15693_poller_report(instance, iso15693_poller_success_or_partial(instance));
                return NfcCommandStop;
            }
            iso15693_poller_report(instance, Iso15693PollerEventCardLost);
            return NfcCommandStop;
        }
        return NfcCommandContinue;
    }

    if(iso_event->type != Iso15693_3PollerEventTypeReady) {
        return NfcCommandContinue;
    }

    instance->activation_errors = 0;

    if(instance->mode == Iso15693PollerModeInfo) {
        // The poller filled Iso15693_3Data (UID + system info) during activation.
        const Iso15693_3Data* poller_data = nfc_poller_get_data(instance->poller);
        iso15693_3_copy(instance->data, poller_data);
        iso15693_poller_report(instance, Iso15693PollerEventSuccess);
        return NfcCommandStop;
    }

    // On the FIRST activation of any write mode (write_state still Start, before any write step), tell
    // the scene a card was detected so its popup switches from "apply the card" to "writing". Fires
    // once, because write_step advances the state.
    if(instance->write_state == Iso15693WriteStateStart &&
       instance->mode != Iso15693PollerModeInfo) {
        iso15693_poller_report(instance, Iso15693PollerEventCardDetected);
    }

    // Write mode. event.instance is the concrete Iso15693_3Poller; raw frames must be sent there.
    return iso15693_poller_write_step(instance, event.instance);
}

Iso15693Poller* iso15693_poller_alloc(Nfc* nfc) {
    Iso15693Poller* instance = malloc(sizeof(Iso15693Poller));
    instance->poller = nfc_poller_alloc(nfc, NfcProtocolIso15693_3);
    instance->data = iso15693_3_alloc();
    instance->clone_source = iso15693_3_alloc();
    instance->mode = Iso15693PollerModeInfo;
    instance->write_state = Iso15693WriteStateStart;
    instance->attempt_gen1 = false;
    instance->activation_errors = 0;
    instance->clone_blocks_total = 0;
    instance->clone_failed_count = 0;
    instance->clone_over_capacity = 0;
    memset(instance->clone_failed_bitmap, 0, sizeof(instance->clone_failed_bitmap));
    instance->callback = NULL;
    instance->context = NULL;
    instance->running = false;
    return instance;
}

void iso15693_poller_free(Iso15693Poller* instance) {
    furi_assert(instance);
    if(instance->running) {
        iso15693_poller_stop(instance);
    }
    nfc_poller_free(instance->poller);
    iso15693_3_free(instance->data);
    iso15693_3_free(instance->clone_source);
    free(instance);
}

// nfc_poller_start is non-blocking: the callback fires on the Nfc worker thread and returns
// NfcCommandStop when finished. The owning scene must still call iso15693_poller_stop() on exit
// so the NfcPoller session state is reset before the next start.
static void iso15693_poller_start_internal(
    Iso15693Poller* instance,
    Iso15693PollerMode mode,
    bool gen1,
    Iso15693PollerCallback callback,
    void* context) {
    furi_assert(instance);
    furi_assert(!instance->running);
    instance->mode = mode;
    instance->attempt_gen1 = gen1;
    instance->callback = callback;
    instance->context = context;
    instance->write_state = Iso15693WriteStateStart;
    instance->activation_errors = 0;
    instance->clone_blocks_total = 0;
    instance->clone_failed_count = 0;
    instance->clone_over_capacity = 0;
    instance->wipe_advertised = 0;
    instance->pass_truncated = false;
    instance->pass_cut_block = 0;
    instance->uid_verified = false;
    instance->clone_used_gen1 = false;
    instance->clone_capacity_confirmed = false;
    instance->clone_blocks_done = 0;
    instance->progress_step = UINT8_MAX; // no band emitted yet, so the first call fires
    instance->clone_afi_failed = false;
    instance->clone_dsfid_failed = false;
    instance->uid_unexpected = false;
    instance->gen1_attempted = gen1;
    instance->uid_unverifiable = false;
    instance->uid_changed = false;
    memset(instance->uid_readback, 0, sizeof(instance->uid_readback));
    memset(instance->clone_failed_bitmap, 0, sizeof(instance->clone_failed_bitmap));
    iso15693_3_reset(instance->data);
    instance->running = true;
    nfc_poller_start(instance->poller, iso15693_poller_nfc_callback, instance);
}

void iso15693_poller_start(
    Iso15693Poller* instance,
    Iso15693PollerCallback callback,
    void* context) {
    iso15693_poller_start_internal(instance, Iso15693PollerModeInfo, false, callback, context);
}

void iso15693_poller_start_write_uid(
    Iso15693Poller* instance,
    const uint8_t* uid,
    Iso15693PollerCallback callback,
    void* context) {
    furi_assert(instance);
    furi_assert(uid);
    memcpy(instance->target_uid, uid, ISO15693_3_UID_SIZE);
    iso15693_poller_start_internal(instance, Iso15693PollerModeWriteUid, false, callback, context);
}

void iso15693_poller_start_write_uid_gen1(
    Iso15693Poller* instance,
    const uint8_t* uid,
    Iso15693PollerCallback callback,
    void* context) {
    furi_assert(instance);
    furi_assert(uid);
    // Opt-in gen1 retry after gen2 left the UID unchanged. write_step sends ONLY the gen1 UID sequence
    // and verifies it; a Write-UID has no payload to follow, so a verified UID is a clean Success.
    memcpy(instance->target_uid, uid, ISO15693_3_UID_SIZE);
    iso15693_poller_start_internal(instance, Iso15693PollerModeWriteUid, true, callback, context);
}

void iso15693_poller_start_clone(
    Iso15693Poller* instance,
    const Iso15693_3Data* source,
    Iso15693PollerCallback callback,
    void* context) {
    furi_assert(instance);
    furi_assert(source);
    // Hold our own copy of the source so it survives the async write; target UID = the source's UID.
    iso15693_3_copy(instance->clone_source, source);
    memcpy(instance->target_uid, source->uid, ISO15693_3_UID_SIZE);
    iso15693_poller_start_internal(instance, Iso15693PollerModeClone, false, callback, context);
}

void iso15693_poller_start_clone_gen1(
    Iso15693Poller* instance,
    const Iso15693_3Data* source,
    Iso15693PollerCallback callback,
    void* context) {
    furi_assert(instance);
    furi_assert(source);
    // Opt-in gen1 retry after gen2 was rejected. Same source/target as a normal clone; write_step
    // writes ONLY the gen1 UID first, verifies it, then writes the data (skipping 56/57/62/63) only if
    // the UID took.
    iso15693_3_copy(instance->clone_source, source);
    memcpy(instance->target_uid, source->uid, ISO15693_3_UID_SIZE);
    iso15693_poller_start_internal(instance, Iso15693PollerModeClone, true, callback, context);
}

void iso15693_poller_start_wipe(
    Iso15693Poller* instance,
    Iso15693PollerCallback callback,
    void* context) {
    furi_assert(instance);
    iso15693_poller_start_internal(instance, Iso15693PollerModeWipe, false, callback, context);
}

void iso15693_poller_get_result(Iso15693Poller* instance, Iso15693PollerResult* result) {
    furi_assert(instance);
    furi_assert(result);
    result->blocks_total = instance->clone_blocks_total;
    result->failed_count = instance->clone_failed_count;
    result->over_capacity = instance->clone_over_capacity;
    result->blocks_advertised = instance->wipe_advertised;
    result->pass_truncated = instance->pass_truncated;
    result->cut_block = instance->pass_cut_block;
    result->uid_verified = instance->uid_verified;
    memcpy(result->failed_bitmap, instance->clone_failed_bitmap, sizeof(result->failed_bitmap));
    result->used_gen1 = instance->clone_used_gen1;
    result->capacity_confirmed = instance->clone_capacity_confirmed;
    // Clone-mode only, mirroring success_or_partial: the flags are reset per run and written only in
    // the clone path, so this guard is future-proofing against them ever leaking cross-mode.
    result->identity_failed = (instance->mode == Iso15693PollerModeClone) &&
                              (instance->clone_afi_failed || instance->clone_dsfid_failed);
    result->blocks_done = instance->clone_blocks_done;
    result->uid_unexpected = instance->uid_unexpected;
    memcpy(result->uid_readback, instance->uid_readback, sizeof(result->uid_readback));
    result->gen1_attempted = instance->gen1_attempted;
    result->uid_unverifiable = instance->uid_unverifiable;
    result->uid_changed = instance->uid_changed;
}

// True if the source image has non-empty data in any of the gen1 backdoor blocks (56/57/62/63) that
// actually exist within its block count. A gen1 fallback overwrites those blocks with UID/commit
// bytes, so it can't reproduce a source that stores real data there -- callers warn about this up
// front. (Purely a source inspection; touches no hardware.)
bool iso15693_poller_source_uses_gen1_blocks(const Iso15693_3Data* source) {
    if(!source) return false;
    const uint16_t block_count = iso15693_3_get_block_count(source);
    const uint8_t block_size = iso15693_3_get_block_size(source);
    if(block_size == 0) return false;
    for(size_t i = 0; i < sizeof(iso15693_poller_backdoor_blocks); i++) {
        const uint16_t block = iso15693_poller_backdoor_blocks[i];
        if(block >= block_count) continue;
        const uint8_t* data = iso15693_3_get_block_data(source, block);
        if(!iso15693_poller_block_is_empty(data, block_size)) return true;
    }
    return false;
}

void iso15693_poller_stop(Iso15693Poller* instance) {
    furi_assert(instance);
    if(instance->running) {
        nfc_poller_stop(instance->poller);
        instance->running = false;
    }
}

const Iso15693_3Data* iso15693_poller_get_data(Iso15693Poller* instance) {
    furi_assert(instance);
    return instance->data;
}
