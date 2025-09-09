#include "weebo_common.h"

#define TAG "WeeboCommon"

void weebo_calculate_pwd(uint8_t* uid, uint8_t* pwd) {
    pwd[0] = uid[1] ^ uid[3] ^ 0xAA;
    pwd[1] = uid[2] ^ uid[4] ^ 0x55;
    pwd[2] = uid[3] ^ uid[5] ^ 0xAA;
    pwd[3] = uid[4] ^ uid[6] ^ 0x55;
}

void weebo_remix(Weebo* weebo) {
    uint8_t PWD[4];
    uint8_t UID[8];
    uint8_t modified[NTAG215_SIZE];
    MfUltralightData* data = mf_ultralight_alloc();
    nfc_device_copy_data(weebo->nfc_device, NfcProtocolMfUltralight, data);

    //random uid
    FURI_LOG_D(TAG, "Generating random UID");
    UID[0] = 0x04;
    furi_hal_random_fill_buf(UID + 1, 6);
    UID[3] |= 0x01; // To avoid forbidden 0x88 value
    UID[7] = UID[3] ^ UID[4] ^ UID[5] ^ UID[6];
    memcpy(weebo->figure + NFC3D_UID_OFFSET, UID, 8);
    memcpy(data->iso14443_3a_data->uid, UID, 7);

    //pack
    nfc3d_amiibo_pack(&weebo->keys, weebo->figure, modified);

    //copy data in
    for(size_t i = 0; i < 130; i++) {
        memcpy(
            data->page[i].data, modified + i * MF_ULTRALIGHT_PAGE_SIZE, MF_ULTRALIGHT_PAGE_SIZE);
    }

    //new pwd
    weebo_calculate_pwd(data->iso14443_3a_data->uid, PWD);
    memcpy(data->page[133].data, PWD, sizeof(PWD));

    //set data
    nfc_device_set_data(weebo->nfc_device, NfcProtocolMfUltralight, data);

    mf_ultralight_free(data);
}
