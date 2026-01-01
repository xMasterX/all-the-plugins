#include "nfc_login_hid.h"
#include "../settings/nfc_login_settings.h"

#include <furi_hal_usb.h>
#include <furi_hal_usb_hid.h>
#include <furi_hal_bt.h>
#include <furi_hal_version.h>
#include <usb_hid.h>
#include <furi.h>

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

static bool furi_hal_bt_hid_start(void) {
    if(g_ble_hid_profile && g_bt_service) {
        return true;
    }

    if(!g_bt_service) {
        g_bt_service = furi_record_open(RECORD_BT);
        if(!g_bt_service) {
            FURI_LOG_E(TAG, "Failed to open BT service");
            return false;
        }
    }

    bt_disconnect(g_bt_service);
    furi_delay_ms(200);

    g_ble_hid_profile = bt_profile_start(g_bt_service, ble_profile_hid, NULL);

    if(!g_ble_hid_profile) {
        FURI_LOG_E(TAG, "Failed to start BLE HID profile");
        return false;
    }

    furi_delay_ms(100);
    furi_hal_bt_start_advertising();
    furi_delay_ms(100);

    return true;
}

static bool furi_hal_bt_hid_is_connected(void) {
    if(!g_bt_service || !g_ble_hid_profile) {
        return false;
    }
    return furi_hal_bt_is_active();
}

static void furi_hal_bt_hid_disconnect(void) {
    if(g_bt_service) {
        bt_disconnect(g_bt_service);
        furi_delay_ms(200);
    }
    furi_hal_bt_stop_advertising();

    if(g_ble_hid_profile && g_bt_service) {
        bt_profile_restore_default(g_bt_service);
        g_ble_hid_profile = NULL;
    }

    if(g_bt_service) {
        furi_record_close(RECORD_BT);
        g_bt_service = NULL;
    }
}
#endif

void deinitialize_hid_with_restore(FuriHalUsbInterface* previous_config) {
    deinitialize_hid_with_restore_and_mode(previous_config, HidModeUsb);
}

void deinitialize_hid_with_restore_and_mode(FuriHalUsbInterface* previous_config, HidMode mode) {
#if HAS_BLE_HID_API
    if(mode == HidModeBle && g_ble_hid_profile) {
        ble_profile_hid_kb_release_all(g_ble_hid_profile);
    } else {
        furi_hal_hid_kb_release_all();
    }
#else
    furi_hal_hid_kb_release_all();
#endif

    furi_delay_ms(HID_INIT_DELAY_MS);

    if(mode == HidModeBle) {
        furi_hal_bt_hid_disconnect();
    } else {
        if(previous_config) {
            furi_hal_usb_set_config(previous_config, NULL);
        } else {
            furi_hal_usb_unlock();
        }
    }
    furi_delay_ms(HID_SETTLE_DELAY_MS);
}

bool initialize_hid_and_wait(void) {
    return initialize_hid_and_wait_with_mode(HidModeUsb);
}

bool initialize_hid_and_wait_with_mode(HidMode mode) {
    if(mode == HidModeBle) {
#ifdef BtIconHid
        furi_hal_bt_set_app_icon(BtIconHid);
#endif

        if(!furi_hal_bt_hid_start()) {
            return false;
        }

        uint8_t retries = HID_CONNECT_TIMEOUT_MS / HID_CONNECT_RETRY_MS;
        for(uint8_t i = 0; i < retries; i++) {
            if(furi_hal_bt_hid_is_connected()) {
                return true;
            }
            furi_delay_ms(HID_CONNECT_RETRY_MS);
        }
        return furi_hal_bt_hid_is_connected();
    } else {
        furi_hal_usb_unlock();
        if(!furi_hal_usb_set_config(&usb_hid, NULL)) {
            return false;
        }
        uint8_t retries = HID_CONNECT_TIMEOUT_MS / HID_CONNECT_RETRY_MS;
        for(uint8_t i = 0; i < retries; i++) {
            if(furi_hal_hid_is_connected()) {
                return true;
            }
            furi_delay_ms(HID_CONNECT_RETRY_MS);
        }
        return furi_hal_hid_is_connected();
    }
}

