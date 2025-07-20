#include "subghz_signal_gen.h"
#include <furi_hal_region.h>
#include <furi.h>
#include <gui/gui.h>
#include <subghz/devices/devices.h>
#include <furi/core/log.h>
#include <furi_hal.h>
#include <lib/subghz/subghz_tx_rx_worker.h>
#include "helpers/radio_device_loader.h"

#define TAG "SubGhzSignalGen"
#define SUBGHZ_FREQUENCY_MIN 300000000
#define SUBGHZ_FREQUENCY_MAX 928000000
#define MESSAGE_MAX_LEN 1024

static FuriHalRegion unlockedRegion = {
    .country_code = "FTW",
    .bands_count = 3,
    .bands = {
        {.start = 299999755, .end = 348000000, .power_limit = 20, .duty_cycle = 50},
        {.start = 386999938, .end = 464000000, .power_limit = 20, .duty_cycle = 50},
        {.start = 778999847, .end = 928000000, .power_limit = 20, .duty_cycle = 50},
    },
};

typedef struct
{
    uint32_t min;
    uint32_t max;
} FrequencyBand;

static const FrequencyBand valid_frequency_bands[] = {
    {300000000, 348000000},
    {387000000, 464000000},
    {779000000, 928000000},
};

#define NUM_FREQUENCY_BANDS (sizeof(valid_frequency_bands) / sizeof(valid_frequency_bands[0]))

static const char *signal_modes[] = {
    "OOK 650kHz",
    "2FSK 2.38kHz",
    "2FSK 47.6kHz",
    "MSK 99.97Kb/s",
    "GFSK 9.99Kb/s",
    "Pattern 0xFF",
    "Sine Wave",
    "Square Wave",
    "Sawtooth Wave",
    "White Noise",
    "Triangle Wave",
    "Chirp Signal",
    "Gaussian Noise",
    "Burst Mode"};

static void subghz_signal_gen_show_splash_screen(SubGhzSignalGenApp *app);
static bool subghz_signal_gen_init_subghz(SubGhzSignalGenApp *app);
static void subghz_signal_gen_start_tx(SubGhzSignalGenApp *app);
static void subghz_signal_gen_switch_mode(SubGhzSignalGenApp *app);
static void subghz_signal_gen_update_view(SubGhzSignalGenApp *app);
static void subghz_signal_gen_adjust_frequency(SubGhzSignalGenApp *app, bool up);
static uint32_t adjust_frequency_to_valid(uint32_t frequency, bool up);
static bool is_frequency_valid(uint32_t frequency);
static int32_t subghz_signal_gen_tx_thread(void *context);
static void subghz_signal_gen_splash_screen_draw_callback(Canvas *canvas, void *context);
static void subghz_signal_gen_draw_callback(Canvas *canvas, void *context);
static void subghz_signal_gen_input_callback(InputEvent *input_event, void *context);

int32_t subghz_signal_gen_app(void *p)
{
    UNUSED(p);

    FURI_LOG_I(TAG, "Starting SubGHz Signal Generator");

    SubGhzSignalGenApp *app = subghz_signal_gen_app_alloc();
    if (!app)
    {
        FURI_LOG_E(TAG, "Failed to allocate SubGhzSignalGenApp");
        return -1;
    }

    subghz_signal_gen_show_splash_screen(app);

    if (!subghz_signal_gen_init_subghz(app))
    {
        subghz_signal_gen_app_free(app);
        return -1;
    }

    subghz_signal_gen_start_tx(app);

    FURI_LOG_I(TAG, "Entering main loop");

    InputEvent event;
    while (app->running)
    {
        if (furi_message_queue_get(app->event_queue, &event, 10) == FuriStatusOk)
        {
            if (event.type == InputTypeShort)
            {
                switch (event.key)
                {
                case InputKeyOk:
                    FURI_LOG_I(TAG, "OK button pressed");
                    subghz_signal_gen_switch_mode(app);
                    subghz_signal_gen_update_view(app);
                    break;
                case InputKeyBack:
                    FURI_LOG_I(TAG, "Back button pressed");
                    app->running = false;
                    break;
                case InputKeyRight:
                    FURI_LOG_I(TAG, "Right button pressed");
                    if (app->cursor_position < 4)
                    {
                        app->cursor_position++;
                        subghz_signal_gen_update_view(app);
                    }
                    break;
                case InputKeyLeft:
                    FURI_LOG_I(TAG, "Left button pressed");
                    if (app->cursor_position > 0)
                    {
                        app->cursor_position--;
                        subghz_signal_gen_update_view(app);
                    }
                    break;
                case InputKeyUp:
                    FURI_LOG_I(TAG, "Up button pressed");
                    subghz_signal_gen_adjust_frequency(app, true);
                    subghz_signal_gen_update_view(app);
                    break;
                case InputKeyDown:
                    FURI_LOG_I(TAG, "Down button pressed");
                    subghz_signal_gen_adjust_frequency(app, false);
                    subghz_signal_gen_update_view(app);
                    break;
                default:
                    break;
                }
            }
        }
    }

    FURI_LOG_I(TAG, "Exiting SubGHz Signal Generator main loop");

    subghz_signal_gen_app_free(app);

    FURI_LOG_I(TAG, "SubGHz Signal Generator exited");

    return 0;
}

