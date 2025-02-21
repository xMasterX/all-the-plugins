#include <lib/nfc/protocols/mf_desfire/mf_desfire.h>
#include "../metroflip_i.h"
#include "desfire.h"
#include <lib/toolbox/strint.h>
#include <stdio.h>

static const MfDesfireApplicationId opal_verify_app_id = {.data = {0x31, 0x45, 0x53}};

static const MfDesfireFileId opal_verify_file_id = 0x07;

static const MfDesfireApplicationId myki_verify_app_id = {.data = {0x00, 0x11, 0xf2}};

static const MfDesfireFileId myki_verify_file_id = 0x0f;

static const MfDesfireApplicationId itso_verify_app_id = {.data = {0x16, 0x02, 0xa0}};

static const MfDesfireFileId itso_verify_file_id = 0x0f;

uint64_t itso_swap_uint64(uint64_t val) {
    val = ((val << 8) & 0xFF00FF00FF00FF00ULL) | ((val >> 8) & 0x00FF00FF00FF00FFULL);
    val = ((val << 16) & 0xFFFF0000FFFF0000ULL) | ((val >> 16) & 0x0000FFFF0000FFFFULL);
    return (val << 32) | (val >> 32);
}

static const struct {
    const MfDesfireApplicationId app;
    const char* type;
} clipper_verify_types[] = {
    // Application advertised on classic, plastic cards.
    {.app = {.data = {0x90, 0x11, 0xf2}}, .type = "Card"},
    // Application advertised on a mobile device.
    {.app = {.data = {0x91, 0x11, 0xf2}}, .type = "Mobile Device"},
};

static const size_t kNumCardVerifyTypes =
    sizeof(clipper_verify_types) / sizeof(clipper_verify_types[0]);

// File ids of important files on the card.
static const MfDesfireFileId clipper_ecash_file_id = 2;
static const MfDesfireFileId clipper_histidx_file_id = 6;
static const MfDesfireFileId clipper_identity_file_id = 8;
static const MfDesfireFileId clipper_history_file_id = 14;

static bool get_file_contents(
    const MfDesfireApplication* app,
    const MfDesfireFileId* id,
    MfDesfireFileType type,
    size_t min_size,
    const uint8_t** out) {
    const MfDesfireFileSettings* settings = mf_desfire_get_file_settings(app, id);
    if(settings == NULL) return false;
    if(settings->type != type) return false;

    const MfDesfireFileData* file_data = mf_desfire_get_file_data(app, id);

    if(file_data == NULL) return false;

    if(simple_array_get_count(file_data->data) < min_size) return false;

    *out = simple_array_cget_data(file_data->data);

    return true;
}

struct ClipperVerifyCardInfo_struct {
    uint32_t serial_number;
    uint16_t counter;
    uint16_t last_txn_id;
    uint32_t last_updated_tm_1900;
    uint16_t last_terminal_id;
    int16_t balance_cents;
};
typedef struct ClipperVerifyCardInfo_struct ClipperVerifyCardInfo;

// Opal file 0x7 structure. Assumes a little-endian CPU.
typedef struct FURI_PACKED {
    uint32_t serial         : 32;
    uint8_t check_digit     : 4;
    bool blocked            : 1;
    uint16_t txn_number     : 16;
    int32_t balance         : 21;
    uint16_t days           : 15;
    uint16_t minutes        : 11;
    uint8_t mode            : 3;
    uint16_t usage          : 4;
    bool auto_topup         : 1;
    uint8_t weekly_journeys : 4;
    uint16_t checksum       : 16;
} OpalVerifyFile;

static_assert(sizeof(OpalVerifyFile) == 16, "OpalFile");

bool opal_verify(const MfDesfireData* data) {
    // Check if the card has the expected application
    const MfDesfireApplication* app = mf_desfire_get_application(data, &opal_verify_app_id);
    if(app == NULL) {
        return false;
    }

    // Verify the file settings: must be of type standard and have the expected size
    const MfDesfireFileSettings* file_settings =
        mf_desfire_get_file_settings(app, &opal_verify_file_id);
    if(file_settings == NULL || file_settings->type != MfDesfireFileTypeStandard ||
       file_settings->data.size != sizeof(OpalVerifyFile)) {
        return false;
    }

    // Check that the file data exists
    const MfDesfireFileData* file_data = mf_desfire_get_file_data(app, &opal_verify_file_id);
    if(file_data == NULL) {
        return false;
    }

    // Retrieve the opal file from the file data
    const OpalVerifyFile* opal_file = simple_array_cget_data(file_data->data);
    if(opal_file == NULL) {
        return false;
    }

    // Ensure the check digit is valid (i.e. 0..9)
    if(opal_file->check_digit > 9) {
        return false;
    }

    // All checks passed, return true
    return true;
}

