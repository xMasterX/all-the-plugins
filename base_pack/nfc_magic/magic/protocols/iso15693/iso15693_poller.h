#pragma once

#include <nfc/nfc_poller.h>
#include <lib/nfc/protocols/iso15693_3/iso15693_3.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Iso15693Poller Iso15693Poller;

// block_number is a uint8_t on the wire, so 256 blocks is the protocol ceiling.
#define ISO15693_POLLER_MAX_BLOCKS        (256)
#define ISO15693_POLLER_BLOCK_BITMAP_SIZE ((ISO15693_POLLER_MAX_BLOCKS + 7) / 8)
_Static_assert(
    ISO15693_POLLER_MAX_BLOCKS <= ISO15693_POLLER_BLOCK_BITMAP_SIZE * 8,
    "ISO15693 block bitmap too small for max blocks");

typedef enum {
    Iso15693PollerModeInfo, // detect + read UID / system info
    Iso15693PollerModeWriteUid, // magic backdoor UID write (gen2 first, then gen1 if untouched)
    Iso15693PollerModeClone, // write UID + every data block from a source image
    Iso15693PollerModeWipe, // zero every data block (UID left unchanged)
} Iso15693PollerMode;

// Outcome of the block-write pass, as a single value rather than something the caller infers from
// counters. Mirrors the three-way split the 2.0 pollers use (gen2_poller.c: none failed -> Success,
// all failed -> Fail, otherwise Partial) and adds the two "nothing was attempted" cases, which is
// where a zero failure count would otherwise read as success.
typedef enum {
    Iso15693BlockPassNotRun, // mode writes no blocks (Info / Write UID)
    Iso15693BlockPassNoGeometry, // card or image reported no usable block geometry
    Iso15693BlockPassNothingWritten, // every attempted block was refused
    Iso15693BlockPassPartial, // some blocks written, some refused
    Iso15693BlockPassComplete, // every attempted block was accepted
} Iso15693BlockPass;

// Per-block result of a clone or wipe. Carries its own denominator, like
// Gen2PollerEventDataPartial and UscuidUlPollerEventDataPartial, so no consumer has to work out
// "did anything actually happen" from a failure count.
//
// Invariant, same as gen2's: Partial is reported only when 0 < failed_count < blocks_written +
// failed_count. The none-failed and nothing-written cases are Success and Fail respectively.
typedef struct {
    Iso15693BlockPass pass;
    uint16_t blocks_total; // blocks we attempted (source image for a clone, card for a wipe)
    uint16_t blocks_written; // blocks the card accepted
    uint16_t failed_count; // blocks refused that we needed to place (bit set in failed_bitmap)
    uint16_t over_capacity; // refused blocks in the empty tail past the card's real capacity
    uint8_t failed_bitmap[ISO15693_POLLER_BLOCK_BITMAP_SIZE]; // bit N set = block N refused
    bool used_gen1; // the gen1 fallback set the UID (so blocks 56/57/62/63 were overwritten)
    bool afi_failed; // the source reported an AFI and the card refused to take it
    bool dsfid_failed; // ditto for DSFID
} Iso15693PollerResult;

// Every consumer switches on this, so each value states exactly what it does and does not promise.
// Note Success means the UID read back as requested; block contents are never read back.
typedef enum {
    Iso15693PollerEventSuccess, // info read ok, or the UID read-back matched and every block took
    Iso15693PollerEventPartial, // UID ok, but some blocks were refused, or the gen1 fallback ran
    Iso15693PollerEventFail, // backdoor write refused (not magic), or no block could be written
    Iso15693PollerEventCardLost, // no card in the field / card removed before the operation finished
    Iso15693PollerEventCardDetected, // a magic candidate activated (drives the write popup UI)
    Iso15693PollerEventWriteProgress, // one more block attempted; read the result for the counts
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

// Magic UID write. `uid` is ISO15693_3_UID_SIZE bytes, MSB-first (uid[0] must be 0xE0).
// The poller writes the gen2 backdoor sequence first (a harmless custom command on a non-magic tag)
// and, only if the gen2 write left the card's UID unchanged, falls back to the destructive gen1
// WRITE-BLOCK sequence. Before each read-back it power-cycles the field (like proxmark's
// switch_off + getUID) so a card that only latches the new UID after a reset is not misreported as a
// failure. Reports Success only if a read-back inventory returns the requested UID.
// Byte-level frames: proxmark3 armsrc/iso15693.c, SetTag15693Uid / SetTag15693Uid_v2.
// Emits Success / Fail / CardLost (no block pass, so never Partial or CardDetected).
void iso15693_poller_start_write_uid(
    Iso15693Poller* instance,
    const uint8_t* uid,
    Iso15693PollerCallback callback,
    void* context);

// Full clone: write `source`'s UID (magic backdoor) and every data block (standard WRITE BLOCK) onto
// a magic card. `source` is an ISO15693-3 image loaded from a saved .nfc.
//
// The UID goes FIRST and the data blocks only follow once the read-back proves the card took it, so
// a tag that turns out not to be magic keeps its data. Emits CardDetected on the first activation
// and WriteProgress per block, then Success (UID matched, every block took), Partial (UID matched,
// some blocks refused, AFI/DSFID refused, or gen1 was used), Fail (UID refused, or no block could be
// written) or CardLost.
void iso15693_poller_start_clone(
    Iso15693Poller* instance,
    const Iso15693_3Data* source,
    Iso15693PollerCallback callback,
    void* context);

// The block-write result of the clone or wipe that just finished. Valid from the moment an event is
// delivered until the next start. See Iso15693PollerResult for what each field means and what the
// Partial invariant is.
const Iso15693PollerResult* iso15693_poller_get_result(Iso15693Poller* instance);

// True if the source stores real data in a gen1 backdoor block (56/57/62/63) that a gen1 fallback
// would overwrite -- so the write flow can warn before a possible gen1 clone. Source inspection only.
bool iso15693_poller_source_uses_gen1_blocks(const Iso15693_3Data* source);

// Wipe: write zeros to every data block on the card (like proxmark's 'hf 15 wipe'). The UID is left
// unchanged -- including on gen1, whose UID registers are ordinary data blocks: zeroing them cannot
// arm a UID change, which needs 0x6996 in the commit block.
// Emits CardDetected on the first activation, then Success / Partial (some blocks refused) / Fail
// (no block could be cleared, or the card reported no usable geometry) / CardLost.
void iso15693_poller_start_wipe(
    Iso15693Poller* instance,
    Iso15693PollerCallback callback,
    void* context);

void iso15693_poller_stop(Iso15693Poller* instance);

// The card read in Info mode. Owned by the poller: copy it out before the poller is freed.
const Iso15693_3Data* iso15693_poller_get_data(Iso15693Poller* instance);

#ifdef __cplusplus
}
#endif
