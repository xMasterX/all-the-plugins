/**
 * @file t5577_mw_tag.h
 * @brief Talking to the tag: finding it, and reading its key slots back.
 */
#pragma once

#include "t5577_multiwriter_i.h"

/** How a tag answered a detect. */
typedef enum {
    T5577MwTagOpen, // answered with no password, so it wants none
    T5577MwTagUnlocked, // answered only once addressed with the session password
    T5577MwTagSilent, // nothing came back
} T5577MwTagAnswer;

/** Whether a detect may fall back to a password frame, and on what authority.
 *
 * A password frame is the same length as a write frame, so a tag that turns out to want no
 * password takes the word into block 0 instead of answering with it. Every caller therefore
 * says what it is entitled to send, and the default is nothing.
 *
 * Belt and braces, deliberately. The module already makes this safe on its own: the plain
 * detect runs first every time, and answering it clears `password_confirmed`, so `Confirmed`
 * at a site whose tag answered plainly already behaves as `Plain`. These modes exist so that a
 * caller states its intent where a reader can see it, and so the guarantee survives a change
 * to how the flag is kept. Do not collapse them because they currently coincide.
 */
typedef enum {
    /** Never send one. For a tag already found open, where a password could only do harm. */
    T5577MwDetectPlain,
    /** Send only a word this tag's own answer has already asked for. */
    T5577MwDetectConfirmed,
    /** Send a word the user typed and has been warned about. The warning is the caller's. */
    T5577MwDetectUnconfirmed,
} T5577MwDetectMode;

/** Find the tag and work out how it reads.
 *
 * A plain detect always goes first, whatever the mode, because it is the one that cannot damage
 * anything: a tag that answers it wants no password, whatever the app happens to be holding.
 * Only when that says nothing does `mode` decide whether a password frame is sent at all.
 *
 * On success the session password's confirmed flag is set from the tag's own answer, in both
 * directions. A tag that answers without a password clears it, so a single locked tag cannot
 * authorise password frames at every tag put on the pad after it.
 *
 * Everything else about the tag - whether it is locked, how many keys it broadcasts, whether
 * its configuration is one this app writes - is read straight off app->config by the accessors
 * below, so there is nothing here that can go stale.
 *
 * @param      app   the app, whose config this fills in
 * @param      mode  what this caller is entitled to send
 */
T5577MwTagAnswer t5577_mw_tag_detect(LfRfid* app, T5577MwDetectMode mode);

/** Whether block 0 will not answer, or take a write, without a password.
 *
 * Both the password bit and answer-on-request, because either means a frame has to carry the
 * word. A failed detect zeroes the config, and neither bit is set in a zero word, so this
 * answers false for a tag that was never found.
 */
static inline bool t5577_mw_tag_locked(const LfRfid* app) {
    return t5577_config_needs_password(app->config.block0);
}

/** Keys the tag broadcasts, from block 0's maxblock. */
static inline uint8_t t5577_mw_tag_keys(const LfRfid* app) {
    return t5577_config_max_block(app->config.block0) / EM41XX_BLOCKS;
}

/** Whether block 0 is a word this app did not write, so the slots are somebody else's data.
 *
 * Asked by composing the word this app would write for that key count and comparing - the same
 * function the writer uses, so the two cannot drift apart.
 */
static inline bool t5577_mw_tag_foreign(const LfRfid* app) {
    return app->config.block0 != em41xx_config_for(app->config.block0, t5577_mw_tag_keys(app));
}

/** Read a key slot back and decode it.
 *
 * Fills in app->slot_key and app->slot_state for the slot. The return value and the state are
 * different questions: the return says the tag answered, the state says what it answered with.
 * A slot that was never written, or that holds what a longer tag left behind, answers perfectly
 * well and holds no key.
 *
 * @return     true when the slot was read
 */
bool t5577_mw_tag_read_slot(LfRfid* app, uint8_t slot);

/** Read one block back and compare it against what was written.
 *
 * @param[out] actual  what the tag holds, when it answered; untouched otherwise
 * @return     T5577BlockHeld, T5577BlockMismatch or T5577BlockNoAnswer
 */
T5577BlockResult
    t5577_mw_tag_check_block(LfRfid* app, uint8_t block, uint32_t expected, uint32_t* actual);
