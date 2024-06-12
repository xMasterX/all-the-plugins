#include "nfc_worker_i.h"
#include <furi_hal_rtc.h>

#include "ST25RFAL002/platform.h"

#define TAG "NfcWorker"

/***************************** NFC Worker API *******************************/

NfcWorker* nfc_worker_alloc() {
    NfcWorker* nfc_worker = malloc(sizeof(NfcWorker));

    // Worker thread attributes
    nfc_worker->thread = furi_thread_alloc_ex("NfcWorker", 8192, nfc_worker_task, nfc_worker);

    nfc_worker->callback = NULL;
    nfc_worker->context = NULL;
    nfc_worker->storage = furi_record_open(RECORD_STORAGE);

    // Initialize rfal
    while(furry_hal_nfc_is_busy()) {
        furi_delay_ms(10);
    }
    nfc_worker_change_state(nfc_worker, NfcWorkerStateReady);

    return nfc_worker;
}

void nfc_worker_free(NfcWorker* nfc_worker) {
    furi_assert(nfc_worker);

    furi_thread_free(nfc_worker->thread);

    furi_record_close(RECORD_STORAGE);

    free(nfc_worker);
}

NfcWorkerState nfc_worker_get_state(NfcWorker* nfc_worker) {
    return nfc_worker->state;
}

void nfc_worker_start(
    NfcWorker* nfc_worker,
    NfcWorkerState state,
    NfcDeviceData* dev_data,
    NfcWorkerCallback callback,
    void* context) {
    furi_check(nfc_worker);
    //furi_check(dev_data);
    while(furry_hal_nfc_is_busy()) {
        furi_delay_ms(10);
    }
    furry_hal_nfc_deinit();
    furry_hal_nfc_init();

    nfc_worker->callback = callback;
    nfc_worker->context = context;
    nfc_worker->dev_data = dev_data;
    nfc_worker_change_state(nfc_worker, state);
    furi_thread_start(nfc_worker->thread);
}

void nfc_worker_stop(NfcWorker* nfc_worker) {
    furi_assert(nfc_worker);
    furi_assert(nfc_worker->thread);
    if(furi_thread_get_state(nfc_worker->thread) != FuriThreadStateStopped) {
        furry_hal_nfc_stop();
        nfc_worker_change_state(nfc_worker, NfcWorkerStateStop);
        furi_thread_join(nfc_worker->thread);
    }
}

void nfc_worker_change_state(NfcWorker* nfc_worker, NfcWorkerState state) {
    nfc_worker->state = state;
}

/***************************** NFC Worker Thread *******************************/

int32_t nfc_worker_task(void* context) {
    NfcWorker* nfc_worker = context;

    furry_hal_nfc_exit_sleep();

    if(nfc_worker->state == NfcWorkerStateRead) {
        if(nfc_worker->dev_data->read_mode == NfcReadModeAuto) {
            nfc_worker_read(nfc_worker);
        } else {
            nfc_worker_read_type(nfc_worker);
        }
    } else if(nfc_worker->state == NfcWorkerStateUidEmulate) {
        nfc_worker_emulate_uid(nfc_worker);
    } else if(nfc_worker->state == NfcWorkerStateMfClassicEmulate) {
        nfc_worker_emulate_mf_classic(nfc_worker);
    } else if(nfc_worker->state == NfcWorkerStateMfClassicWrite) {
        nfc_worker_write_mf_classic(nfc_worker);
    } else if(nfc_worker->state == NfcWorkerStateMfClassicUpdate) {
        nfc_worker_update_mf_classic(nfc_worker);
    } else if(nfc_worker->state == NfcWorkerStateMfClassicDictAttack) {
        nfc_worker_mf_classic_dict_attack(nfc_worker);
    }
    furry_hal_nfc_sleep();
    nfc_worker_change_state(nfc_worker, NfcWorkerStateReady);

    return 0;
}

