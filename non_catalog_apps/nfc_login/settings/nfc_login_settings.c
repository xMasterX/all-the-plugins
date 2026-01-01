#include "nfc_login_settings.h"
#include "../storage/nfc_login_card_storage.h"
#include "../hid/nfc_login_hid.h"

// Import HAS_BLE_HID_API
#ifndef HAS_BLE_HID_API
#define HAS_BLE_HID_API 0
#endif

void app_load_keyboard_layout(App* app) {
    for(int i = 0; i < 128; i++) {
        app->layout[i] = HID_KEYBOARD_NONE;
    }

    for(char c = 'a'; c <= 'z'; c++) {
        app->layout[(unsigned char)c] = HID_KEYBOARD_A + (c - 'a');
    }
    for(char c = 'A'; c <= 'Z'; c++) {
        app->layout[(unsigned char)c] = (KEY_MOD_LEFT_SHIFT << 8) | (HID_KEYBOARD_A + (c - 'A'));
    }
    for(char c = '1'; c <= '9'; c++) {
        app->layout[(unsigned char)c] = HID_KEYBOARD_1 + (c - '1');
    }
    app->layout['0'] = HID_KEYBOARD_0;
    app->layout[' '] = HID_KEYBOARD_SPACEBAR;
    app->layout['\n'] = HID_KEYBOARD_RETURN;
    app->layout['\r'] = HID_KEYBOARD_RETURN;
    app->layout['\t'] = HID_KEYBOARD_TAB;

    app->layout['!'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_1;
    app->layout['@'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_2;
    app->layout['#'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_3;
    app->layout['$'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_4;
    app->layout['%'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_5;
    app->layout['^'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_6;
    app->layout['&'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_7;
    app->layout['*'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_8;
    app->layout['('] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_9;
    app->layout[')'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_0;

    app->layout['-'] = HID_KEYBOARD_MINUS;
    app->layout['_'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_MINUS;
    app->layout['='] = HID_KEYBOARD_EQUAL_SIGN;
    app->layout['+'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_EQUAL_SIGN;
    app->layout['['] = HID_KEYBOARD_OPEN_BRACKET;
    app->layout['{'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_OPEN_BRACKET;
    app->layout[']'] = HID_KEYBOARD_CLOSE_BRACKET;
    app->layout['}'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_CLOSE_BRACKET;
    app->layout['\\'] = HID_KEYBOARD_BACKSLASH;
    app->layout['|'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_BACKSLASH;
    app->layout[';'] = HID_KEYBOARD_SEMICOLON;
    app->layout[':'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_SEMICOLON;
    app->layout['\''] = HID_KEYBOARD_APOSTROPHE;
    app->layout['"'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_APOSTROPHE;
    app->layout['`'] = HID_KEYBOARD_GRAVE_ACCENT;
    app->layout['~'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_GRAVE_ACCENT;
    app->layout[','] = HID_KEYBOARD_COMMA;
    app->layout['<'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_COMMA;
    app->layout['.'] = HID_KEYBOARD_DOT;
    app->layout['>'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_DOT;
    app->layout['/'] = HID_KEYBOARD_SLASH;
    app->layout['?'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_SLASH;

    app->layout_loaded = true;

    if(app->keyboard_layout[0] != '\0') {
        char layout_path[96];
        snprintf(
            layout_path, sizeof(layout_path), "%s/%s", BADUSB_LAYOUTS_DIR, app->keyboard_layout);

        Storage* storage = furi_record_open(RECORD_STORAGE);
        if(storage) {
            File* file = storage_file_alloc(storage);
            if(file) {
                if(storage_file_open(file, layout_path, FSAM_READ, FSOM_OPEN_EXISTING)) {
                    storage_file_read(file, app->layout, sizeof(app->layout));
                    storage_file_close(file);
                }
                storage_file_free(file);
            }
            furi_record_close(RECORD_STORAGE);
        }
    }
}

void app_save_settings(App* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);

    app_ensure_data_dir(storage);

    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, NFC_SETTINGS_FILE, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        char line[128];
        snprintf(line, sizeof(line), "append_enter=%d\n", app->append_enter ? 1 : 0);
        storage_file_write(file, line, strlen(line));
        snprintf(line, sizeof(line), "input_delay=%d\n", app->input_delay_ms);
        storage_file_write(file, line, strlen(line));
        snprintf(line, sizeof(line), "keyboard_layout=%s\n", app->keyboard_layout);
        storage_file_write(file, line, strlen(line));
        snprintf(line, sizeof(line), "passcode_disabled=%d\n", app->passcode_disabled ? 1 : 0);
        storage_file_write(file, line, strlen(line));
        snprintf(line, sizeof(line), "hid_mode=%d\n", app->hid_mode);
        storage_file_write(file, line, strlen(line));
        if(app->has_active_selection && app->active_card_index < app->card_count) {
            snprintf(line, sizeof(line), "active_card_index=%zu\n", app->active_card_index);
            storage_file_write(file, line, strlen(line));
        }
        storage_file_close(file);
    }
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    app_load_keyboard_layout(app);
}

void app_load_settings(App* app) {
    app->append_enter = true;
    app->input_delay_ms = 10;
    strncpy(app->keyboard_layout, "en-US.kl", sizeof(app->keyboard_layout) - 1);
    app->keyboard_layout[sizeof(app->keyboard_layout) - 1] = '\0';
    app->selecting_keyboard_layout = false;
    app->layout_loaded = false;
    app->has_active_selection = false;
    app->active_card_index = 0;
    app->passcode_disabled = false;
// Default to USB, or force USB if BLE HID not available
#if HAS_BLE_HID_API
    app->hid_mode = HidModeUsb;
#else
    app->hid_mode = HidModeUsb; // BLE not available, force USB
#endif

    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(!storage) {
        return;
    }

    File* file = storage_file_alloc(storage);
    if(!file) {
        furi_record_close(RECORD_STORAGE);
        return;
    }

    if(storage_file_open(file, NFC_SETTINGS_FILE, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char line[128];
        size_t len = 0;
        uint8_t ch = 0;

        while(true) {
            len = 0;
            while(len < sizeof(line) - 1) {
                uint16_t rd = storage_file_read(file, &ch, 1);
                if(rd == 0 || ch == '\n') break;
                line[len++] = (char)ch;
            }
            if(len == 0) break;
            line[len] = '\0';

            if(strncmp(line, "append_enter=", 13) == 0) {
                int value = atoi(line + 13);
                app->append_enter = (value != 0);
            } else if(strncmp(line, "input_delay=", 12) == 0) {
                int value = atoi(line + 12);
                if(value == 10 || value == 50 || value == 100 || value == 200) {
                    app->input_delay_ms = (uint16_t)value;
                }
            } else if(strncmp(line, "keyboard_delay=", 15) == 0) {
                int value = atoi(line + 15);
                if(value == 0 || value == 10) {
                    app->input_delay_ms = 10;
                } else if(value == 50) {
                    app->input_delay_ms = 50;
                } else if(value == 100) {
                    app->input_delay_ms = 100;
                } else if(value >= 200) {
                    app->input_delay_ms = 200;
                }
            } else if(strncmp(line, "keyboard_layout=", 17) == 0) {
                const char* layout_name = line + 17;
                size_t name_len = strlen(layout_name);
                if(name_len > 0 && name_len < sizeof(app->keyboard_layout)) {
                    strncpy(app->keyboard_layout, layout_name, sizeof(app->keyboard_layout) - 1);
                    app->keyboard_layout[sizeof(app->keyboard_layout) - 1] = '\0';

                    char* newline = strchr(app->keyboard_layout, '\n');
                    if(newline) *newline = '\0';
                    newline = strchr(app->keyboard_layout, '\r');
                    if(newline) *newline = '\0';
                } else if(name_len == 0) {
                    strncpy(app->keyboard_layout, "en-US.kl", sizeof(app->keyboard_layout) - 1);
                    app->keyboard_layout[sizeof(app->keyboard_layout) - 1] = '\0';
                }
            } else if(strncmp(line, "active_card_index=", 18) == 0) {
                size_t index = (size_t)atoi(line + 18);
                app->active_card_index = index;
            } else if(strncmp(line, "passcode_disabled=", 18) == 0) {
                int value = atoi(line + 18);
                app->passcode_disabled = (value != 0);
            } else if(strncmp(line, "hid_mode=", 9) == 0) {
#if HAS_BLE_HID_API
                int value = atoi(line + 9);
                if(value == HidModeUsb || value == HidModeBle) {
                    app->hid_mode = (HidMode)value;
                }
#else
                // BLE HID not available - force USB mode
                app->hid_mode = HidModeUsb;
#endif
            }

            if(storage_file_tell(file) == storage_file_size(file)) break;
        }

        storage_file_close(file);
    }
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    app_load_keyboard_layout(app);
}
