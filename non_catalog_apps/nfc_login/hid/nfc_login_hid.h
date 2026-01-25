#pragma once

#include "../nfc_login_app.h"

// HAS_BLE_HID_API is defined in nfc_login_hid.c after checking for headers
// Default to 0 if not defined yet (for files that include this header before the .c file)
#ifndef HAS_BLE_HID_API
    #define HAS_BLE_HID_API 0
#endif

void deinitialize_hid_with_restore(FuriHalUsbInterface* previous_config);
bool initialize_hid_and_wait(void);
bool initialize_hid_and_wait_with_mode(HidMode mode);
void deinitialize_hid_with_restore_and_mode(FuriHalUsbInterface* previous_config, HidMode mode);
void release_all_keys_with_mode(HidMode mode);
bool is_ble_hid_ready(void);
uint32_t app_type_password(App* app, const char* password);
void app_start_ble_advertising(void);
void app_stop_ble_advertising(void);