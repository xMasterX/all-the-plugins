// Methods for Picopass (iClass) emulation
//
// Picopass cards are emulated by acting as an ISO15693 NFC listener and
// answering the reader's commands from the saved card data.

#include <furi.h>
#include <furi_hal.h>

#include <storage/storage.h>

#include <nfc/nfc.h>
#include <nfc/helpers/iso13239_crc.h>
#include <flipper_format/flipper_format.h>

#include "action_i.h"
#include "quac.h"

#include <optimized_cipher.h>

// Picopass constants
#define PICOPASS_BLOCK_LEN                 8
#define PICOPASS_MAX_APP_LIMIT             32
#define PICOPASS_CSN_BLOCK_INDEX           0
#define PICOPASS_CONFIG_BLOCK_INDEX        1
#define PICOPASS_SECURE_KD_BLOCK_INDEX     3
#define PICOPASS_SECURE_KC_BLOCK_INDEX     4
#define PICOPASS_SECURE_EPURSE_BLOCK_INDEX 2
#define PICOPASS_MAC_LEN                   4
#define PICOPASS_FUSE_CRYPT1               0x10
#define PICOPASS_FUSE_CRYPT0               0x08
#define PICOPASS_FUSE_CRYPT10              (PICOPASS_FUSE_CRYPT1 | PICOPASS_FUSE_CRYPT0)
#define PICOPASS_FDT_LISTEN_FC             1000
#define PICOPASS_LISTENER_BUFFER_SIZE_MAX  255

// Picopass commands
#define PICOPASS_CMD_ACTALL           0x0A
#define PICOPASS_CMD_ACT              0x8E
#define PICOPASS_CMD_HALT             0x00
#define PICOPASS_CMD_READ_OR_IDENTIFY 0x0C
#define PICOPASS_CMD_SELECT           0x81
#define PICOPASS_CMD_READCHECK_KD     0x88
#define PICOPASS_CMD_READCHECK_KC     0x18
#define PICOPASS_CMD_CHECK            0x05
#define PICOPASS_CMD_READ4            0x06

typedef struct {
    uint8_t data[PICOPASS_BLOCK_LEN];
    bool valid;
} PicopassBlock;

typedef struct {
    PicopassBlock card_data[PICOPASS_MAX_APP_LIMIT];
} PicopassData;

typedef enum {
    PicopassListenerStateIdle,
    PicopassListenerStateHalt,
    PicopassListenerStateActive,
    PicopassListenerStateSelected,
} PicopassListenerState;

typedef struct {
    Nfc* nfc;
    PicopassData* data;
    PicopassListenerState state;
    BitBuffer* tx_buffer;
    BitBuffer* tmp_buffer;
    uint8_t key_block_num;
    LoclassState_t cipher_state;
} PicopassEmulator;

// Helper to convert hex char to nibble
static bool hex_char_to_uint8(char hi, char lo, uint8_t* out) {
    uint8_t result = 0;

    if(hi >= '0' && hi <= '9')
        result = (hi - '0') << 4;
    else if(hi >= 'A' && hi <= 'F')
        result = (hi - 'A' + 10) << 4;
    else if(hi >= 'a' && hi <= 'f')
        result = (hi - 'a' + 10) << 4;
    else
        return false;

    if(lo >= '0' && lo <= '9')
        result |= (lo - '0');
    else if(lo >= 'A' && lo <= 'F')
        result |= (lo - 'A' + 10);
    else if(lo >= 'a' && lo <= 'f')
        result |= (lo - 'a' + 10);
    else
        return false;

    *out = result;
    return true;
}

// Helper to parse hex string to bytes (matches picopass implementation)
static bool picopass_hex_str_to_uint8(const char* value_str, size_t max, uint8_t* value) {
    bool parse_success = false;
    size_t count = 0;

    while(value_str[0] && value_str[1]) {
        if(count++ >= max) {
            parse_success = true;
            break;
        }
        parse_success = hex_char_to_uint8(value_str[0], value_str[1], value++);
        if(!parse_success) break;
        value_str += 3; // Skip 2 hex chars + 1 space
    }
    return parse_success;
}

