// helpers/protopirate_storage.h
#pragma once

#include <furi.h>
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>

#define PROTOPIRATE_APP_FOLDER       APP_DATA_PATH("saved")
#define PROTOPIRATE_APP_EXTENSION    ".psf"
#define PROTOPIRATE_APP_FILE_VERSION 1
#define PROTOPIRATE_TEMP_FILE        APP_DATA_PATH("saved/.temp.psf")

bool protopirate_storage_init(void);

bool protopirate_storage_save_capture(
    FlipperFormat* flipper_format,
    const char* protocol_name,
    FuriString* out_path);

// Save to temp file for emulation (will be deleted on exit)
bool protopirate_storage_save_temp(FlipperFormat* flipper_format);

// Delete temp file
void protopirate_storage_delete_temp(void);

bool protopirate_storage_get_next_filename(const char* protocol_name, FuriString* out_filename);

uint32_t protopirate_storage_get_file_count(void);

bool protopirate_storage_get_file_by_index(
    uint32_t index,
    FuriString* out_path,
    FuriString* out_name);

bool protopirate_storage_delete_file(const char* file_path);

FlipperFormat* protopirate_storage_load_file(const char* file_path);

void protopirate_storage_close_file(FlipperFormat* flipper_format);

bool protopirate_storage_file_exists(const char* file_path);

void protopirate_storage_free_file_list(void);

void protopirate_storage_invalidate_cache(void);
