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
    Iso15693PollerModeWipe, // zero every data block, the gen1 registers included -- no UID command
        // is sent, but that is not the same as leaving the UID alone; see iso15693_poller_start_wipe
} Iso15693PollerMode;

typedef enum {
    Iso15693PollerEventSuccess, // Info: card read. Write/clone: the target UID read back and matched
        // (the UID, plus the AFI/DSFID on a clone, are re-read; block CONTENTS are never compared --
        // a data block counts as written when the card ACKs it). Wipe: the sweep ended without the
        // clock cutting it, and nothing it reached is known to still hold data -- which is weaker than
        // "reached the card's top and cleared everything", because the sweep can also end at the
        // 256-block ceiling, and a block dropped below the advertised count was reached without being
        // proven clear. What the drop does guarantee is that a block proven to hold data is never
        // dropped. Says nothing about whether the identity re-check ran -- that is uid_verified.
    Iso15693PollerEventPartial, // the operation mostly worked but isn't a clean result: a clone lost
        // some data blocks, fell back to gen1, or had its AFI/DSFID write rejected; or a wipe couldn't
        // clear every block, or moved the card's UID. ALSO either mode cut short by the wall-clock
        // bound -- the run's own job is left undone whatever the counts say. Which of those it was is
        // in Iso15693PollerResult, and its flags are not interchangeable: pass_truncated in particular
        // is a qualifier no block figure can show.
    Iso15693PollerEventFail, // the operation didn't take: the backdoor write was rejected (not a
        // magic tag), the gen2 write changed the UID to neither the original nor the target, an opt-in
        // gen1 UID didn't take, the clone source had no data blocks, a wipe cleared nothing, or a
        // Write UID asked for the UID the card already has. Those are NOT the same thing to a user --
        // read Iso15693PollerResult (uid_unexpected / gen1_attempted / uid_unverifiable) to tell them
        // apart before picking a message.
    Iso15693PollerEventCardLost, // no card in the field, or removed before the operation finished.
        // Covers a card lifted DURING a block pass, which makes every remaining block fail and so is
        // indistinguishable from the card's capacity ending there: both loops re-check that the card
        // is present before making any capacity claim, and report this instead of a write result.
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
// Emits CardDetected, then Success (the read-back inventory returns the requested UID), Fail,
// CardLost, or NotGen2 -- the last offering the destructive gen1 retry via
// iso15693_poller_start_write_uid_gen1(). Two distinct Fails, both flagged in the result:
// uid_unexpected, and uid_unverifiable, which is refused BEFORE anything is sent.
// The byte-level frames are defined in iso15693_poller.c (ported from proxmark3 armsrc/iso15693.c,
// SetTag15693Uid / SetTag15693Uid_v2).
void iso15693_poller_start_write_uid(
    Iso15693Poller* instance,
    const uint8_t* uid,
    Iso15693PollerCallback callback,
    void* context);

// Opt-in gen1 UID-write retry (call after start_write_uid reported NotGen2 and the user confirmed).
// Writes the destructive gen1 sequence -- ordinary WRITE BLOCK into blocks 56/57/62/63, the four
// registers the rest of this header calls the gen1 registers (ISO15693_MAGIC_BLK_* in the .c says which
// is which). ANY writable tag accepts an ordinary write, so on a non-magic tag this destroys four
// blocks of user data. A Write-UID has no payload to follow, so a verified UID is a clean Success.
// Emits CardDetected, then Success, Fail (the gen1 UID didn't take) or CardLost. The sequence goes out
// before anything is verified, so a Fail still carries gen1_attempted -- see that field for what the
// caller then owes the user. NOTE: gen1 is NOT hardware-validated.
void iso15693_poller_start_write_uid_gen1(
    Iso15693Poller* instance,
    const uint8_t* uid,
    Iso15693PollerCallback callback,
    void* context);

// Full clone (gen2 attempt): write `source`'s UID via the gen2 backdoor FIRST, and only once that UID
// reads back does it write every data block (standard WRITE BLOCK) -- so a tag that does not take the
// gen2 UID is never clobbered by a doomed clone. `source` is an ISO15693-3 image loaded from a saved
// .nfc. Emits CardDetected, then Success (UID + all blocks), Partial, Fail (the source has no data
// blocks, no data block would take, or uid_unexpected), CardLost, or NotGen2 -- the last offering the
// destructive gen1 retry via iso15693_poller_start_clone_gen1().
void iso15693_poller_start_clone(
    Iso15693Poller* instance,
    const Iso15693_3Data* source,
    Iso15693PollerCallback callback,
    void* context);

// Opt-in gen1 clone retry (call after start_clone reported NotGen2 and the user confirmed). Writes the
// destructive gen1 UID sequence FIRST and, only if that UID reads back, writes the data blocks --
// skipping the gen1 registers, which now hold the UID and so can never match the source. That is why a
// gen1 clone that took still reports Partial and never a clean Success, and why a card that cannot do
// gen1 loses at most those four blocks. Emits CardDetected, then Partial, Fail (the gen1 UID didn't
// take, the source had no data blocks, or every data block was rejected -- gen1_attempted separates
// the first, since those four blocks are gone either way) or CardLost.
// NOTE: gen1 is NOT hardware-validated.
void iso15693_poller_start_clone_gen1(
    Iso15693Poller* instance,
    const Iso15693_3Data* source,
    Iso15693PollerCallback callback,
    void* context);

// The per-block write result of a clone or a wipe. A "failure" here means the block failed EVERY write
// retry (a transient glitch that later succeeded is not a failure).
typedef struct {
    // The blocks this run attempted and reports against, which is mode-dependent: the source block
    // count for a gen2 clone, that count MINUS the 4 skipped gen1 registers for a gen1 clone, or, for a
    // wipe, the advertised count WHILE the sweep runs (the progress popup needs a denominator before
    // the sweep's true length is known) and then the number of blocks the card PROVED it holds.
    // Terminal events therefore always report the measured figure; only WriteProgress can see the
    // advertised one. A count, never an index. See ISO15693_POLLER_WIPE_MAX_BLOCKS in the .c.
    uint16_t blocks_total;
    // Wipe only: the block count the card ADVERTISED, so a report can put the measured figure beside
    // the claim. The gen2 CFG frame programs this number, which is why the two differing is
    // information rather than an error: a card cloned from a smaller source advertises less than it
    // still holds and serves reads for, and a card with fake flash advertises more. 0 for a clone.
    uint16_t blocks_advertised;
    // The run stopped on its wall-clock bound (ISO15693_POLLER_PASS_MAX_MS) rather than at its natural
    // end, so its range is a cut and no report may pass that range off as a finding about the card.
    // BOTH modes carry the same bound. What the flag SAVES differs by mode, and only the clone has the
    // problem it was added for: its back-fill records every block above the cut as a failure --
    // otherwise the "written" figure, derived by subtraction, would claim they all landed -- so without
    // the flag a reader cannot tell a block the card REFUSED from one nothing was ever sent to, and
    // every screen downstream states the stronger claim. A cut WIPE has no back-fill: the sweep breaks
    // out of the loop and both of its bit-setting sites are inside the body the deadline gates, so the
    // unreached blocks are outside the denominator rather than inside the numerator -- which leaves this
    // flag as the only thing that mentions those blocks at all.
    //
    // What a re-run can do about it, since two screens have to answer that: the bound is a WALL CLOCK,
    // not a position, so a card that is consistently this slow is cut in the same place every time and
    // only a transient -- marginal coupling forcing per-block retries -- clears on a second pass.
    // Observed: a retried wipe stopped at the same block. So Retry may be offered but never promised.
    bool pass_truncated;
    // Where the clock cut the run: the first block index NOT attempted. Only meaningful when the flag
    // above is set, and NOT derivable from blocks_total -- after a wipe that is highest_present + 1 and
    // sits at or below the cut -- structurally, so no card can put blocks_total above it. What two
    // cards DO differ in is which side of the ADVERTISED COUNT the cut lands on, and only one of them
    // opens a gap between the cut and the total:
    //   past the claim -- a card that refuses every write but answers a read everywhere never
    //     accumulates an absent run, so the sweep walks beyond the advertised count and the cut lands
    //     above it. But every one of those reads calls wipe_note_present, so blocks_total == cut_block
    //     exactly, so there is no gap at all on this card. Where a gap can open, the bound is
    //     ISO15693_POLLER_WIPE_ABSENT_RUN - 1: the deadline is tested at the TOP of the iteration, so on
    //     every path the run still open when the clock fires is at most that, giving
    //     cut_block - blocks_total <= 7. NOT "you cannot reach the claim after that many absences" --
    //     you can. Below the claim the trip falls through to continue rather than ending the sweep,
    //     which is the advertised-count floor doing its job, and one later block that answers zeroes
    //     the run.
    //   below the claim -- a card claiming 200 while holding 10. Under the claim the sweep never stops
    //     on absence alone, so it grinds on to the clock with the cut somewhere in the middle and
    //     blocks_total stuck at 10. THIS is where the gap gets large.
    // Either way, any string naming where the run stopped has to read this, not blocks_total.
    uint16_t cut_block;
    // Wipe only: the post-power-cycle UID check reached an answer. When false it did not run -- the card
    // did not come back, or did not answer the inventory -- so uid_changed being false is the absence of
    // an observation rather than a clean result, and a caller reporting success should say so.
    bool uid_verified;
    // Blocks that failed and count as a real problem: they held source data (lost), or were empty
    // failures that weren't a clean top-of-card tail. -> Partial. In wipe mode this is every block the
    // report holds against the card, which is more than the ones that still held data: interior blocks
    // that answered nothing are folded in too (they cannot be shown clear, so they fail closed), as are
    // blocks proven present by the activation cache. It prints to the user as "Not cleared: %u" and
    // names indices in Details, so it has to be the whole set.
    uint16_t failed_count;
    // Empty blocks that failed and form a contiguous run at the top of the card (past physical
    // capacity; nothing lost) -> Success with a note. Unused by a wipe.
    uint16_t over_capacity;
    // Bit N set = block N failed, at its TRUE block index, covering both buckets above. A set bit can
    // therefore sit ABOVE blocks_total -- scan the whole bitmap, not [0, blocks_total).
    uint8_t failed_bitmap[ISO15693_POLLER_BLOCK_BITMAP_SIZE];
    // The gen1 fallback set the UID. Implies gen1_attempted; the difference is that here it read back.
    bool used_gen1;
    // The failures are a persistent, contiguous run at the very top of the card, i.e. the source is
    // genuinely larger than the card's physical capacity. False for a scattered or anomalous failure,
    // which is reported generically with no capacity claim.
    bool capacity_confirmed;
    // The source reported an AFI / DSFID, but after the write GET SYSTEM INFO did not read that field
    // back with the source's value, so the copy does not carry it. Verified by read-back, not inferred
    // from the write's return. -> Partial.
    bool identity_failed;
    // Running position of the block pass, for the live progress popup. Meaningful from the first
    // WriteProgress event. Converges on blocks_total for a clone; for a wipe the two need not end up
    // equal, for the reason blocks_total gives.
    uint16_t blocks_done;
    // Fail: the gen2 backdoor moved the UID to neither the original nor the target. That PROVES the
    // card is magic -- an inert tag cannot change its UID -- so it is not "not a magic tag".
    // uid_readback holds what the card answered with, which is the only way back to it.
    bool uid_unexpected;
    uint8_t uid_readback[ISO15693_3_UID_SIZE];
    // This run SENT the destructive gen1 UID sequence, so the gen1 registers have had UID/unlock/commit
    // bytes written at them whatever the outcome. Whether the tag took them is not known -- the frames'
    // return values are discarded, as they must be on a card that may not answer -- but any writable tag
    // accepts an ordinary WRITE BLOCK, so on a Fail the honest report is that those four blocks may have
    // been overwritten on what is most likely an ordinary tag. Set at start, before any frame goes out.
    bool gen1_attempted;
    // Fail, Write UID only: the requested UID is the one the card already has, so nothing was written.
    // A read-back against a UID the card already carries is passed by any tag, magic or not, so a
    // Success there would be unearned -- the run stops instead of claiming one.
    bool uid_unverifiable;
    // Partial, wipe only: the UID read back after the wipe is not the one the card presented before it,
    // so the wipe moved the card's identity. uid_readback holds the UID it now answers to. Only ever set
    // from a positive observation of a different UID -- an inventory that fails outright is logged and
    // ignored, since it cannot be told from the card being lifted the moment the wipe finished.
    bool uid_changed;
} Iso15693PollerResult;

// Fill `result` with the outcome of the last clone or wipe. Valid once a terminal event has been
// reported; the poller resets every field at the start of each run.
void iso15693_poller_get_result(Iso15693Poller* instance, Iso15693PollerResult* result);

// True if the source stores real data in one of the gen1 registers, which a gen1 fallback would
// overwrite -- so the write flow can warn before a possible gen1 clone. Source inspection only.
bool iso15693_poller_source_uses_gen1_blocks(const Iso15693_3Data* source);

// Wipe: write zeros to every data block the card PHYSICALLY holds -- which is exactly what proxmark's
// 'hf 15 wipe' does NOT do: its loop carries a 0..0xFF bound but breaks at the first refused write, so
// on a 64-block card it examines 65.
// It does NOT stop at the card's advertised block count, because that number is programmable (see
// blocks_advertised) and a card still serves reads above it. The sweep runs upward until a run of
// ISO15693_POLLER_WIPE_ABSENT_RUN blocks answers neither a write nor a read, or it hits the 256-block
// ceiling, or the clock cuts it -- see ISO15693_POLLER_WIPE_MAX_BLOCKS in the .c for the hardware
// measurement behind the first of those. BELOW the advertised count it never stops on absence alone:
// the card's own claim is evidence those blocks exist.
// The gen1 registers are cleared too. On gen2 they are ordinary user data, and the wipe performs no
// magic detection, so it cannot spare them on the chance the card is gen1 -- meaning it cannot
// guarantee a gen1 UID survives. It re-reads the UID afterwards, behind the same field power-cycle the
// UID writes use since a gen1 card latches a written UID on the next power-up, and then reports rather
// than promises: uid_changed and uid_verified carry the answer. See the open question in
// iso15693_poller_wipe_blocks.
// Emits CardDetected, then Success / Partial / Fail (nothing could be wiped) / CardLost. Per-block
// detail is in iso15693_poller_get_result(), whose blocks_total is what the card proved it holds, so
// blocks that do not exist are never reported as blocks that wouldn't clear.
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