uint32_t app_type_password(App* app, const char* password) {
    if(!password) return 0;

    if(!app->layout_loaded) {
        app_load_keyboard_layout(app);
    }

    uint32_t approx_typed_ms = 0;
#if HAS_BLE_HID_API
    bool use_ble = (app && app->hid_mode == HidModeBle);
#endif

    for(size_t i = 0; password[i] != '\0'; i++) {
        unsigned char uc = (unsigned char)password[i];
        if(uc >= 128) continue;

        uint16_t full_keycode = app->layout[uc];
        if(full_keycode != HID_KEYBOARD_NONE) {
#if HAS_BLE_HID_API
            if(use_ble && g_ble_hid_profile) {
                ble_profile_hid_kb_press(g_ble_hid_profile, full_keycode);
            } else {
                furi_hal_hid_kb_press(full_keycode);
            }
#else
            furi_hal_hid_kb_press(full_keycode);
#endif

            furi_delay_ms(KEY_PRESS_DELAY_MS);

#if HAS_BLE_HID_API
            if(use_ble && g_ble_hid_profile) {
                ble_profile_hid_kb_release(g_ble_hid_profile, full_keycode);
            } else {
                furi_hal_hid_kb_release(full_keycode);
            }
#else
            furi_hal_hid_kb_release(full_keycode);
#endif

            furi_delay_ms(KEY_RELEASE_DELAY_MS);
            approx_typed_ms += KEY_PRESS_DELAY_MS + KEY_RELEASE_DELAY_MS;

            uint16_t delay = app ? app->input_delay_ms : KEY_RELEASE_DELAY_MS;
            furi_delay_ms(delay);
            approx_typed_ms += delay;
        }
    }

    if(app && app->append_enter) {
#if HAS_BLE_HID_API
        if(use_ble && g_ble_hid_profile) {
            ble_profile_hid_kb_press(g_ble_hid_profile, HID_KEYBOARD_RETURN);
        } else {
            furi_hal_hid_kb_press(HID_KEYBOARD_RETURN);
        }
#else
        furi_hal_hid_kb_press(HID_KEYBOARD_RETURN);
#endif

        furi_delay_ms(ENTER_PRESS_DELAY_MS);

#if HAS_BLE_HID_API
        if(use_ble && g_ble_hid_profile) {
            ble_profile_hid_kb_release(g_ble_hid_profile, HID_KEYBOARD_RETURN);
        } else {
            furi_hal_hid_kb_release(HID_KEYBOARD_RETURN);
        }
#else
        furi_hal_hid_kb_release(HID_KEYBOARD_RETURN);
#endif

        furi_delay_ms(ENTER_RELEASE_DELAY_MS);
        approx_typed_ms += ENTER_PRESS_DELAY_MS + ENTER_RELEASE_DELAY_MS;
    }

    return approx_typed_ms;
}

void app_start_ble_advertising(void) {
#if HAS_BLE_HID_API
    if(!g_bt_service) {
        g_bt_service = furi_record_open(RECORD_BT);
        if(!g_bt_service) {
            FURI_LOG_E(TAG, "Failed to open BT service");
            return;
        }
    }

    bt_disconnect(g_bt_service);
    furi_delay_ms(200);

    g_ble_hid_profile = bt_profile_start(g_bt_service, ble_profile_hid, NULL);

    if(!g_ble_hid_profile) {
        FURI_LOG_E(TAG, "Failed to start BLE HID profile");
        return;
    }

    furi_delay_ms(100);
    furi_hal_bt_start_advertising();
    furi_delay_ms(100);
#else
    FURI_LOG_W(TAG, "BLE HID not available");
#endif
}

void app_stop_ble_advertising(void) {
#if HAS_BLE_HID_API
    if(g_bt_service) {
        bt_disconnect(g_bt_service);
        furi_delay_ms(200);
    }

    furi_hal_bt_stop_advertising();

    if(g_ble_hid_profile && g_bt_service) {
        bt_profile_restore_default(g_bt_service);
        g_ble_hid_profile = NULL;
    }

    if(g_bt_service) {
        furi_record_close(RECORD_BT);
        g_bt_service = NULL;
    }
#endif
}