// Load picopass file. On failure, 'error' (if not NULL) gets a short message.
static bool
    picopass_load_file(void* context, const char* path, PicopassData* data, FuriString* error) {
    App* app = context;
    bool parsed = false;
    FlipperFormat* file = flipper_format_file_alloc(app->storage);
    FuriString* temp_str = furi_string_alloc();
    FuriString* block_str = furi_string_alloc();

    memset(data, 0, sizeof(PicopassData));

    do {
        if(!flipper_format_file_open_existing(file, path)) {
            ACTION_SET_ERROR("Picopass: Can't open file");
            FURI_LOG_E(TAG, "Picopass: Failed to open file: %s", path);
            break;
        }

        uint32_t version = 0;
        if(!flipper_format_read_header(file, temp_str, &version)) {
            ACTION_SET_ERROR("Picopass: Can't read header");
            break;
        }

        if(furi_string_cmp_str(temp_str, "Flipper Picopass device") != 0) {
            ACTION_SET_ERROR("Picopass: Bad header");
            break;
        }

        // Read blocks 0-5 (required blocks)
        bool block_read = true;
        const char* unknown_block = "?? ?? ?? ?? ?? ?? ?? ??";

        for(size_t i = 0; i < 6; i++) {
            furi_string_printf(temp_str, "Block %d", i);
            if(!flipper_format_read_string(file, furi_string_get_cstr(temp_str), block_str)) {
                ACTION_SET_ERROR("Picopass: Can't read Block %d", (int)i);
                block_read = false;
                break;
            }
            if(furi_string_equal_str(block_str, unknown_block)) {
                data->card_data[i].valid = false;
            } else {
                if(!picopass_hex_str_to_uint8(
                       furi_string_get_cstr(block_str),
                       PICOPASS_BLOCK_LEN,
                       data->card_data[i].data)) {
                    ACTION_SET_ERROR("Picopass: Bad hex Block %d", (int)i);
                    block_read = false;
                    break;
                }
                data->card_data[i].valid = true;
            }
        }
        if(!block_read) break;

        // Parse app blocks based on app_limit
        size_t app_limit = data->card_data[PICOPASS_CONFIG_BLOCK_INDEX].data[0];
        if(app_limit > PICOPASS_MAX_APP_LIMIT) app_limit = PICOPASS_MAX_APP_LIMIT;

        for(size_t i = 6; i < app_limit; i++) {
            furi_string_printf(temp_str, "Block %d", i);
            if(!flipper_format_read_string(file, furi_string_get_cstr(temp_str), block_str)) {
                break; // Optional blocks, don't fail if missing
            }
            if(furi_string_equal_str(block_str, unknown_block)) {
                data->card_data[i].valid = false;
            } else {
                if(picopass_hex_str_to_uint8(
                       furi_string_get_cstr(block_str),
                       PICOPASS_BLOCK_LEN,
                       data->card_data[i].data)) {
                    data->card_data[i].valid = true;
                }
            }
        }

        parsed = true;
    } while(false);

    furi_string_free(temp_str);
    furi_string_free(block_str);
    flipper_format_free(file);

    return parsed;
}

// Initialize cipher state with the key
static void picopass_init_cipher_state(PicopassEmulator* emu) {
    uint8_t key[PICOPASS_BLOCK_LEN] = {};
    memcpy(key, emu->data->card_data[emu->key_block_num].data, PICOPASS_BLOCK_LEN);

    uint8_t cc[PICOPASS_BLOCK_LEN] = {};
    memcpy(cc, emu->data->card_data[PICOPASS_SECURE_EPURSE_BLOCK_INDEX].data, PICOPASS_BLOCK_LEN);

    emu->cipher_state = loclass_opt_doTagMAC_1(cc, key);
}

// Generate anti-collision CSN
static void picopass_write_anticoll_csn(const uint8_t* uid, BitBuffer* buffer) {
    bit_buffer_reset(buffer);
    for(size_t i = 0; i < PICOPASS_BLOCK_LEN; i++) {
        bit_buffer_append_byte(buffer, (uid[i] >> 3) | (uid[(i + 1) % 8] << 5));
    }
}

// Send frame with CRC
static void picopass_send_frame(PicopassEmulator* emu, BitBuffer* buffer) {
    iso13239_crc_append(Iso13239CrcTypePicopass, buffer);
    nfc_listener_tx(emu->nfc, buffer);
}

