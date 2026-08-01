#include "iso15693_poller.h"
#include <furi.h>
#include <nfc/nfc_poller.h>
#include <lib/nfc/protocols/iso15693_3/iso15693_3_poller.h>
#include <toolbox/bit_buffer.h>

#define TAG "Iso15693Poller"

// Older SDKs predate ISO15693_3_FDT_WRITE_POLL_FC; fall back to the value the SDK uses today
// (271200 carrier cycles = 20ms, long enough for EEPROM programming before the tag answers).
#ifndef ISO15693_3_FDT_WRITE_POLL_FC
#define ISO15693_3_FDT_WRITE_POLL_FC (271200U)
#endif

// Magic ISO15693 ("Chinese magic") backdoor UID write, ported from proxmark3 (GPLv3)
// SetTag15693Uid / SetTag15693Uid_v2 (armsrc/iso15693.c). Unaddressed frames are sent to
// hidden backdoor blocks; the CRC is appended by iso15693_3_poller_send_frame. Two card
// generations exist and the write tries gen2 then (only if untouched) gen1.
// Proxmark calls this flag combination ISO15_REQ_DATARATE_HIGH.
#define ISO15693_MAGIC_FLAGS (ISO15693_3_REQ_FLAG_DATA_RATE_HI) // high data rate, unaddressed

// gen1: the ordinary WRITE BLOCK opcode aimed at four ordinary data blocks -- 56/57 take the UID,
// 62/63 unlock and commit it. Because these are ordinary blocks reached by an ordinary opcode, any
// other WRITE BLOCK to them (a wipe, say) hits the same registers; see iso15693_poller_write_blocks.
// UID halves are named for the numeric half they carry: uid[0] is 0xE0 and the UID is stored
// MSB-first, so uid[7..4] is the low half. COMMIT is believed to arm the UID change, though proxmark
// sends it unconditionally and does not document why.
#define ISO15693_MAGIC_CMD_WRITE  (0x21U) // == ISO15693_3_CMD_WRITE_BLOCK
#define ISO15693_MAGIC_BLK_UID_LO (0x38U) // 56: uid[7..4]
#define ISO15693_MAGIC_BLK_UID_HI (0x39U) // 57: uid[3..0]
#define ISO15693_MAGIC_BLK_UNLOCK (0x3EU) // 62: written as 0
#define ISO15693_MAGIC_BLK_COMMIT (0x3FU) // 63: written as 0x6996

// gen2: magic write command (0xE0) with a 0x09 subcommand and a block reference; 4 data
// bytes each. Frame layout: 02 E0 09 <ref> d0 d1 d2 d3 (+CRC). These block references live in the
// backdoor's own address space, not the data blocks, so an ordinary WRITE BLOCK cannot reach them.
// Named for the UID half each carries, matching the gen1 pair above.
#define ISO15693_MAGIC_CMD_WRITE_V2     (0xE0U) // ISO15693_MAGIC_WRITE
#define ISO15693_MAGIC_V2_SUB           (0x09U)
#define ISO15693_MAGIC_V2_BLK_CFG       (0x47U) // system-info config: max block / block size / IC ref
#define ISO15693_MAGIC_V2_BLK_CFG2      (0x52U) // written as 0
#define ISO15693_MAGIC_V2_BLK_UID_LO    (0x40U) // uid[7..4]
#define ISO15693_MAGIC_V2_BLK_UID_HI    (0x41U) // uid[3..0]
// Fixed config payload for the CFG block, verbatim from proxmark's gen2 sequence (matches a
// 64-block / 4-byte-block / IC-ref-0x8B card; these values are constant in proxmark too).
#define ISO15693_MAGIC_V2_CFG_MAXBLOCK  (0x3FU)
#define ISO15693_MAGIC_V2_CFG_BLOCKSIZE (0x03U)
#define ISO15693_MAGIC_V2_CFG_IC_REF    (0x8BU)

// Standard ISO15693 identity writes, used to make a clone match the source's AFI / DSFID.
#define ISO15693_MAGIC_CMD_WRITE_AFI   (0x27U) // ISO15693 WRITE AFI
#define ISO15693_MAGIC_CMD_WRITE_DSFID (0x29U) // ISO15693 WRITE DSFID

