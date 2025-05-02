#include "seos_emulator_i.h"

#define TAG "SeosEmulator"

#define NAD_MASK             0x08
#define SEADER_PATH          "/ext/apps_data/seader"
#define SEADER_APP_EXTENSION ".credential"

static uint8_t select_header[] = {0x00, 0xa4, 0x04, 0x00};
static uint8_t standard_seos_aid[] = {0xa0, 0x00, 0x00, 0x04, 0x40, 0x00, 0x01, 0x01, 0x00, 0x01};
static uint8_t MOBILE_SEOS_ADMIN_CARD[] =
    {0xa0, 0x00, 0x00, 0x03, 0x82, 0x00, 0x2d, 0x00, 0x01, 0x01};
static uint8_t OPERATION_SELECTOR[] = {0xa0, 0x00, 0x00, 0x03, 0x82, 0x00, 0x2f, 0x00, 0x01, 0x01};
static uint8_t OPERATION_SELECTOR_POST_RESET[] =
    {0xa0, 0x00, 0x00, 0x03, 0x82, 0x00, 0x31, 0x00, 0x01, 0x01};

static uint8_t SEOS_APPLET_FCI[] =
    {0x6F, 0x0C, 0x84, 0x0A, 0xA0, 0x00, 0x00, 0x04, 0x40, 0x00, 0x01, 0x01, 0x00, 0x01};
static uint8_t FILE_NOT_FOUND[] = {0x6A, 0x82};
static uint8_t success[] = {0x90, 0x00};

static uint8_t select_adf_header[] = {0x80, 0xa5, 0x04, 0x00};
static uint8_t general_authenticate_1[] =
    {0x00, 0x87, 0x00, 0x01, 0x04, 0x7c, 0x02, 0x81, 0x00, 0x00};
static uint8_t general_authenticate_1_response_header[] = {0x7c, 0x0a, 0x81, 0x08};
static uint8_t general_authenticate_2_header[] = {0x00, 0x87, 0x00, 0x01};
static uint8_t secure_messaging_header[] = {0x0c, 0xcb, 0x3f, 0xff};
static uint8_t empty[16] =
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

SeosEmulator* seos_emulator_alloc(SeosCredential* credential) {
    SeosEmulator* seos_emulator = malloc(sizeof(SeosEmulator));
    memset(seos_emulator, 0, sizeof(SeosEmulator));

    // Using DES for greater compatibilty
    seos_emulator->params.cipher = TWO_KEY_3DES_CBC_MODE;
    seos_emulator->params.hash = SHA1;

    memset(seos_emulator->params.rndICC, 0x0d, sizeof(seos_emulator->params.rndICC));
    memset(seos_emulator->params.rNonce, 0x0c, sizeof(seos_emulator->params.rNonce));
    seos_emulator->credential = credential;

    seos_emulator->secure_messaging = NULL;

    seos_emulator->storage = furi_record_open(RECORD_STORAGE);
    seos_emulator->dialogs = furi_record_open(RECORD_DIALOGS);
    seos_emulator->load_path = furi_string_alloc();
    seos_emulator->tx_buffer = bit_buffer_alloc(SEOS_WORKER_MAX_BUFFER_SIZE);

    return seos_emulator;
}

void seos_emulator_free(SeosEmulator* seos_emulator) {
    furi_assert(seos_emulator);

    if(seos_emulator->secure_messaging) {
        secure_messaging_free(seos_emulator->secure_messaging);
    }

    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_DIALOGS);
    furi_string_free(seos_emulator->load_path);
    bit_buffer_free(seos_emulator->tx_buffer);
    free(seos_emulator);
}

void seos_emulator_set_loading_callback(
    SeosEmulator* seos_emulator,
    SeosLoadingCallback callback,
    void* context) {
    furi_assert(seos_emulator);

    seos_emulator->loading_cb = callback;
    seos_emulator->loading_cb_ctx = context;
}

