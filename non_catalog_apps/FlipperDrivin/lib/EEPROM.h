#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include <furi.h>
#include <storage/storage.h>

#define EEPROM_LIB_PATH "/ext/apps_data/eeprom.bin"

class EEPROMClass {
public:
    static constexpr int kSize = 1024;

    EEPROMClass()
        : loaded_(false)
        , dirty_(false)
        , autosave_interval_ms_(0)
        , last_autosave_ms_(0) {
        memset(mem_, 0x00, kSize);
        file_path_[0] = '\0';
    }

    void begin(const char* path = EEPROM_LIB_PATH, uint32_t autosave_interval_ms = 0) {
        const char* new_path = (path && *path) ? path : EEPROM_LIB_PATH;
        if(strncmp(file_path_, new_path, sizeof(file_path_)) != 0) {
            strncpy(file_path_, new_path, sizeof(file_path_) - 1);
            file_path_[sizeof(file_path_) - 1] = '\0';
            loaded_ = false;
            dirty_ = false;
            memset(mem_, 0x00, kSize);
        } else if(file_path_[0] == '\0') {
            file_path_[sizeof(file_path_) - 1] = '\0';
            loaded_ = false;
            dirty_ = false;
            memset(mem_, 0x00, kSize);
        }

        autosave_interval_ms_ = autosave_interval_ms;
        last_autosave_ms_ = now_ms_();
        ensureLoaded_();
    }

    int length() const {
        return kSize;
    }

    uint8_t read(int addr) const {
        ensureLoaded_();
        if(addr < 0 || addr >= kSize) return 0;
        return mem_[addr];
    }

    void write(int addr, uint8_t value) {
        ensureLoaded_();
        if(addr < 0 || addr >= kSize) return;
        mem_[addr] = value;
        dirty_ = true;
    }

    void update(int addr, uint8_t value) {
        ensureLoaded_();
        if(addr < 0 || addr >= kSize) return;
        if(mem_[addr] != value) {
            mem_[addr] = value;
            dirty_ = true;
        }
    }

    template <typename T>
    T& get(int addr, T& out) const {
        ensureLoaded_();
        if(addr < 0 || addr + (int)sizeof(T) > kSize) return out;
        memcpy(&out, mem_ + addr, sizeof(T));
        return out;
    }

    template <typename T>
    const T& put(int addr, const T& in) {
        ensureLoaded_();
        if(addr < 0 || addr + (int)sizeof(T) > kSize) return in;

        bool changed = false;
        const uint8_t* src = reinterpret_cast<const uint8_t*>(&in);
        for(size_t i = 0; i < sizeof(T); i++) {
            int a = addr + (int)i;
            if(mem_[a] != src[i]) {
                mem_[a] = src[i];
                changed = true;
            }
        }
        if(changed) dirty_ = true;
        return in;
    }

    void clear(uint8_t value = 0) {
        ensureLoaded_();
        memset(mem_, value, kSize);
        dirty_ = true;
    }

    void tick() {
        if(autosave_interval_ms_ == 0) return;
        if(!dirty_) return;

        const uint32_t now = now_ms_();
        if((uint32_t)(now - last_autosave_ms_) >= autosave_interval_ms_) {
            (void)commit();
            last_autosave_ms_ = now;
        }
    }

    bool commit() {
        ensureLoaded_();
        if(!dirty_) return true;

        const bool ok = writeFile_();
        if(ok) dirty_ = false;
        return ok;
    }

    bool isDirty() const {
        return dirty_;
    }

private:
    void ensureLoaded_() const {
        if(loaded_) return;

        if(file_path_[0] == '\0') {
            strncpy(file_path_, EEPROM_LIB_PATH, sizeof(file_path_) - 1);
            file_path_[sizeof(file_path_) - 1] = '\0';
        }

        Storage* storage = (Storage*)furi_record_open(RECORD_STORAGE);
        if(!storage) {
            return;
        }

        File* file = storage_file_alloc(storage);
        if(!file) {
            furi_record_close(RECORD_STORAGE);
            return;
        }

        if(storage_file_exists(storage, file_path_)) {
            bool ok = storage_file_open(file, file_path_, FSAM_READ, FSOM_OPEN_EXISTING);
            if(ok) {
                memset(mem_, 0x00, kSize);
                (void)storage_file_read(file, mem_, kSize);
            }
            (void)storage_file_close(file);
        } else {
            memset(mem_, 0x00, kSize);
            bool ok = storage_file_open(file, file_path_, FSAM_WRITE, FSOM_CREATE_ALWAYS);
            if(ok) {
                (void)storage_file_write(file, mem_, kSize);
                (void)storage_file_sync(file);
            }
            (void)storage_file_close(file);
        }

        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);

        loaded_ = true;
        dirty_ = false;
    }

    bool writeFile_() const {
        Storage* storage = (Storage*)furi_record_open(RECORD_STORAGE);
        if(!storage) return false;

        File* file = storage_file_alloc(storage);
        if(!file) {
            furi_record_close(RECORD_STORAGE);
            return false;
        }

        bool ok = storage_file_open(file, file_path_, FSAM_WRITE, FSOM_OPEN_ALWAYS);
        bool success = false;
        
        if(ok) {
            (void)storage_file_seek(file, 0, true);

            size_t wr = storage_file_write(file, mem_, kSize);
            (void)storage_file_sync(file);
            (void)storage_file_close(file);

            success = (wr == (size_t)kSize);
        } else {
            (void)storage_file_close(file);
        }

        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
        return success;
    }

    static uint32_t now_ms_() {
        return furi_get_tick();
    }

private:
    mutable uint8_t mem_[kSize];
    mutable bool loaded_;
    mutable bool dirty_;

    mutable char file_path_[64];

    mutable uint32_t autosave_interval_ms_;
    mutable uint32_t last_autosave_ms_;
};

#if (__cplusplus >= 201703L)
inline EEPROMClass EEPROM;
#else
extern EEPROMClass EEPROM;
#ifdef EEPROM_DEFINE_INSTANCE
EEPROMClass EEPROM;
#endif
#endif