#define ISO15693_POLLER_BUF_SIZE (32U)

// Get System Info stores (block size - 1) in a 5-bit field, so a block is at most 32 bytes.
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

// Write-mode state machine. Each verify runs after a NfcCommandReset field power-cycle.
typedef enum {
    Iso15693WriteStateStart, // read the current UID, send gen2, request a field reset
    Iso15693WriteStateVerifyGen2, // verify gen2; if the UID is untouched, send gen1 + reset
    Iso15693WriteStateVerifyGen1, // verify gen1
    Iso15693WriteStateWriteBlocks, // write one block per callback until the pass is done
} Iso15693WriteState;

struct Iso15693Poller {
    NfcPoller* poller;
    Iso15693_3Data* data; // Info mode: the card as read during activation
    Iso15693PollerMode mode;
    uint8_t target_uid[ISO15693_3_UID_SIZE];
    uint8_t original_uid[ISO15693_3_UID_SIZE]; // UID before the write, to gate the gen1 fallback
    Iso15693WriteState write_state;
    uint32_t activation_errors; // consecutive activation failures (no card) -> timeout
    // Clone mode: the source image, kept separate from `data` so start_internal's reset can't wipe it.
    Iso15693_3Data* clone_source;
    Iso15693PollerResult result; // block-write outcome, handed to callers via _get_result()
    uint16_t block_cursor; // next block for the resumable write pass
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
        tx, ISO15693_MAGIC_BLK_UID_LO, uid[7], uid[6], uid[5], uid[4]);
    iso15693_3_poller_send_frame(iso_poller, tx, rx, ISO15693_3_FDT_WRITE_POLL_FC);

    iso15693_poller_build_gen1_frame(
        tx, ISO15693_MAGIC_BLK_UID_HI, uid[3], uid[2], uid[1], uid[0]);
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
        tx, ISO15693_MAGIC_V2_BLK_UID_LO, uid[7], uid[6], uid[5], uid[4]);
    iso15693_3_poller_send_frame(iso_poller, tx, rx, ISO15693_3_FDT_WRITE_POLL_FC);

    iso15693_poller_build_gen2_frame(
        tx, ISO15693_MAGIC_V2_BLK_UID_HI, uid[3], uid[2], uid[1], uid[0]);
    iso15693_3_poller_send_frame(iso_poller, tx, rx, ISO15693_3_FDT_WRITE_POLL_FC);

    bit_buffer_free(tx);
    bit_buffer_free(rx);
}

// True when the tag answered but set the error flag -- i.e. it understood the command and refused
// it. iso15693_3_poller_send_frame only reports transport failures (no answer, bad CRC), so a
// refusal comes back as success unless the response flags are read. This is the common refusal for
// WRITE AFI ("option not supported", "block locked"), so ignoring it means ignoring most failures.
static bool iso15693_poller_response_is_error(const BitBuffer* rx) {
    if(bit_buffer_get_size_bytes(rx) < 1) return true; // no flags byte at all: not a valid answer
    return (bit_buffer_get_byte(rx, 0) & ISO15693_3_RESP_FLAG_ERROR) != 0;
}