static bool
    seos_emulator_file_load(SeosEmulator* seos_emulator, FuriString* path, bool show_dialog) {
    bool parsed = false;
    FlipperFormat* file = flipper_format_file_alloc(seos_emulator->storage);
    FuriString* temp_str;
    temp_str = furi_string_alloc();
    bool deprecated_version = false;

    if(seos_emulator->loading_cb) {
        seos_emulator->loading_cb(seos_emulator->loading_cb_ctx, true);
    }

    memset(
        seos_emulator->credential->diversifier, 0, sizeof(seos_emulator->credential->diversifier));
    memset(seos_emulator->credential->sio, 0, sizeof(seos_emulator->credential->sio));
    do {
        if(!flipper_format_file_open_existing(file, furi_string_get_cstr(path))) break;

        // Read and verify file header
        uint32_t version = 0;
        if(!flipper_format_read_header(file, temp_str, &version)) break;
        if(furi_string_cmp_str(temp_str, seos_file_header) || (version != seos_file_version)) {
            deprecated_version = true;
            break;
        }

        if(!flipper_format_read_uint32(
               file,
               "Diversifier Length",
               (uint32_t*)&(seos_emulator->credential->diversifier_len),
               1))
            break;
        if(!flipper_format_read_hex(
               file,
               "Diversifier",
               seos_emulator->credential->diversifier,
               seos_emulator->credential->diversifier_len))
            break;

        if(!flipper_format_read_uint32(
               file, "SIO Length", (uint32_t*)&(seos_emulator->credential->sio_len), 1))
            break;
        if(!flipper_format_read_hex(
               file, "SIO", seos_emulator->credential->sio, seos_emulator->credential->sio_len))
            break;

        // optional
        memset(
            seos_emulator->credential->priv_key, 0, sizeof(seos_emulator->credential->priv_key));
        memset(
            seos_emulator->credential->auth_key, 0, sizeof(seos_emulator->credential->auth_key));
        memset(
            seos_emulator->credential->adf_response,
            0,
            sizeof(seos_emulator->credential->adf_response));
        flipper_format_read_hex(
            file,
            "Priv Key",
            seos_emulator->credential->priv_key,
            sizeof(seos_emulator->credential->priv_key));
        flipper_format_read_hex(
            file,
            "Auth Key",
            seos_emulator->credential->auth_key,
            sizeof(seos_emulator->credential->auth_key));
        if(memcmp(seos_emulator->credential->priv_key, empty, sizeof(empty)) != 0) {
            FURI_LOG_I(TAG, "+ Priv Key");
        }
        if(memcmp(seos_emulator->credential->priv_key, empty, sizeof(empty)) != 0) {
            FURI_LOG_I(TAG, "+ Auth Key");
        }
        flipper_format_read_hex(
            file,
            "ADF Response",
            seos_emulator->credential->adf_response,
            sizeof(seos_emulator->credential->adf_response));
        parsed = true;
    } while(false);

    if(seos_emulator->loading_cb) {
        seos_emulator->loading_cb(seos_emulator->loading_cb_ctx, false);
    }

    if((!parsed) && (show_dialog)) {
        if(deprecated_version) {
            dialog_message_show_storage_error(seos_emulator->dialogs, "File format deprecated");
        } else {
            dialog_message_show_storage_error(seos_emulator->dialogs, "Can not parse\nfile");
        }
    }

    furi_string_free(temp_str);
    flipper_format_free(file);

    return parsed;
}

static bool seos_emulator_file_load_seader(
    SeosEmulator* seos_emulator,
    FuriString* path,
    bool show_dialog) {
    bool parsed = false;
    FlipperFormat* file = flipper_format_file_alloc(seos_emulator->storage);
    FuriString* reason = furi_string_alloc_set("Couldn't load file");
    FuriString* temp_str;
    temp_str = furi_string_alloc();
    const char* seader_file_header = "Flipper Seader Credential";
    const uint32_t seader_file_version = 1;

    if(seos_emulator->loading_cb) {
        seos_emulator->loading_cb(seos_emulator->loading_cb_ctx, true);
    }

    memset(
        seos_emulator->credential->diversifier, 0, sizeof(seos_emulator->credential->diversifier));
    memset(seos_emulator->credential->sio, 0, sizeof(seos_emulator->credential->sio));
    do {
        if(!flipper_format_file_open_existing(file, furi_string_get_cstr(path))) break;

        // Read and verify file header
        uint32_t version = 0;
        if(!flipper_format_read_header(file, temp_str, &version)) break;
        if(furi_string_cmp_str(temp_str, seader_file_header) || (version != seader_file_version)) {
            furi_string_printf(reason, "Deprecated file format");
            break;
        }
        // Don't forget, order of keys is important

        if(!flipper_format_key_exist(file, "SIO")) {
            furi_string_printf(reason, "Missing SIO");
            break;
        }
        seos_emulator->credential->sio_len = 64; // Seader SIO size
        // We can't check the return status because it will be false if less than the requested length of bytes was read.
        flipper_format_read_hex(
            file, "SIO", seos_emulator->credential->sio, seos_emulator->credential->sio_len);
        seos_emulator->credential->sio_len = seos_emulator->credential->sio[1] +
                                             4; // 2 for type and length, 2 for null after SIO data

        // -------------
        if(!flipper_format_key_exist(file, "Diversifier")) {
            furi_string_printf(reason, "Missing Diversifier");
            break;
        }
        seos_emulator->credential->diversifier_len = 8; //Seader diversifier size
        flipper_format_read_hex(
            file,
            "Diversifier",
            seos_emulator->credential->diversifier,
            seos_emulator->credential->diversifier_len);
        uint8_t* end = memchr(seos_emulator->credential->diversifier, 0, 8);
        if(end) {
            seos_emulator->credential->diversifier_len =
                end - seos_emulator->credential->diversifier;
        } // Returns NULL if char cannot be found

        SeosCredential* cred = seos_emulator->credential;
        char display[128 * 2 + 1];

        memset(display, 0, sizeof(display));
        for(uint8_t i = 0; i < cred->sio_len; i++) {
            snprintf(display + (i * 2), sizeof(display), "%02x", cred->sio[i]);
        }
        FURI_LOG_D(TAG, "SIO: %s", display);

        memset(display, 0, sizeof(display));
        for(uint8_t i = 0; i < cred->diversifier_len; i++) {
            snprintf(display + (i * 2), sizeof(display), "%02x", cred->diversifier[i]);
        }
        FURI_LOG_D(TAG, "Diversifier: %s", display);

        parsed = true;
    } while(false);

    if(seos_emulator->loading_cb) {
        seos_emulator->loading_cb(seos_emulator->loading_cb_ctx, false);
    }

    if((!parsed) && (show_dialog)) {
        dialog_message_show_storage_error(seos_emulator->dialogs, furi_string_get_cstr(reason));
    }

    furi_string_free(reason);
    furi_string_free(temp_str);
    flipper_format_free(file);

    return parsed;
}

