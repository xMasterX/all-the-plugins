// protopirate_app.c
#include "protopirate_app_i.h"

#include <furi.h>
#include <furi_hal.h>
#include "protocols/protocol_items.h"
#include "helpers/protopirate_settings.h"
#include "helpers/protopirate_storage.h"
#include "protocols/keys.h"
#include <string.h>

#define TAG           "ProtoPirateApp"
#define LOG_HEAP(msg) FURI_LOG_I(TAG, "%s - Free heap: %zu", msg, memmgr_get_free_heap())

static bool protopirate_app_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    ProtoPirateApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool protopirate_app_back_event_callback(void* context) {
    furi_assert(context);
    ProtoPirateApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void protopirate_app_tick_event_callback(void* context) {
    furi_assert(context);
    ProtoPirateApp* app = context;
    scene_manager_handle_tick_event(app->scene_manager);
}

ProtoPirateApp* protopirate_app_alloc() {
    ProtoPirateApp* app = malloc(sizeof(ProtoPirateApp));

    LOG_HEAP("Start alloc");
    FURI_LOG_I(TAG, "Allocating ProtoPirate Decoder App");

    // GUI
    app->gui = furi_record_open(RECORD_GUI);
    LOG_HEAP("After GUI");

    // View Dispatcher
    app->view_dispatcher = view_dispatcher_alloc();
#if defined(FW_ORIGIN_RM)
    view_dispatcher_enable_queue(app->view_dispatcher);
#endif
    app->scene_manager = scene_manager_alloc(&protopirate_scene_handlers, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, protopirate_app_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, protopirate_app_back_event_callback);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, protopirate_app_tick_event_callback, 100);

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    LOG_HEAP("After ViewDispatcher");

    // Open Notification record
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    // Open Dialogs record
    app->dialogs = furi_record_open(RECORD_DIALOGS);

    // Variable Item List
    app->variable_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        ProtoPirateViewVariableItemList,
        variable_item_list_get_view(app->variable_item_list));

    // SubMenu
    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, ProtoPirateViewSubmenu, submenu_get_view(app->submenu));

    // Widget
    app->widget = widget_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, ProtoPirateViewWidget, widget_get_view(app->widget));

    // File Browser path
    app->file_path = furi_string_alloc();
    furi_string_set(app->file_path, PROTOPIRATE_APP_FOLDER);

    // About View
    app->view_about = view_alloc();
    view_dispatcher_add_view(app->view_dispatcher, ProtoPirateViewAbout, app->view_about);

    LOG_HEAP("After basic views");

    // Receiver Views
    app->protopirate_receiver = protopirate_view_receiver_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        ProtoPirateViewReceiver,
        protopirate_view_receiver_get_view(app->protopirate_receiver));

    app->protopirate_receiver_info = protopirate_view_receiver_info_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        ProtoPirateViewReceiverInfo,
        protopirate_view_receiver_info_get_view(app->protopirate_receiver_info));

    LOG_HEAP("After receiver views");

    // Init setting - KEEP THIS, it's small
    app->setting = subghz_setting_alloc();
    app->loaded_file_path = NULL;
    subghz_setting_load(app->setting, EXT_PATH("subghz/assets/setting_user"));

    LOG_HEAP("After subghz_setting");

    // Load saved settings
    ProtoPirateSettings settings;
    protopirate_settings_load(&settings);

    // Apply auto-save setting
    app->auto_save = settings.auto_save;

    // Apply loaded frequency and preset, with validation
    uint32_t frequency = settings.frequency;
    uint8_t preset_index = settings.preset_index;

    // Validate frequency
    bool frequency_valid = false;
    for(size_t i = 0; i < subghz_setting_get_frequency_count(app->setting); i++) {
        if(subghz_setting_get_frequency(app->setting, i) == frequency) {
            frequency_valid = true;
            break;
        }
    }
    if(!frequency_valid) {
        frequency = subghz_setting_get_default_frequency(app->setting);
        FURI_LOG_W(TAG, "Saved frequency invalid, using default: %lu", frequency);
    }

    // Validate preset index
    if(preset_index >= subghz_setting_get_preset_count(app->setting)) {
        preset_index = 0;
        FURI_LOG_W(TAG, "Saved preset index invalid, using default");
    }

    // Initialize TxRx structure with minimal setup
    app->lock = ProtoPirateLockOff;
    app->txrx = malloc(sizeof(ProtoPirateTxRx));
    memset(app->txrx, 0, sizeof(ProtoPirateTxRx));

    app->txrx->preset = malloc(sizeof(SubGhzRadioPreset));
    app->txrx->preset->name = furi_string_alloc();
    app->txrx->txrx_state = ProtoPirateTxRxStateIDLE;
    app->txrx->rx_key_state = ProtoPirateRxKeyStateIDLE;

    // Get preset name and data
    const char* preset_name = subghz_setting_get_preset_name(app->setting, preset_index);
    uint8_t* preset_data = subghz_setting_get_preset_data(app->setting, preset_index);
    size_t preset_data_size = subghz_setting_get_preset_data_size(app->setting, preset_index);

    FURI_LOG_I(
        TAG,
        "Settings: freq=%lu, preset=%s, auto_save=%d, hopping=%d",
        frequency,
        preset_name,
        settings.auto_save,
        settings.hopping_enabled);

    protopirate_preset_init(app, preset_name, frequency, preset_data, preset_data_size);

    // Apply hopping state from settings
    app->txrx->hopper_state = settings.hopping_enabled ? ProtoPirateHopperStateRunning :
                                                         ProtoPirateHopperStateOFF;
    app->txrx->hopper_idx_frequency = 0;
    app->txrx->hopper_timeout = 0;
    app->txrx->idx_menu_chosen = 0;

    LOG_HEAP("After txrx basic setup");

    // DEFERRED: These will be initialized in receiver scene
    app->txrx->worker = NULL;
    app->txrx->environment = NULL;
    app->txrx->receiver = NULL;
    app->txrx->history = NULL;
    app->txrx->radio_device = NULL;

    // Mark as not initialized
    app->radio_initialized = false;

    LOG_HEAP("App alloc complete (radio deferred)");

    return app;
}

