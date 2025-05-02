#pragma once

#include <storage/storage.h>
#include <dialogs/dialogs.h>
#include <lib/toolbox/path.h>
#include <lib/nfc/protocols/nfc_generic_event.h>
#include <lib/nfc/protocols/iso14443_4a/iso14443_4a_listener.h>
#include <lib/nfc/helpers/iso14443_crc.h>
#include <mbedtls/des.h>
#include <mbedtls/aes.h>

#include "secure_messaging.h"

typedef void (*SeosLoadingCallback)(void* context, bool state);

typedef struct {
    Iso14443_3aListener* iso14443_listener;
    BitBuffer* tx_buffer;
    BitBuffer* rx_buffer;

    AuthParameters params;

    SecureMessaging* secure_messaging;

    SeosCredential* credential;

    char name[SEOS_FILE_NAME_MAX_LENGTH + 1];
    FuriString* load_path;
    SeosLoadingCallback loading_cb;
    void* loading_cb_ctx;
    Storage* storage;
    DialogsApp* dialogs;
    enum {
        SeosLoadSeos,
        SeosLoadSeader
    } load_type;
} SeosEmulator;

NfcCommand seos_worker_listener_callback(NfcGenericEvent event, void* context);

SeosEmulator* seos_emulator_alloc(SeosCredential* credential);

void seos_emulator_free(SeosEmulator* seos_emulator);

void seos_emulator_set_loading_callback(
    SeosEmulator* seos_emulator,
    SeosLoadingCallback callback,
    void* context);

bool seos_emulator_file_select(SeosEmulator* seos_emulator);
bool seos_emulator_delete(SeosEmulator* seos_emulator, bool use_load_path);

void seos_emulator_general_authenticate_1(BitBuffer* tx_buffer, AuthParameters params);
bool seos_emulator_general_authenticate_2(
    const uint8_t* buffer,
    size_t buffer_len,
    SeosCredential* credential,
    AuthParameters* params,
    BitBuffer* tx_buffer);

void seos_emulator_select_aid(BitBuffer* tx_buffer);
void seos_emulator_select_adf(
    AuthParameters* params,
    SeosCredential* credential,
    BitBuffer* tx_buffer);
