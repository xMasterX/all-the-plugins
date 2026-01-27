// helpers/protopirate_storage.c
#include "protopirate_storage.h"

#define TAG "ProtoPirateStorage"

bool protopirate_storage_init(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool result = storage_simply_mkdir(storage, PROTOPIRATE_APP_FOLDER);
    furi_record_close(RECORD_STORAGE);
    return result;
}

static void sanitize_filename(const char* input, char* output, size_t output_size) {
    size_t i = 0;
    size_t j = 0;
    while(input[i] != '\0' && j < output_size - 1) {
        char c = input[i];
        if(c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
           c == '>' || c == '|' || c == ' ') {
            output[j] = '_';
        } else {
            output[j] = c;
        }
        i++;
        j++;
    }
    output[j] = '\0';
}

bool protopirate_storage_get_next_filename(const char* protocol_name, FuriString* out_filename) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FuriString* temp_path = furi_string_alloc();
    uint32_t index = 0;
    bool found = false;

    char safe_name[64];
    sanitize_filename(protocol_name, safe_name, sizeof(safe_name));

    while(!found && index < 999) {
        furi_string_printf(
            temp_path,
            "%s/%s_%03lu%s",
            PROTOPIRATE_APP_FOLDER,
            safe_name,
            (unsigned long)index,
            PROTOPIRATE_APP_EXTENSION);

        if(!storage_file_exists(storage, furi_string_get_cstr(temp_path))) {
            furi_string_set(out_filename, temp_path);
            found = true;
        } else {
            index++;
        }
    }

    furi_string_free(temp_path);
    furi_record_close(RECORD_STORAGE);
    return found;
}