SubGhzSignalGenApp *subghz_signal_gen_app_alloc(void)
{
    SubGhzSignalGenApp *app = malloc(sizeof(SubGhzSignalGenApp));
    if (!app)
    {
        return NULL;
    }

    app->view_port = view_port_alloc();
    app->event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->frequency = 315000000;
    app->cursor_position = 0;
    app->running = true;
    app->tx_running = false;
    app->signal_mode = SignalModeOok650Async;
    app->gui = furi_record_open(RECORD_GUI);

    furi_hal_region_set(&unlockedRegion);

    view_port_draw_callback_set(app->view_port, subghz_signal_gen_draw_callback, app);
    view_port_input_callback_set(app->view_port, subghz_signal_gen_input_callback, app);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    app->tx_thread = NULL;
    app->subghz_txrx = NULL;
    app->device = NULL;

    subghz_devices_init();
    app->subghz_txrx = subghz_tx_rx_worker_alloc();

    furi_hal_power_suppress_charge_enter();

    return app;
}

void subghz_signal_gen_app_free(SubGhzSignalGenApp *app)
{
    furi_assert(app);

#ifdef FURI_DEBUG
    FURI_LOG_D(TAG, "Enter subghz_signal_gen_app_free");
#endif

    if (app->tx_running)
    {
        app->tx_running = false;
#ifdef FURI_DEBUG
        FURI_LOG_D(TAG, "Set tx_running to false");
#endif
    }

    if (app->tx_thread)
    {
#ifdef FURI_DEBUG
        FURI_LOG_D(TAG, "Joining tx_thread");
#endif
        furi_thread_join(app->tx_thread);
        furi_thread_free(app->tx_thread);
        app->tx_thread = NULL;
#ifdef FURI_DEBUG
        FURI_LOG_D(TAG, "tx_thread joined and freed");
#endif
    }

    if (app->subghz_txrx)
    {
        if (subghz_tx_rx_worker_is_running(app->subghz_txrx))
        {
#ifdef FURI_DEBUG
            FURI_LOG_D(TAG, "subghz_txrx worker is running, stopping");
#endif
            subghz_tx_rx_worker_stop(app->subghz_txrx);
#ifdef FURI_DEBUG
            FURI_LOG_D(TAG, "subghz_txrx worker stopped");
#endif
        }
#ifdef FURI_DEBUG
        FURI_LOG_D(TAG, "Freeing subghz_txrx worker");
#endif
        subghz_tx_rx_worker_free(app->subghz_txrx);
        app->subghz_txrx = NULL;
#ifdef FURI_DEBUG
        FURI_LOG_D(TAG, "subghz_txrx worker freed");
#endif
    }

#ifdef FURI_DEBUG
    FURI_LOG_D(TAG, "Ending radio device");
#endif
    if (app->device)
    {
#ifdef FURI_DEBUG
        const char *device_name = app->device->name ? app->device->name : "Unknown";
        FURI_LOG_D(TAG, "Device name: %s", device_name);
#endif

        if (radio_device_loader_is_external(app->device))
        {
#ifdef FURI_DEBUG
            FURI_LOG_D(TAG, "Device is external, performing manual cleanup");
#endif
            if (furi_hal_power_is_otg_enabled())
            {
#ifdef FURI_DEBUG
                FURI_LOG_D(TAG, "OTG power is enabled, disabling");
#endif
                furi_hal_power_disable_otg();
#ifdef FURI_DEBUG
                FURI_LOG_D(TAG, "OTG power disabled");
#endif
            }
        }
        else
        {
#ifdef FURI_DEBUG
            FURI_LOG_D(TAG, "Device is internal, calling radio_device_loader_end");
#endif
            radio_device_loader_end(app->device);
#ifdef FURI_DEBUG
            FURI_LOG_D(TAG, "radio_device_loader_end completed");
#endif
        }

        app->device = NULL;
#ifdef FURI_DEBUG
        FURI_LOG_D(TAG, "Radio device ended");
#endif
    }

#ifdef FURI_DEBUG
    FURI_LOG_D(TAG, "Calling subghz_devices_deinit");
#endif
    subghz_devices_deinit();
#ifdef FURI_DEBUG
    FURI_LOG_D(TAG, "subghz_devices_deinit completed");
#endif

#ifdef FURI_DEBUG
    FURI_LOG_D(TAG, "Exiting power suppression mode");
#endif
    furi_hal_power_suppress_charge_exit();

#ifdef FURI_DEBUG
    FURI_LOG_D(TAG, "Removing view port from GUI");
#endif
    gui_remove_view_port(app->gui, app->view_port);
#ifdef FURI_DEBUG
    FURI_LOG_D(TAG, "gui_remove_view_port completed");
#endif

    view_port_free(app->view_port);
#ifdef FURI_DEBUG
    FURI_LOG_D(TAG, "view_port freed");
#endif

#ifdef FURI_DEBUG
    FURI_LOG_D(TAG, "Closing GUI record");
#endif
    furi_record_close(RECORD_GUI);
#ifdef FURI_DEBUG
    FURI_LOG_D(TAG, "GUI record closed");
#endif

#ifdef FURI_DEBUG
    FURI_LOG_D(TAG, "Freeing event queue");
#endif
    furi_message_queue_free(app->event_queue);
#ifdef FURI_DEBUG
    FURI_LOG_D(TAG, "Event queue freed");
#endif

    free(app);
#ifdef FURI_DEBUG
    FURI_LOG_D(TAG, "Exit subghz_signal_gen_app_free");
#endif
}