bool seos_emulator_file_select_seader(SeosEmulator* seos_emulator) {
    furi_assert(seos_emulator);
    bool res = false;

    FuriString* app_folder = furi_string_alloc_set(SEADER_PATH);

    DialogsFileBrowserOptions browser_options;
    dialog_file_browser_set_basic_options(&browser_options, SEADER_APP_EXTENSION, &I_Nfc_10px);
    browser_options.base_path = SEADER_PATH;

    res = dialog_file_browser_show(
        seos_emulator->dialogs, seos_emulator->load_path, app_folder, &browser_options);

    furi_string_free(app_folder);
    if(res) {
        FuriString* filename;
        filename = furi_string_alloc();
        path_extract_filename(seos_emulator->load_path, filename, true);
        strncpy(seos_emulator->name, furi_string_get_cstr(filename), SEOS_FILE_NAME_MAX_LENGTH);
        res = seos_emulator_file_load_seader(seos_emulator, seos_emulator->load_path, true);
        furi_string_free(filename);
    }

    return res;
}

bool seos_emulator_file_select_seos(SeosEmulator* seos_emulator) {
    furi_assert(seos_emulator);
    bool res = false;

    FuriString* seos_app_folder = furi_string_alloc_set(STORAGE_APP_DATA_PATH_PREFIX);

    DialogsFileBrowserOptions browser_options;
    dialog_file_browser_set_basic_options(&browser_options, SEOS_APP_EXTENSION, &I_Nfc_10px);
    browser_options.base_path = STORAGE_APP_DATA_PATH_PREFIX;

    res = dialog_file_browser_show(
        seos_emulator->dialogs, seos_emulator->load_path, seos_app_folder, &browser_options);

    furi_string_free(seos_app_folder);
    if(res) {
        FuriString* filename;
        filename = furi_string_alloc();
        path_extract_filename(seos_emulator->load_path, filename, true);
        strncpy(seos_emulator->name, furi_string_get_cstr(filename), SEOS_FILE_NAME_MAX_LENGTH);
        res = seos_emulator_file_load(seos_emulator, seos_emulator->load_path, true);
        furi_string_free(filename);
    }

    return res;
}

bool seos_emulator_file_select(SeosEmulator* seos_emulator) {
    if(seos_emulator->load_type == SeosLoadSeos) {
        return seos_emulator_file_select_seos(seos_emulator);
    } else if(seos_emulator->load_type == SeosLoadSeader) {
        return seos_emulator_file_select_seader(seos_emulator);
    }
    return false;
}

bool seos_emulator_delete(SeosEmulator* seos_emulator, bool use_load_path) {
    furi_assert(seos_emulator);
    bool deleted = false;
    FuriString* file_path;
    file_path = furi_string_alloc();

    do {
        // Delete original file
        if(use_load_path && !furi_string_empty(seos_emulator->load_path)) {
            furi_string_set(file_path, seos_emulator->load_path);
        } else {
            furi_string_printf(
                file_path, APP_DATA_PATH("%s%s"), seos_emulator->name, SEOS_APP_EXTENSION);
        }
        if(!storage_simply_remove(seos_emulator->storage, furi_string_get_cstr(file_path))) break;
        deleted = true;
    } while(0);

    if(!deleted) {
        dialog_message_show_storage_error(seos_emulator->dialogs, "Can not remove file");
    }

    furi_string_free(file_path);
    return deleted;
}

void seos_emulator_select_aid(BitBuffer* tx_buffer) {
    FURI_LOG_D(TAG, "Select AID");
    bit_buffer_append_bytes(tx_buffer, SEOS_APPLET_FCI, sizeof(SEOS_APPLET_FCI));
}

