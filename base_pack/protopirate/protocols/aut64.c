#include <string.h>
#include "aut64.h"

// https://www.usenix.org/system/files/conference/usenixsecurity16/sec16_paper_garcia.pdf

/*
 * AUT64 algorithm, 12 rounds
 * 8 bytes block size, 8 bytes key size
 * 8 bytes pbox size, 16 bytes sbox size
 * 
 * Based on: Reference AUT64 implementation in JavaScript (aut64.js)
 * Vencislav Atanasov, 2025-09-13
 * 
 * Based on: Reference AUT64 implementation in python
 * C Hicks, hkscy.org, 03-01-19
 */

static const uint8_t table_ln[AUT64_NUM_ROUNDS][8] = {
    {
        0x4,
        0x5,
        0x6,
        0x7,
        0x0,
        0x1,
        0x2,
        0x3,
    }, // Round 0
    {
        0x5,
        0x4,
        0x7,
        0x6,
        0x1,
        0x0,
        0x3,
        0x2,
    }, // Round 1
    {
        0x6,
        0x7,
        0x4,
        0x5,
        0x2,
        0x3,
        0x0,
        0x1,
    }, // Round 2
    {
        0x7,
        0x6,
        0x5,
        0x4,
        0x3,
        0x2,
        0x1,
        0x0,
    }, // Round 3
    {
        0x0,
        0x1,
        0x2,
        0x3,
        0x4,
        0x5,
        0x6,
        0x7,
    }, // Round 4
    {
        0x1,
        0x0,
        0x3,
        0x2,
        0x5,
        0x4,
        0x7,
        0x6,
    }, // Round 5
    {
        0x2,
        0x3,
        0x0,
        0x1,
        0x6,
        0x7,
        0x4,
        0x5,
    }, // Round 6
    {
        0x3,
        0x2,
        0x1,
        0x0,
        0x7,
        0x6,
        0x5,
        0x4,
    }, // Round 7
    {
        0x5,
        0x4,
        0x7,
        0x6,
        0x1,
        0x0,
        0x3,
        0x2,
    }, // Round 8
    {
        0x4,
        0x5,
        0x6,
        0x7,
        0x0,
        0x1,
        0x2,
        0x3,
    }, // Round 9
    {
        0x7,
        0x6,
        0x5,
        0x4,
        0x3,
        0x2,
        0x1,
        0x0,
    }, // Round 10
    {
        0x6,
        0x7,
        0x4,
        0x5,
        0x2,
        0x3,
        0x0,
        0x1,
    }, // Round 11
};

static const uint8_t table_un[AUT64_NUM_ROUNDS][8] = {
    {
        0x1,
        0x0,
        0x3,
        0x2,
        0x5,
        0x4,
        0x7,
        0x6,
    }, // Round 0
    {
        0x0,
        0x1,
        0x2,
        0x3,
        0x4,
        0x5,
        0x6,
        0x7,
    }, // Round 1
    {
        0x3,
        0x2,
        0x1,
        0x0,
        0x7,
        0x6,
        0x5,
        0x4,
    }, // Round 2
    {
        0x2,
        0x3,
        0x0,
        0x1,
        0x6,
        0x7,
        0x4,
        0x5,
    }, // Round 3
    {
        0x5,
        0x4,
        0x7,
        0x6,
        0x1,
        0x0,
        0x3,
        0x2,
    }, // Round 4
    {
        0x4,
        0x5,
        0x6,
        0x7,
        0x0,
        0x1,
        0x2,
        0x3,
    }, // Round 5
    {
        0x7,
        0x6,
        0x5,
        0x4,
        0x3,
        0x2,
        0x1,
        0x0,
    }, // Round 6
    {
        0x6,
        0x7,
        0x4,
        0x5,
        0x2,
        0x3,
        0x0,
        0x1,
    }, // Round 7
    {
        0x3,
        0x2,
        0x1,
        0x0,
        0x7,
        0x6,
        0x5,
        0x4,
    }, // Round 8
    {
        0x2,
        0x3,
        0x0,
        0x1,
        0x6,
        0x7,
        0x4,
        0x5,
    }, // Round 9
    {
        0x1,
        0x0,
        0x3,
        0x2,
        0x5,
        0x4,
        0x7,
        0x6,
    }, // Round 10
    {
        0x0,
        0x1,
        0x2,
        0x3,
        0x4,
        0x5,
        0x6,
        0x7,
    }, // Round 11
};