static bool protopirate_storage_write_capture_data(
    FlipperFormat* save_file,
    FlipperFormat* flipper_format) {
    flipper_format_rewind(flipper_format);

    FuriString* string_value = furi_string_alloc();
    uint32_t uint32_value;
    uint32_t uint32_array_size = 0;

    if(flipper_format_read_string(flipper_format, "Protocol", string_value))
        flipper_format_write_string(save_file, "Protocol", string_value);

    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "Bit", &uint32_value, 1))
        flipper_format_write_uint32(save_file, "Bit", &uint32_value, 1);

    flipper_format_rewind(flipper_format);
    if(flipper_format_read_string(flipper_format, "Key", string_value)) {
        flipper_format_write_string(save_file, "Key", string_value);
    } else {
        flipper_format_rewind(flipper_format);
        if(flipper_format_get_value_count(flipper_format, "Key", &uint32_array_size) &&
           uint32_array_size > 0 && uint32_array_size < 1024) {
            uint32_t* uint32_array = malloc(sizeof(uint32_t) * uint32_array_size);
            if(uint32_array) {
                if(flipper_format_read_uint32(
                       flipper_format, "Key", uint32_array, uint32_array_size))
                    flipper_format_write_uint32(save_file, "Key", uint32_array, uint32_array_size);
                free(uint32_array);
            }
        }
    }

    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "Frequency", &uint32_value, 1))
        flipper_format_write_uint32(save_file, "Frequency", &uint32_value, 1);

    flipper_format_rewind(flipper_format);
    if(flipper_format_read_string(flipper_format, "Preset", string_value))
        flipper_format_write_string(save_file, "Preset", string_value);

    flipper_format_rewind(flipper_format);
    if(flipper_format_get_value_count(flipper_format, "Custom_preset_module", &uint32_array_size) &&
       uint32_array_size > 0) {
        if(flipper_format_read_string(flipper_format, "Custom_preset_module", string_value))
            flipper_format_write_string(save_file, "Custom_preset_module", string_value);
    }

    flipper_format_rewind(flipper_format);
    if(flipper_format_get_value_count(flipper_format, "Custom_preset_data", &uint32_array_size) &&
       uint32_array_size > 0 && uint32_array_size < 1024) {
        uint8_t* custom_data = malloc(uint32_array_size);
        if(custom_data) {
            if(flipper_format_read_hex(
                   flipper_format, "Custom_preset_data", custom_data, uint32_array_size))
                flipper_format_write_hex(
                    save_file, "Custom_preset_data", custom_data, uint32_array_size);
            free(custom_data);
        }
    }

    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "TE", &uint32_value, 1))
        flipper_format_write_uint32(save_file, "TE", &uint32_value, 1);

    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "Serial", &uint32_value, 1))
        flipper_format_write_uint32(save_file, "Serial", &uint32_value, 1);

    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "Btn", &uint32_value, 1))
        flipper_format_write_uint32(save_file, "Btn", &uint32_value, 1);

    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "Cnt", &uint32_value, 1))
        flipper_format_write_uint32(save_file, "Cnt", &uint32_value, 1);

    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "BSMagic", &uint32_value, 1))
        flipper_format_write_uint32(save_file, "BSMagic", &uint32_value, 1);

    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "CRC", &uint32_value, 1))
        flipper_format_write_uint32(save_file, "CRC", &uint32_value, 1);

    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "Type", &uint32_value, 1))
        flipper_format_write_uint32(save_file, "Type", &uint32_value, 1);

    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "Seed", &uint32_value, 1))
        flipper_format_write_uint32(save_file, "Seed", &uint32_value, 1);

    flipper_format_rewind(flipper_format);
    uint8_t val_field[2];
    if(flipper_format_read_hex(flipper_format, "ValidationField", val_field, 2)) {
        flipper_format_write_hex(save_file, "ValidationField", val_field, 2);
    } else {
        flipper_format_rewind(flipper_format);
        if(flipper_format_read_uint32(flipper_format, "ValidationField", &uint32_value, 1))
            flipper_format_write_uint32(save_file, "ValidationField", &uint32_value, 1);
    }

    flipper_format_rewind(flipper_format);
    if(flipper_format_read_string(flipper_format, "Key_2", string_value))
        flipper_format_write_string(save_file, "Key_2", string_value);

    flipper_format_rewind(flipper_format);
    uint8_t key1_buf[8];
    if(flipper_format_read_hex(flipper_format, "Key1", key1_buf, 8))
        flipper_format_write_hex(save_file, "Key1", key1_buf, 8);

    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "Check", &uint32_value, 1))
        flipper_format_write_uint32(save_file, "Check", &uint32_value, 1);

    flipper_format_rewind(flipper_format);
    if(flipper_format_get_value_count(flipper_format, "RAW_Data", &uint32_array_size) &&
       uint32_array_size > 0 && uint32_array_size < 4096) {
        uint32_t* uint32_array = malloc(sizeof(uint32_t) * uint32_array_size);
        if(uint32_array) {
            if(flipper_format_read_uint32(
                   flipper_format, "RAW_Data", uint32_array, uint32_array_size))
                flipper_format_write_uint32(
                    save_file, "RAW_Data", uint32_array, uint32_array_size);
            free(uint32_array);
        }
    }

    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "DataHi", &uint32_value, 1))
        flipper_format_write_uint32(save_file, "DataHi", &uint32_value, 1);

    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "DataLo", &uint32_value, 1))
        flipper_format_write_uint32(save_file, "DataLo", &uint32_value, 1);

    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "RawCnt", &uint32_value, 1))
        flipper_format_write_uint32(save_file, "RawCnt", &uint32_value, 1);

    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "Encrypted", &uint32_value, 1))
        flipper_format_write_uint32(save_file, "Encrypted", &uint32_value, 1);

    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "Decrypted", &uint32_value, 1))
        flipper_format_write_uint32(save_file, "Decrypted", &uint32_value, 1);

    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "KIAVersion", &uint32_value, 1))
        flipper_format_write_uint32(save_file, "KIAVersion", &uint32_value, 1);

    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "BS", &uint32_value, 1))
        flipper_format_write_uint32(save_file, "BS", &uint32_value, 1);

    flipper_format_rewind(flipper_format);
    if(flipper_format_read_string(flipper_format, "Manufacture", string_value))
        flipper_format_write_string(save_file, "Manufacture", string_value);

    furi_string_free(string_value);
    return true;
}

