// protopirate_app_i.c
#include "protopirate_app_i.h"

#define TAG "ProtoPirateTxRx"

void protopirate_preset_init(
    void* context,
    const char* preset_name,
    uint32_t frequency,
    uint8_t* preset_data,
    size_t preset_data_size) {
    furi_assert(context);
    ProtoPirateApp* app = context;
    furi_string_set(app->txrx->preset->name, preset_name);
    app->txrx->preset->frequency = frequency;
    app->txrx->preset->data = preset_data;
    app->txrx->preset->data_size = preset_data_size;
}

bool protopirate_set_preset(ProtoPirateApp* app, const char* preset) {
    if(!strcmp(preset, "FuriHalSubGhzPresetOok270Async")) {
        furi_string_set(app->txrx->preset->name, "AM270");
    } else if(!strcmp(preset, "FuriHalSubGhzPresetOok650Async")) {
        furi_string_set(app->txrx->preset->name, "AM650");
    } else if(!strcmp(preset, "FuriHalSubGhzPreset2FSKDev238Async")) {
        furi_string_set(app->txrx->preset->name, "FM238");
    } else if(!strcmp(preset, "FuriHalSubGhzPreset2FSKDev12KAsync")) {
        furi_string_set(app->txrx->preset->name, "FM12K");
    } else if(!strcmp(preset, "FuriHalSubGhzPreset2FSKDev476Async")) {
        furi_string_set(app->txrx->preset->name, "FM476");
    } else if(!strcmp(preset, "FuriHalSubGhzPresetCustom")) {
        furi_string_set(app->txrx->preset->name, "CUSTOM");
    } else {
        FURI_LOG_E(TAG, "Unknown preset");
        return false;
    }
    return true;
}

void protopirate_get_frequency_modulation(
    ProtoPirateApp* app,
    FuriString* frequency,
    FuriString* modulation) {
    furi_assert(app);
    if(frequency != NULL) {
        furi_string_printf(
            frequency,
            "%03ld.%02ld",
            app->txrx->preset->frequency / 1000000 % 1000,
            app->txrx->preset->frequency / 10000 % 100);
    }
    if(modulation != NULL) {
        furi_string_printf(modulation, "%.2s", furi_string_get_cstr(app->txrx->preset->name));
    }
}

void protopirate_begin(ProtoPirateApp* app, uint8_t* preset_data) {
    furi_assert(app);
    subghz_devices_reset(app->txrx->radio_device);
    subghz_devices_idle(app->txrx->radio_device);
    subghz_devices_load_preset(app->txrx->radio_device, FuriHalSubGhzPresetCustom, preset_data);
    app->txrx->txrx_state = ProtoPirateTxRxStateIDLE;
}

uint32_t protopirate_rx(ProtoPirateApp* app, uint32_t frequency) {
    furi_assert(app);
    if(!subghz_devices_is_frequency_valid(app->txrx->radio_device, frequency)) {
        furi_crash("ProtoPirate: Incorrect RX frequency.");
    }
    furi_assert(
        app->txrx->txrx_state != ProtoPirateTxRxStateRx &&
        app->txrx->txrx_state != ProtoPirateTxRxStateSleep);

    subghz_devices_idle(app->txrx->radio_device);
    uint32_t value = subghz_devices_set_frequency(app->txrx->radio_device, frequency);
    subghz_devices_flush_rx(app->txrx->radio_device);
    subghz_devices_set_rx(app->txrx->radio_device);

    subghz_devices_start_async_rx(
        app->txrx->radio_device, subghz_worker_rx_callback, app->txrx->worker);

    subghz_worker_start(app->txrx->worker);
    app->txrx->txrx_state = ProtoPirateTxRxStateRx;
    return value;
}

void protopirate_idle(ProtoPirateApp* app) {
    furi_assert(app);
    furi_assert(app->txrx->txrx_state != ProtoPirateTxRxStateSleep);
    subghz_devices_idle(app->txrx->radio_device);
    app->txrx->txrx_state = ProtoPirateTxRxStateIDLE;
}

void protopirate_rx_end(ProtoPirateApp* app) {
    furi_assert(app);
    furi_assert(app->txrx->txrx_state == ProtoPirateTxRxStateRx);
    if(subghz_worker_is_running(app->txrx->worker)) {
        subghz_worker_stop(app->txrx->worker);
        subghz_devices_stop_async_rx(app->txrx->radio_device);
    }
    subghz_devices_idle(app->txrx->radio_device);
    app->txrx->txrx_state = ProtoPirateTxRxStateIDLE;
}

void protopirate_sleep(ProtoPirateApp* app) {
    furi_assert(app);
    subghz_devices_sleep(app->txrx->radio_device);
    app->txrx->txrx_state = ProtoPirateTxRxStateSleep;
}

void protopirate_hopper_update(ProtoPirateApp* app) {
    furi_assert(app);

    switch(app->txrx->hopper_state) {
    case ProtoPirateHopperStateOFF:
    case ProtoPirateHopperStatePause:
        return;
    case ProtoPirateHopperStateRSSITimeOut:
        if(app->txrx->hopper_timeout != 0) {
            app->txrx->hopper_timeout--;
            return;
        }
        break;
    default:
        break;
    }
    float rssi = -127.0f;
    if(app->txrx->hopper_state != ProtoPirateHopperStateRSSITimeOut) {
        rssi = subghz_devices_get_rssi(app->txrx->radio_device);

        if(rssi > -90.0f) {
            app->txrx->hopper_timeout = 10;
            app->txrx->hopper_state = ProtoPirateHopperStateRSSITimeOut;
            return;
        }
    } else {
        app->txrx->hopper_state = ProtoPirateHopperStateRunning;
    }

    if(app->txrx->hopper_idx_frequency <
       subghz_setting_get_hopper_frequency_count(app->setting) - 1) {
        app->txrx->hopper_idx_frequency++;
    } else {
        app->txrx->hopper_idx_frequency = 0;
    }

    if(app->txrx->txrx_state == ProtoPirateTxRxStateRx) {
        protopirate_rx_end(app);
    }
    if(app->txrx->txrx_state == ProtoPirateTxRxStateIDLE) {
        subghz_receiver_reset(app->txrx->receiver);
        app->txrx->preset->frequency =
            subghz_setting_get_hopper_frequency(app->setting, app->txrx->hopper_idx_frequency);
        protopirate_rx(app, app->txrx->preset->frequency);
    }
}

void protopirate_tx(ProtoPirateApp* app, uint32_t frequency) {
    furi_assert(app);
    if(!subghz_devices_is_frequency_valid(app->txrx->radio_device, frequency)) {
        return;
    }

    furi_assert(app->txrx->txrx_state == ProtoPirateTxRxStateIDLE);

    subghz_devices_idle(app->txrx->radio_device);
    subghz_devices_set_frequency(app->txrx->radio_device, frequency);
    subghz_devices_set_tx(app->txrx->radio_device);

    app->txrx->txrx_state = ProtoPirateTxRxStateTx;
}

void protopirate_tx_stop(ProtoPirateApp* app) {
    furi_assert(app);
    furi_assert(app->txrx->txrx_state == ProtoPirateTxRxStateTx);

    subghz_devices_idle(app->txrx->radio_device);
    app->txrx->txrx_state = ProtoPirateTxRxStateIDLE;
}
