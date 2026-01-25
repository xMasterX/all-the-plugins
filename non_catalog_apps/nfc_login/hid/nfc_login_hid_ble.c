#include "nfc_login_hid_ble.h"
#include "../settings/nfc_login_settings.h"

#include <furi_hal_bt.h>
#include <furi.h>
#include <string.h>

#undef HAS_BLE_HID_API

#ifdef __has_include
    #if __has_include(<extra_profiles/hid_profile.h>) && __has_include(<bt/bt_service/bt.h>)
        #include <extra_profiles/hid_profile.h>
        #include <bt/bt_service/bt.h>
        #define HAS_BLE_HID_API 1
    #else
        #define HAS_BLE_HID_API 0
    #endif
#else
    #define HAS_BLE_HID_API 0
#endif

#if HAS_BLE_HID_API
static FuriHalBleProfileBase* g_ble_hid_profile = NULL;
static Bt* g_bt_service = NULL;
static bool g_ble_is_connected = false;

static void ble_connection_status_callback(BtStatus status, void* context) {
    UNUSED(context);
    if(status == BtStatusConnected) {
        g_ble_is_connected = true;
        FURI_LOG_I(TAG, "BLE HID connected");
    } else if(status < BtStatusConnected) {
        g_ble_is_connected = false;
        FURI_LOG_I(TAG, "BLE HID disconnected");
    }
}

bool ble_hid_init(void) {
    // If already initialized and connected, do nothing
    if(g_ble_hid_profile && g_bt_service && g_ble_is_connected) {
        return true;
    }
    
    // Open BT service
    if(!g_bt_service) {
        g_bt_service = furi_record_open(RECORD_BT);
        if(!g_bt_service) {
            FURI_LOG_E(TAG, "Failed to open BT service");
            return false;
        }
    }
    
    // Only disconnect if we're not already connected (Windows 11 doesn't like forced disconnects)
    if(!g_ble_is_connected) {
        bt_disconnect(g_bt_service);
        furi_delay_ms(300); // Longer delay for Windows 11 compatibility
    }
    
    // Start BLE HID profile (or reuse existing)
    if(!g_ble_hid_profile) {
        g_ble_hid_profile = bt_profile_start(g_bt_service, ble_profile_hid, NULL);
        if(!g_ble_hid_profile) {
            FURI_LOG_E(TAG, "Failed to start BLE HID profile");
            return false;
        }
    }
    
    // Set connection callback
    bt_set_status_changed_callback(g_bt_service, ble_connection_status_callback, NULL);
    
    // Start advertising (if not already active)
    if(!furi_hal_bt_is_active()) {
        furi_delay_ms(150); // Longer delay for Windows 11
        furi_hal_bt_start_advertising();
        furi_delay_ms(150);
    }
    
    FURI_LOG_I(TAG, "BLE HID initialized and advertising");
    return true;
}

void ble_hid_deinit(void) {
    if(g_bt_service) {
        bt_set_status_changed_callback(g_bt_service, NULL, NULL);
        bt_disconnect(g_bt_service);
        furi_delay_ms(200);
    }
    
    furi_hal_bt_stop_advertising();
    g_ble_is_connected = false;
    
    if(g_ble_hid_profile && g_bt_service) {
        bt_profile_restore_default(g_bt_service);
        g_ble_hid_profile = NULL;
    }
    
    if(g_bt_service) {
        furi_record_close(RECORD_BT);
        g_bt_service = NULL;
    }
    
    FURI_LOG_I(TAG, "BLE HID deinitialized");
}

bool ble_hid_is_ready(void) {
    // BLE HID is ready only if profile exists AND we're connected
    return (g_ble_hid_profile != NULL && g_bt_service != NULL && g_ble_is_connected);
}

bool ble_hid_is_connected(void) {
    return g_ble_is_connected;
}

void ble_hid_release_all_keys(void) {
    if(g_ble_hid_profile) {
        ble_profile_hid_kb_release_all(g_ble_hid_profile);
    }
}

void ble_hid_press_key(uint16_t keycode) {
    if(!g_ble_hid_profile) {
        FURI_LOG_E(TAG, "BLE HID: Cannot press key - no profile");
        return;
    }
    bool result = ble_profile_hid_kb_press(g_ble_hid_profile, keycode);
    if(!result) {
        FURI_LOG_W(TAG, "BLE HID: Key press failed for keycode 0x%04X", keycode);
    }
}

