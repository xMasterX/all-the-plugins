#include "passy_common.h"

#define PASSY_WORKER_MAX_BUFFER_SIZE 128

#define TAG "PassyCommon"

static char asn1_log[PASSY_WORKER_MAX_BUFFER_SIZE];
int print_struct_callback(const void* buffer, size_t size, void* app_key) {
    if(app_key) {
        char* str = (char*)app_key;
        size_t next = strlen(str);
        strncpy(str + next, buffer, size);
    } else {
        uint8_t next = strlen(asn1_log);
        strncpy(asn1_log + next, buffer, size);
    }
    return 0;
}

void passy_log_bitbuffer(char* tag, char* prefix, BitBuffer* buffer) {
    furi_assert(buffer);

    size_t length = bit_buffer_get_size_bytes(buffer);
    const uint8_t* data = bit_buffer_get_data(buffer);

    char display[PASSY_WORKER_MAX_BUFFER_SIZE * 2 + 1];

    size_t limit = MIN((size_t)PASSY_WORKER_MAX_BUFFER_SIZE, length);
    memset(display, 0, sizeof(display));
    for(uint8_t i = 0; i < limit; i++) {
        snprintf(display + (i * 2), sizeof(display), "%02x", data[i]);
    }
    if(prefix) {
        FURI_LOG_D(tag, "%s %d: %s", prefix, length, display);
    } else {
        FURI_LOG_D(tag, "Buffer %d: %s", length, display);
    }
}

void passy_log_buffer(char* tag, char* prefix, uint8_t* buffer, size_t buffer_len) {
    char display[PASSY_WORKER_MAX_BUFFER_SIZE * 2 + 1];

    size_t limit = MIN((size_t)PASSY_WORKER_MAX_BUFFER_SIZE, buffer_len);
    memset(display, 0, sizeof(display));
    for(uint8_t i = 0; i < limit; i++) {
        snprintf(display + (i * 2), sizeof(display), "%02x", buffer[i]);
    }
    if(prefix) {
        FURI_LOG_D(tag, "%s %d: %s", prefix, limit, display);
    } else {
        FURI_LOG_D(tag, "Buffer %d: %s", limit, display);
    }
}

// ISO/IEC 9797-1 MAC Algorithm 3
void passy_mac(uint8_t* key, uint8_t* data, size_t data_length, uint8_t* mac, bool prepadded) {
    size_t block_count = data_length / 8;
    uint8_t y[8];
    memset(y, 0, sizeof(y));

    for(size_t i = 0; i < block_count; i++) {
        uint8_t* x = data + (i * 8);
        //passy_log_buffer(TAG, "x", x, 8);

        uint8_t iv[8];
        memcpy(iv, y, sizeof(y));
        mbedtls_des_context ctx;
        mbedtls_des_init(&ctx);
        mbedtls_des_setkey_enc(&ctx, key); // uses 8 bytes
        mbedtls_des_crypt_cbc(&ctx, MBEDTLS_DES_ENCRYPT, 8, iv, x, y);
        mbedtls_des_free(&ctx);
        //passy_log_buffer(TAG, "y", y, 8);
    }

    mbedtls_des_context ctx;

    if(!prepadded) {
        // last block
        uint8_t last_block[8] = {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        uint8_t iv[8];
        memcpy(iv, y, sizeof(y));
        mbedtls_des_init(&ctx);
        mbedtls_des_setkey_enc(&ctx, key); // uses 8 bytes
        mbedtls_des_crypt_cbc(&ctx, MBEDTLS_DES_ENCRYPT, 8, iv, last_block, y);
        //passy_log_buffer(TAG, "y", y, 8);
    }

    uint8_t b[8];
    mbedtls_des_init(&ctx);
    mbedtls_des_setkey_dec(&ctx, key + 8); // uses 8 bytes
    mbedtls_des_crypt_ecb(&ctx, y, b);
    //passy_log_buffer(TAG, "b", b, 8);

    mbedtls_des_init(&ctx);
    mbedtls_des_setkey_enc(&ctx, key); // uses 8 bytes
    mbedtls_des_crypt_ecb(&ctx, b, mac);
    //passy_log_buffer(TAG, "mac", mac, 8);

    mbedtls_des_free(&ctx);
}

// https://en.wikipedia.org/wiki/Machine-readable_passport
char passy_checksum(char* str) {
    uint8_t values[] = {
        0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12,
        13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
        // < = filler
        // A = 10
        // B = 11
        // C = 12
        // D = 13
        // E = 14
        // F = 15
        // G = 16
        // H = 17
        // I = 18
        // J = 19
        // K = 20
        // L = 21
        // M = 22
        // N = 23
        // O = 24
        // P = 25
        // Q = 26
        // R = 27
        // S = 28
        // T = 29
        // U = 30
        // V = 31
        // W = 32
        // X = 33
        // Y = 34
        // Z = 35
    };

    size_t sum = 0;
    uint8_t weight_map[] = {7, 3, 1};
    for(uint8_t i = 0; i < strlen(str); i++) {
        uint8_t value;
        uint8_t weight = weight_map[(i % 3)];
        if(str[i] >= '0' && str[i] <= '9') {
            value = values[str[i] - '0'];
        } else if(str[i] >= 'A' && str[i] <= 'Z') {
            value = values[str[i] - 'A' + 10];
        } else if(str[i] == '<') {
            value = 0;
        } else {
            FURI_LOG_E(TAG, "Invalid character %02x", str[i]);
            return -1;
        }
        sum += value * weight;
    }
    return 0x30 + (sum % 10);
}
