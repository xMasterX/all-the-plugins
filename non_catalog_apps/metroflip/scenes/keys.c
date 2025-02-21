#include "../metroflip_i.h"
#include "keys.h"
#include <bit_lib.h>
#include <nfc/protocols/mf_classic/mf_classic.h>
#include <nfc/protocols/mf_classic/mf_classic_poller.h>
#include <nfc/nfc.h>
#include <nfc/protocols/mf_classic/mf_classic_poller_sync.h>
#include <string.h>

#define TAG "keys_check"

const MfClassicKeyPair troika_1k_key[] = {
    {.a = 0x08b386463229},
};

const MfClassicKeyPair troika_4k_key[] = {
    {.a = 0xA73F5DC1D333},
};

const MfClassicKeyPair smartrider_verify_key[] = {
    {.a = 0x2031D1E57A3B},
};

const MfClassicKeyPair charliecard_1k_verify_key[] = {
    {.a = 0x5EC39B022F2B},
};

const MfClassicKeyPair bip_1k_verify_key[] = {
    {.a = 0x3a42f33af429},
};

const MfClassicKeyPair metromoney_1k_verify_key[] = {
    {.a = 0x9C616585E26D},
};

const uint8_t gocard_verify_data[1][14] = {
    {0x16, 0x18, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x5A, 0x5B, 0x20, 0x21, 0x22, 0x23}};

const uint8_t gocard_verify_data2[1][14] = {
    {0x16, 0x18, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x01, 0x01}};

static bool charliecard_verify(Nfc* nfc, MfClassicData* mfc_data, bool data_loaded) {
    bool verified = false;
    FURI_LOG_I(TAG, "verifying charliecard..");
    const uint8_t verify_sector = 1;
    do {
        if(!data_loaded) {
            const uint8_t verify_block =
                mf_classic_get_first_block_num_of_sector(verify_sector) + 1;
            FURI_LOG_I(TAG, "Verifying sector %u", verify_sector);

            MfClassicKey key = {0};
            bit_lib_num_to_bytes_be(charliecard_1k_verify_key[0].a, COUNT_OF(key.data), key.data);

            MfClassicAuthContext auth_context;
            MfClassicError error = mf_classic_poller_sync_auth(
                nfc, verify_block, &key, MfClassicKeyTypeA, &auth_context);
            if(error != MfClassicErrorNone) {
                FURI_LOG_I(TAG, "Failed to read block %u: %d", verify_block, error);
                break;
            }

            verified = true;
        } else {
            MfClassicSectorTrailer* sec_tr =
                mf_classic_get_sector_trailer_by_sector(mfc_data, verify_sector);
            FURI_LOG_I(TAG, "%2x", sec_tr->key_a.data[1]);
            uint64_t key = bit_lib_bytes_to_num_be(sec_tr->key_a.data, 6);
            if(key != charliecard_1k_verify_key[0].a) {
                FURI_LOG_I(TAG, "not equall");
                break;
            }

            verified = true;
        }
    } while(false);

    return verified;
}

bool bip_verify(Nfc* nfc, MfClassicData* mfc_data, bool data_loaded) {
    bool verified = false;

    do {
        if(!data_loaded) {
            const uint8_t verify_sector = 0;
            uint8_t block_num = mf_classic_get_first_block_num_of_sector(verify_sector);
            FURI_LOG_I(TAG, "Verifying sector %u", verify_sector);

            MfClassicKey key = {};
            bit_lib_num_to_bytes_be(bip_1k_verify_key[0].a, COUNT_OF(key.data), key.data);

            MfClassicAuthContext auth_ctx = {};
            MfClassicError error =
                mf_classic_poller_sync_auth(nfc, block_num, &key, MfClassicKeyTypeA, &auth_ctx);

            if(error != MfClassicErrorNone) {
                FURI_LOG_I(TAG, "Failed to read block %u: %d", block_num, error);
                break;
            }

            verified = true;
        } else {
            MfClassicSectorTrailer* sec_tr = mf_classic_get_sector_trailer_by_sector(mfc_data, 0);

            uint64_t key = bit_lib_bytes_to_num_be(sec_tr->key_a.data, 6);
            if(key != bip_1k_verify_key[0].a) {
                break;
            }

            verified = true;
        }
    } while(false);

    return verified;
}

static bool metromoney_verify(Nfc* nfc, MfClassicData* mfc_data, bool data_loaded) {
    bool verified = false;
    const uint8_t ticket_sector_number = 1;
    do {
        if(!data_loaded) {
            const uint8_t ticket_block_number =
                mf_classic_get_first_block_num_of_sector(ticket_sector_number) + 1;
            FURI_LOG_D(TAG, "Verifying sector %u", ticket_sector_number);

            MfClassicKey key = {0};
            bit_lib_num_to_bytes_be(metromoney_1k_verify_key[0].a, COUNT_OF(key.data), key.data);

            MfClassicAuthContext auth_context;
            MfClassicError error = mf_classic_poller_sync_auth(
                nfc, ticket_block_number, &key, MfClassicKeyTypeA, &auth_context);
            if(error != MfClassicErrorNone) {
                FURI_LOG_D(TAG, "Failed to read block %u: %d", ticket_block_number, error);
                break;
            }

            verified = true;
        } else {
            MfClassicSectorTrailer* sec_tr =
                mf_classic_get_sector_trailer_by_sector(mfc_data, ticket_sector_number);

            uint64_t key = bit_lib_bytes_to_num_be(sec_tr->key_a.data, 6);
            if(key != metromoney_1k_verify_key[0].a) {
                break;
            }

            verified = true;
        }
    } while(false);

    return verified;
}

