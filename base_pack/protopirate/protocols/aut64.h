#pragma once

#include <stdint.h>

#define AUT64_NUM_ROUNDS 12
#define AUT64_BLOCK_SIZE 8
#define AUT64_KEY_SIZE   8
#define AUT64_PBOX_SIZE  8
#define AUT64_SBOX_SIZE  16

struct aut64_key {
    uint8_t index;
    uint8_t key[AUT64_KEY_SIZE];
    uint8_t pbox[AUT64_PBOX_SIZE];
    uint8_t sbox[AUT64_SBOX_SIZE];
};

#define AUT64_KEY_STRUCT_PACKED_SIZE 16

void aut64_encrypt(const struct aut64_key key, uint8_t message[]);
void aut64_decrypt(const struct aut64_key key, uint8_t message[]);
void aut64_pack(uint8_t dest[], const struct aut64_key src);
void aut64_unpack(struct aut64_key* dest, const uint8_t src[]);
