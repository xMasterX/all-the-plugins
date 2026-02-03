#include "raw_file_reader.h"
#include "../protopirate_app_i.h"

#define TAG "RawFileReader"

RawFileReader* raw_file_reader_alloc(void) {
    RawFileReader* reader = malloc(sizeof(RawFileReader));
    memset(reader, 0, sizeof(RawFileReader));
    return reader;
}

void raw_file_reader_free(RawFileReader* reader) {
    if(!reader) return;
    raw_file_reader_close(reader);
    free(reader);
}

bool raw_file_reader_open(RawFileReader* reader, const char* file_path) {
    if(!reader || !file_path) return false;

    raw_file_reader_close(reader);

    reader->storage = furi_record_open(RECORD_STORAGE);
    reader->storage_opened = true;
    reader->ff = flipper_format_file_alloc(reader->storage);

    if(!flipper_format_file_open_existing(reader->ff, file_path)) {
        FURI_LOG_E(TAG, "Failed to open file: %s", file_path);
        raw_file_reader_close(reader);
        return false;
    }

    FuriString* temp_str = furi_string_alloc();
    uint32_t version = 0;

    bool valid = false;
    do {
        if(!flipper_format_read_header(reader->ff, temp_str, &version)) {
            FURI_LOG_E(TAG, "Failed to read header");
            break;
        }

        if(furi_string_cmp_str(temp_str, "Flipper SubGhz RAW File") != 0) {
            FURI_LOG_E(TAG, "Not a RAW file");
            break;
        }

        if(!flipper_format_read_string(reader->ff, "Protocol", temp_str)) {
            FURI_LOG_E(TAG, "Missing Protocol field");
            break;
        }

        if(furi_string_cmp_str(temp_str, "RAW") != 0) {
            FURI_LOG_E(TAG, "Protocol is not RAW");
            break;
        }

        valid = true;
    } while(false);

    furi_string_free(temp_str);

    if(!valid) {
        raw_file_reader_close(reader);
        return false;
    }

    reader->buffer_count = 0;
    reader->buffer_index = 0;
    reader->file_finished = false;
    reader->current_level = true;

    FURI_LOG_I(TAG, "Opened RAW file: %s", file_path);
    return true;
}

void raw_file_reader_close(RawFileReader* reader) {
    if(!reader) return;

    if(reader->ff) {
        flipper_format_free(reader->ff);
        reader->ff = NULL;
    }

    if(reader->storage_opened) {
        furi_record_close(RECORD_STORAGE);
        reader->storage_opened = false;
    }

    reader->storage = NULL;
    reader->buffer_count = 0;
    reader->buffer_index = 0;
    reader->file_finished = false;
}

static bool raw_file_reader_load_chunk(RawFileReader* reader) {
    if(reader->file_finished) return false;

    uint32_t count = 0;
    if(!flipper_format_get_value_count(reader->ff, "RAW_Data", &count) || count == 0) {
        reader->file_finished = true;
        return false;
    }

    size_t to_read = (count < RAW_READER_BUFFER_SIZE) ? count : RAW_READER_BUFFER_SIZE;

    if(!flipper_format_read_int32(reader->ff, "RAW_Data", reader->buffer, to_read)) {
        reader->file_finished = true;
        return false;
    }

    reader->buffer_count = to_read;
    reader->buffer_index = 0;

    return true;
}

bool raw_file_reader_get_next(RawFileReader* reader, bool* level, uint32_t* duration) {
    if(!reader || !level || !duration) return false;

    if(reader->buffer_index >= reader->buffer_count) {
        if(!raw_file_reader_load_chunk(reader)) {
            return false;
        }
    }

    int32_t value = reader->buffer[reader->buffer_index++];

    if(value >= 0) {
        *level = true;
        *duration = (uint32_t)value;
    } else {
        *level = false;
        *duration = (uint32_t)(-value);
    }

    return true;
}

bool raw_file_reader_is_finished(RawFileReader* reader) {
    if(!reader) return true;
    return reader->file_finished && (reader->buffer_index >= reader->buffer_count);
}