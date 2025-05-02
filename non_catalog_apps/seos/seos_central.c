#include "seos_central_i.h"

#define TAG "SeosCentral"

static uint8_t success[] = {0x90, 0x00};
static uint8_t file_not_found[] = {0x6A, 0x82};

static uint8_t select_header[] = {0x00, 0xa4, 0x04, 0x00};
static uint8_t standard_seos_aid[] = {0xa0, 0x00, 0x00, 0x04, 0x40, 0x00, 0x01, 0x01, 0x00, 0x01};
static uint8_t select_adf_header[] = {0x80, 0xa5, 0x04, 0x00};
static uint8_t general_authenticate_1[] =
    {0x00, 0x87, 0x00, 0x01, 0x04, 0x7c, 0x02, 0x81, 0x00, 0x00};
static uint8_t general_authenticate_2_header[] = {0x00, 0x87, 0x00, 0x01};
static uint8_t secure_messaging_header[] = {0x0c, 0xcb, 0x3f, 0xff};

SeosCentral* seos_central_alloc(Seos* seos) {
    SeosCentral* seos_central = malloc(sizeof(SeosCentral));
    memset(seos_central, 0, sizeof(SeosCentral));
    seos_central->seos = seos;
    seos_central->credential = seos->credential;

    seos_central->phase = SELECT_AID;
    // Using DES for greater compatibilty
    seos_central->params.cipher = TWO_KEY_3DES_CBC_MODE;
    seos_central->params.hash = SHA1;

    memset(seos_central->params.rndICC, 0x0d, sizeof(seos_central->params.rndICC));
    memset(seos_central->params.rNonce, 0x0c, sizeof(seos_central->params.rNonce));

    seos_central->secure_messaging = NULL;

    seos_central->seos_att = seos_att_alloc(seos);
    seos_att_set_notify_callback(seos_central->seos_att, seos_central_notify, seos_central);

    return seos_central;
}

void seos_central_free(SeosCentral* seos_central) {
    furi_assert(seos_central);
    seos_att_free(seos_central->seos_att);
    free(seos_central);
}

void seos_central_start(SeosCentral* seos_central, FlowMode mode) {
    seos_att_start(seos_central->seos_att, BLE_CENTRAL, mode);
}

void seos_central_stop(SeosCentral* seos_central) {
    seos_att_stop(seos_central->seos_att);
}

