#pragma once

#include "../nfc_login_app.h"
#include <furi_hal_usb.h>

// USB HID functions - completely separate from BLE
bool usb_hid_init(void);
void usb_hid_deinit(void);
void usb_hid_deinit_with_restore(FuriHalUsbInterface* previous_config);
FuriHalUsbInterface* usb_hid_get_previous_config(void);
void usb_hid_set_previous_config(FuriHalUsbInterface* config);
void usb_hid_release_all_keys(void);
void usb_hid_press_key(uint16_t keycode);
void usb_hid_release_key(uint16_t keycode);
uint32_t usb_hid_type_password(App* app, const char* password);
