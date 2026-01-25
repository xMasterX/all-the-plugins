// helpers/protopirate_storage.c
#include "protopirate_storage.h"
#include <toolbox/stream/file_stream.h>
#include <toolbox/dir_walk.h>

#define TAG                  "ProtoPirateStorage"
#define MAX_FILES_TO_DISPLAY 50

// Structure to hold file info
typedef struct {
    char name[64];
} FileEntry;

// Dynamically allocated array to hold file entries (circular buffer)
static FileEntry* g_file_entries = NULL;
static uint32_t g_file_count = 0;
static bool g_file_list_valid = false;
static bool g_file_list_building = false;  // NEW: Prevent re-entry
static FuriMutex* g_storage_mutex = NULL;  // NEW: Thread safety

// NEW: Initialize mutex (call once at app start)
static void protopirate_storage_ensure_mutex(void) {
    if(g_storage_mutex == NULL) {
        g_storage_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    }
}

bool protopirate_storage_init(void) {
    protopirate_storage_ensure_mutex();
    
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool result = storage_simply_mkdir(storage, PROTOPIRATE_APP_FOLDER);
    furi_record_close(RECORD_STORAGE);
    return result;
}

// Sanitize protocol name for use as filename
static void sanitize_filename(const char* input, char* output, size_t output_size) {
    size_t i = 0;
    size_t j = 0;
    while(input[i] != '\0' && j < output_size - 1) {
        char c = input[i];
        // Replace problematic characters AND spaces with underscore
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

// Find next available filename for a protocol
bool protopirate_storage_get_next_filename(const char* protocol_name, FuriString* out_filename) {
    protopirate_storage_ensure_mutex();
    
    if(furi_mutex_acquire(g_storage_mutex, 100) != FuriStatusOk) {
        FURI_LOG_W(TAG, "Could not acquire mutex for get_next_filename");
        return false;
    }
    
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FuriString* temp_path = furi_string_alloc();
    uint32_t index = 0;
    bool found = false;

    // Sanitize protocol name for filename
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
    furi_mutex_release(g_storage_mutex);
    return found;
}

// Helper to write capture data to a FlipperFormat file
static bool protopirate_storage_write_capture_data(
    FlipperFormat* save_file,
    FlipperFormat* flipper_format) {
    flipper_format_rewind(flipper_format);

    FuriString* string_value = furi_string_alloc();
    uint32_t uint32_value;
    uint32_t uint32_array_size = 0;

    // Protocol name
    if(flipper_format_read_string(flipper_format, "Protocol", string_value))
        flipper_format_write_string(save_file, "Protocol", string_value);

    // Bit count
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "Bit", &uint32_value, 1))
        flipper_format_write_uint32(save_file, "Bit", &uint32_value, 1);

    // Key data
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_string(flipper_format, "Key", string_value)) {
        flipper_format_write_string(save_file, "Key", string_value);
    } else {
        flipper_format_rewind(flipper_format);
        if(flipper_format_get_value_count(flipper_format, "Key", &uint32_array_size) &&
           uint32_array_size > 0 && uint32_array_size < 1024) {  // NEW: Bounds check
            uint32_t* uint32_array = malloc(sizeof(uint32_t) * uint32_array_size);
            if(uint32_array) {
                if(flipper_format_read_uint32(
                       flipper_format, "Key", uint32_array, uint32_array_size))
                    flipper_format_write_uint32(save_file, "Key", uint32_array, uint32_array_size);
                free(uint32_array);
            }
        }
    }

    // Frequency
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "Frequency", &uint32_value, 1))
        flipper_format_write_uint32(save_file, "Frequency", &uint32_value, 1);

    // Preset
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_string(flipper_format, "Preset", string_value))
        flipper_format_write_string(save_file, "Preset", string_value);

    // Custom preset module
    flipper_format_rewind(flipper_format);
    if(flipper_format_get_value_count(flipper_format, "Custom_preset_module", &uint32_array_size) &&
       uint32_array_size > 0) {
        if(flipper_format_read_string(flipper_format, "Custom_preset_module", string_value))
            flipper_format_write_string(save_file, "Custom_preset_module", string_value);
    }

    // Custom preset data
    flipper_format_rewind(flipper_format);
    if(flipper_format_get_value_count(flipper_format, "Custom_preset_data", &uint32_array_size) &&
       uint32_array_size > 0 && uint32_array_size < 1024) {  // NEW: Bounds check
        uint8_t* custom_data = malloc(uint32_array_size);
        if(custom_data) {
            if(flipper_format_read_hex(
                   flipper_format, "Custom_preset_data", custom_data, uint32_array_size))
                flipper_format_write_hex(
                    save_file, "Custom_preset_data", custom_data, uint32_array_size);
            free(custom_data);
        }
    }

    // TE
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "TE", &uint32_value, 1))
        flipper_format_write_uint32(save_file, "TE", &uint32_value, 1);

    // Serial
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "Serial", &uint32_value, 1))
        flipper_format_write_uint32(save_file, "Serial", &uint32_value, 1);

    // Btn
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "Btn", &uint32_value, 1))
        flipper_format_write_uint32(save_file, "Btn", &uint32_value, 1);

    // Cnt
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "Cnt", &uint32_value, 1))
        flipper_format_write_uint32(save_file, "Cnt", &uint32_value, 1);

    // BS Magic
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "BSMagic", &uint32_value, 1))
        flipper_format_write_uint32(save_file, "BSMagic", &uint32_value, 1);

    // CRC
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "CRC", &uint32_value, 1))
        flipper_format_write_uint32(save_file, "CRC", &uint32_value, 1);

    // Type
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "Type", &uint32_value, 1))
        flipper_format_write_uint32(save_file, "Type", &uint32_value, 1);

    // Check
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "Check", &uint32_value, 1))
        flipper_format_write_uint32(save_file, "Check", &uint32_value, 1);

    // RAW_Data - with strict bounds check
    flipper_format_rewind(flipper_format);
    if(flipper_format_get_value_count(flipper_format, "RAW_Data", &uint32_array_size) &&
       uint32_array_size > 0 && uint32_array_size < 4096) {  // NEW: Strict bounds check
        uint32_t* uint32_array = malloc(sizeof(uint32_t) * uint32_array_size);
        if(uint32_array) {
            if(flipper_format_read_uint32(
                   flipper_format, "RAW_Data", uint32_array, uint32_array_size))
                flipper_format_write_uint32(
                    save_file, "RAW_Data", uint32_array, uint32_array_size);
            free(uint32_array);
        }
    }

    // DataHi
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "DataHi", &uint32_value, 1))
        flipper_format_write_uint32(save_file, "DataHi", &uint32_value, 1);

    // DataLo
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "DataLo", &uint32_value, 1))
        flipper_format_write_uint32(save_file, "DataLo", &uint32_value, 1);

    // RawCnt
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "RawCnt", &uint32_value, 1))
        flipper_format_write_uint32(save_file, "RawCnt", &uint32_value, 1);

    // Encrypted
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "Encrypted", &uint32_value, 1))
        flipper_format_write_uint32(save_file, "Encrypted", &uint32_value, 1);

    // Decrypted
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "Decrypted", &uint32_value, 1))
        flipper_format_write_uint32(save_file, "Decrypted", &uint32_value, 1);

    // Version
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "KIAVersion", &uint32_value, 1))
        flipper_format_write_uint32(save_file, "KIAVersion", &uint32_value, 1);

    // BS
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "BS", &uint32_value, 1))
        flipper_format_write_uint32(save_file, "BS", &uint32_value, 1);

    // Manufacture name (for StarLine)
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_string(flipper_format, "Manufacture", string_value))
        flipper_format_write_string(save_file, "Manufacture", string_value);

    furi_string_free(string_value);
    return true;
}

