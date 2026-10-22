/**
 * @file t5577_mw_listener.h
 * @brief An EM4100 reader that counts every frame, rather than waiting for agreement.
 *
 * The LF-RFID worker cannot be used for this. It only reports a read once the same protocol
 * has decoded the *same data* `validate_count` times in a row after the first - three, for
 * EM4100, so four identical frames - and resets
 * that counter the moment a frame differs (lfrfid_worker_modes.c, "validate protocol"). A tag
 * holding three keys sends key 1, key 2, key 3, key 1, so no two consecutive frames ever agree
 * and the worker never reports a read at all. The tag the worker cannot read is precisely the
 * tag this app exists to make.
 *
 * That is worth knowing in itself: a reader strict enough to demand agreement rejects these
 * tags outright, which is the mechanism behind "not every reader will accept that". A lenient
 * reader takes the first frame it decodes, and this listens the way a lenient reader reads -
 * raw pulses into the EM4100 decoder, every frame it yields counted as it arrives.
 */
#pragma once

#include <toolbox/protocols/protocol_dict.h>

#include "protocols/EM41XX.h"

/** Distinct keys the tally holds. Anything past this is counted as one lump.
 *
 * The same as the number this app can write, deliberately. A fifth slot would buy one more key
 * name at the price of a row the screen has no room for - the crowded notice shares that line -
 * and would let a single corrupt frame raise the divisor below, which is what decides when a
 * run has collected whole cycles. Capped here, a stray decode lands in `crowded` instead, where
 * it is visible and harmless.
 */
#define T5577_MW_TALLY_KEYS EM41XX_MAX_KEYS

typedef struct T5577MwListener T5577MwListener;

/** A snapshot of a run: what has been heard, and how the run itself is doing. */
typedef struct {
    uint64_t key[T5577_MW_TALLY_KEYS];
    uint16_t count[T5577_MW_TALLY_KEYS];
    uint8_t keys;
    uint16_t crowded; // frames whose key the tally had no room for
    uint16_t reads; // frames decoded in total

    // And what the run is doing, rather than what it heard.
    uint32_t dropped; // capture periods the ring had no room for

    /** Ticks since the last frame decoded, or since the run started if none has.
     *
     * Ticks rather than milliseconds because the SDK converts only the other way; compare it
     * against furi_ms_to_ticks(your threshold).
     *
     * The one thing a frame count cannot say: a run that has stopped collecting looks exactly
     * like one that is still going, and with the end condition below needing a *particular*
     * further frame, "some frames arrived" no longer means "this run will finish".
     */
    uint32_t quiet_ticks;
} T5577MwTally;

/** @param dict  the app's protocol dictionary; used only while listening */
T5577MwListener* t5577_mw_listener_alloc(ProtocolDict* dict);

void t5577_mw_listener_free(T5577MwListener* instance);

/** Raise the field and start counting. Clears the tally.
 *
 * Stops itself once it has heard `min_frames` frames AND a whole number of cycles - that is,
 * once the total divides by the number of distinct keys the tally has room for. The tag sends
 * its keys in a loop, so only a multiple of the key count gives every key the same number of
 * turns; stopping anywhere else makes whichever keys fell in the part-cycle look stronger than
 * the rest. Past T5577_MW_TALLY_KEYS the divisor saturates and that guarantee is only
 * approximate - `crowded` says when, and the per-key counts show it.
 *
 * It is the listener that stops rather than the caller because the caller cannot see the
 * individual frames. A three key tag sends about thirty a second, so a screen polling every
 * 150ms sees them four or five at a time and sails past the exact frame to stop on.
 */
void t5577_mw_listener_start(T5577MwListener* instance, uint16_t min_frames);

/** Drop the field and stop. Safe to call when not running; the tally is kept. */
void t5577_mw_listener_stop(T5577MwListener* instance);

/** Copy out what has been heard. Safe while the listener is running. */
void t5577_mw_listener_tally(T5577MwListener* instance, T5577MwTally* tally);

/** Frames a run will end on, given how many distinct keys it has found.
 *
 * The same rule the listener stops on, in the form a screen needs: a count to count towards
 * rather than a test to apply. Shared so the number shown and the number stopped on cannot
 * drift apart - the listener ends a run at the first multiple of `keys` at or past
 * `min_frames`, which is exactly this.
 *
 * Takes the key count rather than the frame count on purpose: a target that moved with the
 * run would be a denominator chasing its numerator.
 */
static inline uint16_t t5577_mw_listener_target(uint16_t min_frames, uint8_t keys) {
    if(keys == 0) return min_frames;

    return ((min_frames + keys - 1) / keys) * keys;
}

/** Whether the run has reached its target and stopped counting.
 *
 * Once this is true the tally no longer moves, so what it holds is what the run collected.
 */
bool t5577_mw_listener_complete(const T5577MwListener* instance);