// NEW: Initialize radio subsystem - call from receiver scene
bool protopirate_radio_init(ProtoPirateApp* app) {
    if(app->radio_initialized) {
        FURI_LOG_D(TAG, "Radio already initialized");
        return true;
    }

    LOG_HEAP("Radio init start");

    // Allocate history
    app->txrx->history = protopirate_history_alloc();
    LOG_HEAP("After history alloc");

    // Allocate worker
    app->txrx->worker = subghz_worker_alloc();
    LOG_HEAP("After worker alloc");

    // Create environment with our custom protocols
    app->txrx->environment = subghz_environment_alloc();
    LOG_HEAP("After environment alloc");

    FURI_LOG_I(TAG, "Registering %zu ProtoPirate protocols", protopirate_protocol_registry.size);
    subghz_environment_set_protocol_registry(
        app->txrx->environment, (void*)&protopirate_protocol_registry);

    // Load keystores
    subghz_environment_load_keystore(app->txrx->environment, PROTOPIRATE_KEYSTORE_DIR_NAME);
    LOG_HEAP("After keystore load");

    // Load ProtoPirate specific keys
    protopirate_keys_load(app->txrx->environment);
    FURI_LOG_I(TAG, "Loaded ProtoPirate secure keys");
    LOG_HEAP("After keys load");

    // Create receiver
    app->txrx->receiver = subghz_receiver_alloc_init(app->txrx->environment);
    LOG_HEAP("After receiver alloc");

    // Initialize SubGhz devices
    subghz_devices_init();

    // Try external CC1101 first, fallback to internal
    app->txrx->radio_device = radio_device_loader_set(NULL, SubGhzRadioDeviceTypeExternalCC1101);

    if(!app->txrx->radio_device) {
        FURI_LOG_E(TAG, "Failed to initialize any radio device!");
        return false;
    }

#ifndef REMOVE_LOGS
    const char* device_name = subghz_devices_get_name(app->txrx->radio_device);
    bool is_external = device_name && strstr(device_name, "ext");
    FURI_LOG_I(
        TAG,
        "Radio device initialized: %s (%s)",
        device_name ? device_name : "unknown",
        is_external ? "external" : "internal");
#endif

    subghz_devices_reset(app->txrx->radio_device);
    subghz_devices_idle(app->txrx->radio_device);

    // Set filter to accept decodable protocols
    subghz_receiver_set_filter(app->txrx->receiver, SubGhzProtocolFlag_Decodable);

    // Set up worker callbacks
    subghz_worker_set_overrun_callback(
        app->txrx->worker, (SubGhzWorkerOverrunCallback)subghz_receiver_reset);
    subghz_worker_set_pair_callback(
        app->txrx->worker, (SubGhzWorkerPairCallback)subghz_receiver_decode);
    subghz_worker_set_context(app->txrx->worker, app->txrx->receiver);

    app->radio_initialized = true;
    LOG_HEAP("Radio init complete");

    return true;
}

