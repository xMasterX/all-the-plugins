// helpers/radio_device_loader.c
#include "radio_device_loader.h"

#include <applications/drivers/subghz/cc1101_ext/cc1101_ext_interconnect.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <furi.h>
#include <furi_hal.h>

#define TAG "RadioDeviceLoader"

static void radio_device_loader_power_on() {
    uint8_t attempts = 0;
    while(!furi_hal_power_is_otg_enabled() && attempts++ < 5) {
        furi_hal_power_enable_otg();
        // CC1101 power-up time
        furi_delay_ms(10);
    }
    FURI_LOG_D(TAG, "OTG power enabled after %d attempts", attempts);
}

static void radio_device_loader_power_off() {
    if(furi_hal_power_is_otg_enabled()) {
        furi_hal_power_disable_otg();
        FURI_LOG_D(TAG, "OTG power disabled");
    }
}

bool radio_device_loader_is_connect_external(const char* name) {
    bool is_connect = false;
    bool is_otg_enabled = furi_hal_power_is_otg_enabled();

    if(!is_otg_enabled) {
        radio_device_loader_power_on();
    }

    const SubGhzDevice* device = subghz_devices_get_by_name(name);
    if(device) {
        is_connect = subghz_devices_is_connect(device);
        FURI_LOG_D(TAG, "External device '%s' connect check: %s", name, is_connect ? "YES" : "NO");
    } else {
        FURI_LOG_W(TAG, "Could not get device by name: %s", name);
    }

    if(!is_otg_enabled) {
        radio_device_loader_power_off();
    }
    return is_connect;
}

const SubGhzDevice* radio_device_loader_set(
    const SubGhzDevice* current_radio_device,
    SubGhzRadioDeviceType radio_device_type) {
    const SubGhzDevice* radio_device = NULL;

    if(radio_device_type == SubGhzRadioDeviceTypeExternalCC1101 &&
       radio_device_loader_is_connect_external(SUBGHZ_DEVICE_CC1101_EXT_NAME)) {
        radio_device_loader_power_on();
        radio_device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_EXT_NAME);
        if(radio_device) {
            subghz_devices_begin(radio_device);
            FURI_LOG_I(TAG, "External CC1101 initialized successfully");
        } else {
            FURI_LOG_E(TAG, "Failed to get external CC1101 device");
        }
    } else if(current_radio_device == NULL) {
        radio_device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
        if(radio_device) {
            FURI_LOG_I(TAG, "Internal CC1101 selected");
        } else {
            FURI_LOG_E(TAG, "Failed to load internal CC1101");
        }
    } else {
        radio_device_loader_end(current_radio_device);
        radio_device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
        FURI_LOG_I(TAG, "Switched to internal CC1101");
    }

    return radio_device;
}

bool radio_device_loader_is_external(const SubGhzDevice* radio_device) {
    if(!radio_device) {
        FURI_LOG_W(TAG, "is_external called with NULL device");
        return false;
    }

    const SubGhzDevice* internal_device =
        subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    bool is_external = (radio_device != internal_device);

    FURI_LOG_D(
        TAG,
        "is_external check: device=%p, internal=%p, result=%s",
        radio_device,
        internal_device,
        is_external ? "EXTERNAL" : "INTERNAL");

    return is_external;
}

void radio_device_loader_end(const SubGhzDevice* radio_device) {
    furi_assert(radio_device);

    radio_device_loader_power_off();

    if(radio_device != subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME)) {
        subghz_devices_end(radio_device);
        FURI_LOG_I(TAG, "External radio device ended");
    } else {
        FURI_LOG_D(TAG, "Internal radio device - no cleanup needed");
    }
}