static const uint8_t table_offset[256] = {
    // 0    1    2    3    4    5    6    7    8    9    A    B    C    D    E    F
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // 0
    0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF, // 1
    0x0, 0x2, 0x4, 0x6, 0x8, 0xA, 0xC, 0xE, 0x3, 0x1, 0x7, 0x5, 0xB, 0x9, 0xF, 0xD, // 2
    0x0, 0x3, 0x6, 0x5, 0xC, 0xF, 0xA, 0x9, 0xB, 0x8, 0xD, 0xE, 0x7, 0x4, 0x1, 0x2, // 3
    0x0, 0x4, 0x8, 0xC, 0x3, 0x7, 0xB, 0xF, 0x6, 0x2, 0xE, 0xA, 0x5, 0x1, 0xD, 0x9, // 4
    0x0, 0x5, 0xA, 0xF, 0x7, 0x2, 0xD, 0x8, 0xE, 0xB, 0x4, 0x1, 0x9, 0xC, 0x3, 0x6, // 5
    0x0, 0x6, 0xC, 0xA, 0xB, 0xD, 0x7, 0x1, 0x5, 0x3, 0x9, 0xF, 0xE, 0x8, 0x2, 0x4, // 6
    0x0, 0x7, 0xE, 0x9, 0xF, 0x8, 0x1, 0x6, 0xD, 0xA, 0x3, 0x4, 0x2, 0x5, 0xC, 0xB, // 7
    0x0, 0x8, 0x3, 0xB, 0x6, 0xE, 0x5, 0xD, 0xC, 0x4, 0xF, 0x7, 0xA, 0x2, 0x9, 0x1, // 8
    0x0, 0x9, 0x1, 0x8, 0x2, 0xB, 0x3, 0xA, 0x4, 0xD, 0x5, 0xC, 0x6, 0xF, 0x7, 0xE, // 9
    0x0, 0xA, 0x7, 0xD, 0xE, 0x4, 0x9, 0x3, 0xF, 0x5, 0x8, 0x2, 0x1, 0xB, 0x6, 0xC, // A
    0x0, 0xB, 0x5, 0xE, 0xA, 0x1, 0xF, 0x4, 0x7, 0xC, 0x2, 0x9, 0xD, 0x6, 0x8, 0x3, // B
    0x0, 0xC, 0xB, 0x7, 0x5, 0x9, 0xE, 0x2, 0xA, 0x6, 0x1, 0xD, 0xF, 0x3, 0x4, 0x8, // C
    0x0, 0xD, 0x9, 0x4, 0x1, 0xC, 0x8, 0x5, 0x2, 0xF, 0xB, 0x6, 0x3, 0xE, 0xA, 0x7, // D
    0x0, 0xE, 0xF, 0x1, 0xD, 0x3, 0x2, 0xC, 0x9, 0x7, 0x6, 0x8, 0x4, 0xA, 0xB, 0x5, // E
    0x0, 0xF, 0xD, 0x2, 0x9, 0x6, 0x4, 0xB, 0x1, 0xE, 0xC, 0x3, 0x8, 0x7, 0x5, 0xA // F
};

static const uint8_t table_sub[16] = {
    0x0,
    0x1,
    0x9,
    0xE,
    0xD,
    0xB,
    0x7,
    0x6,
    0xF,
    0x2,
    0xC,
    0x5,
    0xA,
    0x4,
    0x3,
    0x8,
};

static uint8_t key_nibble(
    const struct aut64_key key,
    const uint8_t nibble,
    const uint8_t table[],
    const uint8_t iteration) {
    const uint8_t keyValue = key.key[table[iteration]];
    const uint8_t offset = (keyValue << 4) | nibble;
    return table_offset[offset];
}

static uint8_t round_key(const struct aut64_key key, const uint8_t state[], const uint8_t roundN) {
    uint8_t result_hi = 0, result_lo = 0;

    for(int i = 0; i < AUT64_BLOCK_SIZE - 1; i++) {
        result_hi ^= key_nibble(key, state[i] >> 4, table_un[roundN], i);
        result_lo ^= key_nibble(key, state[i] & 0x0F, table_ln[roundN], i);
    }

    return (result_hi << 4) | result_lo;
}

static uint8_t final_byte_nibble(const struct aut64_key key, const uint8_t table[]) {
    const uint8_t keyValue = key.key[table[AUT64_BLOCK_SIZE - 1]];
    return table_sub[keyValue] << 4;
}

static uint8_t encrypt_final_byte_nibble(
    const struct aut64_key key,
    const uint8_t nibble,
    const uint8_t table[]) {
    const uint8_t offset = final_byte_nibble(key, table);

    int i;

    for(i = 0; i < 16; i++) {
        if(table_offset[offset + i] == nibble) {
            break;
        }
    }

    return i;
}

static uint8_t
    encrypt_compress(const struct aut64_key key, const uint8_t state[], const uint8_t roundN) {
    const uint8_t roundKey = round_key(key, state, roundN);
    uint8_t result_hi = roundKey >> 4, result_lo = roundKey & 0x0F;

    result_hi ^=
        encrypt_final_byte_nibble(key, state[AUT64_BLOCK_SIZE - 1] >> 4, table_un[roundN]);
    result_lo ^=
        encrypt_final_byte_nibble(key, state[AUT64_BLOCK_SIZE - 1] & 0x0F, table_ln[roundN]);

    return (result_hi << 4) | result_lo;
}