// Deinitialize radio subsystem
void protopirate_radio_deinit(ProtoPirateApp* app) {
    if(!app->radio_initialized) return;

    FURI_LOG_I(TAG, "Deinitializing radio");

    // Make sure we're not receiving
    if(app->txrx->txrx_state == ProtoPirateTxRxStateRx) {
        subghz_worker_stop(app->txrx->worker);
        subghz_devices_stop_async_rx(app->txrx->radio_device);
    }

    subghz_devices_sleep(app->txrx->radio_device);
    radio_device_loader_end(app->txrx->radio_device);
    subghz_devices_deinit();

    subghz_receiver_free(app->txrx->receiver);
    app->txrx->receiver = NULL;

    subghz_environment_free(app->txrx->environment);
    app->txrx->environment = NULL;

    protopirate_history_free(app->txrx->history);
    app->txrx->history = NULL;

    subghz_worker_free(app->txrx->worker);
    app->txrx->worker = NULL;

    app->txrx->radio_device = NULL;
    app->radio_initialized = false;

    LOG_HEAP("Radio deinit complete");
}

void protopirate_app_free(ProtoPirateApp* app) {
    if(!app) return;
    furi_assert(app);

    FURI_LOG_I(TAG, "Freeing ProtoPirate Decoder App");

    // Save settings before exiting
    ProtoPirateSettings settings;
    settings.frequency = app->txrx->preset->frequency;
    settings.auto_save = app->auto_save;
    settings.hopping_enabled = (app->txrx->hopper_state != ProtoPirateHopperStateOFF);

    // Find current preset index
    settings.preset_index = 0;
    const char* current_preset = furi_string_get_cstr(app->txrx->preset->name);
    for(uint8_t i = 0; i < subghz_setting_get_preset_count(app->setting); i++) {
        if(strcmp(subghz_setting_get_preset_name(app->setting, i), current_preset) == 0) {
            settings.preset_index = i;
            break;
        }
    }

    FURI_LOG_I(
        TAG,
        "Saving settings: freq=%lu, preset=%u, auto_save=%d, hopping=%d",
        settings.frequency,
        settings.preset_index,
        settings.auto_save,
        settings.hopping_enabled);

    protopirate_settings_save(&settings);

    // Deinitialize radio if it was initialized
    protopirate_radio_deinit(app);

    if(app->loaded_file_path) {
        furi_string_free(app->loaded_file_path);
        app->loaded_file_path = NULL;
    }

    // Submenu
    view_dispatcher_remove_view(app->view_dispatcher, ProtoPirateViewSubmenu);
    submenu_free(app->submenu);

    // Variable Item List
    view_dispatcher_remove_view(app->view_dispatcher, ProtoPirateViewVariableItemList);
    variable_item_list_free(app->variable_item_list);

    // About View
    view_dispatcher_remove_view(app->view_dispatcher, ProtoPirateViewAbout);
    view_free(app->view_about);

    // File path
    if(app->file_path) {
        furi_string_free(app->file_path);
    }

    // Widget
    view_dispatcher_remove_view(app->view_dispatcher, ProtoPirateViewWidget);
    widget_free(app->widget);

    // Receiver
    view_dispatcher_remove_view(app->view_dispatcher, ProtoPirateViewReceiver);
    protopirate_view_receiver_free(app->protopirate_receiver);

    // Receiver Info
    view_dispatcher_remove_view(app->view_dispatcher, ProtoPirateViewReceiverInfo);
    protopirate_view_receiver_info_free(app->protopirate_receiver_info);

    // Setting
    subghz_setting_free(app->setting);

    // Free preset
    furi_string_free(app->txrx->preset->name);
    free(app->txrx->preset);
    free(app->txrx);

    // View dispatcher
    view_dispatcher_free(app->view_dispatcher);
    scene_manager_free(app->scene_manager);

    // Close Dialogs
    furi_record_close(RECORD_DIALOGS);
    app->dialogs = NULL;

    // Notifications
    furi_record_close(RECORD_NOTIFICATION);
    app->notifications = NULL;

    // Close records
    furi_record_close(RECORD_GUI);

    free(app);
}

int32_t protopirate_app(char* p) {
    furi_hal_power_suppress_charge_enter();

    ProtoPirateApp* protopirate_app = protopirate_app_alloc();

    // Handle Command line PSF that may have been passed to us
    bool load_saved = (p && strlen(p));
    if(load_saved) protopirate_app->loaded_file_path = furi_string_alloc_set(p);

    scene_manager_next_scene(
        protopirate_app->scene_manager,
        (load_saved) ? ProtoPirateSceneSavedInfo : ProtoPirateSceneStart);

    view_dispatcher_run(protopirate_app->view_dispatcher);

    protopirate_app_free(protopirate_app);

    furi_hal_power_suppress_charge_exit();

    return 0;
}