void seos_emulator_general_authenticate_1(BitBuffer* tx_buffer, AuthParameters params) {
    bit_buffer_append_bytes(
        tx_buffer,
        general_authenticate_1_response_header,
        sizeof(general_authenticate_1_response_header));
    bit_buffer_append_bytes(tx_buffer, params.rndICC, sizeof(params.rndICC));
}

// 0a00
// 00870001 2c7c 2a82 28 bbb4e9156136f27f687e2967865dfe812e33c95ddcf9294a4340d26da3e76db0220d1163c591e5b8 00
bool seos_emulator_general_authenticate_2(
    const uint8_t* buffer,
    size_t buffer_len,
    SeosCredential* credential,
    AuthParameters* params,
    BitBuffer* tx_buffer) {
    FURI_LOG_D(TAG, "seos_emulator_general_authenticate_2");
    UNUSED(buffer_len);

    uint8_t* rx_data = (uint8_t*)buffer;
    uint8_t* cryptogram = rx_data + sizeof(general_authenticate_2_header) + 5;
    size_t encrypted_len = 32;
    uint8_t* mac = cryptogram + encrypted_len;

    params->key_no = rx_data[3];

    if(memcmp(credential->priv_key, empty, sizeof(empty)) == 0) {
        seos_worker_diversify_key(
            SEOS_ADF1_READ,
            credential->diversifier,
            credential->diversifier_len,
            SEOS_ADF_OID,
            SEOS_ADF_OID_LEN,
            params->cipher,
            params->hash,
            params->key_no,
            true,
            params->priv_key);
    } else {
        memcpy(params->priv_key, credential->priv_key, sizeof(params->priv_key));
    }
    if(memcmp(credential->auth_key, empty, sizeof(empty)) == 0) {
        seos_worker_diversify_key(
            SEOS_ADF1_READ,
            credential->diversifier,
            credential->diversifier_len,
            SEOS_ADF_OID,
            SEOS_ADF_OID_LEN,
            params->cipher,
            params->hash,
            params->key_no,
            false,
            params->auth_key);
    } else {
        memcpy(params->auth_key, credential->auth_key, sizeof(params->auth_key));
    }

    uint8_t cmac[16];
    if(params->cipher == AES_128_CBC) {
        aes_cmac(params->auth_key, sizeof(params->auth_key), cryptogram, encrypted_len, cmac);
    } else if(params->cipher == TWO_KEY_3DES_CBC_MODE) {
        des_cmac(params->auth_key, sizeof(params->auth_key), cryptogram, encrypted_len, cmac);
    } else {
        FURI_LOG_W(TAG, "Cipher not matched");
        return false;
    }

    if(memcmp(cmac, mac, SEOS_WORKER_CMAC_SIZE) != 0) {
        FURI_LOG_W(TAG, "Incorrect cryptogram mac %02x... vs %02x...", cmac[0], mac[0]);
        return false;
    }

    uint8_t clear[32];
    if(params->cipher == AES_128_CBC) {
        seos_worker_aes_decrypt(params->priv_key, encrypted_len, cryptogram, clear);
    } else if(params->cipher == TWO_KEY_3DES_CBC_MODE) {
        seos_worker_des_decrypt(params->priv_key, encrypted_len, cryptogram, clear);
    } else {
        FURI_LOG_W(TAG, "Cipher not matched");
    }

    size_t index = 0;
    memcpy(params->UID, clear + index, sizeof(params->UID));
    index += sizeof(params->UID);
    if(memcmp(clear + index, params->rndICC, sizeof(params->rndICC)) != 0) {
        FURI_LOG_W(TAG, "Incorrect rndICC returned");
        return false;
    }
    index += sizeof(params->rndICC);
    memcpy(params->cNonce, clear + index, sizeof(params->cNonce));
    index += sizeof(params->cNonce);

    // Construct response
    uint8_t response_header[] = {0x7c, 0x2a, 0x82, 0x28};
    memset(clear, 0, sizeof(clear));
    memset(cmac, 0, sizeof(cmac));
    index = 0;
    memcpy(clear + index, params->rndICC, sizeof(params->rndICC));
    index += sizeof(params->rndICC);
    memcpy(clear + index, params->UID, sizeof(params->UID));
    index += sizeof(params->UID);
    memcpy(clear + index, params->rNonce, sizeof(params->rNonce));
    index += sizeof(params->rNonce);

    uint8_t encrypted[32];
    if(params->cipher == AES_128_CBC) {
        seos_worker_aes_encrypt(params->priv_key, sizeof(clear), clear, encrypted);

        aes_cmac(params->auth_key, sizeof(params->auth_key), encrypted, sizeof(encrypted), cmac);
    } else if(params->cipher == TWO_KEY_3DES_CBC_MODE) {
        seos_worker_des_encrypt(params->priv_key, sizeof(clear), clear, encrypted);
        des_cmac(params->auth_key, sizeof(params->auth_key), encrypted, sizeof(encrypted), cmac);
    } else {
        FURI_LOG_W(TAG, "Cipher not matched");
    }

    bit_buffer_append_bytes(tx_buffer, response_header, sizeof(response_header));
    bit_buffer_append_bytes(tx_buffer, encrypted, sizeof(encrypted));
    bit_buffer_append_bytes(tx_buffer, cmac, SEOS_WORKER_CMAC_SIZE);

    return true;
}