// Save to temp file for emulation
bool protopirate_storage_save_temp(FlipperFormat* flipper_format) {
    protopirate_storage_ensure_mutex();
    
    if(furi_mutex_acquire(g_storage_mutex, 500) != FuriStatusOk) {
        FURI_LOG_W(TAG, "Could not acquire mutex for save_temp");
        return false;
    }
    
    if(!protopirate_storage_init()) {
        FURI_LOG_E(TAG, "Failed to create app folder");
        furi_mutex_release(g_storage_mutex);
        return false;
    }

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* save_file = flipper_format_file_alloc(storage);
    bool result = false;

    do {
        // Delete old temp file if exists
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
    furi_mutex_release(g_storage_mutex);
    return result;
}

// Delete temp file
void protopirate_storage_delete_temp(void) {
    protopirate_storage_ensure_mutex();
    
    if(furi_mutex_acquire(g_storage_mutex, 100) != FuriStatusOk) {
        FURI_LOG_W(TAG, "Could not acquire mutex for delete_temp");
        return;
    }
    
    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(storage_file_exists(storage, PROTOPIRATE_TEMP_FILE)) {
        storage_simply_remove(storage, PROTOPIRATE_TEMP_FILE);
        FURI_LOG_I(TAG, "Deleted temp file");
    }
    furi_record_close(RECORD_STORAGE);
    furi_mutex_release(g_storage_mutex);
}

// Save a capture to a new file
bool protopirate_storage_save_capture(
    FlipperFormat* flipper_format,
    const char* protocol_name,
    FuriString* out_path) {
    protopirate_storage_ensure_mutex();
    
    if(furi_mutex_acquire(g_storage_mutex, 1000) != FuriStatusOk) {
        FURI_LOG_W(TAG, "Could not acquire mutex for save_capture");
        return false;
    }
    
    // Release mutex temporarily for init (it will re-acquire)
    furi_mutex_release(g_storage_mutex);
    
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

    // Re-acquire mutex for the actual save
    if(furi_mutex_acquire(g_storage_mutex, 1000) != FuriStatusOk) {
        FURI_LOG_W(TAG, "Could not re-acquire mutex for save_capture");
        furi_string_free(file_path);
        return false;
    }

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* save_file = flipper_format_file_alloc(storage);
    bool result = false;

    do {
        // Create new file
        if(!flipper_format_file_open_new(save_file, furi_string_get_cstr(file_path))) {
            FURI_LOG_E(TAG, "Failed to create file");
            break;
        }

        // Write standard SubGhz file header
        if(!flipper_format_write_header_cstr(save_file, "Flipper SubGhz Key File", 1)) {
            FURI_LOG_E(TAG, "Failed to write header");
            break;
        }

        protopirate_storage_write_capture_data(save_file, flipper_format);

        if(out_path) furi_string_set(out_path, file_path);

        // Invalidate cache
        g_file_list_valid = false;

        result = true;
        FURI_LOG_I(TAG, "Saved capture to %s", furi_string_get_cstr(file_path));

    } while(false);

    flipper_format_free(save_file);
    furi_string_free(file_path);
    furi_record_close(RECORD_STORAGE);
    furi_mutex_release(g_storage_mutex);
    return result;
}

// Build file list using circular buffer - keeps only last MAX_FILES_TO_DISPLAY
static void protopirate_storage_build_file_list(void) {
    // NEW: Prevent re-entry
    if(g_file_list_building) {
        FURI_LOG_W(TAG, "File list build already in progress, skipping");
        return;
    }
    g_file_list_building = true;
    
    FURI_LOG_D(TAG, "Building file list...");
    
    // Allocate buffer if needed
    if(g_file_entries == NULL) {
        g_file_entries = malloc(sizeof(FileEntry) * MAX_FILES_TO_DISPLAY);
        if(g_file_entries == NULL) {
            FURI_LOG_E(TAG, "Failed to allocate file list buffer");
            g_file_count = 0;
            g_file_list_valid = true;
            g_file_list_building = false;
            return;
        }
    }

    g_file_count = 0;
    memset(g_file_entries, 0, sizeof(FileEntry) * MAX_FILES_TO_DISPLAY);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    
    // NEW: Check if storage is available
    if(storage == NULL) {
        FURI_LOG_E(TAG, "Failed to open storage record");
        g_file_list_valid = true;  // Mark valid to prevent retry loop
        g_file_list_building = false;
        return;
    }

    if(!storage_dir_exists(storage, PROTOPIRATE_APP_FOLDER)) {
        storage_simply_mkdir(storage, PROTOPIRATE_APP_FOLDER);
        furi_record_close(RECORD_STORAGE);
        g_file_list_valid = true;
        g_file_list_building = false;
        FURI_LOG_D(TAG, "Created app folder, no files yet");
        return;
    }

    File* dir = storage_file_alloc(storage);
    if(dir == NULL) {
        FURI_LOG_E(TAG, "Failed to allocate directory handle");
        furi_record_close(RECORD_STORAGE);
        g_file_list_valid = true;
        g_file_list_building = false;
        return;
    }
    
    FileInfo file_info;

    // Use circular buffer index
    uint32_t write_index = 0;
    uint32_t total_found = 0;
    uint32_t iterations = 0;
    const uint32_t MAX_ITERATIONS = 500;  // NEW: Safety limit

    if(storage_dir_open(dir, PROTOPIRATE_APP_FOLDER)) {
        char name[256];
        while(storage_dir_read(dir, &file_info, name, sizeof(name))) {
            // NEW: Safety check against infinite loops
            iterations++;
            if(iterations > MAX_ITERATIONS) {
                FURI_LOG_E(TAG, "Too many iterations, breaking out");
                break;
            }
            
            // Skip temp file and hidden files
            if(name[0] == '.') continue;

            if(!file_info_is_dir(&file_info) && strstr(name, PROTOPIRATE_APP_EXTENSION)) {
                // Copy name without extension
                strncpy(
                    g_file_entries[write_index].name,
                    name,
                    sizeof(g_file_entries[write_index].name) - 1);
                g_file_entries[write_index].name[sizeof(g_file_entries[write_index].name) - 1] =
                    '\0';

                // Remove extension
                char* dot = strrchr(g_file_entries[write_index].name, '.');
                if(dot) *dot = '\0';

                // Circular buffer: wrap around
                write_index = (write_index + 1) % MAX_FILES_TO_DISPLAY;
                total_found++;
            }
        }
        storage_dir_close(dir);
    } else {
        FURI_LOG_E(TAG, "Failed to open directory");
    }

    storage_file_free(dir);
    furi_record_close(RECORD_STORAGE);

    // Calculate actual count (up to MAX)
    g_file_count = (total_found < MAX_FILES_TO_DISPLAY) ? total_found : MAX_FILES_TO_DISPLAY;

    // If we wrapped around, we need to reorder the circular buffer
    // so that newest files come first
    if(total_found > MAX_FILES_TO_DISPLAY) {
        // write_index points to oldest entry, we need to rotate
        FileEntry* temp = malloc(sizeof(FileEntry) * MAX_FILES_TO_DISPLAY);
        if(temp) {
            for(uint32_t i = 0; i < MAX_FILES_TO_DISPLAY; i++) {
                // Read from write_index backwards (newest is at write_index - 1)
                uint32_t src = (write_index + MAX_FILES_TO_DISPLAY - 1 - i) % MAX_FILES_TO_DISPLAY;
                temp[i] = g_file_entries[src];
            }
            memcpy(g_file_entries, temp, sizeof(FileEntry) * MAX_FILES_TO_DISPLAY);
            free(temp);
        }
    } else if(g_file_count > 1) {
        // Simple reverse for non-wrapped case
        for(uint32_t i = 0; i < g_file_count / 2; i++) {
            FileEntry temp = g_file_entries[i];
            g_file_entries[i] = g_file_entries[g_file_count - 1 - i];
            g_file_entries[g_file_count - 1 - i] = temp;
        }
    }

    g_file_list_valid = true;
    g_file_list_building = false;
    FURI_LOG_I(
        TAG,
        "Built file list with %lu entries (total on disk: %lu, iterations: %lu)",
        (unsigned long)g_file_count,
        (unsigned long)total_found,
        (unsigned long)iterations);
}

uint32_t protopirate_storage_get_file_count(void) {
    protopirate_storage_ensure_mutex();
    
    // NEW: Try to acquire mutex with timeout
    if(furi_mutex_acquire(g_storage_mutex, 200) != FuriStatusOk) {
        FURI_LOG_W(TAG, "Could not acquire mutex for get_file_count, returning cached");
        return g_file_count;  // Return cached value instead of blocking
    }
    
    if(!g_file_list_valid && !g_file_list_building) {
        protopirate_storage_build_file_list();
    }
    
    uint32_t count = g_file_count;
    furi_mutex_release(g_storage_mutex);
    return count;
}

void protopirate_storage_invalidate_cache(void) {
    g_file_list_valid = false;
}

bool protopirate_storage_get_file_by_index(
    uint32_t index,
    FuriString* out_path,
    FuriString* out_name) {
    protopirate_storage_ensure_mutex();
    
    if(furi_mutex_acquire(g_storage_mutex, 200) != FuriStatusOk) {
        FURI_LOG_W(TAG, "Could not acquire mutex for get_file_by_index");
        return false;
    }
    
    if(!g_file_list_valid && !g_file_list_building) {
        protopirate_storage_build_file_list();
    }

    if(g_file_entries == NULL || index >= g_file_count) {
        furi_mutex_release(g_storage_mutex);
        return false;
    }

    if(out_path) {
        furi_string_printf(
            out_path,
            "%s/%s%s",
            PROTOPIRATE_APP_FOLDER,
            g_file_entries[index].name,
            PROTOPIRATE_APP_EXTENSION);
    }
    if(out_name) {
        furi_string_set_str(out_name, g_file_entries[index].name);
    }

    furi_mutex_release(g_storage_mutex);
    return true;
}

// Delete a capture file
bool protopirate_storage_delete_file(const char* file_path) {
    protopirate_storage_ensure_mutex();
    
    if(furi_mutex_acquire(g_storage_mutex, 500) != FuriStatusOk) {
        FURI_LOG_W(TAG, "Could not acquire mutex for delete_file");
        return false;
    }
    
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool result = storage_simply_remove(storage, file_path);
    furi_record_close(RECORD_STORAGE);

    if(result) g_file_list_valid = false;

    furi_mutex_release(g_storage_mutex);
    
    FURI_LOG_I(TAG, "Delete file %s: %s", file_path, result ? "OK" : "FAILED");
    return result;
}

// Load a capture file
// IMPORTANT: Caller MUST call protopirate_storage_close_file() when done!
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

// Close a loaded file and release storage resources
// Must be called after protopirate_storage_load_file()
void protopirate_storage_close_file(FlipperFormat* flipper_format) {
    if(flipper_format) flipper_format_free(flipper_format);
    furi_record_close(RECORD_STORAGE);
}

// Check if a file exists
bool protopirate_storage_file_exists(const char* file_path) {
    if(!file_path) return false;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool exists = storage_file_exists(storage, file_path);
    furi_record_close(RECORD_STORAGE);

    return exists;
}

// Free the internal file list cache
// Call this ONLY when exiting the app entirely
void protopirate_storage_free_file_list(void) {
    // Don't try to acquire mutex here - just do cleanup
    // This should only be called during app shutdown
    
    if(g_file_entries != NULL) {
        free(g_file_entries);
        g_file_entries = NULL;
    }
    g_file_count = 0;
    g_file_list_valid = false;
    g_file_list_building = false;
    
    // Free mutex only on full app cleanup
    if(g_storage_mutex != NULL) {
        furi_mutex_free(g_storage_mutex);
        g_storage_mutex = NULL;
    }
    
    FURI_LOG_D(TAG, "File list freed");
}