// Make the clone match the source's AFI / DSFID via the standard ISO15693 WRITE AFI / WRITE DSFID
// commands (only for fields the source actually reported). Frames: 02 27 <afi> and 02 29 <dsfid>
// (+CRC).
//
// Unlike the backdoor UID frames, these have no read-back to fall back on -- the verify step issues
// INVENTORY, which returns the UID and nothing else. So a failure here has to be recorded rather
// than shrugged off: readers in AFI-filtered deployments ignore a tag whose AFI doesn't match, and
// a clone that silently kept the card's own AFI would look perfect and not work.
static void
    iso15693_poller_write_identity(Iso15693Poller* instance, Iso15693_3Poller* iso_poller) {
    const Iso15693_3SystemInfo* sys = &instance->clone_source->system_info;
    BitBuffer* tx = bit_buffer_alloc(ISO15693_POLLER_BUF_SIZE);
    BitBuffer* rx = bit_buffer_alloc(ISO15693_POLLER_BUF_SIZE);

    if(sys->flags & ISO15693_3_SYSINFO_FLAG_DSFID) {
        bit_buffer_reset(tx);
        bit_buffer_append_byte(tx, ISO15693_MAGIC_FLAGS);
        bit_buffer_append_byte(tx, ISO15693_MAGIC_CMD_WRITE_DSFID);
        bit_buffer_append_byte(tx, sys->dsfid);
        if(iso15693_3_poller_send_frame(iso_poller, tx, rx, ISO15693_3_FDT_WRITE_POLL_FC) !=
               Iso15693_3ErrorNone ||
           iso15693_poller_response_is_error(rx)) {
            FURI_LOG_W(TAG, "DSFID write refused");
            instance->result.dsfid_failed = true;
        }
    }
    if(sys->flags & ISO15693_3_SYSINFO_FLAG_AFI) {
        bit_buffer_reset(tx);
        bit_buffer_append_byte(tx, ISO15693_MAGIC_FLAGS);
        bit_buffer_append_byte(tx, ISO15693_MAGIC_CMD_WRITE_AFI);
        bit_buffer_append_byte(tx, sys->afi);
        if(iso15693_3_poller_send_frame(iso_poller, tx, rx, ISO15693_3_FDT_WRITE_POLL_FC) !=
               Iso15693_3ErrorNone ||
           iso15693_poller_response_is_error(rx)) {
            FURI_LOG_W(TAG, "AFI write refused");
            instance->result.afi_failed = true;
        }
    }

    bit_buffer_free(tx);
    bit_buffer_free(rx);
}

static void iso15693_poller_report(Iso15693Poller* instance, Iso15693PollerEvent event) {
    if(instance->callback) {
        instance->callback(event, instance->context);
    }
}

// The four ordinary data blocks the gen1 backdoor repurposes as UID / unlock / commit registers.
static const uint8_t iso15693_gen1_backdoor_blocks[] = {
    ISO15693_MAGIC_BLK_UID_LO,
    ISO15693_MAGIC_BLK_UID_HI,
    ISO15693_MAGIC_BLK_UNLOCK,
    ISO15693_MAGIC_BLK_COMMIT,
};

static bool iso15693_poller_is_gen1_backdoor_block(uint16_t block) {
    for(size_t i = 0; i < COUNT_OF(iso15693_gen1_backdoor_blocks); i++) {
        if(block == iso15693_gen1_backdoor_blocks[i]) return true;
    }
    return false;
}

// True if `block` in the source image is entirely zero.
static bool iso15693_poller_block_is_empty(
    const Iso15693_3Data* image,
    uint16_t block,
    uint8_t block_size) {
    const uint8_t* block_data = iso15693_3_get_block_data(image, (uint8_t)block);
    for(uint8_t i = 0; i < block_size; i++) {
        if(block_data[i] != 0) return false;
    }
    return true;
}

// Reclassify the refused blocks that form a contiguous empty run at the very top of what we
// attempted: those, and only those, are explainable as "past the card's real capacity", and they
// cost nothing because the card reports unwritten blocks as zero anyway.
//
// A refusal anywhere else has some other cause -- a block locked on the target, or a transient RF
// error -- and we cannot tell which, so it stays a real failure. Guessing "capacity" for those is
// what let the UI print a fabricated card size in a success tone.
static void iso15693_poller_excuse_empty_tail(Iso15693Poller* instance, uint8_t block_size) {
    Iso15693PollerResult* result = &instance->result;

    for(uint16_t block = result->blocks_total; block-- > 0;) {
        const uint8_t mask = (uint8_t)(1u << (block % 8));
        if(!(result->failed_bitmap[block / 8] & mask)) break; // tail ended: the card took this one
        if(!iso15693_poller_block_is_empty(instance->clone_source, block, block_size)) {
            break; // real data we could not place -- a genuine shortfall, not a capacity limit
        }
        result->failed_bitmap[block / 8] &= (uint8_t)~mask;
        result->failed_count--;
        result->over_capacity++;
    }
}