void seos_emulator_des_adf_payload(SeosCredential* credential, uint8_t* buffer) {
    // Synethic IV
    /// random bytes
    uint8_t rnd[4] = {0, 0, 0, 0};
    uint8_t cmac[8] = {0};
    /// cmac
    des_cmac(SEOS_ADF1_PRIV_MAC, sizeof(SEOS_ADF1_PRIV_MAC), rnd, sizeof(rnd), cmac);
    uint8_t iv[8];
    memcpy(iv + 0, rnd, sizeof(rnd));
    memcpy(iv + sizeof(rnd), cmac, sizeof(iv) - sizeof(rnd));

    // Copy IV to buffer because mbedtls_des3_crypt_cbc mutates it
    memcpy(buffer + 0, iv, sizeof(iv));

    uint8_t clear[0x30];
    memset(clear, 0, sizeof(clear));
    size_t index = 0;

    // OID
    clear[index++] = 0x06;
    clear[index++] = SEOS_ADF_OID_LEN, memcpy(clear + index, SEOS_ADF_OID, SEOS_ADF_OID_LEN);
    index += SEOS_ADF_OID_LEN;
    // diversifier
    clear[index++] = 0xcf;
    clear[index++] = credential->diversifier_len;
    memcpy(clear + index, credential->diversifier, credential->diversifier_len);
    index += credential->diversifier_len;

    mbedtls_des3_context ctx;
    mbedtls_des3_init(&ctx);
    mbedtls_des3_set2key_enc(&ctx, SEOS_ADF1_PRIV_ENC);
    mbedtls_des3_crypt_cbc(
        &ctx, MBEDTLS_DES_ENCRYPT, sizeof(clear), iv, clear, buffer + sizeof(iv));
    mbedtls_des3_free(&ctx);
}

