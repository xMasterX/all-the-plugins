#include "EM41XX.h"

#include <furi.h>
#include <bit_lib/bit_lib.h>

#define EM41XX_LINES   (10)
#define EM41XX_COLUMNS (4)

#define EM41XX_CONFIG_BLANK (LFRFID_T5577_MODULATION_MANCHESTER | LFRFID_T5577_BITRATE_RF_64)

#define EM41XX_HEADER      (0b111111111)
#define EM41XX_HEADER_BITS (9)

// Bits one encoded key occupies, which is also the two blocks it is stored in.
#define EM41XX_FRAME_BITS (EM41XX_BLOCKS * 32)

// Bytes an EM4100 key occupies.
#define EM41XX_KEY_BYTES (5)

static bool get_parity(uint16_t data) {
    return bit_lib_get_bit_count(data) & 1u;
}

static bool get_line_parity_bit(uint8_t line_num, uint64_t data) {
    uint8_t line = (data >> (EM41XX_COLUMNS * line_num)) & 0x0F;
    return get_parity(line);
}

static bool get_column_parity_bit(uint8_t column_num, uint64_t data) {
    uint16_t column = 0;

    for(int i = 0; i < EM41XX_LINES; i++) {
        column <<= 1;
        column |= (data >> (EM41XX_COLUMNS * i + column_num)) & 1;
    }

    return get_parity(column);
}

static uint64_t em41xx_encode(uint64_t data) {
    uint64_t result = EM41XX_HEADER;

    for(int i = EM41XX_LINES - 1; i >= 0; i--) {
        result <<= EM41XX_COLUMNS;
        uint8_t line = (data >> (i * EM41XX_COLUMNS)) & 0x0F;
        result |= line;

        result <<= 1;
        result |= get_line_parity_bit(i, data);
    }

    for(int i = EM41XX_COLUMNS - 1; i >= 0; i--) {
        result <<= 1;
        result |= get_column_parity_bit(i, data);
    }

    result <<= 1;

    return result;
}

bool em41xx_first_block_plausible(uint32_t high) {
    // The header is frame bits 63..55, which is the first block's bits 31..23.
    return (high >> (32 - EM41XX_HEADER_BITS)) == EM41XX_HEADER;
}

bool em41xx_decode(uint64_t frame, uint64_t* key) {
    if(!em41xx_first_block_plausible((uint32_t)(frame >> 32))) return false;
    // The stop bit, which the encoder shifts in as the final zero.
    if(frame & 1) return false;

    // Each line was shifted in as four data bits then its parity, the most significant line
    // first, so line `i` sits `(EM41XX_LINES - 1 - i)` groups of five down from the header.
    uint64_t data = 0;
    for(int i = EM41XX_LINES - 1; i >= 0; i--) {
        // Five bits per line, counted up from the stop bit and the four column parities.
        const uint8_t parity_shift = 5 * (i + 1);
        const uint8_t line = (frame >> (parity_shift + 1)) & 0x0F;

        data |= (uint64_t)line << (i * EM41XX_COLUMNS);

        if((bool)((frame >> parity_shift) & 1) != get_line_parity_bit(i, data)) return false;
    }

    // Column parities were shifted in after the lines, the highest column first, which leaves
    // column `c` one place above the stop bit plus `c`.
    for(int c = 0; c < EM41XX_COLUMNS; c++) {
        if((bool)((frame >> (c + 1)) & 1) != get_column_parity_bit(c, data)) return false;
    }

    *key = data;
    return true;
}

void em41xx_key_blocks(uint64_t key, uint32_t* high, uint32_t* low) {
    furi_assert(high);
    furi_assert(low);

    const uint64_t frame = em41xx_encode(key);

    *high = (uint32_t)(frame >> 32);
    *low = (uint32_t)(frame & 0xFFFFFFFF);
}

void em41xx_key_format(uint64_t key, char* out, size_t size) {
    furi_assert(out);
    furi_check(size >= EM41XX_KEY_STR_SIZE);

    uint8_t bytes[EM41XX_KEY_BYTES];
    bit_lib_num_to_bytes_be(key, sizeof(bytes), bytes);

    snprintf(
        out, size, "%02X %02X %02X %02X %02X", bytes[0], bytes[1], bytes[2], bytes[3], bytes[4]);
}

bool add_em41xx_data(LFRFIDT5577* data, uint64_t key, uint8_t from_index) {
    if(from_index + EM41XX_BLOCKS > (LFRFID_T5577_BLOCK_COUNT - 1)) return false;

    em41xx_key_blocks(key, &data->block[from_index], &data->block[from_index + 1]);
    data->blocks_to_write = LFRFID_T5577_BLOCK_COUNT;

    uint8_t mask_addition = (1 << from_index);
    mask_addition |= (1 << (from_index + 1));

    data->mask |= mask_addition;

    return true;
}

uint32_t em41xx_config_for(uint32_t old_block0, uint8_t keys_count) {
    if(keys_count > EM41XX_MAX_KEYS) return 0;

    const uint32_t config = EM41XX_CONFIG_BLANK | ((uint32_t)(keys_count * EM41XX_BLOCKS)
                                                   << LFRFID_T5577_MAXBLOCK_SHIFT);

    return config | (old_block0 & EM41XX_CONFIG_TAG_BITS);
}

bool set_em41xx_config(LFRFIDT5577* data, uint8_t keys_count, uint32_t old_block0) {
    if(keys_count > EM41XX_MAX_KEYS) return false;

    data->block[0] = em41xx_config_for(old_block0, keys_count);
    data->mask |= 1;

    return true;
}
