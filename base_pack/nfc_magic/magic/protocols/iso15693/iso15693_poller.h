#pragma once

#include <nfc/nfc_poller.h>
#include <lib/nfc/protocols/iso15693_3/iso15693_3.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Iso15693Poller Iso15693Poller;

// Size (bytes) of the per-block failure bitmap; covers up to 256 blocks (the ISO15693 max).
#define ISO15693_POLLER_BLOCK_BITMAP_SIZE (32U)

typedef enum {
    Iso15693PollerModeInfo, // detect + read UID / system info
    Iso15693PollerModeWriteUid, // magic backdoor UID write (gen2 only; gen1 is a separate opt-in run)
    Iso15693PollerModeClone, // write UID + all data blocks from a source image
    Iso15693PollerModeWipe, // zero every data block, 56/57/62/63 included (no UID command is sent,
        // but on gen1 those four blocks ARE the UID registers -- see iso15693_poller_wipe_blocks)
} Iso15693PollerMode;

typedef enum {
    Iso15693PollerEventSuccess, // Info: card read. Write/clone: the target UID read back and matched
        // (the UID, plus the AFI/DSFID on a clone, are re-read; block CONTENTS are never compared --
        // a data block counts as written when the card ACKs it). Wipe: every block it attempted
        // accepted the zero write (no UID command is sent, and the UID is never re-read).
    Iso15693PollerEventPartial, // the operation mostly worked but isn't a clean result: a clone lost
        // some data blocks, fell back to gen1 (overwriting 56/57/62/63), or had its AFI/DSFID write
        // rejected; or a wipe couldn't clear every block.
    Iso15693PollerEventFail, // the operation didn't take: the backdoor write was rejected (not a
        // magic tag), the gen2 write changed the UID to neither the original nor the target, an opt-in
        // gen1 UID didn't take, the clone source had no data blocks, or a wipe cleared nothing.
    Iso15693PollerEventCardLost, // no card in the field / card removed before the operation finished
    Iso15693PollerEventCardDetected, // first activation of any write mode -- clone, wipe AND Write
        // UID. Flips the shared write popup off "apply the card" onto "Writing". Not sent in Info mode.
    Iso15693PollerEventWriteProgress, // some blocks done; read the result for the running counts.
        // Emitted a bounded number of times per pass, NOT per block -- see
        // ISO15693_POLLER_PROGRESS_STEPS in the .c for why that bound is a correctness constraint
    Iso15693PollerEventNotGen2, // gen2 left the UID unchanged (not a gen2 magic card, or not magic
        // at all). Nothing was written; the scene offers the opt-in gen1 retry. Emitted for a clone AND
        // for a bare Write-UID -- both gate the destructive gen1 attempt behind that consent.
} Iso15693PollerEvent;

typedef void (*Iso15693PollerCallback)(Iso15693PollerEvent event, void* context);

Iso15693Poller* iso15693_poller_alloc(Nfc* nfc);

void iso15693_poller_free(Iso15693Poller* instance);

// Detect + read (Info mode). Emits Success once a card is read, or CardLost after a bounded number
// of activation attempts with no card in the field.
void iso15693_poller_start(
    Iso15693Poller* instance,
    Iso15693PollerCallback callback,
    void* context);

// Magic UID write (gen2 attempt). `uid` is ISO15693_3_UID_SIZE bytes, MSB-first (uid[0] must be 0xE0).
// Writes ONLY the gen2 backdoor sequence -- a harmless custom command on a non-magic tag. Before the
// read-back it power-cycles the field (like proxmark's switch_off + getUID) so a card that only
// latches the new UID after a reset is not misreported as a failure.
// Reports CardDetected (first activation), then Success (the read-back inventory returns the
// requested UID), Fail (the UID changed to neither the original nor the target) or CardLost. If gen2
// leaves the UID unchanged it reports NotGen2 WITHOUT having written anything, so the caller can offer
// the destructive gen1 retry via iso15693_poller_start_write_uid_gen1().
// The byte-level frames are defined in iso15693_poller.c (ported from proxmark3 armsrc/iso15693.c,
// SetTag15693Uid / SetTag15693Uid_v2).
void iso15693_poller_start_write_uid(
    Iso15693Poller* instance,
    const uint8_t* uid,
    Iso15693PollerCallback callback,
    void* context);

// Opt-in gen1 UID-write retry (call after start_write_uid reported NotGen2 and the user confirmed).
// Writes the destructive gen1 sequence -- ordinary WRITE BLOCK into blocks 56/57/62/63, which ANY
// writable tag accepts, so on a non-magic tag this destroys four blocks of user data -- then verifies.
// A Write-UID has no payload to follow, so a verified UID is a clean Success.
// Reports CardDetected (first activation), then Success, Fail (the gen1 UID didn't take) or CardLost.
// NOTE: gen1 is NOT hardware-validated.
void iso15693_poller_start_write_uid_gen1(
    Iso15693Poller* instance,
    const uint8_t* uid,
    Iso15693PollerCallback callback,
    void* context);

