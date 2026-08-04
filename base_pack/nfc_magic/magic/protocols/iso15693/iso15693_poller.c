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
#define ISO15693_MAGIC_BLK_UNLOCK   (0x3EU) // written as 0
#define ISO15693_MAGIC_BLK_COMMIT   (0x3FU) // written as 0x6996 (arms the UID change)
#define ISO15693_MAGIC_BLK_UID_7654 (0x38U) // uid[7..4]
#define ISO15693_MAGIC_BLK_UID_3210 (0x39U) // uid[3..0]

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
// popup can hang forever with no card. Each failed activation adds a ~100ms delay in the SDK poller,
// so this is roughly a 5-7 second timeout.
#define ISO15693_POLLER_MAX_ACTIVATION_ERRORS (40U)

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
// Staying well under 16 keeps that impossible. Emitting per block does NOT -- if you want that, the
// loop has to yield to the Nfc worker between blocks (return NfcCommandContinue and resume from a
// cursor) the way uscuid_ul_poller.c does, and only then is per-block safe.
#define ISO15693_POLLER_PROGRESS_STEPS (8U)

// Write-mode state machine. Each verify runs after a NfcCommandReset field power-cycle.
typedef enum {
    Iso15693WriteStateStart, // note the current UID, send the backdoor UID (gen2, or gen1 on an
        // opt-in gen1 run), request a field reset
    Iso15693WriteStateVerifyGen2, // verify gen2: on a match write the payload; if the UID is untouched
        // report NotGen2 and STOP so the scene can offer the gen1 opt-in (it is not sent from here)
    Iso15693WriteStateVerifyGen1, // verify the opt-in gen1 UID; on a match write the payload
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
        // (they carry the UID, not source data). wipe: the card's full advertised block count
    // Clone mode: blocks that failed to write and count as a real problem: either they held source
    // data (lost), or they were empty failures that did NOT form a clean capacity tail (so we can't
    // call them over-capacity). Drives Partial. Wipe mode: reused as the count of blocks that still
    // held data after a failed zero-write (i.e. all wipe failures).
    uint16_t clone_failed_count;
    // Clone mode: empty source blocks past the card's real capacity -- a contiguous run above the last
    // block that did write, so nothing was lost. Drives the "clone complete, with a note" Success.
    // Wipe mode: unused (stays 0).
    uint16_t clone_over_capacity;
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
    const uint8_t block_size = iso15693_3_get_block_size(source);

    // A block number is a uint8_t on the wire and the failure bitmap holds this many bits, so only the
    // first 256 blocks can be attempted or accounted for. Real ISO15693 tags never exceed this; clamp
    // defensively so a corrupt/hand-edited source can't make clone_blocks_total overstate what was
    // actually written (which would skew the over-capacity "holds X of Y" report).
    if(source_count > ISO15693_POLLER_BLOCK_BITMAP_SIZE * 8) {
        source_count = ISO15693_POLLER_BLOCK_BITMAP_SIZE * 8;
    }

    // Report the count of blocks we actually attempt: for gen1, exclude the 4 backdoor registers we
    // skip below so the "Cloned X/Y" total isn't inflated by blocks that only ever hold the UID.
    uint16_t total = source_count;
    if(skip_backdoor) {
        if(ISO15693_MAGIC_BLK_UID_7654 < source_count) total--;
        if(ISO15693_MAGIC_BLK_UID_3210 < source_count) total--;
        if(ISO15693_MAGIC_BLK_UNLOCK < source_count) total--;
        if(ISO15693_MAGIC_BLK_COMMIT < source_count) total--;
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
    // Retry a failed write a few times so a transient RF glitch doesn't masquerade as a real limit: a
    // block that fails EVERY attempt is genuinely unwritable. Since magic cards ignore their own lock
    // bits, that means the block is past the card's physical capacity. Classify each persistent
    // failure by whether the source had data there (non-empty -> data lost; empty -> nothing lost),
    // and track position so we can confirm below that the failures are a contiguous run at the TOP of
    // the card -- the capacity signature. A scattered/interior failure is anomalous and won't be
    // called capacity.
    //
    // Do NOT skip blocks locked in the SOURCE image: the source's lock bits describe the ORIGINAL
    // card, not the magic target (which is writable regardless), and locked blocks are exactly where
    // real tags keep provisioned data. Attempt every block.
    bool wrote_any = false; // at least one block accepted a write
    bool wrote_above_failure = false; // a block wrote ABOVE one that failed -> not a capacity tail
    uint16_t done = 0; // blocks attempted, the denominator the progress popup shows
    for(uint16_t block = 0; block < source_count && block < ISO15693_POLLER_BLOCK_BITMAP_SIZE * 8;
        block++) {
        if(skip_backdoor &&
           (block == ISO15693_MAGIC_BLK_UID_7654 || block == ISO15693_MAGIC_BLK_UID_3210 ||
            block == ISO15693_MAGIC_BLK_UNLOCK || block == ISO15693_MAGIC_BLK_COMMIT)) {
            continue; // gen1 owns these; the gen1 UID sequence already wrote them
        }
        // Before the write, so a clean run reports progress too -- the success path below continues
        // straight to the next block.
        iso15693_poller_report_progress(instance, done++, total);
        const uint8_t* block_data = iso15693_3_get_block_data(source, block);
        Iso15693_3Error error = Iso15693_3ErrorNone;
        for(uint32_t attempt = 0; attempt < ISO15693_POLLER_WRITE_ATTEMPTS; attempt++) {
            error =
                iso15693_3_poller_write_block(iso_poller, block_data, (uint8_t)block, block_size);
            if(error == Iso15693_3ErrorNone) break;
            furi_delay_ms(ISO15693_POLLER_VERIFY_RETRY_MS);
        }
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

        bool non_empty = false;
        for(uint8_t i = 0; i < block_size; i++) {
            if(block_data[i] != 0) {
                non_empty = true;
                break;
            }
        }
        if(non_empty) {
            instance->clone_failed_count++;
        } else {
            instance->clone_over_capacity++;
        }
    }
    iso15693_poller_report_progress(instance, done, total); // land on 100%

    // The failures are a real capacity edge only if they form a contiguous run at the very top of the
    // card, with at least one block below them having written -- the shape of "source bigger than the
    // card". Confirm that before making any capacity claim.
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

    const bool failures_are_top_tail = any_failure && wrote_any && !wrote_above_failure;
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

// Wipe mode: write zeros to every data block on the card itself -- no UID command is sent -- using
// the target's own reported geometry. We attempt every block rather than pre-skipping the target's locked ones: a
// magic card often ignores its own lock bits and accepts the write. A block whose zero-write fails is
// only a real failure if it STILL HOLDS DATA -- a phantom block past the card's real capacity (the
// card over-reports its geometry) or an already-clear block left nothing behind, so counting it would
// be a false "partial wipe". That is decided by a FRESH read-back of the block, not by the copy taken
// at activation (see below); a block we cannot read back is counted, so the wipe fails closed. A
// locked block that genuinely retains data is counted (the wipe's privacy promise wasn't kept there),
// which also lets the "nothing could be wiped" guard fire.
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
    const uint16_t block_count = iso15693_3_get_block_count(target);
    const uint8_t block_size = iso15693_3_get_block_size(target);

    instance->clone_blocks_total = block_count;
    instance->clone_failed_count = 0;
    instance->clone_over_capacity = 0;
    memset(instance->clone_failed_bitmap, 0, sizeof(instance->clone_failed_bitmap));

    if(block_count == 0 || block_size == 0) return 0;

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
    // An earlier revision tried to de-arm by pre-writing the commit block. That was withdrawn: it
    // wrote commit before unlock, the reverse of the only ordering the hardware is documented to
    // accept, so it would either be rejected outright or -- worse -- leave unlock freshly zeroed,
    // i.e. one step INTO the arm sequence, right before this loop touches the UID registers.
    //
    // Left as-is deliberately, matching proxmark's `hf 15 wipe`, which walks 0..0xFF with no de-arm
    // at all. The grounded fix is not reordering blind writes but verifying: re-read the UID after
    // the wipe and report a mismatch instead of claiming the UID is unchanged. That needs a gen1
    // card to validate against, which nobody on this PR has.
    for(uint16_t block = 0; block < block_count && block < ISO15693_POLLER_BLOCK_BITMAP_SIZE * 8;
        block++) {
        iso15693_poller_report_progress(instance, block + 1, block_count);
        Iso15693_3Error error =
            iso15693_3_poller_write_block(iso_poller, zeros, (uint8_t)block, size);
        if(error == Iso15693_3ErrorNone) {
            wiped++;
            continue;
        }

        // The zero-write failed, so whatever the block held is still there. Only decline to count it
        // when the block is POSITIVELY known to be clear: a phantom block past the card's real
        // capacity, or an already-empty block, has nothing left to lose. A locked block that still
        // holds data IS a real failure.
        // Re-read the block rather than trusting the copy taken at activation. That copy is not
        // trustworthy: iso15693_3_poller_activate passes its read_blocks result through
        // iso15693_3_poller_filter_error, which maps Timeout and NotSupported to None, so activation
        // can report success having stopped reading at the FIRST failed block, leaving every later
        // block at its zeroed allocation value. Believing that cache would score a block that still
        // holds data as empty and report a hollow "wipe complete" -- and the blocks most likely to
        // fail a read are exactly the high ones that don't read until first written.
        //
        // If the re-read itself fails we do NOT know the block is clear, so count it. Over-reporting a
        // wipe failure is recoverable; claiming a wipe that did not happen is not.
        uint8_t remaining[ISO15693_MAX_BLOCK_SIZE] = {0};
        bool has_data = true; // fail closed until a successful read proves the block is clear
        if(iso15693_3_poller_read_block(iso_poller, remaining, (uint8_t)block, size) ==
           Iso15693_3ErrorNone) {
            has_data = false;
            for(uint8_t i = 0; i < size; i++) {
                if(remaining[i] != 0) {
                    has_data = true;
                    break;
                }
            }
        }
        if(has_data) {
            instance->clone_failed_count++;
            instance->clone_failed_bitmap[block / 8] |= (uint8_t)(1u << (block % 8));
        }
    }

    // Same removal ambiguity as the clone loop: a card lifted mid-wipe makes every remaining
    // zero-write (and its read-back) fail, which would otherwise be reported as "N blocks wouldn't
    // clear" -- blaming the card for what was really a removal. Only checked when something failed.
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
    if(clone && instance->clone_blocks_total > 0 &&
       instance->clone_failed_count >= instance->clone_blocks_total) {
        return Iso15693PollerEventFail;
    }
    if(instance->clone_failed_count > 0 || gen1_clone || identity_failed) {
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
            bool card_lost = false;
            const uint16_t wiped = iso15693_poller_wipe_blocks(instance, iso_poller, &card_lost);
            if(card_lost) {
                // The card left mid-wipe: don't blame it for blocks that never got the chance.
                iso15693_poller_report(instance, Iso15693PollerEventCardLost);
                return NfcCommandStop;
            }
            // If not a single block accepted the zero-write, nothing was wiped: the card reported no
            // usable geometry, or every block is read-only / write-protected. Report Fail (the UID was
            // never touched) rather than a hollow Success.
            Iso15693PollerEvent outcome = (wiped == 0) ?
                                              Iso15693PollerEventFail :
                                              iso15693_poller_success_or_partial(instance);
            iso15693_poller_report(instance, outcome);
            return NfcCommandStop;
        }
        // Remember the current UID so we can tell whether the gen2 write changed anything. The poller
        // read the UID into its data during activation.
        const Iso15693_3Data* poller_data = nfc_poller_get_data(instance->poller);
        memcpy(instance->original_uid, poller_data->uid, ISO15693_3_UID_SIZE);

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
            // Opt-in gen1 clone (the user accepted it on the "not gen2 magic" screen). Mirror the gen2
            // flow: write ONLY the gen1 UID sequence now, verify it in VerifyGen1, and write the data
            // blocks there only if the UID took -- so a non-magic tag that can't do gen1 loses at most
            // the four backdoor registers, not all its data. Only a clone takes this path (a bare
            // Write-UID does its gen1 fallback inline from VerifyGen2).
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

    case Iso15693WriteStateVerifyGen1:
    default: {
        if(iso15693_poller_verify_inventory(iso_poller, readback) != Iso15693_3ErrorNone) {
            iso15693_poller_report(instance, Iso15693PollerEventCardLost);
            return NfcCommandStop;
        }
        if(memcmp(readback, instance->target_uid, ISO15693_3_UID_SIZE) != 0) {
            // gen1 UID didn't take -- not a gen1 card either. Only the four backdoor registers were
            // touched (the UID attempt); the bulk of the data was never written.
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
        if(++instance->activation_errors >= ISO15693_POLLER_MAX_ACTIVATION_ERRORS) {
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
    instance->clone_used_gen1 = false;
    instance->clone_capacity_confirmed = false;
    instance->clone_blocks_done = 0;
    instance->progress_step = UINT8_MAX; // no band emitted yet, so the first call fires
    instance->clone_afi_failed = false;
    instance->clone_dsfid_failed = false;
    instance->uid_unexpected = false;
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
    const uint8_t gen1_blocks[] = {
        ISO15693_MAGIC_BLK_UID_7654,
        ISO15693_MAGIC_BLK_UID_3210,
        ISO15693_MAGIC_BLK_UNLOCK,
        ISO15693_MAGIC_BLK_COMMIT};
    for(size_t i = 0; i < sizeof(gen1_blocks); i++) {
        const uint16_t block = gen1_blocks[i];
        if(block >= block_count) continue;
        const uint8_t* data = iso15693_3_get_block_data(source, block);
        for(uint8_t j = 0; j < block_size; j++) {
            if(data[j] != 0) return true;
        }
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