static bool nfc_worker_read_mf_classic(NfcWorker* nfc_worker, FurryHalNfcTxRxContext* tx_rx) {
    furi_assert(nfc_worker->callback);
    bool read_success = false;

    do {
        // Try to read card with key cache
        FURI_LOG_I(TAG, "Search for key cache ...");
        if(nfc_worker->callback(NfcWorkerEventReadMfClassicLoadKeyCache, nfc_worker->context)) {
            FURI_LOG_I(TAG, "Load keys cache success. Start reading");
            uint8_t sectors_read =
                mf_classic_update_card(tx_rx, &nfc_worker->dev_data->mf_classic_data);
            uint8_t sectors_total =
                mf_classic_get_total_sectors_num(nfc_worker->dev_data->mf_classic_data.type);
            FURI_LOG_I(TAG, "Read %d sectors out of %d total", sectors_read, sectors_total);
            read_success = mf_classic_is_card_read(&nfc_worker->dev_data->mf_classic_data);
        }
    } while(false);

    return read_success;
}

static bool nfc_worker_read_nfca(NfcWorker* nfc_worker, FurryHalNfcTxRxContext* tx_rx) {
    FurryHalNfcDevData* nfc_data = &nfc_worker->dev_data->nfc_data;

    bool card_read = false;
    furry_hal_nfc_sleep();
    if(mf_classic_check_card_type(nfc_data->atqa[0], nfc_data->atqa[1], nfc_data->sak)) {
        FURI_LOG_I(TAG, "Mifare Classic detected");
        nfc_worker->dev_data->protocol = NfcDeviceProtocolMifareClassic;
        nfc_worker->dev_data->mf_classic_data.type =
            mf_classic_get_classic_type(nfc_data->atqa[0], nfc_data->atqa[1], nfc_data->sak);
        card_read = nfc_worker_read_mf_classic(nfc_worker, tx_rx);
    } else if(nfc_data->interface == FurryHalNfcInterfaceIsoDep) {
        FURI_LOG_I(TAG, "ISO14443-4 card detected");

        nfc_worker->dev_data->protocol = NfcDeviceProtocolUnknown;

        card_read = true;
    } else {
        nfc_worker->dev_data->protocol = NfcDeviceProtocolUnknown;
        card_read = true;
    }

    return card_read;
}

void nfc_worker_read(NfcWorker* nfc_worker) {
    furi_assert(nfc_worker);
    furi_assert(nfc_worker->callback);

    nfc_device_data_clear(nfc_worker->dev_data);
    NfcDeviceData* dev_data = nfc_worker->dev_data;
    FurryHalNfcDevData* nfc_data = &nfc_worker->dev_data->nfc_data;
    FurryHalNfcTxRxContext tx_rx = {};
    NfcWorkerEvent event = 0;
    bool card_not_detected_notified = false;

    while(nfc_worker->state == NfcWorkerStateRead) {
        if(furry_hal_nfc_detect(nfc_data, 300)) {
            // Process first found device
            nfc_worker->callback(NfcWorkerEventCardDetected, nfc_worker->context);
            card_not_detected_notified = false;
            if(nfc_data->type == FurryHalNfcTypeA) {
                if(nfc_worker_read_nfca(nfc_worker, &tx_rx)) {
                    if(dev_data->protocol == NfcDeviceProtocolMifareClassic) {
                        event = NfcWorkerEventReadMfClassicDone;
                        break;
                    } else if(dev_data->protocol == NfcDeviceProtocolUnknown) {
                        event = NfcWorkerEventReadUidNfcA;
                        break;
                    }
                } else {
                    if(dev_data->protocol == NfcDeviceProtocolMifareClassic) {
                        event = NfcWorkerEventReadMfClassicDictAttackRequired;
                        break;
                    }
                }
            } else if(nfc_data->type == FurryHalNfcTypeB) {
                event = NfcWorkerEventReadUidNfcB;
                break;
            } else if(nfc_data->type == FurryHalNfcTypeF) {
                event = NfcWorkerEventReadUidNfcF;
                break;
            }
        } else {
            if(!card_not_detected_notified) {
                nfc_worker->callback(NfcWorkerEventNoCardDetected, nfc_worker->context);
                card_not_detected_notified = true;
            }
        }
        furry_hal_nfc_sleep();
        furi_delay_ms(100);
    }
    // Notify caller and exit
    if(event > NfcWorkerEventReserved) {
        nfc_worker->callback(event, nfc_worker->context);
    }
}