// NFC listener callback
static NfcCommand picopass_listener_callback(NfcEvent event, void* context) {
    PicopassEmulator* emu = context;

    if(event.type != NfcEventTypeRxEnd) {
        return NfcCommandContinue;
    }

    BitBuffer* rx_buf = event.data.buffer;
    size_t rx_bits = bit_buffer_get_size(rx_buf);

    if(rx_bits < 8) return NfcCommandContinue;

    uint8_t cmd = bit_buffer_get_byte(rx_buf, 0);

    bool secured = (emu->data->card_data[PICOPASS_CONFIG_BLOCK_INDEX].data[7] &
                    PICOPASS_FUSE_CRYPT10) != PICOPASS_FUSE_CRYPT0;

    switch(cmd) {
    case PICOPASS_CMD_ACTALL:
        if(rx_bits == 8 && emu->state != PicopassListenerStateHalt) {
            emu->state = PicopassListenerStateActive;
            nfc_iso15693_listener_tx_sof(emu->nfc);
        }
        break;

    case PICOPASS_CMD_ACT:
        if(rx_bits == 8 && emu->state == PicopassListenerStateActive) {
            nfc_iso15693_listener_tx_sof(emu->nfc);
        }
        break;

    case PICOPASS_CMD_HALT:
        if(rx_bits == 8) {
            emu->state = PicopassListenerStateIdle;
            nfc_iso15693_listener_tx_sof(emu->nfc);
        }
        break;

    case PICOPASS_CMD_READ_OR_IDENTIFY:
        if(rx_bits == 8 && emu->state == PicopassListenerStateActive) {
            // IDENTIFY - return anti-collision CSN
            picopass_write_anticoll_csn(
                emu->data->card_data[PICOPASS_CSN_BLOCK_INDEX].data, emu->tx_buffer);
            picopass_send_frame(emu, emu->tx_buffer);
        } else if(rx_bits == 8 * 4 && emu->state == PicopassListenerStateSelected) {
            // READ command
            uint8_t block_num = bit_buffer_get_byte(rx_buf, 1);
            if(block_num >= PICOPASS_MAX_APP_LIMIT) break;

            if(secured && (block_num == PICOPASS_SECURE_KD_BLOCK_INDEX ||
                           block_num == PICOPASS_SECURE_KC_BLOCK_INDEX)) {
                // Return 0xFF for key blocks
                bit_buffer_reset(emu->tx_buffer);
                for(size_t i = 0; i < PICOPASS_BLOCK_LEN; i++) {
                    bit_buffer_append_byte(emu->tx_buffer, 0xFF);
                }
            } else {
                bit_buffer_copy_bytes(
                    emu->tx_buffer, emu->data->card_data[block_num].data, PICOPASS_BLOCK_LEN);
            }
            picopass_send_frame(emu, emu->tx_buffer);
        }
        break;

    case PICOPASS_CMD_SELECT:
        if(rx_bits == 8 * 9) {
            const uint8_t* received_data = bit_buffer_get_data(rx_buf);
            const uint8_t* csn = emu->data->card_data[PICOPASS_CSN_BLOCK_INDEX].data;

            // Check CSN based on state
            if(emu->state == PicopassListenerStateHalt ||
               emu->state == PicopassListenerStateIdle) {
                if(memcmp(&received_data[1], csn, PICOPASS_BLOCK_LEN) != 0) {
                    break;
                }
            } else {
                // Compare with anti-collision CSN
                picopass_write_anticoll_csn(csn, emu->tmp_buffer);
                if(memcmp(
                       &received_data[1],
                       bit_buffer_get_data(emu->tmp_buffer),
                       PICOPASS_BLOCK_LEN) != 0) {
                    emu->state = PicopassListenerStateIdle;
                    break;
                }
            }

            emu->state = PicopassListenerStateSelected;
            bit_buffer_copy_bytes(emu->tx_buffer, csn, PICOPASS_BLOCK_LEN);
            picopass_send_frame(emu, emu->tx_buffer);
        }
        break;

    case PICOPASS_CMD_READCHECK_KD:
    case PICOPASS_CMD_READCHECK_KC:
        if(emu->state == PicopassListenerStateSelected && rx_bits == 8 * 2) {
            uint8_t block_num = bit_buffer_get_byte(rx_buf, 1);
            if(block_num != PICOPASS_SECURE_EPURSE_BLOCK_INDEX) break;

            uint8_t new_key_block = (cmd == PICOPASS_CMD_READCHECK_KD) ?
                                        PICOPASS_SECURE_KD_BLOCK_INDEX :
                                        PICOPASS_SECURE_KC_BLOCK_INDEX;

            if(emu->key_block_num != new_key_block) {
                emu->key_block_num = new_key_block;
                picopass_init_cipher_state(emu);
            }

            // Return e-purse block (no CRC for READCHECK)
            bit_buffer_copy_bytes(
                emu->tx_buffer, emu->data->card_data[block_num].data, PICOPASS_BLOCK_LEN);
            nfc_listener_tx(emu->nfc, emu->tx_buffer);
        }
        break;

    case PICOPASS_CMD_CHECK:
        if(emu->state == PicopassListenerStateSelected && rx_bits == 8 * 9) {
            if(!secured) break;

            const uint8_t* key = emu->data->card_data[emu->key_block_num].data;
            bool have_key = emu->data->card_data[emu->key_block_num].valid;
            const uint8_t* rx_data = bit_buffer_get_data(rx_buf);

            if(have_key) {
                uint8_t rmac[4] = {};
                uint8_t tmac[4] = {};

                loclass_opt_doBothMAC_2(emu->cipher_state, &rx_data[1], rmac, tmac, key);

                if(memcmp(&rx_data[5], rmac, PICOPASS_MAC_LEN) != 0) {
                    // Bad MAC - reset cipher and don't respond
                    picopass_init_cipher_state(emu);
                    break;
                }

                // Send tag MAC response
                bit_buffer_copy_bytes(emu->tx_buffer, tmac, sizeof(tmac));
                nfc_listener_tx(emu->nfc, emu->tx_buffer);
            } else {
                // CVE-2024-41566 exploit - dummy response
                uint8_t dummy[4] = {0xFF, 0xFF, 0xFF, 0xFF};
                bit_buffer_copy_bytes(emu->tx_buffer, dummy, 4);
                nfc_listener_tx(emu->nfc, emu->tx_buffer);
            }
        }
        break;

    case PICOPASS_CMD_READ4:
        if(emu->state == PicopassListenerStateSelected && rx_bits == 8 * 4) {
            uint8_t block_num = bit_buffer_get_byte(rx_buf, 1);
            if(block_num + 4 > PICOPASS_MAX_APP_LIMIT) break;

            bit_buffer_reset(emu->tx_buffer);
            for(size_t i = 0; i < 4; i++) {
                uint8_t bn = block_num + i;
                if(secured && (bn == PICOPASS_SECURE_KD_BLOCK_INDEX ||
                               bn == PICOPASS_SECURE_KC_BLOCK_INDEX)) {
                    for(size_t j = 0; j < PICOPASS_BLOCK_LEN; j++) {
                        bit_buffer_append_byte(emu->tx_buffer, 0xFF);
                    }
                } else {
                    for(size_t j = 0; j < PICOPASS_BLOCK_LEN; j++) {
                        bit_buffer_append_byte(emu->tx_buffer, emu->data->card_data[bn].data[j]);
                    }
                }
            }
            picopass_send_frame(emu, emu->tx_buffer);
        }
        break;
    }

    return NfcCommandContinue;
}

