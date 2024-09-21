#include "radio_scanner_app.h"
#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_gpio.h>
#include <gui/elements.h>
#include <furi_hal_speaker.h>
#include <subghz/devices/devices.h>

#define TAG "RadioScannerApp"

#define SUBGHZ_FREQUENCY_MIN  300000000
#define SUBGHZ_FREQUENCY_MAX  928000000
#define SUBGHZ_FREQUENCY_STEP 10000
#define SUBGHZ_DEVICE_NAME    "cc1101_int"

static void radio_scanner_draw_callback(Canvas* canvas, void* context) {
    FURI_LOG_D(TAG, "Enter radio_scanner_draw_callback");
    RadioScannerApp* app = context;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 2, AlignCenter, AlignTop, "Radio Scanner");

    canvas_set_font(canvas, FontSecondary);
    char freq_str[32];
    snprintf(freq_str, sizeof(freq_str), "Freq: %.2f MHz", (double)app->frequency / 1000000);
    canvas_draw_str_aligned(canvas, 64, 18, AlignCenter, AlignTop, freq_str);

    char rssi_str[32];
    snprintf(rssi_str, sizeof(rssi_str), "RSSI: %.2f", (double)app->rssi);
    canvas_draw_str_aligned(canvas, 64, 30, AlignCenter, AlignTop, rssi_str);

    char sensitivity_str[32];
    snprintf(sensitivity_str, sizeof(sensitivity_str), "Sens: %.2f", (double)app->sensitivity);
    canvas_draw_str_aligned(canvas, 64, 42, AlignCenter, AlignTop, sensitivity_str);

    canvas_draw_str_aligned(
        canvas, 64, 54, AlignCenter, AlignTop, app->scanning ? "Scanning..." : "Locked");

    FURI_LOG_D(TAG, "Exit radio_scanner_draw_callback");
}

static void radio_scanner_input_callback(InputEvent* input_event, void* context) {
    furi_assert(context);
    FURI_LOG_D(TAG, "Enter radio_scanner_input_callback");
    FURI_LOG_D(TAG, "Input event: type=%d, key=%d", input_event->type, input_event->key);
    FuriMessageQueue* event_queue = context;
    furi_message_queue_put(event_queue, input_event, FuriWaitForever);
    FURI_LOG_D(TAG, "Exit radio_scanner_input_callback");
}

static void radio_scanner_rx_callback(const void* data, size_t size, void* context) {
    (void)data;
    (void)context;

    FURI_LOG_D(TAG, "radio_scanner_rx_callback called with size: %zu", size);
}

static void radio_scanner_update_rssi(RadioScannerApp* app) {
    FURI_LOG_D(TAG, "Enter radio_scanner_update_rssi");
    if(app->radio_device) {
        app->rssi = subghz_devices_get_rssi(app->radio_device);
        FURI_LOG_D(TAG, "Updated RSSI: %f", (double)app->rssi);
    } else {
        FURI_LOG_E(TAG, "Radio device is NULL");
        app->rssi = -127.0f;
    }
    FURI_LOG_D(TAG, "Exit radio_scanner_update_rssi");
}

static bool radio_scanner_init_subghz(RadioScannerApp* app) {
    FURI_LOG_D(TAG, "Enter radio_scanner_init_subghz");
    subghz_devices_init();
    FURI_LOG_D(TAG, "SubGHz devices initialized");

    const SubGhzDevice* device = subghz_devices_get_by_name(SUBGHZ_DEVICE_NAME);
    if(!device) {
        FURI_LOG_E(TAG, "Failed to get SubGhzDevice");
        return false;
    }
    FURI_LOG_D(TAG, "SubGhzDevice obtained: %s", subghz_devices_get_name(device));

    app->radio_device = device;

    subghz_devices_begin(device);
    FURI_LOG_D(TAG, "SubGhzDevice begun");

    if(!subghz_devices_is_frequency_valid(device, app->frequency)) {
        FURI_LOG_E(TAG, "Invalid frequency: %lu", app->frequency);
        return false;
    }
    FURI_LOG_D(TAG, "Frequency is valid: %lu", app->frequency);

    subghz_devices_load_preset(device, FuriHalSubGhzPreset2FSKDev238Async, NULL);
    FURI_LOG_D(TAG, "Preset loaded");

    subghz_devices_set_frequency(device, app->frequency);
    FURI_LOG_D(TAG, "Frequency set to %lu", app->frequency);

    subghz_devices_start_async_rx(device, radio_scanner_rx_callback, app);
    FURI_LOG_D(TAG, "Asynchronous RX started");

    if(furi_hal_speaker_acquire(30)) {
        app->speaker_acquired = true;
        subghz_devices_set_async_mirror_pin(device, &gpio_speaker);
        FURI_LOG_D(TAG, "Speaker acquired and async mirror pin set");
    } else {
        app->speaker_acquired = false;
        FURI_LOG_E(TAG, "Failed to acquire speaker");
    }

    FURI_LOG_D(TAG, "Exit radio_scanner_init_subghz");
    return true;
}