static bool smartrider_verify(Nfc* nfc, MfClassicData* mfc_data, bool data_loaded) {
    bool verified = false;

    do {
        if(!data_loaded) {
            const uint8_t block_number = mf_classic_get_first_block_num_of_sector(0) + 1;
            FURI_LOG_D(TAG, "Verifying sector 0");

            MfClassicKey key = {0};
            bit_lib_num_to_bytes_be(smartrider_verify_key[0].a, COUNT_OF(key.data), key.data);

            MfClassicAuthContext auth_context;
            MfClassicError error = mf_classic_poller_sync_auth(
                nfc, block_number, &key, MfClassicKeyTypeA, &auth_context);
            if(error != MfClassicErrorNone) {
                FURI_LOG_D(TAG, "Failed to read block %u: %d", block_number, error);
                break;
            }

            verified = true;
        } else {
            MfClassicSectorTrailer* sec_tr = mf_classic_get_sector_trailer_by_sector(mfc_data, 0);

            uint64_t key = bit_lib_bytes_to_num_be(sec_tr->key_a.data, 6);
            if(key != smartrider_verify_key[0].a) {
                break;
            }

            verified = true;
        }
    } while(false);

    return verified;
}

static bool troika_get_card_config(TroikaCardConfig* config, MfClassicType type) {
    bool success = true;

    if(type == MfClassicType1k) {
        config->data_sector = 11;
        config->keys = troika_1k_key;
    } else if(type == MfClassicType4k) {
        config->data_sector = 8; // Further testing needed
        config->keys = troika_4k_key;
    } else {
        success = false;
    }

    return success;
}

static bool
    troika_verify_type(Nfc* nfc, MfClassicData* mfc_data, bool data_loaded, MfClassicType type) {
    bool verified = false;

    do {
        if(!data_loaded) {
            TroikaCardConfig cfg = {};
            if(!troika_get_card_config(&cfg, type)) break;

            const uint8_t block_num = mf_classic_get_first_block_num_of_sector(cfg.data_sector);
            FURI_LOG_D(TAG, "Verifying sector %lu", cfg.data_sector);

            MfClassicKey key = {0};
            bit_lib_num_to_bytes_be(cfg.keys[0].a, COUNT_OF(key.data), key.data);

            MfClassicAuthContext auth_context;
            MfClassicError error = mf_classic_poller_sync_auth(
                nfc, block_num, &key, MfClassicKeyTypeA, &auth_context);
            if(error != MfClassicErrorNone) {
                FURI_LOG_D(TAG, "Failed to read block %u: %d", block_num, error);
                break;
            }
            FURI_LOG_D(TAG, "Verify success!");
            verified = true;
        } else {
            TroikaCardConfig cfg = {};
            if(!troika_get_card_config(&cfg, type)) break;
            MfClassicSectorTrailer* sec_tr =
                mf_classic_get_sector_trailer_by_sector(mfc_data, cfg.data_sector);

            uint64_t key = bit_lib_bytes_to_num_be(sec_tr->key_a.data, 6);
            if(key != cfg.keys[0].a) {
                break;
            }

            verified = true;
        }
    } while(false);

    return verified;
}

static bool troika_verify(Nfc* nfc, MfClassicData* mfc_data, bool data_loaded) {
    return troika_verify_type(nfc, mfc_data, data_loaded, MfClassicType1k) ||
           troika_verify_type(nfc, mfc_data, data_loaded, MfClassicType4k);
}

static bool gocard_verify(MfClassicData* mfc_data, bool data_loaded) {
    bool verified = false;
    FURI_LOG_I(TAG, "verifying charliecard..");
    do {
        if(data_loaded) {
            uint8_t* buffer = &mfc_data->block[1].data[1];
            size_t buffer_size = 14;

            if(memcmp(buffer, gocard_verify_data[0], buffer_size) == 0) {
                FURI_LOG_I(TAG, "Match!");
            } else {
                FURI_LOG_I(TAG, "No match.");
                if(memcmp(buffer, gocard_verify_data2[0], buffer_size) == 0) {
                    FURI_LOG_I(TAG, "Match!");
                } else {
                    FURI_LOG_I(TAG, "No match.");
                    break;
                }
            }

            verified = true;
        }
    } while(false);

    return verified;
}

CardType determine_card_type(Nfc* nfc, MfClassicData* mfc_data, bool data_loaded) {
    FURI_LOG_I(TAG, "checking keys..");
    UNUSED(bip_verify);

    if(bip_verify(nfc, mfc_data, data_loaded)) {
        return CARD_TYPE_BIP;
    } else if(metromoney_verify(nfc, mfc_data, data_loaded)) {
        return CARD_TYPE_METROMONEY;
    } else if(smartrider_verify(nfc, mfc_data, data_loaded)) {
        return CARD_TYPE_SMARTRIDER;
    } else if(troika_verify(nfc, mfc_data, data_loaded)) {
        return CARD_TYPE_TROIKA;
    } else if(charliecard_verify(nfc, mfc_data, data_loaded)) {
        return CARD_TYPE_CHARLIECARD;
    } else if(gocard_verify(mfc_data, data_loaded)) {
        return CARD_TYPE_GOCARD;
    } else {
        FURI_LOG_I(TAG, "its unknown");
        return CARD_TYPE_UNKNOWN;
    }
}