void ble_hid_release_key(uint16_t keycode) {
    if(!g_ble_hid_profile) {
        FURI_LOG_E(TAG, "BLE HID: Cannot release key - no profile");
        return;
    }
    bool result = ble_profile_hid_kb_release(g_ble_hid_profile, keycode);
    if(!result) {
        FURI_LOG_W(TAG, "BLE HID: Key release failed for keycode 0x%04X", keycode);
    }
}

uint32_t ble_hid_type_password(App* app, const char* password) {
    if(!password || !app) return 0;
    
    if(!g_ble_hid_profile) {
        FURI_LOG_E(TAG, "BLE HID: No profile available");
        return 0;
    }
    
    if(!app->layout_loaded) {
        app_load_keyboard_layout(app);
    }
    
    uint32_t approx_typed_ms = 0;
    
    // Type each character using BLE ONLY
    for(size_t i = 0; password[i] != '\0'; i++) {
        unsigned char uc = (unsigned char)password[i];
        if(uc >= 128) {
            FURI_LOG_W(TAG, "BLE HID: Skipping non-ASCII char 0x%02X", uc);
            continue;
        }
        
        uint16_t full_keycode = app->layout[uc];
        if(full_keycode == HID_KEYBOARD_NONE) {
            FURI_LOG_W(TAG, "BLE HID: No keycode for char '%c' (0x%02X)", uc, uc);
            continue;
        }
        
        // Extract modifier and base keycode from full_keycode
        // Format: full_keycode = (modifier << 8) | keycode
        uint8_t modifier = (full_keycode >> 8) & 0xFF;
        uint8_t base_keycode = full_keycode & 0xFF;
        
        // Send key with appropriate format based on modifier
        if(modifier != 0) {
            ble_profile_hid_kb_press(g_ble_hid_profile, full_keycode);
        } else {
            ble_profile_hid_kb_press(g_ble_hid_profile, base_keycode);
        }
        furi_delay_ms(KEY_PRESS_DELAY_MS);
        
        // Release - use same format as press
        if(modifier != 0) {
            ble_profile_hid_kb_release(g_ble_hid_profile, full_keycode);
        } else {
            ble_profile_hid_kb_release(g_ble_hid_profile, base_keycode);
        }
        furi_delay_ms(KEY_RELEASE_DELAY_MS);
        approx_typed_ms += KEY_PRESS_DELAY_MS + KEY_RELEASE_DELAY_MS;
        
        uint16_t delay = app->input_delay_ms;
        furi_delay_ms(delay);
        approx_typed_ms += delay;
    }
    
    // Press Enter if needed
    if(app->append_enter) {
        ble_hid_press_key(HID_KEYBOARD_RETURN);
        furi_delay_ms(ENTER_PRESS_DELAY_MS);
        ble_hid_release_key(HID_KEYBOARD_RETURN);
        furi_delay_ms(ENTER_RELEASE_DELAY_MS);
        approx_typed_ms += ENTER_PRESS_DELAY_MS + ENTER_RELEASE_DELAY_MS;
    }
    
    return approx_typed_ms;
}

void ble_hid_start_advertising(void) {
    // If already initialized and connected, do nothing (Windows 11 doesn't like re-initialization)
    if(g_ble_hid_profile && g_bt_service && g_ble_is_connected) {
        return;
    }
    
    // If already initialized but not connected, just ensure advertising is on
    if(g_ble_hid_profile && g_bt_service) {
        if(!furi_hal_bt_is_active()) {
            furi_hal_bt_start_advertising();
        }
        return;
    }
    
    // Otherwise, initialize fresh
    ble_hid_init();
}

void ble_hid_stop_advertising(void) {
    ble_hid_deinit();
}

#else
// BLE HID not available - stub implementations
bool ble_hid_init(void) { return false; }
void ble_hid_deinit(void) {}
bool ble_hid_is_ready(void) { return false; }
bool ble_hid_is_connected(void) { return false; }
void ble_hid_release_all_keys(void) {}
void ble_hid_press_key(uint16_t keycode) { UNUSED(keycode); }
void ble_hid_release_key(uint16_t keycode) { UNUSED(keycode); }
uint32_t ble_hid_type_password(App* app, const char* password) { 
    UNUSED(app); 
    UNUSED(password); 
    return 0; 
}
void ble_hid_start_advertising(void) {}
void ble_hid_stop_advertising(void) {}
#endif
