#include "progress.h"
#include <furi.h>
#include <storage/storage.h>

#define PROGRESS_DIR     EXT_PATH("apps_data/morse_trainer")
#define PROGRESS_PATH    PROGRESS_DIR "/progress.bin"
#define SETTINGS_PATH    PROGRESS_DIR "/settings.bin"
#define PROGRESS_MAGIC   0x544D /* "MT" */
#define PROGRESS_VERSION 2
#define SETTINGS_MAGIC   0x534D /* "MS" */
#define SETTINGS_VERSION 4

/* Older bodies are byte-prefixes of the current structs (see progress.h), so
 * migration = read the shorter body and keep defaults for the new fields. */
#define PROGRESS_V1_BODY (MODE_COUNT + MODE_COUNT * LEVEL_COUNT)
#define SETTINGS_V1_BODY 2
#define SETTINGS_V2_BODY 3
#define SETTINGS_V3_BODY 4

typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint8_t version;
} FileHeader;

static void progress_defaults(MorseProgress* progress) {
    memset(progress, 0, sizeof(MorseProgress));
    for(uint8_t m = 0; m < MODE_COUNT; m++)
        progress->unlocked[m] = 1;
}

static bool progress_valid(const MorseProgress* progress) {
    for(uint8_t m = 0; m < MODE_COUNT; m++) {
        if(progress->unlocked[m] < 1 || progress->unlocked[m] > LEVEL_COUNT) return false;
        for(uint8_t l = 0; l < LEVEL_COUNT; l++) {
            if(progress->stars[m][l] > 3) return false;
        }
    }
    return true;
}

void morse_progress_load(MorseProgress* progress) {
    progress_defaults(progress);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, PROGRESS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        FileHeader header;
        MorseProgress loaded;
        memset(&loaded, 0, sizeof(loaded));
        bool ok = false;
        /* Bad magic/version/values = start fresh; never crash on bad data. */
        if(storage_file_read(file, &header, sizeof(header)) == sizeof(header) &&
           header.magic == PROGRESS_MAGIC) {
            if(header.version == PROGRESS_VERSION) {
                ok = storage_file_read(file, &loaded, sizeof(loaded)) == sizeof(loaded);
            } else if(header.version == 1) {
                /* v1 file: unlocked+stars only; stats fields stay zeroed. */
                ok = storage_file_read(file, &loaded, PROGRESS_V1_BODY) == PROGRESS_V1_BODY;
            }
        }
        if(ok && progress_valid(&loaded)) *progress = loaded;
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

void morse_settings_load(MorseSettings* settings) {
    settings->sound = 1;
    settings->vibro = 1;
    settings->farnsworth = 1; /* relaxed gaps by default: this app targets beginners */
    settings->led = 1;
    settings->visual = 1; /* auto: training wheels on early levels only */
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, SETTINGS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        FileHeader header;
        MorseSettings loaded = *settings;
        bool ok = false;
        if(storage_file_read(file, &header, sizeof(header)) == sizeof(header) &&
           header.magic == SETTINGS_MAGIC) {
            if(header.version == SETTINGS_VERSION) {
                ok = storage_file_read(file, &loaded, sizeof(loaded)) == sizeof(loaded);
            } else if(header.version == 3) {
                ok = storage_file_read(file, &loaded, SETTINGS_V3_BODY) == SETTINGS_V3_BODY;
            } else if(header.version == 2) {
                ok = storage_file_read(file, &loaded, SETTINGS_V2_BODY) == SETTINGS_V2_BODY;
            } else if(header.version == 1) {
                ok = storage_file_read(file, &loaded, SETTINGS_V1_BODY) == SETTINGS_V1_BODY;
            }
        }
        if(ok && loaded.sound <= 1 && loaded.vibro <= 1 && loaded.farnsworth <= 1 &&
           loaded.led <= 1 && loaded.visual <= 2) {
            *settings = loaded;
        }
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

void morse_settings_save(const MorseSettings* settings) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, PROGRESS_DIR);
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, SETTINGS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        FileHeader header = {.magic = SETTINGS_MAGIC, .version = SETTINGS_VERSION};
        storage_file_write(file, &header, sizeof(header));
        storage_file_write(file, settings, sizeof(MorseSettings));
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

void morse_progress_save(const MorseProgress* progress) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, PROGRESS_DIR); /* ok if it already exists */
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, PROGRESS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        FileHeader header = {.magic = PROGRESS_MAGIC, .version = PROGRESS_VERSION};
        storage_file_write(file, &header, sizeof(header));
        storage_file_write(file, progress, sizeof(MorseProgress));
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}