void seos_central_notify(void* context, const uint8_t* buffer, size_t buffer_len) {
    SeosCentral* seos_central = (SeosCentral*)context;
    seos_log_buffer(TAG, "notify", (uint8_t*)buffer, buffer_len);
    const uint8_t* data = buffer;
    if(data[0] == 0xe1) {
        FURI_LOG_W(TAG, "Reader BLE Error code: %02x", data[1]);
        return;
    }

    BitBuffer* response = bit_buffer_alloc(128);

    if(data[0] != BLE_START) {
        FURI_LOG_W(TAG, "Unexpected start of BLE packet");
    }
    const uint8_t* apdu = data + 1; // Match name to nfc version for easier copying

    if(memcmp(apdu, select_header, sizeof(select_header)) == 0) {
        if(memcmp(apdu + sizeof(select_header) + 1, standard_seos_aid, sizeof(standard_seos_aid)) ==
           0) {
            bit_buffer_append_byte(response, BLE_START);
            seos_emulator_select_aid(response);
            bit_buffer_append_bytes(response, (uint8_t*)success, sizeof(success));
            seos_central->phase = SELECT_ADF;
        } else {
            bit_buffer_append_byte(response, BLE_START);
            bit_buffer_append_bytes(response, (uint8_t*)file_not_found, sizeof(file_not_found));
        }
    } else if(memcmp(apdu, select_adf_header, sizeof(select_adf_header)) == 0) {
        const uint8_t* oid_list = apdu + sizeof(select_adf_header) + 1;
        size_t oid_list_len = apdu[sizeof(select_adf_header)];

        bit_buffer_append_byte(response, BLE_START);

        if(seos_emulator_select_adf(
               oid_list, oid_list_len, &seos_central->params, seos_central->credential, response)) {
            bit_buffer_append_bytes(response, (uint8_t*)success, sizeof(success));
            seos_central->phase = GENERAL_AUTHENTICATION_1;
        } else {
            FURI_LOG_W(TAG, "Failed to match any ADF OID");
        }
    } else if(memcmp(apdu, general_authenticate_1, sizeof(general_authenticate_1)) == 0) {
        bit_buffer_append_byte(response, BLE_START);

        seos_emulator_general_authenticate_1(response, seos_central->params);

        bit_buffer_append_bytes(response, (uint8_t*)success, sizeof(success));
        seos_central->phase = GENERAL_AUTHENTICATION_2;
    } else if(memcmp(apdu, general_authenticate_2_header, sizeof(general_authenticate_2_header)) == 0) {
        bit_buffer_append_byte(response, BLE_START);

        if(seos_emulator_general_authenticate_2(
               apdu, buffer_len - 1, seos_central->credential, &seos_central->params, response)) {
            FURI_LOG_I(TAG, "Authenticated");

            view_dispatcher_send_custom_event(
                seos_central->seos->view_dispatcher, SeosCustomEventAuthenticated);
            seos_central->secure_messaging = secure_messaging_alloc(&seos_central->params);
            bit_buffer_append_bytes(response, (uint8_t*)success, sizeof(success));
        } else {
            bit_buffer_reset(response);
        }
        seos_central->phase = REQUEST_SIO;
    } else if(memcmp(apdu, secure_messaging_header, sizeof(secure_messaging_header)) == 0) {
        uint8_t request_sio[] = {0x5c, 0x02, 0xff, 0x00};

        bit_buffer_append_byte(response, BLE_START);

        if(seos_central->secure_messaging) {
            FURI_LOG_D(TAG, "Unwrap secure message");

            // c0 0ccb3fff 16 8508fa8395d30de4e8e097008e085da7edbd833b002d00
            // Ignore 1 BLE_START byte
            size_t bytes_to_ignore = 1;
            BitBuffer* tmp = bit_buffer_alloc(buffer_len);
            bit_buffer_append_bytes(tmp, buffer + bytes_to_ignore, buffer_len - bytes_to_ignore);

            seos_log_bitbuffer(TAG, "NFC received(wrapped)", tmp);
            secure_messaging_unwrap_apdu(seos_central->secure_messaging, tmp);
            seos_log_bitbuffer(TAG, "NFC received(clear)", tmp);

            const uint8_t* message = bit_buffer_get_data(tmp);
            if(memcmp(message, request_sio, sizeof(request_sio)) == 0) {
                view_dispatcher_send_custom_event(
                    seos_central->seos->view_dispatcher, SeosCustomEventSIORequested);
                BitBuffer* sio_file = bit_buffer_alloc(128);
                bit_buffer_append_bytes(sio_file, message + 2, 2); // fileId
                bit_buffer_append_byte(sio_file, seos_central->credential->sio_len);
                bit_buffer_append_bytes(
                    sio_file, seos_central->credential->sio, seos_central->credential->sio_len);

                seos_log_bitbuffer(TAG, "sio_file", sio_file);
                secure_messaging_wrap_rapdu(
                    seos_central->secure_messaging,
                    (uint8_t*)bit_buffer_get_data(sio_file),
                    bit_buffer_get_size_bytes(sio_file),
                    response);

                bit_buffer_free(sio_file);
            } else {
                FURI_LOG_W(TAG, "Did not match the cleartext request");
            }
            bit_buffer_append_bytes(response, (uint8_t*)success, sizeof(success));

            bit_buffer_free(tmp);
        } else {
            uint8_t no_sm[] = {0x69, 0x88};
            bit_buffer_append_bytes(response, no_sm, sizeof(no_sm));
        }

    } else {
        FURI_LOG_W(TAG, "no match for attribute_value");
    }

    if(bit_buffer_get_size_bytes(response) > 0) {
        seos_att_write_request(seos_central->seos_att, response);
    }
    bit_buffer_free(response);
}
