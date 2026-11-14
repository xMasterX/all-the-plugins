#pragma once

#include <lfrfid/tools/t5577.h>

/** Blocks one EM4100 key occupies. */
#define EM41XX_BLOCKS (2)

/** Keys this app fits on a tag.
 *
 * Block 0 is the configuration and block 7 is the password, so the six blocks between them
 * hold three keys - which is the whole premise of the app.
 */
#define EM41XX_MAX_KEYS (3)

/** Characters em41xx_key_format() needs, terminator included. */
#define EM41XX_KEY_STR_SIZE (sizeof("AA BB CC DD EE"))

/** The bits of block 0 that belong to the tag rather than to this app.
 *
 * Protection is the tag's own state: rewriting the key count must carry it forward untouched,
 * and comparing a tag's word against one of ours must ignore it. Both happen through
 * em41xx_config_for(), so this mask has one home.
 */
#define EM41XX_CONFIG_TAG_BITS ((uint32_t)(LFRFID_T5577_PWD | LFRFID_T5577_AOR))

/** First block of a key slot. Slots are numbered from zero and sit at blocks 1, 3 and 5. */
static inline uint8_t em41xx_slot_block(uint8_t slot) {
    return 1 + slot * EM41XX_BLOCKS;
}

bool add_em41xx_data(LFRFIDT5577* data, uint64_t key, uint8_t from_index);

/** Put the configuration for `keys_count` keys into block 0 of `data`, and mask it in.
 *
 * @param      old_block0  block 0 as the tag holds it now, whose protection bits carry over
 */
bool set_em41xx_config(LFRFIDT5577* data, uint8_t keys_count, uint32_t old_block0);

/** The block 0 word this app writes for a tag broadcasting `keys_count` keys.
 *
 * ASK/Manchester at RF/64, maxblock set to the last block in use, and whatever protection bits
 * `old_block0` already carried. One function for both directions: composing the word to write,
 * and asking whether a word a tag holds is one of ours. Were those written separately they
 * could drift, and a tag this app wrote would start reading back as somebody else's.
 */
uint32_t em41xx_config_for(uint32_t old_block0, uint8_t keys_count);

/** Whether a slot's first block could begin an EM4100 frame.
 *
 * The nine header bits live entirely in the first block, so a block that does not carry them
 * cannot decode however the second block reads - which saves addressing a block, and a block
 * read costs about a second and a half.
 */
bool em41xx_first_block_plausible(uint32_t high);

/** The two block words a slot holds for `key`, first block first.
 *
 * The same split add_em41xx_data() writes, exposed because reading a write back has to compare
 * against the words that were actually sent rather than re-deriving them.
 */
void em41xx_key_blocks(uint64_t key, uint32_t* high, uint32_t* low);

/** Recover the key from an encoded EM4100 frame.
 *
 * The inverse of the encoder, and the test for whether a slot holds a key at all: a block pair
 * that was never written, or was written by something else, fails the header, the row and
 * column parities or the stop bit. Leftover data from a shorter tag is the case that matters -
 * blocks 3 to 6 keep their contents when block 0 is rewritten for fewer keys.
 *
 * @param      frame  block pair as the tag holds it, first block in the high 32 bits
 * @param[out] key    the 40 bit key, untouched unless the frame checks out
 * @return     true when every parity agreed
 */
bool em41xx_decode(uint64_t frame, uint64_t* key);

/** Write a key as "AA BB CC DD EE" into `out`, which needs EM41XX_KEY_STR_SIZE bytes.
 *
 * Shared so that the check screen and the test screen cannot disagree about byte order - the
 * user compares one against the other, which only works if they are formatted in one place.
 */
void em41xx_key_format(uint64_t key, char* out, size_t size);