static bool subghz_signal_gen_init_subghz(SubGhzSignalGenApp *app)
{
#ifdef FURI_DEBUG
    FURI_LOG_D(TAG, "Enter subghz_signal_gen_init_subghz");
#endif
    app->device = radio_device_loader_set(NULL, SubGhzRadioDeviceTypeExternalCC1101);

    if (!app->device)
    {
        FURI_LOG_W(TAG, "External CC1101 not found, trying internal CC1101.");
        app->device = radio_device_loader_set(NULL, SubGhzRadioDeviceTypeInternal);
        if (!app->device)
        {
            FURI_LOG_E(TAG, "Failed to initialize internal CC1101.");
            return false;
        }
    }

    subghz_devices_reset(app->device);
    subghz_devices_idle(app->device);

    FURI_LOG_I(TAG, "Initialized device %s", app->device->name);

    subghz_devices_load_preset(app->device, FuriHalSubGhzPresetOok650Async, NULL);

    if (subghz_tx_rx_worker_start(app->subghz_txrx, app->device, app->frequency))
    {
        FURI_LOG_I(TAG, "Started SubGhz TX RX worker with device %s", app->device->name);
#ifdef FURI_DEBUG
        FURI_LOG_D(TAG, "Exit subghz_signal_gen_init_subghz");
#endif
        return true;
    }
    else
    {
        FURI_LOG_E(TAG, "Failed to start TX RX worker with device %s", app->device->name);
        subghz_tx_rx_worker_stop(app->subghz_txrx);
#ifdef FURI_DEBUG
        FURI_LOG_D(TAG, "Exit subghz_signal_gen_init_subghz");
#endif
        return false;
    }
}

static void subghz_signal_gen_show_splash_screen(SubGhzSignalGenApp *app)
{
    view_port_draw_callback_set(app->view_port, subghz_signal_gen_splash_screen_draw_callback, app);
    view_port_update(app->view_port);
    furi_delay_ms(2000);
    view_port_draw_callback_set(app->view_port, subghz_signal_gen_draw_callback, app);
}

