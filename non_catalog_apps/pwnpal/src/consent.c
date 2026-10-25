#include "../include/consent.h"

#include <storage/storage.h>
#include <datetime/datetime.h>
#include <furi_hal_rtc.h>

// same data dir as the persona; created lazily on record
#define CONSENT_DIR "/ext/apps_data/pwnpal"

static uint64_t consent_now_unix(void) {
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    return (uint64_t)datetime_datetime_to_timestamp(&dt);
}

bool consent_is_given(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool given = false;

    if(storage_file_open(file, PWNPAL_CONSENT_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        PwnpalConsent c;
        uint16_t read = storage_file_read(file, &c, sizeof(c));
        given =
            (read == sizeof(c) && c.magic == PWNPAL_CONSENT_MAGIC &&
             c.version == PWNPAL_CONSENT_VERSION);
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return given;
}

void consent_record(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, CONSENT_DIR);

    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, PWNPAL_CONSENT_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        PwnpalConsent c = {
            .magic = PWNPAL_CONSENT_MAGIC,
            .version = PWNPAL_CONSENT_VERSION,
            .accepted_unix = consent_now_unix(),
        };
        storage_file_write(file, &c, sizeof(c));
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}