static PicopassEmulator*
    action_picopass_emulator_start(void* context, const char* path, FuriString* error) {
    App* app = context;

    if(!storage_file_exists(app->storage, path)) {
        ACTION_SET_ERROR("Picopass: File not found");
        FURI_LOG_E(TAG, "Picopass: File does not exist: %s", path);
        return NULL;
    }

    PicopassData* data = malloc(sizeof(PicopassData));
    if(!picopass_load_file(app, path, data, error)) {
        FURI_LOG_E(TAG, "Picopass: Failed to load file: %s", path);
        free(data);
        return NULL;
    }

    FURI_LOG_I(TAG, "Picopass: Starting emulation: %s", path);

    Nfc* nfc = nfc_alloc();

    PicopassEmulator* emu = malloc(sizeof(PicopassEmulator));
    emu->nfc = nfc;
    emu->data = data;
    emu->state = PicopassListenerStateIdle;
    emu->tx_buffer = bit_buffer_alloc(PICOPASS_LISTENER_BUFFER_SIZE_MAX);
    emu->tmp_buffer = bit_buffer_alloc(PICOPASS_LISTENER_BUFFER_SIZE_MAX);
    emu->key_block_num = 0;

    // Configure NFC - order matters!
    nfc_set_fdt_listen_fc(nfc, PICOPASS_FDT_LISTEN_FC);
    nfc_config(nfc, NfcModeListener, NfcTechIso15693);

    // Start emulation
    nfc_start(nfc, picopass_listener_callback, emu);

    return emu;
}

static void action_picopass_emulator_stop(PicopassEmulator* emu) {
    if(!emu) return;

    FURI_LOG_I(TAG, "Picopass: Stopping emulation");
    nfc_stop(emu->nfc);
    bit_buffer_free(emu->tx_buffer);
    bit_buffer_free(emu->tmp_buffer);
    nfc_free(emu->nfc);
    free(emu->data);
    free(emu);
}

void action_picopass_tx(void* context, const FuriString* action_path, FuriString* error) {
    App* app = context;
    int32_t duration_ms = (int32_t)app->settings.picopass_duration;

    PicopassEmulator* emu =
        action_picopass_emulator_start(context, furi_string_get_cstr(action_path), error);
    if(!emu) {
        // error already set by emulator_start
        return;
    }

    FURI_LOG_I(TAG, "Picopass: Emulating for %ld ms", duration_ms);

    const int32_t interval_ms = 100;
    while(duration_ms > 0) {
        furi_delay_ms(interval_ms);
        duration_ms -= interval_ms;
    }

    action_picopass_emulator_stop(emu);
    FURI_LOG_I(TAG, "Picopass: Done");
}