static void subghz_signal_gen_splash_screen_draw_callback(Canvas *canvas, void *context)
{
    UNUSED(context);

    canvas_clear(canvas);

    for (int x = 0; x < 128; x += 8)
    {
        for (int y = 0; y < 64; y += 8)
        {
            canvas_draw_dot(canvas, x, y);
        }
    }

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 10, AlignCenter, AlignTop, "SubGHz");
    canvas_draw_str_aligned(canvas, 64, 22, AlignCenter, AlignTop, "Signal Generator");
    canvas_draw_str_aligned(canvas, 64, 40, AlignCenter, AlignTop, "by RocketGod");
    canvas_draw_frame(canvas, 0, 0, 128, 64);
}

static void subghz_signal_gen_draw_callback(Canvas *canvas, void *context)
{
    SubGhzSignalGenApp *app = (SubGhzSignalGenApp *)context;
    canvas_clear(canvas);

    char freq_str[20];
    snprintf(freq_str, sizeof(freq_str), "%3lu.%02lu",
             app->frequency / 1000000,
             (app->frequency % 1000000) / 10000);

    int total_width = strlen(freq_str) * 12;
    int start_x = (128 - total_width) / 2;
    int digit_position = 0;

    for (size_t i = 0; i < strlen(freq_str); i++)
    {
        bool highlight = (digit_position == app->cursor_position);

        if (freq_str[i] != '.')
        {
            canvas_set_font(canvas, highlight ? FontBigNumbers : FontPrimary);
            char temp[2] = {freq_str[i], '\0'};
            canvas_draw_str_aligned(canvas, start_x + (i * 12), 10, AlignCenter, AlignTop, temp);
            digit_position++;
        }
        else
        {
            canvas_set_font(canvas, FontPrimary);
            char temp[2] = {freq_str[i], '\0'};
            canvas_draw_str_aligned(canvas, start_x + (i * 12), 10, AlignCenter, AlignTop, temp);
        }
    }

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 55, AlignCenter, AlignTop, signal_modes[app->signal_mode]);
}

static void subghz_signal_gen_input_callback(InputEvent *input_event, void *context)
{
    SubGhzSignalGenApp *app = (SubGhzSignalGenApp *)context;
    furi_message_queue_put(app->event_queue, input_event, FuriWaitForever);
}

static void subghz_signal_gen_adjust_frequency(SubGhzSignalGenApp *app, bool up)
{
#ifdef FURI_DEBUG
    FURI_LOG_D(TAG, "Enter subghz_signal_gen_adjust_frequency");
#endif
    uint32_t frequency = app->frequency;
    uint32_t step;

    switch (app->cursor_position)
    {
    case 0:
        step = 100000000;
        break;
    case 1:
        step = 10000000;
        break;
    case 2:
        step = 1000000;
        break;
    case 3:
        step = 100000;
        break;
    case 4:
        step = 10000;
        break;
    default:
        return;
    }

    frequency = up ? frequency + step : frequency - step;

    if (frequency > SUBGHZ_FREQUENCY_MAX)
    {
        frequency = SUBGHZ_FREQUENCY_MIN;
    }
    else if (frequency < SUBGHZ_FREQUENCY_MIN)
    {
        frequency = SUBGHZ_FREQUENCY_MAX;
    }

    frequency = adjust_frequency_to_valid(frequency, up);
    app->frequency = frequency;

    FURI_LOG_I(TAG, "Frequency adjusted to %lu Hz", app->frequency);

    if (app->tx_running)
    {
        if (app->subghz_txrx && app->device)
        {
            subghz_tx_rx_worker_stop(app->subghz_txrx);
            if (subghz_tx_rx_worker_start(app->subghz_txrx, app->device, app->frequency))
            {
                FURI_LOG_I(TAG, "Restarted SubGhz TX RX worker with new frequency %lu Hz", app->frequency);
            }
            else
            {
                FURI_LOG_E(TAG, "Failed to restart SubGhz TX RX worker");
            }
        }
        else
        {
            FURI_LOG_E(TAG, "Cannot adjust frequency, subghz_txrx or device is NULL");
        }
    }
#ifdef FURI_DEBUG
    FURI_LOG_D(TAG, "Exit subghz_signal_gen_adjust_frequency");
#endif
}