void seos_emulator_aes_adf_payload(SeosCredential* credential, uint8_t* buffer) {
    // Synethic IV
    /// random bytes
    uint8_t rnd[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t cmac[16] = {0};
    /// cmac
    aes_cmac(SEOS_ADF1_PRIV_MAC, sizeof(SEOS_ADF1_PRIV_MAC), rnd, sizeof(rnd), cmac);
    uint8_t iv[16];
    memcpy(iv + 0, rnd, sizeof(rnd));
    memcpy(iv + sizeof(rnd), cmac, sizeof(iv) - sizeof(rnd));

    // Copy IV to buffer because mbedtls_aes_crypt_cbc mutates it
    memcpy(buffer + 0, iv, sizeof(iv));

    uint8_t clear[0x30];
    memset(clear, 0, sizeof(clear));
    size_t index = 0;

    // OID
    clear[index++] = 0x06;
    clear[index++] = SEOS_ADF_OID_LEN;
    memcpy(clear + index, SEOS_ADF_OID, SEOS_ADF_OID_LEN);
    index += SEOS_ADF_OID_LEN;
    // diversifier
    clear[index++] = 0xcf;
    clear[index++] = credential->diversifier_len;
    memcpy(clear + index, credential->diversifier, credential->diversifier_len);
    index += credential->diversifier_len;

    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_enc(&ctx, SEOS_ADF1_PRIV_ENC, sizeof(SEOS_ADF1_PRIV_ENC) * 8);
    mbedtls_aes_crypt_cbc(
        &ctx, MBEDTLS_AES_ENCRYPT, sizeof(clear), iv, clear, buffer + sizeof(iv));
    mbedtls_aes_free(&ctx);
}

void seos_emulator_select_adf(
    AuthParameters* params,
    SeosCredential* credential,
    BitBuffer* tx_buffer) {
    FURI_LOG_D(TAG, "Select ADF");
    // Shortcut if the credential file contained the hardcoded response
    if(credential->adf_response[2] != 0x00 && credential->adf_response[2] == params->cipher) {
        FURI_LOG_I(TAG, "Using hardcoded ADF Response");
        bit_buffer_append_bytes(
            tx_buffer, credential->adf_response, sizeof(credential->adf_response));
        seos_log_bitbuffer(TAG, "Select ADF (0xcd02...)", tx_buffer);
        return;
    }

    size_t prefix_len = bit_buffer_get_size_bytes(tx_buffer);
    size_t des_cryptogram_length = 56;
    size_t aes_cryptogram_length = 64;
    uint8_t header[] = {0xcd, 0x02, params->cipher, params->hash};
    bit_buffer_append_bytes(tx_buffer, header, sizeof(header));

    // cryptogram
    // 06112b0601040181e438010102011801010202 cf 07 3d4c010c71cfa7 e2d0b41a00cc5e494c8d52b6e562592399fe614a
    uint8_t buffer[64];
    uint8_t cmac[16];
    memset(buffer, 0, sizeof(buffer));
    if(params->cipher == AES_128_CBC) {
        uint8_t cryptogram_prefix[] = {0x85, aes_cryptogram_length};
        bit_buffer_append_bytes(tx_buffer, cryptogram_prefix, sizeof(cryptogram_prefix));

        seos_emulator_aes_adf_payload(credential, buffer);
        bit_buffer_append_bytes(tx_buffer, buffer, aes_cryptogram_length);

        aes_cmac(
            SEOS_ADF1_PRIV_MAC,
            sizeof(SEOS_ADF1_PRIV_MAC),
            (uint8_t*)bit_buffer_get_data(tx_buffer) + prefix_len,
            bit_buffer_get_size_bytes(tx_buffer) - prefix_len,
            cmac);
    } else if(params->cipher == TWO_KEY_3DES_CBC_MODE) {
        uint8_t cryptogram_prefix[] = {0x85, des_cryptogram_length};
        bit_buffer_append_bytes(tx_buffer, cryptogram_prefix, sizeof(cryptogram_prefix));

        seos_emulator_des_adf_payload(credential, buffer);
        bit_buffer_append_bytes(tx_buffer, buffer, des_cryptogram_length);

        // +2 / -2 is to ignore iso14a framing
        des_cmac(
            SEOS_ADF1_PRIV_MAC,
            sizeof(SEOS_ADF1_PRIV_MAC),
            (uint8_t*)bit_buffer_get_data(tx_buffer) + prefix_len,
            bit_buffer_get_size_bytes(tx_buffer) - prefix_len,
            cmac);
    }

    uint8_t cmac_prefix[] = {0x8e, 0x08};
    bit_buffer_append_bytes(tx_buffer, cmac_prefix, sizeof(cmac_prefix));
    bit_buffer_append_bytes(tx_buffer, cmac, SEOS_WORKER_CMAC_SIZE);

    seos_log_bitbuffer(TAG, "Select ADF (0xcd02...)", tx_buffer);
}

NfcCommand seos_worker_listener_inspect_reader(Seos* seos) {
    SeosEmulator* seos_emulator = seos->seos_emulator;
    BitBuffer* tx_buffer = seos_emulator->tx_buffer;
    NfcCommand ret = NfcCommandContinue;

    const uint8_t* rx_data = bit_buffer_get_data(seos_emulator->rx_buffer);
    bool NAD = (rx_data[0] & NAD_MASK) == NAD_MASK;
    uint8_t offset = NAD ? 2 : 1;

    // + x to skip stuff before APDU
    const uint8_t* apdu = rx_data + offset;

    if(memcmp(apdu, select_header, sizeof(select_header)) == 0) {
        if(memcmp(
               apdu + sizeof(select_header) + 1, OPERATION_SELECTOR, sizeof(OPERATION_SELECTOR)) ==
           0) {
            uint8_t enableInspection[] = {
                0x6f, 0x08, 0x85, 0x06, 0x02, 0x01, 0x40, 0x02, 0x01, 0x00};

            bit_buffer_append_bytes(tx_buffer, enableInspection, sizeof(enableInspection));
            view_dispatcher_send_custom_event(seos->view_dispatcher, SeosCustomEventAIDSelected);
        } else {
            bit_buffer_append_bytes(tx_buffer, (uint8_t*)FILE_NOT_FOUND, sizeof(FILE_NOT_FOUND));
        }
    } else if(bit_buffer_get_size_bytes(seos_emulator->rx_buffer) > (size_t)(offset + 2)) {
        FURI_LOG_I(TAG, "NFC stop; %d bytes", bit_buffer_get_size_bytes(seos_emulator->rx_buffer));
        ret = NfcCommandStop;
    }

    return ret;
}

NfcCommand seos_worker_listener_process_message(Seos* seos) {
    SeosEmulator* seos_emulator = seos->seos_emulator;
    BitBuffer* tx_buffer = seos_emulator->tx_buffer;
    NfcCommand ret = NfcCommandContinue;

    const uint8_t* rx_data = bit_buffer_get_data(seos_emulator->rx_buffer);
    bool NAD = (rx_data[0] & NAD_MASK) == NAD_MASK;
    uint8_t offset = NAD ? 2 : 1;

    // + x to skip stuff before APDU
    const uint8_t* apdu = rx_data + offset;

    if(memcmp(apdu, select_header, sizeof(select_header)) == 0) {
        if(memcmp(apdu + sizeof(select_header) + 1, standard_seos_aid, sizeof(standard_seos_aid)) ==
           0) {
            seos_emulator_select_aid(seos_emulator->tx_buffer);
            view_dispatcher_send_custom_event(seos->view_dispatcher, SeosCustomEventAIDSelected);

        } else if(
            memcmp(
                apdu + sizeof(select_header) + 1,
                OPERATION_SELECTOR_POST_RESET,
                sizeof(OPERATION_SELECTOR_POST_RESET)) == 0) {
            FURI_LOG_I(TAG, "OPERATION_SELECTOR_POST_RESET");
            bit_buffer_append_bytes(
                seos_emulator->tx_buffer, (uint8_t*)FILE_NOT_FOUND, sizeof(FILE_NOT_FOUND));
        } else if(
            memcmp(
                apdu + sizeof(select_header) + 1,
                OPERATION_SELECTOR,
                sizeof(OPERATION_SELECTOR)) == 0) {
            FURI_LOG_I(TAG, "OPERATION_SELECTOR");
            bit_buffer_append_bytes(
                seos_emulator->tx_buffer, (uint8_t*)FILE_NOT_FOUND, sizeof(FILE_NOT_FOUND));
        } else if(
            memcmp(
                apdu + sizeof(select_header) + 1,
                MOBILE_SEOS_ADMIN_CARD,
                sizeof(MOBILE_SEOS_ADMIN_CARD)) == 0) {
            FURI_LOG_I(TAG, "MOBILE_SEOS_ADMIN_CARD");
            bit_buffer_append_bytes(
                seos_emulator->tx_buffer, (uint8_t*)FILE_NOT_FOUND, sizeof(FILE_NOT_FOUND));
        } else {
            seos_log_bitbuffer(TAG, "Reject select", seos_emulator->rx_buffer);
            bit_buffer_append_bytes(
                seos_emulator->tx_buffer, (uint8_t*)FILE_NOT_FOUND, sizeof(FILE_NOT_FOUND));
        }
    } else if(memcmp(apdu, select_adf_header, sizeof(select_adf_header)) == 0) {
        // is our adf in the list?
        // +1 to skip APDU length byte
        void* p = memmem(
            apdu + sizeof(select_adf_header) + 1,
            apdu[sizeof(select_adf_header)],
            SEOS_ADF_OID,
            SEOS_ADF_OID_LEN);
        if(p) {
            BitBuffer* tmp = bit_buffer_alloc(SEOS_ADF_OID_LEN);
            bit_buffer_append_bytes(tmp, p, SEOS_ADF_OID_LEN);
            seos_log_bitbuffer(TAG, "Matched ADF", tmp);
            bit_buffer_free(tmp);
            view_dispatcher_send_custom_event(seos->view_dispatcher, SeosCustomEventADFMatched);

            seos_emulator_select_adf(
                &seos_emulator->params, seos_emulator->credential, seos_emulator->tx_buffer);
        } else {
            FURI_LOG_W(TAG, "Failed to match any ADF OID");
        }
    } else if(memcmp(apdu, general_authenticate_1, sizeof(general_authenticate_1)) == 0) {
        seos_emulator_general_authenticate_1(seos_emulator->tx_buffer, seos_emulator->params);
    } else if(memcmp(apdu, general_authenticate_2_header, sizeof(general_authenticate_2_header)) == 0) {
        if(!seos_emulator_general_authenticate_2(
               apdu,
               bit_buffer_get_size_bytes(seos_emulator->rx_buffer),
               seos_emulator->credential,
               &seos_emulator->params,
               seos_emulator->tx_buffer)) {
            FURI_LOG_W(TAG, "Failure in General Authenticate 2");
            ret = NfcCommandStop;
            return ret;
        }
        view_dispatcher_send_custom_event(seos->view_dispatcher, SeosCustomEventAuthenticated);
        // Prepare for future communication
        seos_emulator->secure_messaging = secure_messaging_alloc(&seos_emulator->params);
    } else if(memcmp(apdu, secure_messaging_header, sizeof(secure_messaging_header)) == 0) {
        uint8_t request_sio[] = {0x5c, 0x02, 0xff, 0x00};

        if(seos_emulator->secure_messaging) {
            FURI_LOG_D(TAG, "Unwrap secure message");

            // 0b00 0ccb3fff 16 8508fa8395d30de4e8e097008e085da7edbd833b002d00
            // Ignore 2 iso frame bytes
            size_t bytes_to_ignore = offset;
            BitBuffer* tmp = bit_buffer_alloc(bit_buffer_get_size_bytes(seos_emulator->rx_buffer));
            bit_buffer_append_bytes(
                tmp,
                bit_buffer_get_data(seos_emulator->rx_buffer) + bytes_to_ignore,
                bit_buffer_get_size_bytes(seos_emulator->rx_buffer) - bytes_to_ignore);

            seos_log_bitbuffer(TAG, "NFC received(wrapped)", tmp);
            secure_messaging_unwrap_apdu(seos_emulator->secure_messaging, tmp);
            seos_log_bitbuffer(TAG, "NFC received(clear)", tmp);

            const uint8_t* message = bit_buffer_get_data(tmp);
            if(memcmp(message, request_sio, sizeof(request_sio)) == 0) {
                view_dispatcher_send_custom_event(
                    seos->view_dispatcher, SeosCustomEventSIORequested);
                BitBuffer* sio_file = bit_buffer_alloc(128);
                bit_buffer_append_bytes(sio_file, message + 2, 2); // fileId
                bit_buffer_append_byte(sio_file, seos_emulator->credential->sio_len);
                bit_buffer_append_bytes(
                    sio_file, seos_emulator->credential->sio, seos_emulator->credential->sio_len);

                secure_messaging_wrap_rapdu(
                    seos_emulator->secure_messaging,
                    (uint8_t*)bit_buffer_get_data(sio_file),
                    bit_buffer_get_size_bytes(sio_file),
                    tx_buffer);

                bit_buffer_free(sio_file);
            }

            bit_buffer_free(tmp);
        } else {
            uint8_t no_sm[] = {0x69, 0x88};
            bit_buffer_append_bytes(tx_buffer, no_sm, sizeof(no_sm));
        }
    } else {
        // I'm trying to find a good place to re-assert that we're emulating so we don't get stuck on a previous UI screen when we emulate repeatedly
        view_dispatcher_send_custom_event(seos->view_dispatcher, SeosCustomEventEmulate);
    }

    return ret;
}

NfcCommand seos_worker_listener_callback(NfcGenericEvent event, void* context) {
    furi_assert(context);
    furi_assert(event.protocol == NfcProtocolIso14443_4a);
    furi_assert(event.event_data);
    Seos* seos = context;
    SeosEmulator* seos_emulator = seos->seos_emulator;

    NfcCommand ret = NfcCommandContinue;
    Iso14443_4aListenerEvent* iso14443_4a_event = event.event_data;
    Iso14443_3aListener* iso14443_listener = event.instance;
    seos_emulator->iso14443_listener = iso14443_listener;

    BitBuffer* tx_buffer = seos_emulator->tx_buffer;
    bit_buffer_reset(tx_buffer);

    switch(iso14443_4a_event->type) {
    case Iso14443_4aListenerEventTypeReceivedData:
        seos_emulator->rx_buffer = iso14443_4a_event->data->buffer;
        const uint8_t* rx_data = bit_buffer_get_data(seos_emulator->rx_buffer);
        bool NAD = (rx_data[0] & NAD_MASK) == NAD_MASK;
        uint8_t offset = NAD ? 2 : 1;

        if(bit_buffer_get_size_bytes(iso14443_4a_event->data->buffer) == offset) {
            FURI_LOG_I(TAG, "No contents in frame");
            break;
        }

        seos_log_bitbuffer(TAG, "NFC received", seos_emulator->rx_buffer);

        // Some ISO14443a framing I need to figure out
        bit_buffer_append_bytes(tx_buffer, rx_data, offset);

        if(seos->flow_mode == FLOW_CRED) {
            ret = seos_worker_listener_process_message(seos);
        } else if(seos->flow_mode == FLOW_INSPECT) {
            ret = seos_worker_listener_inspect_reader(seos);
        }

        if(bit_buffer_get_size_bytes(seos_emulator->tx_buffer) >
           offset) { // contents belong iso framing

            if(memcmp(
                   FILE_NOT_FOUND,
                   bit_buffer_get_data(tx_buffer) + bit_buffer_get_size_bytes(tx_buffer) -
                       sizeof(FILE_NOT_FOUND),
                   sizeof(FILE_NOT_FOUND)) != 0) {
                bit_buffer_append_bytes(tx_buffer, success, sizeof(success));
            }

            iso14443_crc_append(Iso14443CrcTypeA, tx_buffer);

            seos_log_bitbuffer(TAG, "NFC transmit", seos_emulator->tx_buffer);

            NfcError error = nfc_listener_tx((Nfc*)iso14443_listener, tx_buffer);
            if(error != NfcErrorNone) {
                FURI_LOG_W(TAG, "Tx error: %d", error);
                break;
            }
        } else {
            iso14443_crc_append(Iso14443CrcTypeA, tx_buffer);

            seos_log_bitbuffer(TAG, "NFC transmit", seos_emulator->tx_buffer);

            NfcError error = nfc_listener_tx((Nfc*)iso14443_listener, tx_buffer);
            if(error != NfcErrorNone) {
                FURI_LOG_W(TAG, "Tx error: %d", error);
                break;
            }
        }
        break;
    case Iso14443_4aListenerEventTypeHalted:
        FURI_LOG_I(TAG, "Halted");
        break;
    case Iso14443_4aListenerEventTypeFieldOff:
        FURI_LOG_I(TAG, "Field Off");
        break;
    }

    if(ret == NfcCommandStop) {
        view_dispatcher_send_custom_event(seos->view_dispatcher, SeosCustomEventReaderError);
    }
    return ret;
}