static void radio_scanner_process_scanning(RadioScannerApp* app) {
    FURI_LOG_D(TAG, "Enter radio_scanner_process_scanning");
    radio_scanner_update_rssi(app);
    FURI_LOG_D(TAG, "RSSI after update: %f", (double)app->rssi);
    bool signal_detected = (app->rssi > app->sensitivity);
    FURI_LOG_D(TAG, "Signal detected: %d", signal_detected);

    if(signal_detected) {
        if(app->scanning) {
            app->scanning = false;
            FURI_LOG_D(TAG, "Scanning stopped");
        }
    } else {
        if(!app->scanning) {
            app->scanning = true;
            FURI_LOG_D(TAG, "Scanning started");
        }
    }

    if(app->scanning) {
        uint32_t new_frequency = (app->scan_direction == ScanDirectionUp) ?
                                     app->frequency + SUBGHZ_FREQUENCY_STEP :
                                     app->frequency - SUBGHZ_FREQUENCY_STEP;

        if(!subghz_devices_is_frequency_valid(app->radio_device, new_frequency)) {
            if(app->scan_direction == ScanDirectionUp) {
                if(new_frequency < 387000000) {
                    new_frequency = 387000000;
                } else if(new_frequency < 779000000) {
                    new_frequency = 779000000;
                } else if(new_frequency > 928000000) {
                    new_frequency = 300000000;
                }
            } else {
                if(new_frequency > 464000000) {
                    new_frequency = 464000000;
                } else if(new_frequency > 348000000) {
                    new_frequency = 348000000;
                } else if(new_frequency < 300000000) {
                    new_frequency = 928000000;
                }
            }
            FURI_LOG_D(TAG, "Adjusted frequency to next valid range: %lu", new_frequency);
        }

        subghz_devices_stop_async_rx(app->radio_device);
        FURI_LOG_D(TAG, "Asynchronous RX stopped");

        subghz_devices_idle(app->radio_device);
        FURI_LOG_D(TAG, "Device set to idle");

        app->frequency = new_frequency;
        subghz_devices_set_frequency(app->radio_device, app->frequency);
        FURI_LOG_D(TAG, "Frequency set to %lu", app->frequency);

        subghz_devices_start_async_rx(app->radio_device, radio_scanner_rx_callback, app);
        FURI_LOG_D(TAG, "Asynchronous RX restarted");
    }
    FURI_LOG_D(TAG, "Exit radio_scanner_process_scanning");
}

RadioScannerApp* radio_scanner_app_alloc() {
    FURI_LOG_D(TAG, "Enter radio_scanner_app_alloc");
    RadioScannerApp* app = malloc(sizeof(RadioScannerApp));
    if(!app) {
        FURI_LOG_E(TAG, "Failed to allocate RadioScannerApp");
        return NULL;
    }
    FURI_LOG_D(TAG, "RadioScannerApp allocated");

    app->view_port = view_port_alloc();
    FURI_LOG_D(TAG, "ViewPort allocated");

    app->event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    FURI_LOG_D(TAG, "Event queue allocated");

    app->running = true;
    app->frequency = 433920000;
    app->rssi = -100.0f;
    app->sensitivity = -85.0f;
    app->scanning = true;
    app->scan_direction = ScanDirectionUp;
    app->speaker_acquired = false;
    app->radio_device = NULL;

    view_port_draw_callback_set(app->view_port, radio_scanner_draw_callback, app);
    FURI_LOG_D(TAG, "Draw callback set");

    view_port_input_callback_set(app->view_port, radio_scanner_input_callback, app->event_queue);
    FURI_LOG_D(TAG, "Input callback set");

    FURI_LOG_D(TAG, "Exit radio_scanner_app_alloc");
    return app;
}