static void subghz_signal_gen_switch_mode(SubGhzSignalGenApp *app)
{
#ifdef FURI_DEBUG
    FURI_LOG_D(TAG, "Enter subghz_signal_gen_switch_mode");
#endif
    if (app->tx_running)
    {
        app->tx_running = false;
        if (app->tx_thread)
        {
            furi_thread_join(app->tx_thread);
            furi_thread_free(app->tx_thread);
            app->tx_thread = NULL;
        }
    }

    if (app->subghz_txrx)
    {
        if (subghz_tx_rx_worker_is_running(app->subghz_txrx))
        {
            subghz_tx_rx_worker_stop(app->subghz_txrx);
        }
    }

    if (!app->device)
    {
        app->device = radio_device_loader_set(NULL, SubGhzRadioDeviceTypeExternalCC1101);
        if (!app->device)
        {
            app->device = radio_device_loader_set(NULL, SubGhzRadioDeviceTypeInternal);
            if (!app->device)
            {
                return;
            }
        }
    }

    subghz_devices_begin(app->device);
    subghz_devices_reset(app->device);
    subghz_devices_idle(app->device);

    app->signal_mode = (app->signal_mode + 1) % 14;

    switch (app->signal_mode)
    {
    case SignalModeOok650Async:
        subghz_devices_load_preset(app->device, FuriHalSubGhzPresetOok650Async, NULL);
        break;
    case SignalMode2FSKDev238Async:
        subghz_devices_load_preset(app->device, FuriHalSubGhzPreset2FSKDev238Async, NULL);
        break;
    case SignalMode2FSKDev476Async:
        subghz_devices_load_preset(app->device, FuriHalSubGhzPreset2FSKDev476Async, NULL);
        break;
    case SignalModeMSK99_97KbAsync:
        subghz_devices_load_preset(app->device, FuriHalSubGhzPresetMSK99_97KbAsync, NULL);
        break;
    case SignalModeGFSK9_99KbAsync:
        subghz_devices_load_preset(app->device, FuriHalSubGhzPresetGFSK9_99KbAsync, NULL);
        break;
    case SignalModePattern0xFF:
        subghz_devices_load_preset(app->device, FuriHalSubGhzPresetOok650Async, NULL);
        break;
    case SignalModeSineWave:
    case SignalModeSquareWave:
    case SignalModeSawtoothWave:
    case SignalModeWhiteNoise:
    case SignalModeTriangleWave:
    case SignalModeChirp:
    case SignalModeGaussianNoise:
    case SignalModeBurst:
        FURI_LOG_I(TAG, "Switched to waveform generation mode: %d", app->signal_mode);
        break;
    default:
        return;
    }

    if (app->subghz_txrx)
    {
        if (subghz_tx_rx_worker_start(app->subghz_txrx, app->device, app->frequency))
        {
            subghz_signal_gen_start_tx(app);
        }
    }
    else
    {
        app->subghz_txrx = subghz_tx_rx_worker_alloc();
        if (app->subghz_txrx)
        {
            if (subghz_tx_rx_worker_start(app->subghz_txrx, app->device, app->frequency))
            {
                subghz_signal_gen_start_tx(app);
            }
        }
    }
#ifdef FURI_DEBUG
    FURI_LOG_D(TAG, "Exit subghz_signal_gen_switch_mode");
#endif
}

static void subghz_signal_gen_start_tx(SubGhzSignalGenApp *app)
{
#ifdef FURI_DEBUG
    FURI_LOG_D(TAG, "Enter subghz_signal_gen_start_tx");
#endif
    app->tx_running = true;
    app->tx_thread = furi_thread_alloc();
    furi_thread_set_name(app->tx_thread, "Signal Gen TX");
    furi_thread_set_stack_size(app->tx_thread, 2048);
    furi_thread_set_context(app->tx_thread, app);
    furi_thread_set_callback(app->tx_thread, subghz_signal_gen_tx_thread);
    furi_thread_start(app->tx_thread);
#ifdef FURI_DEBUG
    FURI_LOG_D(TAG, "Exit subghz_signal_gen_start_tx");
#endif
}