// Geometry for the current mode's block pass: the source image for a clone, the card for a wipe.
static const Iso15693_3Data* iso15693_poller_pass_image(Iso15693Poller* instance) {
    return (instance->mode == Iso15693PollerModeWipe) ? nfc_poller_get_data(instance->poller) :
                                                        instance->clone_source;
}

// Prepare a block pass. Returns false when there is nothing to attempt, having recorded which kind
// of nothing it was -- a clone whose *source file* carries no blocks is not the card's fault, and
// must not be reported as one.
//
// Do our best to reproduce the card EXACTLY: we attempt every block and deliberately do NOT cap at
// the target's advertised block count. On these magic cards WRITE BLOCK is gated by physical memory,
// not by the reported count (verified on hardware: writes succeed well past the advertised count,
// and a card's high blocks don't even read until they've been written -- so a pre-write read may
// under-report true capacity). Capping would also leave stale data in the reachable gap when
// re-cloning onto a card that currently advertises fewer blocks. The only reliable capacity test is
// to write the block and see if it takes.
//
// We also do NOT skip blocks locked in the SOURCE image: the source's lock bits describe the
// ORIGINAL card, not the magic target (which is writable regardless), and locked blocks are exactly
// where real tags keep provisioned data.
static bool iso15693_poller_begin_block_pass(Iso15693Poller* instance) {
    const Iso15693_3Data* image = iso15693_poller_pass_image(instance);
    const uint16_t block_count = iso15693_3_get_block_count(image);
    const uint8_t block_size = iso15693_3_get_block_size(image);
    Iso15693PollerResult* result = &instance->result;

    instance->block_cursor = 0;

    if(block_count == 0 || block_size == 0 || block_size > ISO15693_MAX_BLOCK_SIZE) {
        result->pass = (instance->mode == Iso15693PollerModeWipe) ?
                           Iso15693BlockPassNoGeometry :
                           Iso15693BlockPassNoSourceBlocks;
        return false;
    }

    // What we will actually attempt, which is not what the image advertised when it claims more than
    // the 256 blocks a uint8_t block number can address. Set up front so progress events carry a
    // meaningful "of N" from the first one.
    result->blocks_total = (block_count < ISO15693_POLLER_MAX_BLOCKS) ? block_count :
                                                                        ISO15693_POLLER_MAX_BLOCKS;
    return true;
}

// Write the block at the cursor and advance. Returns false when the pass is over -- either finished
// or aborted because the card left the field.
//
// One block per call, so the caller can return to the Nfc worker loop between blocks. That is not a
// style choice: the progress event below lands in the ViewDispatcher's fixed-size queue with
// FuriWaitForever, and a Back press has the GUI thread sitting in furi_thread_join waiting for this
// worker. Emitting a whole card's worth of events without yielding fills the queue and deadlocks
// both threads. The USCUID-UL poller yields per page for the same reason.
static bool
    iso15693_poller_write_next_block(Iso15693Poller* instance, Iso15693_3Poller* iso_poller) {
    Iso15693PollerResult* result = &instance->result;
    const uint16_t block = instance->block_cursor;

    if(block >= result->blocks_total) return false;
    instance->block_cursor++;

    // A clone that got its UID from gen1 must leave gen1's registers alone: they are ordinary data
    // blocks reached by the ordinary WRITE BLOCK opcode, so writing the source's bytes over them
    // would land arbitrary data in the unlock and commit registers of a card that was just armed.
    // (Harmless on gen2, whose backdoor lives in a separate address space -- hence the used_gen1
    // test rather than an unconditional skip. A wipe writes zeros, which cannot arm a commit.)
    if(result->used_gen1 && iso15693_poller_is_gen1_backdoor_block(block)) {
        result->gen1_reserved++;
        return true;
    }

    const uint8_t zeros[ISO15693_MAX_BLOCK_SIZE] = {0};
    const Iso15693_3Data* image = iso15693_poller_pass_image(instance);
    const uint8_t block_size = iso15693_3_get_block_size(image);
    const bool wipe = (instance->mode == Iso15693PollerModeWipe);
    const uint8_t* block_data = wipe ? zeros : iso15693_3_get_block_data(image, (uint8_t)block);

    const Iso15693_3Error error =
        iso15693_3_poller_write_block(iso_poller, block_data, (uint8_t)block, block_size);

    if(error == Iso15693_3ErrorNone) {
        result->blocks_written++;
    } else if(error == Iso15693_3ErrorTimeout || error == Iso15693_3ErrorNotPresent) {
        FURI_LOG_W(TAG, "block %u: card stopped answering (err %d)", block, error);
        // The card stopped answering. That is not a refusal, and must never be recorded as one:
        // treating it as one is what let a lifted card be reported as write-protected, or -- when
        // the source's tail was empty -- excused as capacity and announced as a complete clone.
        result->card_lost = true;
        return false;
    } else {
        FURI_LOG_W(TAG, "block %u refused (err %d)", block, error);
        result->failed_count++;
        result->failed_bitmap[block / 8] |= (uint8_t)(1u << (block % 8));
    }

    iso15693_poller_report(instance, Iso15693PollerEventWriteProgress);
    return true;
}