void radio_scanner_app_free(RadioScannerApp* app) {
    FURI_LOG_D(TAG, "Enter radio_scanner_app_free");
    if(app->speaker_acquired && furi_hal_speaker_is_mine()) {
        subghz_devices_set_async_mirror_pin(app->radio_device, NULL);
        furi_hal_speaker_release();
        app->speaker_acquired = false;
        FURI_LOG_D(TAG, "Speaker released");
    }

    if(app->radio_device) {
        subghz_devices_stop_async_rx(app->radio_device);
        FURI_LOG_D(TAG, "Asynchronous RX stopped");
        subghz_devices_end(app->radio_device);
        FURI_LOG_D(TAG, "SubGhzDevice stopped and ended");
    }

    subghz_devices_deinit();
    FURI_LOG_D(TAG, "SubGHz devices deinitialized");

    view_port_free(app->view_port);
    FURI_LOG_D(TAG, "ViewPort freed");

    furi_message_queue_free(app->event_queue);
    FURI_LOG_D(TAG, "Event queue freed");

    free(app);
    FURI_LOG_D(TAG, "RadioScannerApp memory freed");
}

int32_t radio_scanner_app(void* p) {
    UNUSED(p);
    FURI_LOG_D(TAG, "Enter radio_scanner_app");

    RadioScannerApp* app = radio_scanner_app_alloc();
    if(!app) {
        FURI_LOG_E(TAG, "Failed to allocate app");
        return -1;
    }

    Gui* gui = furi_record_open(RECORD_GUI);
    FURI_LOG_D(TAG, "GUI record opened");

    gui_add_view_port(gui, app->view_port, GuiLayerFullscreen);
    FURI_LOG_D(TAG, "ViewPort added to GUI");

    if(!radio_scanner_init_subghz(app)) {
        FURI_LOG_E(TAG, "Failed to initialize SubGHz");
        radio_scanner_app_free(app);
        return 255;
    }
    FURI_LOG_D(TAG, "SubGHz initialized successfully");

    InputEvent event;
    while(app->running) {
        FURI_LOG_D(TAG, "Main loop iteration");
        if(app->scanning) {
            FURI_LOG_D(TAG, "Scanning is active");
            radio_scanner_process_scanning(app);
        } else {
            FURI_LOG_D(TAG, "Scanning is inactive, updating RSSI");
            radio_scanner_update_rssi(app);
        }

        FURI_LOG_D(TAG, "Checking for input events");
        if(furi_message_queue_get(app->event_queue, &event, 10) == FuriStatusOk) {
            FURI_LOG_D(TAG, "Input event received: type=%d, key=%d", event.type, event.key);
            if(event.type == InputTypeShort) {
                if(event.key == InputKeyOk) {
                    app->scanning = !app->scanning;
                    FURI_LOG_D(TAG, "Toggled scanning: %d", app->scanning);
                } else if(event.key == InputKeyUp) {
                    app->sensitivity += 1.0f;
                    FURI_LOG_D(TAG, "Increased sensitivity: %f", (double)app->sensitivity);
                } else if(event.key == InputKeyDown) {
                    app->sensitivity -= 1.0f;
                    FURI_LOG_D(TAG, "Decreased sensitivity: %f", (double)app->sensitivity);
                } else if(event.key == InputKeyLeft) {
                    app->scan_direction = ScanDirectionDown;
                    FURI_LOG_D(TAG, "Scan direction set to down");
                } else if(event.key == InputKeyRight) {
                    app->scan_direction = ScanDirectionUp;
                    FURI_LOG_D(TAG, "Scan direction set to up");
                } else if(event.key == InputKeyBack) {
                    app->running = false;
                    FURI_LOG_D(TAG, "Exiting app");
                }
            }
        }

        view_port_update(app->view_port);
        furi_delay_ms(10);
    }

    if(app->speaker_acquired && furi_hal_speaker_is_mine()) {
        subghz_devices_set_async_mirror_pin(app->radio_device, NULL);
        furi_hal_speaker_release();
        app->speaker_acquired = false;
        FURI_LOG_D(TAG, "Speaker released at app exit");
    }

    gui_remove_view_port(gui, app->view_port);
    FURI_LOG_D(TAG, "ViewPort removed from GUI");

    furi_record_close(RECORD_GUI);
    FURI_LOG_D(TAG, "GUI record closed");

    radio_scanner_app_free(app);
    FURI_LOG_D(TAG, "Exit radio_scanner_app");
    return 0;
}