bool protopirate_storage_save_temp(FlipperFormat* flipper_format) {
    if(!protopirate_storage_init()) {
        FURI_LOG_E(TAG, "Failed to create app folder");
        return false;
    }

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* save_file = flipper_format_file_alloc(storage);
    bool result = false;

    do {
        storage_simply_remove(storage, PROTOPIRATE_TEMP_FILE);

        if(!flipper_format_file_open_new(save_file, PROTOPIRATE_TEMP_FILE)) {
            FURI_LOG_E(TAG, "Failed to create temp file");
            break;
        }

        if(!flipper_format_write_header_cstr(save_file, "Flipper SubGhz Key File", 1)) {
            FURI_LOG_E(TAG, "Failed to write header");
            break;
        }

        protopirate_storage_write_capture_data(save_file, flipper_format);

        result = true;
        FURI_LOG_I(TAG, "Saved temp file: %s", PROTOPIRATE_TEMP_FILE);

    } while(false);

    flipper_format_free(save_file);
    furi_record_close(RECORD_STORAGE);
    return result;
}

void protopirate_storage_delete_temp(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(storage_file_exists(storage, PROTOPIRATE_TEMP_FILE)) {
        storage_simply_remove(storage, PROTOPIRATE_TEMP_FILE);
        FURI_LOG_I(TAG, "Deleted temp file");
    }
    furi_record_close(RECORD_STORAGE);
}

bool protopirate_storage_save_capture(
    FlipperFormat* flipper_format,
    const char* protocol_name,
    FuriString* out_path) {
    if(!protopirate_storage_init()) {
        FURI_LOG_E(TAG, "Failed to create app folder");
        return false;
    }

    FuriString* file_path = furi_string_alloc();

    if(!protopirate_storage_get_next_filename(protocol_name, file_path)) {
        FURI_LOG_E(TAG, "Failed to get next filename");
        furi_string_free(file_path);
        return false;
    }

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* save_file = flipper_format_file_alloc(storage);
    bool result = false;

    do {
        if(!flipper_format_file_open_new(save_file, furi_string_get_cstr(file_path))) {
            FURI_LOG_E(TAG, "Failed to create file");
            break;
        }

        if(!flipper_format_write_header_cstr(save_file, "Flipper SubGhz Key File", 1)) {
            FURI_LOG_E(TAG, "Failed to write header");
            break;
        }

        protopirate_storage_write_capture_data(save_file, flipper_format);

        if(out_path) furi_string_set(out_path, file_path);

        result = true;
        FURI_LOG_I(TAG, "Saved capture to %s", furi_string_get_cstr(file_path));

    } while(false);

    flipper_format_free(save_file);
    furi_string_free(file_path);
    furi_record_close(RECORD_STORAGE);
    return result;
}

bool protopirate_storage_delete_file(const char* file_path) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool result = storage_simply_remove(storage, file_path);
    furi_record_close(RECORD_STORAGE);

    FURI_LOG_I(TAG, "Delete file %s: %s", file_path, result ? "OK" : "FAILED");
    return result;
}

FlipperFormat* protopirate_storage_load_file(const char* file_path) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* flipper_format = flipper_format_file_alloc(storage);

    if(!flipper_format_file_open_existing(flipper_format, file_path)) {
        FURI_LOG_E(TAG, "Failed to open file %s", file_path);
        flipper_format_free(flipper_format);
        furi_record_close(RECORD_STORAGE);
        return NULL;
    }

    return flipper_format;
}

void protopirate_storage_close_file(FlipperFormat* flipper_format) {
    if(flipper_format) {
        flipper_format_free(flipper_format);
    }
    furi_record_close(RECORD_STORAGE);
}

bool protopirate_storage_file_exists(const char* file_path) {
    if(!file_path) return false;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool exists = storage_file_exists(storage, file_path);
    furi_record_close(RECORD_STORAGE);

    return exists;
}