// Full clone (gen2 attempt): write `source`'s UID via the gen2 backdoor FIRST, and only once that UID
// reads back does it write every data block (standard WRITE BLOCK) -- so a tag that does not take the
// gen2 UID is never clobbered by a doomed clone. `source` is an ISO15693-3 image loaded from a saved
// .nfc. Reports CardDetected (first activation), then Success (UID + all blocks), Partial (some / the
// AFI/DSFID write failed), Fail (source has no data blocks) or CardLost. If gen2 leaves the UID
// unchanged (not a gen2 magic card) it reports NotGen2 without writing anything, so the caller can
// offer the destructive gen1 retry via iso15693_poller_start_clone_gen1().
// CardLost also covers a card lifted DURING the block loop: that makes every remaining block fail,
// which is indistinguishable from the card's capacity ending there, so the loop re-checks the card is
// present before making any capacity claim and reports CardLost instead of a write result.
void iso15693_poller_start_clone(
    Iso15693Poller* instance,
    const Iso15693_3Data* source,
    Iso15693PollerCallback callback,
    void* context);

// Opt-in gen1 clone retry (call after start_clone reported NotGen2 and the user confirmed). Writes the
// destructive gen1 UID sequence FIRST (stamping the UID/unlock/commit into blocks 56/57/62/63) and,
// only if that UID reads back, writes the data blocks -- skipping 56/57/62/63, which now hold the UID,
// so they can't match the source -> Partial. A card that can't do gen1 therefore loses at most those
// four blocks. Reports CardDetected (first activation), then Partial (a gen1 clone that took is ALWAYS
// Partial -- 56/57/62/63 now hold the UID, so they can't match the source; this path never reports a
// clean Success), Fail (the gen1 UID didn't take) or CardLost. NOTE: gen1 is NOT hardware-validated.
void iso15693_poller_start_clone_gen1(
    Iso15693Poller* instance,
    const Iso15693_3Data* source,
    Iso15693PollerCallback callback,
    void* context);

// The per-block write result of a clone or a wipe. A "failure" here means the block failed EVERY write
// retry (a transient glitch that later succeeded is not a failure).
typedef struct {
    // The blocks this run attempted and reports against, which is mode-dependent: the source block
    // count for a gen2 clone, that count MINUS the 4 skipped backdoor registers for a gen1 clone, or
    // the card's full advertised block count for a wipe (which skips nothing).
    // NOTE failed_bitmap is indexed by TRUE block number, so a set bit can sit above blocks_total --
    // scan the whole bitmap, not [0, blocks_total).
    uint16_t blocks_total;
    // Blocks that failed and count as a real problem: they held source data (lost), or were empty
    // failures that weren't a clean top-of-card tail. -> Partial. In wipe mode, blocks that still held
    // data after a failed zero-write.
    uint16_t failed_count;
    // Empty blocks that failed and form a contiguous run at the top of the card (past physical
    // capacity; nothing lost) -> Success with a note. Unused by a wipe.
    uint16_t over_capacity;
    // Bit N set = block N failed, at its TRUE block index (covers both buckets above).
    uint8_t failed_bitmap[ISO15693_POLLER_BLOCK_BITMAP_SIZE];
    // The gen1 fallback set the UID, which overwrites blocks 56/57/62/63.
    bool used_gen1;
    // The failures are a persistent, contiguous run at the very top of the card, i.e. the source is
    // genuinely larger than the card's physical capacity. False for a scattered/anomalous failure
    // (reported generically, with no capacity claim).
    bool capacity_confirmed;
    // The source reported an AFI / DSFID, but after the write GET SYSTEM INFO did not read that field
    // back with the source's value, so the copy does not carry it. Verified by read-back, not inferred
    // from the write's return. -> Partial.
    bool identity_failed;
    // Running position of the block pass, for the live progress popup. Meaningful from the first
    // WriteProgress event; equals blocks_total once the pass has finished.
    uint16_t blocks_done;
} Iso15693PollerResult;

// Fill `result` with the outcome of the last clone or wipe. Valid once a terminal event has been
// reported; the poller resets every field at the start of each run.
void iso15693_poller_get_result(Iso15693Poller* instance, Iso15693PollerResult* result);

// True if the source stores real data in a gen1 backdoor block (56/57/62/63) that a gen1 fallback
// would overwrite -- so the write flow can warn before a possible gen1 clone. Source inspection only.
bool iso15693_poller_source_uses_gen1_blocks(const Iso15693_3Data* source);

// Wipe: write zeros to every data block on the card, like proxmark's 'hf 15 wipe'. Blocks
// 56/57/62/63 are cleared too -- on gen2 they are ordinary user data. On gen1 those same blocks are
// the UID/unlock/commit registers and nothing re-reads the UID afterwards, so the wipe does not
// guarantee the UID survives on a gen1 card; see the open question in iso15693_poller_wipe_blocks.
// Reports CardDetected (first activation), then Success / Partial (some blocks failed) / Fail
// (nothing could be wiped) / CardLost -- the last of which also covers a card lifted DURING the
// loop, so blocks that never got the chance aren't reported as blocks the card refused to clear.
// Per-block detail is available via iso15693_poller_get_result().
void iso15693_poller_start_wipe(
    Iso15693Poller* instance,
    Iso15693PollerCallback callback,
    void* context);

void iso15693_poller_stop(Iso15693Poller* instance);

// The last Info-mode read result (UID + system info + block data). Owned by the poller; valid until
// the poller is freed, so the Info scene copies it out. Read-only.
const Iso15693_3Data* iso15693_poller_get_data(Iso15693Poller* instance);

#ifdef __cplusplus
}
#endif