static uint8_t decrypt_final_byte_nibble(
    const struct aut64_key key,
    const uint8_t nibble,
    const uint8_t table[],
    const uint8_t result) {
    const uint8_t offset = final_byte_nibble(key, table);

    return table_offset[(result ^ nibble) + offset];
}

static uint8_t
    decrypt_compress(const struct aut64_key key, const uint8_t state[], const uint8_t roundN) {
    const uint8_t roundKey = round_key(key, state, roundN);
    uint8_t result_hi = roundKey >> 4, result_lo = roundKey & 0x0F;

    result_hi = decrypt_final_byte_nibble(
        key, state[AUT64_BLOCK_SIZE - 1] >> 4, table_un[roundN], result_hi);
    result_lo = decrypt_final_byte_nibble(
        key, state[AUT64_BLOCK_SIZE - 1] & 0x0F, table_ln[roundN], result_lo);

    return (result_hi << 4) | result_lo;
}

static uint8_t substitute(const struct aut64_key key, const uint8_t byte) {
    return (key.sbox[byte >> 4] << 4) | key.sbox[byte & 0x0F];
}

static void permute_bytes(const struct aut64_key key, uint8_t state[]) {
    uint8_t result[AUT64_PBOX_SIZE] = {0};

    for(int i = 0; i < AUT64_PBOX_SIZE; i++) {
        result[key.pbox[i]] = state[i];
    }

    memcpy(state, result, AUT64_PBOX_SIZE);
}

static uint8_t permute_bits(const struct aut64_key key, const uint8_t byte) {
    uint8_t result = 0;

    for(int i = 0; i < 8; i++) {
        if(byte & (1 << i)) {
            result |= (1 << key.pbox[i]);
        }
    }

    return result;
}

static void reverse_box(uint8_t* reversed, const uint8_t* box, const size_t len) {
    for(size_t i = 0; i < len; i++) {
        for(size_t j = 0; j < len; j++) {
            if(box[j] == i) {
                reversed[i] = j;
                break;
            }
        }
    }
}

void aut64_encrypt(const struct aut64_key key, uint8_t message[]) {
    struct aut64_key reverse_key;
    memcpy(reverse_key.key, key.key, AUT64_KEY_SIZE);
    reverse_box(reverse_key.pbox, key.pbox, AUT64_PBOX_SIZE);
    reverse_box(reverse_key.sbox, key.sbox, AUT64_SBOX_SIZE);

    for(int i = 0; i < AUT64_NUM_ROUNDS; i++) {
        permute_bytes(reverse_key, message);
        message[7] = encrypt_compress(reverse_key, message, i);
        message[7] = substitute(reverse_key, message[7]);
        message[7] = permute_bits(reverse_key, message[7]);
        message[7] = substitute(reverse_key, message[7]);
    }
}

void aut64_decrypt(const struct aut64_key key, uint8_t message[]) {
    for(int i = AUT64_NUM_ROUNDS - 1; i >= 0; i--) {
        message[7] = substitute(key, message[7]);
        message[7] = permute_bits(key, message[7]);
        message[7] = substitute(key, message[7]);
        message[7] = decrypt_compress(key, message, i);
        permute_bytes(key, message);
    }
}

void aut64_pack(uint8_t dest[], const struct aut64_key src) {
    dest[0] = src.index;

    for(uint8_t i = 0; i < sizeof(src.key) / 2; i++) {
        dest[i + 1] = (src.key[i * 2] << 4) | src.key[i * 2 + 1];
    }

    uint32_t pbox = 0;

    for(uint8_t i = 0; i < sizeof(src.pbox); i++) {
        pbox = (pbox << 3) | src.pbox[i];
    }

    dest[5] = pbox >> 16;
    dest[6] = (pbox >> 8) & 0xFF;
    dest[7] = pbox & 0xFF;

    for(uint8_t i = 0; i < sizeof(src.sbox) / 2; i++) {
        dest[i + 8] = (src.sbox[i * 2] << 4) | src.sbox[i * 2 + 1];
    }
}

void aut64_unpack(struct aut64_key* dest, const uint8_t src[]) {
    dest->index = src[0];

    for(uint8_t i = 0; i < sizeof(dest->key) / 2; i++) {
        dest->key[i * 2] = src[i + 1] >> 4;
        dest->key[i * 2 + 1] = src[i + 1] & 0xF;
    }

    uint32_t pbox = (src[5] << 16) | (src[6] << 8) | src[7];

    for(int8_t i = sizeof(dest->pbox) - 1; i >= 0; i--) {
        dest->pbox[i] = pbox & 0x7;
        pbox >>= 3;
    }

    for(uint8_t i = 0; i < sizeof(dest->sbox) / 2; i++) {
        dest->sbox[i * 2] = src[i + 8] >> 4;
        dest->sbox[i * 2 + 1] = src[i + 8] & 0xF;
    }
}