// Classify the finished pass. Only reached when the card stayed in the field.
static void iso15693_poller_finish_block_pass(Iso15693Poller* instance) {
    Iso15693PollerResult* result = &instance->result;
    const Iso15693_3Data* image = iso15693_poller_pass_image(instance);

    // Only a clone has a notion of "the source had nothing there anyway".
    if(instance->mode != Iso15693PollerModeWipe) {
        iso15693_poller_excuse_empty_tail(instance, iso15693_3_get_block_size(image));
    }

    if(result->blocks_written == 0) {
        result->pass = Iso15693BlockPassNothingWritten;
    } else if(result->failed_count > 0) {
        result->pass = Iso15693BlockPassPartial;
    } else {
        result->pass = Iso15693BlockPassComplete;
    }
}

// The terminal outcome once a write finishes, using the same three-way split as the 2.0 pollers
// (gen2_poller.c gen2_poller_wipe/write_handler): nothing written -> Fail, nothing refused ->
// Success, anything between -> Partial.
//
// A clone that fell back to gen1 is also Partial: gen1 stamps the UID into blocks 56/57 and
// unlock/commit into 62/63, so those can no longer match the source. (A bare Write-UID has no source
// data to disturb, so gen1 there is still a clean Success.)
static Iso15693PollerEvent iso15693_poller_write_outcome(Iso15693Poller* instance) {
    const Iso15693PollerResult* result = &instance->result;

    // NotRun is Write UID: no block pass ran, so only the identity/gen1 tests below apply.
    if(result->pass == Iso15693BlockPassNoGeometry ||
       result->pass == Iso15693BlockPassNothingWritten) {
        return Iso15693PollerEventFail;
    }
    // The source file carried no blocks. The UID write still succeeded, so this is not a failure of
    // the card -- report it as Partial and let the screen name the file as the reason.
    if(result->pass == Iso15693BlockPassNoSourceBlocks) return Iso15693PollerEventPartial;

    const bool gen1_clone = (instance->mode == Iso15693PollerModeClone) && result->used_gen1;
    const bool identity_lost = result->afi_failed || result->dsfid_failed;
    if(result->pass == Iso15693BlockPassPartial || gen1_clone || identity_lost) {
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

// Start the block pass, or report straight away if there is nothing to attempt.
static NfcCommand
    iso15693_poller_start_block_pass(Iso15693Poller* instance, Iso15693_3Poller* iso_poller) {
    if(instance->mode == Iso15693PollerModeClone) {
        iso15693_poller_write_identity(instance, iso_poller);
    }
    if(!iso15693_poller_begin_block_pass(instance)) {
        iso15693_poller_report(instance, iso15693_poller_write_outcome(instance));
        return NfcCommandStop;
    }
    instance->write_state = Iso15693WriteStateWriteBlocks;
    return NfcCommandContinue;
}

// One block per callback. Yielding between blocks is what keeps a Back press from deadlocking the
// app -- see iso15693_poller_write_next_block.
static NfcCommand
    iso15693_poller_step_block_pass(Iso15693Poller* instance, Iso15693_3Poller* iso_poller) {
    if(iso15693_poller_write_next_block(instance, iso_poller)) return NfcCommandContinue;

    if(instance->result.card_lost) {
        // The card stopped answering mid-pass. Say that, rather than describing the card's memory.
        iso15693_poller_report(instance, Iso15693PollerEventCardLost);
        return NfcCommandStop;
    }

    iso15693_poller_finish_block_pass(instance);

    // "Nothing was written" still has two causes -- a write-protected card, or one that went missing
    // between the last write and now. Probe once before blaming the card.
    Iso15693PollerEvent outcome = iso15693_poller_write_outcome(instance);
    if(instance->result.pass == Iso15693BlockPassNothingWritten) {
        uint8_t readback[ISO15693_3_UID_SIZE] = {0};
        if(iso15693_poller_verify_inventory(iso_poller, readback) != Iso15693_3ErrorNone) {
            outcome = Iso15693PollerEventCardLost;
        }
    }

    iso15693_poller_report(instance, outcome);
    return NfcCommandStop;
}

// Drives one write-mode step. Runs on the Nfc worker thread with the field active. Returns the
// NfcCommand for the poller: Reset power-cycles the field (so the next Ready verifies a freshly
// re-powered card), Stop ends the operation.
//
// Order matters for safety. The backdoor UID write goes FIRST and the data blocks only follow once
// the read-back proves the card accepted it. Detection cannot tell a magic ISO15693 tag from an
// ordinary one -- any NfcV tag that activates is only a candidate -- so writing data first meant an
// ordinary tag was overwritten wholesale before the app discovered it wasn't magic. gen2's backdoor
// is a custom command an ordinary tag ignores, which makes it a genuinely non-destructive probe;
// only the gen1 fallback touches real memory, and then just four blocks.
static NfcCommand
    iso15693_poller_write_step(Iso15693Poller* instance, Iso15693_3Poller* iso_poller) {
    uint8_t readback[ISO15693_3_UID_SIZE] = {0};

    switch(instance->write_state) {
    case Iso15693WriteStateStart: {
        // Wipe zeros the card's own blocks and never touches the UID, so it's a single pass with no
        // backdoor write or field reset.
        if(instance->mode == Iso15693PollerModeWipe) {
            return iso15693_poller_start_block_pass(instance, iso_poller);
        }
        // Remember the current UID so the destructive gen1 fallback only runs if gen2 left the
        // card untouched. The poller read the UID into its data during activation.
        const Iso15693_3Data* poller_data = nfc_poller_get_data(instance->poller);
        memcpy(instance->original_uid, poller_data->uid, ISO15693_3_UID_SIZE);

        // The gen2 CFG block programs what the card reports for geometry / IC ref. For a clone, use
        // the source's values so the copy advertises the same chip identity; otherwise the fixed
        // magic default. (gen1 has no geometry block, so a gen1 fallback keeps the card's own.)
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
            // gen2 took: the card is magic. Safe to write the payload now.
            return iso15693_poller_start_block_pass(instance, iso_poller);
        }
        if(memcmp(readback, instance->original_uid, ISO15693_3_UID_SIZE) == 0) {
            // gen2 changed nothing: a gen1 card, or an ordinary tag. gen1 uses standard WRITE BLOCK,
            // so this is the first frame that can damage an ordinary tag -- but it reaches only
            // blocks 56/57/62/63, and the user confirmed a write that says so. Still no data blocks.
            iso15693_poller_send_backdoor_uid_gen1(iso_poller, instance->target_uid);
            instance->write_state = Iso15693WriteStateVerifyGen1;
            return NfcCommandReset;
        }
        // gen2 changed the UID but not to the target: stop rather than compound it with gen1.
        iso15693_poller_report(instance, Iso15693PollerEventFail);
        return NfcCommandStop;
    }

    case Iso15693WriteStateWriteBlocks:
        return iso15693_poller_step_block_pass(instance, iso_poller);

    case Iso15693WriteStateVerifyGen1:
    default: {
        if(iso15693_poller_verify_inventory(iso_poller, readback) != Iso15693_3ErrorNone) {
            iso15693_poller_report(instance, Iso15693PollerEventCardLost);
            return NfcCommandStop;
        }
        if(memcmp(readback, instance->target_uid, ISO15693_3_UID_SIZE) != 0) {
            // Not magic by either method. The gen1 attempt did write blocks 56/57/62/63, but no
            // source data went down, so an ordinary tag loses four blocks rather than all of them.
            iso15693_poller_report(instance, Iso15693PollerEventFail);
            return NfcCommandStop;
        }
        // gen1 set the UID (NOTE: gen1 path is not hardware-validated). Record it so a clone reports
        // Partial and flags that blocks 56/57/62/63 now hold UID/commit bytes, not the source's data.
        instance->result.used_gen1 = true;
        return iso15693_poller_start_block_pass(instance, iso_poller);
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

    // On the FIRST activation of a clone/wipe (write_state still Start, before any write step),
    // tell the scene a card was detected so its popup switches from "apply the same card" to
    // "writing" -- the other magic pollers emit this event; ours previously did not, so the ISO15693
    // clone popup sat on "apply the same card" for the whole write. Fires once (write_step advances
    // the state). Not emitted in a bare Write-UID (its scene has a static popup and its own callback).
    if(instance->write_state == Iso15693WriteStateStart &&
       (instance->mode == Iso15693PollerModeClone || instance->mode == Iso15693PollerModeWipe)) {
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
    instance->activation_errors = 0;
    memset(instance->target_uid, 0, sizeof(instance->target_uid));
    memset(instance->original_uid, 0, sizeof(instance->original_uid));
    memset(&instance->result, 0, sizeof(instance->result));
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
    Iso15693PollerCallback callback,
    void* context) {
    furi_assert(instance);
    furi_assert(!instance->running);
    instance->mode = mode;
    instance->callback = callback;
    instance->context = context;
    instance->write_state = Iso15693WriteStateStart;
    instance->activation_errors = 0;
    instance->block_cursor = 0;
    memset(&instance->result, 0, sizeof(instance->result));
    iso15693_3_reset(instance->data);
    instance->running = true;
    nfc_poller_start(instance->poller, iso15693_poller_nfc_callback, instance);
}

void iso15693_poller_start(
    Iso15693Poller* instance,
    Iso15693PollerCallback callback,
    void* context) {
    iso15693_poller_start_internal(instance, Iso15693PollerModeInfo, callback, context);
}

void iso15693_poller_start_write_uid(
    Iso15693Poller* instance,
    const uint8_t* uid,
    Iso15693PollerCallback callback,
    void* context) {
    furi_assert(instance);
    furi_assert(uid);
    memcpy(instance->target_uid, uid, ISO15693_3_UID_SIZE);
    iso15693_poller_start_internal(instance, Iso15693PollerModeWriteUid, callback, context);
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
    iso15693_poller_start_internal(instance, Iso15693PollerModeClone, callback, context);
}

void iso15693_poller_start_wipe(
    Iso15693Poller* instance,
    Iso15693PollerCallback callback,
    void* context) {
    furi_assert(instance);
    iso15693_poller_start_internal(instance, Iso15693PollerModeWipe, callback, context);
}

const Iso15693PollerResult* iso15693_poller_get_result(Iso15693Poller* instance) {
    furi_assert(instance);
    return &instance->result;
}

// True if the source image has non-empty data in any of the gen1 backdoor blocks (56/57/62/63) that
// actually exist within its block count. A gen1 fallback overwrites those blocks with UID/commit
// bytes, so it can't reproduce a source that stores real data there -- callers warn about this up
// front. (Purely a source inspection; touches no hardware.)
bool iso15693_poller_source_uses_gen1_blocks(const Iso15693_3Data* source) {
    if(!source) return false;
    const uint16_t block_count = iso15693_3_get_block_count(source);
    const uint8_t block_size = iso15693_3_get_block_size(source);
    if(block_size == 0 || block_size > ISO15693_MAX_BLOCK_SIZE) return false;
    for(size_t i = 0; i < COUNT_OF(iso15693_gen1_backdoor_blocks); i++) {
        const uint16_t block = iso15693_gen1_backdoor_blocks[i];
        if(block >= block_count) continue;
        if(!iso15693_poller_block_is_empty(source, block, block_size)) return true;
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