void nfc_worker_read_type(NfcWorker* nfc_worker) {
    furi_assert(nfc_worker);
    furi_assert(nfc_worker->callback);

    NfcReadMode read_mode = nfc_worker->dev_data->read_mode;
    nfc_device_data_clear(nfc_worker->dev_data);
    NfcDeviceData* dev_data = nfc_worker->dev_data;
    FurryHalNfcDevData* nfc_data = &nfc_worker->dev_data->nfc_data;
    FurryHalNfcTxRxContext tx_rx = {};
    NfcWorkerEvent event = 0;
    bool card_not_detected_notified = false;

    while(nfc_worker->state == NfcWorkerStateRead) {
        if(furry_hal_nfc_detect(nfc_data, 300)) {
            FURI_LOG_D(TAG, "Card detected");
            furry_hal_nfc_sleep();
            // Process first found device
            nfc_worker->callback(NfcWorkerEventCardDetected, nfc_worker->context);
            card_not_detected_notified = false;
            if(nfc_data->type == FurryHalNfcTypeA) {
                if(read_mode == NfcReadModeMfClassic) {
                    nfc_worker->dev_data->protocol = NfcDeviceProtocolMifareClassic;
                    nfc_worker->dev_data->mf_classic_data.type = mf_classic_get_classic_type(
                        nfc_data->atqa[0], nfc_data->atqa[1], nfc_data->sak);
                    if(nfc_worker_read_mf_classic(nfc_worker, &tx_rx)) {
                        FURI_LOG_D(TAG, "Card read");
                        dev_data->protocol = NfcDeviceProtocolMifareClassic;
                        event = NfcWorkerEventReadMfClassicDone;
                        break;
                    } else {
                        FURI_LOG_D(TAG, "Card read failed");
                        dev_data->protocol = NfcDeviceProtocolMifareClassic;
                        event = NfcWorkerEventReadMfClassicDictAttackRequired;
                        break;
                    }
                } else if(read_mode == NfcReadModeNFCA) {
                    nfc_worker->dev_data->protocol = NfcDeviceProtocolUnknown;
                    event = NfcWorkerEventReadUidNfcA;
                    break;
                }
            }
        } else {
            if(!card_not_detected_notified) {
                nfc_worker->callback(NfcWorkerEventNoCardDetected, nfc_worker->context);
                card_not_detected_notified = true;
            }
        }
        furry_hal_nfc_sleep();
        furi_delay_ms(100);
    }
    // Notify caller and exit
    if(event > NfcWorkerEventReserved) {
        nfc_worker->callback(event, nfc_worker->context);
    }
}

void nfc_worker_emulate_uid(NfcWorker* nfc_worker) {
    FurryHalNfcTxRxContext tx_rx = {};
    FurryHalNfcDevData* data = &nfc_worker->dev_data->nfc_data;
    NfcReaderRequestData* reader_data = &nfc_worker->dev_data->reader_data;

    // TODO add support for RATS
    // Need to save ATS to support ISO-14443A-4 emulation

    while(nfc_worker->state == NfcWorkerStateUidEmulate) {
        if(furry_hal_nfc_listen(data->uid, data->uid_len, data->atqa, data->sak, false, 100)) {
            if(furry_hal_nfc_tx_rx(&tx_rx, 100)) {
                reader_data->size = tx_rx.rx_bits / 8;
                if(reader_data->size > 0) {
                    memcpy(reader_data->data, tx_rx.rx_data, reader_data->size);
                    if(nfc_worker->callback) {
                        nfc_worker->callback(NfcWorkerEventSuccess, nfc_worker->context);
                    }
                }
            } else {
                FURI_LOG_E(TAG, "Failed to get reader commands");
            }
        }
    }
}