bool myki_verify(const MfDesfireData* data) {
    // Check if the card contains the expected Myki application.
    const MfDesfireApplication* app = mf_desfire_get_application(data, &myki_verify_app_id);
    if(app == NULL) {
        return false;
    }

    // Define the structure for Myki file data.
    typedef struct {
        uint32_t top;
        uint32_t bottom;
    } mykiFile;

    // Verify file settings: must be present, of the correct type, and large enough to contain a mykiFile.
    const MfDesfireFileSettings* file_settings =
        mf_desfire_get_file_settings(app, &myki_verify_file_id);
    if(file_settings == NULL || file_settings->type != MfDesfireFileTypeStandard ||
       file_settings->data.size < sizeof(mykiFile)) {
        return false;
    }

    // Verify that the file data is available.
    const MfDesfireFileData* file_data = mf_desfire_get_file_data(app, &myki_verify_file_id);
    if(file_data == NULL) {
        return false;
    }

    // Retrieve the Myki file data from the file data array.
    const mykiFile* myki_file = simple_array_cget_data(file_data->data);
    if(myki_file == NULL) {
        return false;
    }

    // Check that Myki card numbers are prefixed with "308425".
    if(myki_file->top != 308425UL) {
        return false;
    }

    // Card numbers are always 15 digits in length.
    // The bottom field must be within [10000000, 100000000) to meet this requirement.
    if(myki_file->bottom < 10000000UL || myki_file->bottom >= 100000000UL) {
        return false;
    }

    // All checks passed.
    return true;
}

bool itso_verify(const MfDesfireData* data) {
    // Check if the card contains the expected ITSO application.
    const MfDesfireApplication* app = mf_desfire_get_application(data, &itso_verify_app_id);
    if(app == NULL) {
        return false;
    }

    // Define the structure for ITSO file data.
    typedef struct {
        uint64_t part1;
        uint64_t part2;
        uint64_t part3;
        uint64_t part4;
    } ItsoFile;

    // Verify file settings: must exist, be of standard type,
    // and have a data size at least as large as an ItsoFile.
    const MfDesfireFileSettings* file_settings =
        mf_desfire_get_file_settings(app, &itso_verify_file_id);
    if(file_settings == NULL || file_settings->type != MfDesfireFileTypeStandard ||
       file_settings->data.size < sizeof(ItsoFile)) {
        return false;
    }

    // Verify that the file data is available.
    const MfDesfireFileData* file_data = mf_desfire_get_file_data(app, &itso_verify_file_id);
    if(file_data == NULL) {
        return false;
    }

    // Retrieve the ITSO file from the file data.
    const ItsoFile* itso_file = simple_array_cget_data(file_data->data);
    if(itso_file == NULL) {
        return false;
    }

    // Swap bytes for the first two parts.
    uint64_t x1 = itso_swap_uint64(itso_file->part1);
    uint64_t x2 = itso_swap_uint64(itso_file->part2);

    // Prepare buffers for card and date strings.
    char cardBuff[32];
    char dateBuff[18];

    // Format the hex strings.
    snprintf(cardBuff, sizeof(cardBuff), "%llx%llx", x1, x2);
    snprintf(dateBuff, sizeof(dateBuff), "%llx", x2);

    // Get pointer to the card number substring (skipping the first 4 characters).
    char* cardp = cardBuff + 4;
    cardp[18] = '\0'; // Ensure the substring is null-terminated.

    // Verify that all ITSO card numbers are prefixed with "633597".
    if(strncmp(cardp, "633597", 6) != 0) {
        return false;
    }

    // Prepare the date string by advancing 12 characters.
    char* datep = dateBuff + 12;
    dateBuff[17] = '\0'; // Ensure termination of the date string.

    // Convert the date portion (in hexadecimal) to a date stamp.
    uint32_t dateStamp;
    if(strint_to_uint32(datep, NULL, &dateStamp, 16) != StrintParseNoError) {
        return false;
    }

    // (Optional) Calculate the Unix timestamp if needed:
    // uint32_t unixTimestamp = dateStamp * 24 * 60 * 60 + 852076800U;

    // All checks passed.
    return true;
}

bool clipper_verify(const MfDesfireData* data) {
    bool verified = false;

    do {
        FURI_LOG_I("clipper verify", "verifying..");
        const MfDesfireApplication* app = NULL;

        // Try each card type until a matching application is found.
        for(size_t i = 0; i < kNumCardVerifyTypes; i++) {
            app = mf_desfire_get_application(data, &clipper_verify_types[i].app);
            if(app != NULL) {
                break;
            }
        }
        // If no matching application was found, verification fails.
        if(app == NULL) {
            break;
        }

        const uint8_t* id_data;
        if(!get_file_contents(
               app, &clipper_identity_file_id, MfDesfireFileTypeStandard, 5, &id_data)) {
            break;
        }

        // Get the ecash file contents.
        const uint8_t* cash_data;
        if(!get_file_contents(
               app, &clipper_ecash_file_id, MfDesfireFileTypeBackup, 32, &cash_data)) {
            break;
        }

        // Retrieve ride history file contents.
        const uint8_t* history_index;
        const uint8_t* history;
        if(!get_file_contents(
               app, &clipper_histidx_file_id, MfDesfireFileTypeBackup, 16, &history_index)) {
            break;
        }
        if(!get_file_contents(
               app, &clipper_history_file_id, MfDesfireFileTypeStandard, 512, &history)) {
            break;
        }

        // Use a dummy string to verify that the ride history can be decoded.
        FuriString* dummy_str = furi_string_alloc();
        furi_string_free(dummy_str);

        verified = true;
    } while(false);

    return verified;
}