static int32_t subghz_signal_gen_tx_thread(void *context)
{
    SubGhzSignalGenApp *app = context;
    uint8_t signal_data[MESSAGE_MAX_LEN];

    const char *device_name = (app->device && app->device->name) ? app->device->name : "Unknown";
    FURI_LOG_I(TAG, "TX Thread started with mode %d on device %s", app->signal_mode, device_name);

    switch (app->signal_mode)
    {
    case SignalModeOok650Async:
        memset(signal_data, 0xFF, sizeof(signal_data));
        break;
    case SignalMode2FSKDev238Async:
    case SignalMode2FSKDev476Async:
        for (size_t i = 0; i < sizeof(signal_data); i++)
        {
            signal_data[i] = (i % 2 == 0) ? 0xAA : 0x55;
        }
        break;
    case SignalModeMSK99_97KbAsync:
    case SignalModeGFSK9_99KbAsync:
        for (size_t i = 0; i < sizeof(signal_data); i++)
        {
            signal_data[i] = rand() % 256;
        }
        break;
    case SignalModePattern0xFF:
        memset(signal_data, 0xFF, sizeof(signal_data));
        break;
    case SignalModeSineWave:
        for (size_t i = 0; i < sizeof(signal_data); i++)
        {
            signal_data[i] = (uint8_t)(127 * sinf(2 * M_PI * i / sizeof(signal_data)) + 128);
        }
        break;
    case SignalModeSquareWave:
        for (size_t i = 0; i < sizeof(signal_data); i++)
        {
            signal_data[i] = (i % 2 == 0) ? 0xFF : 0x00;
        }
        break;
    case SignalModeSawtoothWave:
        for (size_t i = 0; i < sizeof(signal_data); i++)
        {
            signal_data[i] = (uint8_t)(255 * i / sizeof(signal_data));
        }
        break;
    case SignalModeWhiteNoise:
        for (size_t i = 0; i < sizeof(signal_data); i++)
        {
            signal_data[i] = rand() % 256;
        }
        break;
    case SignalModeTriangleWave:
        for (size_t i = 0; i < sizeof(signal_data); i++)
        {
            signal_data[i] = (i < sizeof(signal_data) / 2) ? (i * 255 / (sizeof(signal_data) / 2)) : (255 - (i * 255 / (sizeof(signal_data) / 2)));
        }
        break;
    case SignalModeChirp:
        for (size_t i = 0; i < sizeof(signal_data); i++)
        {
            signal_data[i] = (uint8_t)(127 * sinf(2 * M_PI * i * (1 + (float)i / sizeof(signal_data))));
        }
        break;
    case SignalModeGaussianNoise:
        for (size_t i = 0; i < sizeof(signal_data); i++)
        {
            float u1 = (float)rand() / RAND_MAX;
            float u2 = (float)rand() / RAND_MAX;
            float gaussian_noise = sqrtf(-2.0f * logf(u1)) * cosf(2 * M_PI * u2);
            signal_data[i] = (uint8_t)(127 * gaussian_noise + 128);
        }
        break;
    case SignalModeBurst:
        for (size_t i = 0; i < sizeof(signal_data); i++)
        {
            signal_data[i] = (i % 10 == 0) ? 0xFF : 0x00;
        }
        break;
    }

    while (app->tx_running)
    {
        if (app->subghz_txrx)
        {
            bool write_result = subghz_tx_rx_worker_write(app->subghz_txrx, signal_data, sizeof(signal_data));
            if (!write_result)
            {
                furi_delay_ms(20);
            }
        }
        else
        {
            break;
        }
        furi_delay_ms(10);
    }

    FURI_LOG_I(TAG, "TX Thread exiting");
    return 0;
}

static void subghz_signal_gen_update_view(SubGhzSignalGenApp *app)
{
    view_port_update(app->view_port);
}

static uint32_t adjust_frequency_to_valid(uint32_t frequency, bool up)
{
    if (is_frequency_valid(frequency))
    {
        return frequency;
    }
    else
    {
        if (up)
        {
            for (size_t i = 0; i < NUM_FREQUENCY_BANDS; i++)
            {
                if (frequency < valid_frequency_bands[i].min)
                {
                    return valid_frequency_bands[i].min;
                }
            }
            return valid_frequency_bands[0].min;
        }
        else
        {
            for (int i = NUM_FREQUENCY_BANDS - 1; i >= 0; i--)
            {
                if (frequency > valid_frequency_bands[i].max)
                {
                    return valid_frequency_bands[i].max;
                }
            }
            return valid_frequency_bands[NUM_FREQUENCY_BANDS - 1].max;
        }
    }
}

static bool is_frequency_valid(uint32_t frequency)
{
    for (size_t i = 0; i < NUM_FREQUENCY_BANDS; i++)
    {
        if (frequency >= valid_frequency_bands[i].min && frequency <= valid_frequency_bands[i].max)
        {
            return true;
        }
    }
    return false;
}