static bool nfc_worker_mf_get_b_key_from_sector_trailer(
    FurryHalNfcTxRxContext* tx_rx,
    uint16_t sector,
    uint64_t key,
    uint64_t* found_key) {
    // Some access conditions allow reading B key via A key

    uint8_t block = mf_classic_get_sector_trailer_block_num_by_sector(sector);

    Crypto1 crypto = {};
    MfClassicBlock block_tmp = {};
    MfClassicAuthContext auth_context = {.sector = sector, .key_a = MF_CLASSIC_NO_KEY, .key_b = 0};

    furry_hal_nfc_sleep();

    if(mf_classic_auth_attempt(tx_rx, &crypto, &auth_context, key)) {
        if(mf_classic_read_block(tx_rx, &crypto, block, &block_tmp)) {
            *found_key = nfc_util_bytes2num(&block_tmp.value[10], sizeof(uint8_t) * 6);

            return *found_key;
        }
    }

    return false;
}

static void nfc_worker_mf_classic_key_attack(
    NfcWorker* nfc_worker,
    uint64_t key,
    FurryHalNfcTxRxContext* tx_rx,
    uint16_t start_sector) {
    furi_assert(nfc_worker);
    furi_assert(nfc_worker->callback);

    bool card_found_notified = true;
    bool card_removed_notified = false;

    MfClassicData* data = &nfc_worker->dev_data->mf_classic_data;
    NfcMfClassicDictAttackData* dict_attack_data =
        &nfc_worker->dev_data->mf_classic_dict_attack_data;
    uint32_t total_sectors = mf_classic_get_total_sectors_num(data->type);

    furi_assert(start_sector < total_sectors);

    nfc_worker->callback(NfcWorkerEventKeyAttackStart, nfc_worker->context);

    // Check every sector's A and B keys with the given key
    for(size_t i = start_sector; i < total_sectors; i++) {
        nfc_worker->callback(NfcWorkerEventKeyAttackNextSector, nfc_worker->context);
        dict_attack_data->current_sector = i;
        furry_hal_nfc_sleep();
        if(furry_hal_nfc_activate_nfca(200, NULL)) {
            furry_hal_nfc_sleep();
            if(!card_found_notified) {
                nfc_worker->callback(NfcWorkerEventCardDetected, nfc_worker->context);
                card_found_notified = true;
                card_removed_notified = false;
            }
            uint8_t block_num = mf_classic_get_sector_trailer_block_num_by_sector(i);
            if(mf_classic_is_sector_read(data, i)) continue;
            if(!mf_classic_is_key_found(data, i, MfClassicKeyA)) {
                FURI_LOG_D(TAG, "Trying A key for sector %d, key: %012llX", i, key);
                if(mf_classic_authenticate(tx_rx, block_num, key, MfClassicKeyA)) {
                    mf_classic_set_key_found(data, i, MfClassicKeyA, key);
                    FURI_LOG_D(TAG, "Key A found: %012llX", key);
                    nfc_worker->callback(NfcWorkerEventFoundKeyA, nfc_worker->context);

                    uint64_t found_key;
                    if(nfc_worker_mf_get_b_key_from_sector_trailer(tx_rx, i, key, &found_key)) {
                        FURI_LOG_D(TAG, "Found B key via reading sector %d", i);
                        mf_classic_set_key_found(data, i, MfClassicKeyB, found_key);

                        if(nfc_worker->state == NfcWorkerStateMfClassicDictAttack) {
                            nfc_worker->callback(NfcWorkerEventFoundKeyB, nfc_worker->context);
                        }
                    }
                }
                furry_hal_nfc_sleep();
            }
            if(!mf_classic_is_key_found(data, i, MfClassicKeyB)) {
                FURI_LOG_D(TAG, "Trying B key for sector %d, key: %012llX", i, key);
                if(mf_classic_authenticate(tx_rx, block_num, key, MfClassicKeyB)) {
                    mf_classic_set_key_found(data, i, MfClassicKeyB, key);
                    FURI_LOG_D(TAG, "Key B found: %012llX", key);
                    nfc_worker->callback(NfcWorkerEventFoundKeyB, nfc_worker->context);
                }
            }

            if(mf_classic_is_sector_read(data, i)) continue;
            mf_classic_read_sector(tx_rx, data, i);
        } else {
            if(!card_removed_notified) {
                nfc_worker->callback(NfcWorkerEventNoCardDetected, nfc_worker->context);
                card_removed_notified = true;
                card_found_notified = false;
            }
        }
        if(nfc_worker->state != NfcWorkerStateMfClassicDictAttack) break;
    }
    nfc_worker->callback(NfcWorkerEventKeyAttackStop, nfc_worker->context);
}

void nfc_worker_mf_classic_dict_attack(NfcWorker* nfc_worker) {
    furi_assert(nfc_worker);
    furi_assert(nfc_worker->callback);

    MfClassicData* data = &nfc_worker->dev_data->mf_classic_data;
    NfcMfClassicDictAttackData* dict_attack_data =
        &nfc_worker->dev_data->mf_classic_dict_attack_data;
    uint32_t total_sectors = mf_classic_get_total_sectors_num(data->type);
    uint64_t key = 0;
    uint64_t prev_key = 0;
    FurryHalNfcTxRxContext tx_rx = {};
    bool card_found_notified = true;
    bool card_removed_notified = false;

    // Load dictionary
    MfClassicDict* dict = dict_attack_data->dict;
    if(!dict) {
        FURI_LOG_E(TAG, "Dictionary not found");
        nfc_worker->callback(NfcWorkerEventNoDictFound, nfc_worker->context);
        return;
    }

    FURI_LOG_D(
        TAG, "Start Dictionary attack, Key Count %lu", mf_classic_dict_get_total_keys(dict));
    for(size_t i = 0; i < total_sectors; i++) {
        FURI_LOG_I(TAG, "Sector %d", i);
        nfc_worker->callback(NfcWorkerEventNewSector, nfc_worker->context);
        uint8_t block_num = mf_classic_get_sector_trailer_block_num_by_sector(i);
        if(mf_classic_is_sector_read(data, i)) continue;
        if(mf_classic_is_key_found(data, i, MfClassicKeyA) &&
           mf_classic_is_key_found(data, i, MfClassicKeyB))
            continue;
        uint16_t key_index = 0;
        while(mf_classic_dict_get_next_key(dict, &key)) {
            FURI_LOG_T(TAG, "Key %d", key_index);
            if(++key_index % NFC_DICT_KEY_BATCH_SIZE == 0) {
                nfc_worker->callback(NfcWorkerEventNewDictKeyBatch, nfc_worker->context);
            }
            furry_hal_nfc_sleep();
            uint32_t cuid;
            if(furry_hal_nfc_activate_nfca(200, &cuid)) {
                bool deactivated = false;
                if(!card_found_notified) {
                    nfc_worker->callback(NfcWorkerEventCardDetected, nfc_worker->context);
                    card_found_notified = true;
                    card_removed_notified = false;
                    nfc_worker_mf_classic_key_attack(nfc_worker, prev_key, &tx_rx, i);
                    deactivated = true;
                }
                FURI_LOG_D(TAG, "Try to auth to sector %d with key %012llX", i, key);
                if(!mf_classic_is_key_found(data, i, MfClassicKeyA)) {
                    if(mf_classic_authenticate_skip_activate(
                           &tx_rx, block_num, key, MfClassicKeyA, !deactivated, cuid)) {
                        mf_classic_set_key_found(data, i, MfClassicKeyA, key);
                        FURI_LOG_D(TAG, "Key A found: %012llX", key);
                        nfc_worker->callback(NfcWorkerEventFoundKeyA, nfc_worker->context);

                        uint64_t found_key;
                        if(nfc_worker_mf_get_b_key_from_sector_trailer(
                               &tx_rx, i, key, &found_key)) {
                            FURI_LOG_D(TAG, "Found B key via reading sector %d", i);
                            mf_classic_set_key_found(data, i, MfClassicKeyB, found_key);

                            if(nfc_worker->state == NfcWorkerStateMfClassicDictAttack) {
                                nfc_worker->callback(NfcWorkerEventFoundKeyB, nfc_worker->context);
                            }

                            nfc_worker_mf_classic_key_attack(nfc_worker, found_key, &tx_rx, i + 1);
                            break;
                        }
                        nfc_worker_mf_classic_key_attack(nfc_worker, key, &tx_rx, i + 1);
                    }
                    furry_hal_nfc_sleep();
                    deactivated = true;
                } else {
                    // If the key A is marked as found and matches the searching key, invalidate it
                    MfClassicSectorTrailer* sec_trailer =
                        mf_classic_get_sector_trailer_by_sector(data, i);

                    uint8_t current_key[6];
                    nfc_util_num2bytes(key, 6, current_key);

                    if(mf_classic_is_key_found(data, i, MfClassicKeyA) &&
                       memcmp(sec_trailer->key_a, current_key, 6) == 0) {
                        if(!mf_classic_authenticate_skip_activate(
                               &tx_rx, block_num, key, MfClassicKeyA, !deactivated, cuid)) {
                            mf_classic_set_key_not_found(data, i, MfClassicKeyA);
                            FURI_LOG_D(TAG, "Key %dA not found in attack", i);
                        }
                    }
                    furry_hal_nfc_sleep();
                    deactivated = true;
                }
                if(!mf_classic_is_key_found(data, i, MfClassicKeyB)) {
                    if(mf_classic_authenticate_skip_activate(
                           &tx_rx, block_num, key, MfClassicKeyB, !deactivated, cuid)) { //-V547
                        FURI_LOG_D(TAG, "Key B found: %012llX", key);
                        mf_classic_set_key_found(data, i, MfClassicKeyB, key);
                        nfc_worker->callback(NfcWorkerEventFoundKeyB, nfc_worker->context);
                        nfc_worker_mf_classic_key_attack(nfc_worker, key, &tx_rx, i + 1);
                    }
                    deactivated = true; //-V1048
                } else {
                    // If the key B is marked as found and matches the searching key, invalidate it
                    MfClassicSectorTrailer* sec_trailer =
                        mf_classic_get_sector_trailer_by_sector(data, i);

                    uint8_t current_key[6];
                    nfc_util_num2bytes(key, 6, current_key);

                    if(mf_classic_is_key_found(data, i, MfClassicKeyB) &&
                       memcmp(sec_trailer->key_b, current_key, 6) == 0) {
                        if(!mf_classic_authenticate_skip_activate(
                               &tx_rx, block_num, key, MfClassicKeyB, !deactivated, cuid)) { //-V547
                            mf_classic_set_key_not_found(data, i, MfClassicKeyB);
                            FURI_LOG_D(TAG, "Key %dB not found in attack", i);
                        }
                        furry_hal_nfc_sleep();
                        deactivated = true; //-V1048
                    }
                }
                if(mf_classic_is_key_found(data, i, MfClassicKeyA) &&
                   mf_classic_is_key_found(data, i, MfClassicKeyB))
                    break;
                if(nfc_worker->state != NfcWorkerStateMfClassicDictAttack) break;
            } else {
                if(!card_removed_notified) {
                    nfc_worker->callback(NfcWorkerEventNoCardDetected, nfc_worker->context);
                    card_removed_notified = true;
                    card_found_notified = false;
                }
                if(nfc_worker->state != NfcWorkerStateMfClassicDictAttack) break;
            }
            prev_key = key;
        }
        if(nfc_worker->state != NfcWorkerStateMfClassicDictAttack) break;
        mf_classic_read_sector(&tx_rx, data, i);
        mf_classic_dict_rewind(dict);
    }
    if(nfc_worker->state == NfcWorkerStateMfClassicDictAttack) {
        nfc_worker->callback(NfcWorkerEventSuccess, nfc_worker->context);
    } else {
        nfc_worker->callback(NfcWorkerEventAborted, nfc_worker->context);
    }
}
