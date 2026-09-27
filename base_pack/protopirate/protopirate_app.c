// protopirate_app.c
#include "protopirate_app_i.h"

#include <furi.h>
#include <furi_hal.h>
#include "helpers/protopirate_settings.h"
#include "helpers/protopirate_storage.h"
#include "helpers/protopirate_psa_bf_host.h"
#include "helpers/protopirate_views.h"
#include "helpers/protopirate_radio.h"
#include <string.h>

#define TAG "PPApp"

// -----------------------------------------------------------------------------
// Plugin load / unload
// -----------------------------------------------------------------------------
const FlipperAppPluginDescriptor* load_plugin_fal(
    FlipperApplication** fal_app,
    const char* plugin_path,
    const char* application_id,
    uint32_t api_version) {
    const FlipperAppPluginDescriptor* app_descriptor;

    bool return_value = false;
    do {
        FlipperApplicationPreloadStatus preload_res =
            flipper_application_preload(*fal_app, plugin_path);
        if(preload_res != FlipperApplicationPreloadStatusSuccess) {
            FURI_LOG_E(TAG, "Failed to preload plugin: %s", plugin_path);
            break;
        }

        if(!flipper_application_is_plugin(*fal_app)) {
            FURI_LOG_E(TAG, "Plugin file is not a library");
            break;
        }

        FlipperApplicationLoadStatus load_status = flipper_application_map_to_memory(*fal_app);
        if(load_status != FlipperApplicationLoadStatusSuccess) {
            FURI_LOG_E(TAG, "Failed to load plugin file");
            break;
        }

        FURI_LOG_D(TAG, "is mapped to memory");
        app_descriptor = flipper_application_plugin_get_descriptor(*fal_app);
        if(strcmp(app_descriptor->appid, application_id) != 0) {
            FURI_LOG_E(TAG, "Application id mismatch %s", application_id);
            break;
        }

        if(app_descriptor->ep_api_version != api_version) {
            FURI_LOG_E(TAG, "API version mismatch %lu", api_version);
            break;
        }

        FURI_LOG_I(
            TAG,
            "Loaded plugin for appid '%s', API %lu",
            app_descriptor->appid,
            app_descriptor->ep_api_version);
        return_value = true;
    } while(false);

    //Free the applciation if there was an error, otherwise return the app descriptor.
    if(return_value) {
        return app_descriptor;
    } else {
        if(*fal_app) {
            flipper_application_free(*fal_app);
            *fal_app = NULL;
        }
        return NULL;
    }
}

void shared_plugin_unload(ProtoPirateApp* app, ProtoPirateSharedPlugin plugin_type) {
    //Clear the invalid reference to the plugin.
    FlipperApplication** fal_needs_free = NULL;
    switch(plugin_type) {
    case ProtoPirateSharedPluginsConfig: {
        fal_needs_free = &app->plugin_flipper_application;
        app->config_plugin = NULL;
        break;
    }
    case ProtoPirateSharedPluginsSavedInfo: {
        fal_needs_free = &app->plugin_flipper_application;
        app->saved_info_plugin = NULL;
        break;
    }
    case ProtoPirateSharedPluginsAbout: {
        fal_needs_free = &app->plugin_flipper_application;
        app->about_plugin = NULL;
        break;
    }
#ifdef ENABLE_EMULATE_FEATURE
    case ProtoPirateSharedPluginsEmulate: {
        fal_needs_free = &app->plugin_flipper_application;
        app->emulate_plugin = NULL;
        break;
    }
#endif
    case ProtoPirateSharedPluginsToolScene:
    case ProtoPirateSharedPluginsSubDecode:
#ifdef ENABLE_TIMING_TUNER_SCENE
    case ProtoPirateSharedPluginsTimingTuner:
#endif
    {
        fal_needs_free = &app->tool_scene_plugin_flipper_application;
        app->tool_scene_plugin = NULL;
        break;
    }
    case ProtoPirateSharedPluginsPSABruteforce: {
        fal_needs_free = &app->psa_bf_plugin_flipper_application;
        app->psa_bf_plugin = NULL;
        break;
    }
    case ProtoPirateSharedPluginsTXRX: {
        fal_needs_free = &app->txrx->protocol_plugin_flipper_application;
        app->txrx->protocol_plugin = NULL;
        break;
    }
    default:
        return;
    }

    //Free the flipper application.
    if(*fal_needs_free) {
        flipper_application_free(*fal_needs_free);
        *fal_needs_free = NULL;
    }
}

bool shared_plugin_load(
    ProtoPirateApp* app,
    ProtoPirateSharedPlugin plugin_type,
    const char* txrx_path) {
    //Get the APPID and API VERSION for the Plugin we are loading.
    const char* application_id = NULL;
    const char* plugin_path = NULL;
    uint32_t api_version = 0;
    FlipperApplication** fal_needs_alloc = NULL;

    switch(plugin_type) {
    case ProtoPirateSharedPluginsConfig: {
        if(app->config_plugin) return true;
        application_id = PROTOPIRATE_CONFIG_PLUGIN_APP_ID;
        api_version = PROTOPIRATE_CONFIG_PLUGIN_API_VERSION;
        plugin_path = CONFIG_PLUGIN_PATH;
        fal_needs_alloc = &app->plugin_flipper_application;
        break;
    }
    case ProtoPirateSharedPluginsSavedInfo: {
        if(app->saved_info_plugin) return true;
        application_id = PROTOPIRATE_SAVED_INFO_PLUGIN_APP_ID;
        api_version = PROTOPIRATE_SAVED_INFO_PLUGIN_API_VERSION;
        plugin_path = SAVED_INFO_PLUGIN_PATH;
        fal_needs_alloc = &app->plugin_flipper_application;
        break;
    }
    case ProtoPirateSharedPluginsAbout: {
        if(app->about_plugin) return true;
        application_id = PROTOPIRATE_ABOUT_PLUGIN_APP_ID;
        api_version = PROTOPIRATE_ABOUT_PLUGIN_API_VERSION;
        plugin_path = ABOUT_PLUGIN_PATH;
        fal_needs_alloc = &app->plugin_flipper_application;
        break;
    }
#ifdef ENABLE_EMULATE_FEATURE
    case ProtoPirateSharedPluginsEmulate: {
        if(app->emulate_plugin) return true;
        application_id = PROTOPIRATE_EMULATE_PLUGIN_APP_ID;
        api_version = PROTOPIRATE_EMULATE_PLUGIN_API_VERSION;
        plugin_path = EMULATE_PLUGIN_PATH;
        fal_needs_alloc = &app->plugin_flipper_application;
        break;
    }
#endif
    case ProtoPirateSharedPluginsSubDecode: {
        if(app->tool_scene_plugin) return true;
        application_id = PROTOPIRATE_TOOL_SCENE_PLUGIN_APP_ID;
        api_version = PROTOPIRATE_TOOL_SCENE_PLUGIN_API_VERSION;
        plugin_path = SUB_DECODE_PLUGIN_PATH;
        fal_needs_alloc = &app->tool_scene_plugin_flipper_application;
        break;
    }
#ifdef ENABLE_TIMING_TUNER_SCENE
    case ProtoPirateSharedPluginsTimingTuner: {
        if(app->tool_scene_plugin) return true;
        application_id = PROTOPIRATE_TOOL_SCENE_PLUGIN_APP_ID;
        api_version = PROTOPIRATE_TOOL_SCENE_PLUGIN_API_VERSION;
        plugin_path = TIMING_TUNER_PLUGIN_PATH;
        fal_needs_alloc = &app->tool_scene_plugin_flipper_application;
        break;
    }
#endif
    case ProtoPirateSharedPluginsPSABruteforce: {
        if(app->psa_bf_plugin) return true;
        application_id = PROTOPIRATE_PSA_BF_PLUGIN_APP_ID;
        api_version = PROTOPIRATE_PSA_BF_PLUGIN_API_VERSION;
        plugin_path = PSA_BF_PLUGIN_PATH;
        fal_needs_alloc = &app->psa_bf_plugin_flipper_application;
        break;
    }
    case ProtoPirateSharedPluginsTXRX: {
        if(app->txrx->protocol_plugin) return true;
        application_id = PROTOPIRATE_PROTOCOL_PLUGIN_APP_ID;
        api_version = PROTOPIRATE_PROTOCOL_PLUGIN_API_VERSION;
        plugin_path = txrx_path;
        fal_needs_alloc = &app->txrx->protocol_plugin_flipper_application;
        break;
    }
    default:
        return false;
    }

    FURI_LOG_D(TAG, "Loading plugin: %s", plugin_path);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperApplication* fal_app = flipper_application_alloc(storage, firmware_api_interface);
    bool return_value = false;
    do {
        //Load the FAP
        const FlipperAppPluginDescriptor* app_descriptor =
            load_plugin_fal(&fal_app, plugin_path, application_id, api_version);
        if(app_descriptor) {
            //Get the Plugin and assign it.
            if(plugin_type == ProtoPirateSharedPluginsConfig) {
                const ProtoPirateConfigPlugin* plugin_config = app_descriptor->entry_point;
                if(!plugin_config || !plugin_config->on_enter) {
                    FURI_LOG_E(TAG, "Config plugin entry point is invalid");
                } else {
                    app->config_plugin = plugin_config;
                    return_value = true;
                }
            } else if(plugin_type == ProtoPirateSharedPluginsSavedInfo) {
                const ProtoPirateSavedInfoPlugin* plugin_saved_info = app_descriptor->entry_point;
                if(!plugin_saved_info || !plugin_saved_info->on_enter) {
                    FURI_LOG_E(TAG, "Saved Info plugin entry point is invalid");
                } else {
                    app->saved_info_plugin = plugin_saved_info;
                    return_value = true;
                }
            } else if(plugin_type == ProtoPirateSharedPluginsAbout) {
                const ProtoPirateAboutPlugin* plugin_about = app_descriptor->entry_point;
                if(!plugin_about || !plugin_about->on_enter) {
                    FURI_LOG_E(TAG, "About plugin entry point is invalid");
                } else {
                    app->about_plugin = plugin_about;
                    return_value = true;
                }
            }
#ifdef ENABLE_EMULATE_FEATURE
            else if(plugin_type == ProtoPirateSharedPluginsEmulate) {
                const ProtoPirateEmulatePlugin* plugin_emulate = app_descriptor->entry_point;
                if(!plugin_emulate || !plugin_emulate->on_enter) {
                    FURI_LOG_E(TAG, "Emulate plugin entry point is invalid");
                } else {
                    app->emulate_plugin = plugin_emulate;
                    return_value = true;
                }
            }
#endif
            else if(
                plugin_type == ProtoPirateSharedPluginsSubDecode
#ifdef ENABLE_TIMING_TUNER_SCENE
                || plugin_type == ProtoPirateSharedPluginsTimingTuner
#endif
            ) {
                const ProtoPirateToolScenePlugin* plugin_tool_scene = app_descriptor->entry_point;
                if(!plugin_tool_scene || !plugin_tool_scene->on_enter) {
                    FURI_LOG_E(TAG, "Tool Scene plugin entry point is invalid");
                } else {
                    app->tool_scene_plugin = plugin_tool_scene;
                    return_value = true;
                }
            } else if(plugin_type == ProtoPirateSharedPluginsPSABruteforce) {
                const ProtoPiratePsaBfPlugin* plugin_psa_bf = app_descriptor->entry_point;
                if(!plugin_psa_bf || !plugin_psa_bf->needs_bruteforce) {
                    FURI_LOG_E(TAG, "PSA plugin entry needs_bruteforce is invalid");
                } else {
                    app->psa_bf_plugin = plugin_psa_bf;
                    return_value = true;
                }
            } else if(plugin_type == ProtoPirateSharedPluginsTXRX) {
                const ProtoPirateProtocolPlugin* plugin_txrx = app_descriptor->entry_point;
                if(!plugin_txrx || !plugin_txrx->registry) {
                    FURI_LOG_E(TAG, "Protocol plugin registry entry is invalid");
                } else {
                    app->txrx->protocol_plugin = plugin_txrx;
                    return_value = true;
                }
            }
        }
    } while(false);

    //Free the plugin if there was an error, otherwise we are done!
    if(fal_needs_alloc) *fal_needs_alloc = fal_app;
    furi_record_close(RECORD_STORAGE);
    return return_value;
}

static bool protopirate_app_custom_event_callback(void* context, uint32_t event) {
    furi_check(context);
    ProtoPirateApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool protopirate_app_back_event_callback(void* context) {
    furi_check(context);
    ProtoPirateApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void protopirate_app_tick_event_callback(void* context) {
    furi_check(context);
    ProtoPirateApp* app = context;
    scene_manager_handle_tick_event(app->scene_manager);
}

ProtoPirateApp* protopirate_app_alloc() {
    protopirate_storage_purge_temp_history_at_startup();
    ProtoPirateApp* app = malloc(sizeof(ProtoPirateApp));
    if(!app) {
        FURI_LOG_E(TAG, "Failed to allocate ProtoPirateApp app !");
        return NULL;
    }
    memset(app, 0, sizeof(ProtoPirateApp));

    FURI_LOG_I(TAG, "Allocating ProtoPirate Decoder App");

    // GUI
    app->gui = furi_record_open(RECORD_GUI);

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

    // Open Notification record
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    // Open Dialogs record
    app->dialogs = furi_record_open(RECORD_DIALOGS);

    // SubMenu
    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, ProtoPirateViewSubmenu, submenu_get_view(app->submenu));

    app->save_protocol = NULL;
    app->save_history_idx = 0;
    app->emulate_disabled_for_loaded = false;
    app->save_filename = NULL;

    // File Browser path
    app->file_path = furi_string_alloc();
    furi_string_set(app->file_path, PROTOPIRATE_APP_FOLDER);

    // Load saved settings
    ProtoPirateSettings settings;
    protopirate_settings_load(&settings);

    // Apply auto-save setting
    app->auto_save = settings.auto_save;
    app->sound = settings.sound;
    app->check_saved = settings.check_saved;
    app->tx_power = settings.tx_power;
    app->datetime_filenames = settings.datetime_filenames;
#ifdef ENABLE_EMULATE_FEATURE
    app->emulate_feature_enabled = settings.emulate_feature_enabled;
#else
    app->emulate_feature_enabled = false;
#endif

    // Init setting - KEEP THIS, it's small
    app->setting = subghz_setting_alloc();
    app->loaded_file_path = NULL;
    app->start_tx_time = 0;
    app->deferred_storage_timer = NULL;
    subghz_setting_load(app->setting, EXT_PATH("subghz/assets/setting_user"));

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
    furi_check(app->txrx);
    memset(app->txrx, 0, sizeof(ProtoPirateTxRx));

    app->txrx->preset = malloc(sizeof(SubGhzRadioPreset));
    furi_check(app->txrx->preset);
    app->txrx->preset->name = furi_string_alloc();
    furi_check(app->txrx->preset->name);
    app->txrx->txrx_state = ProtoPirateTxRxStateIDLE;
    app->txrx->rx_key_state = ProtoPirateRxKeyStateIDLE;
    app->txrx->protocol_registry_route = ProtoPirateProtocolRegistryRouteAMDefault;

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

    // Null out plugin pointers just in case.
    app->plugin_flipper_application = NULL;
    app->config_plugin = NULL;
    app->saved_info_plugin = NULL;
    app->about_plugin = NULL;
    app->plugin_flipper_application = NULL;
    app->psa_bf_plugin = NULL;
    app->tool_scene_plugin_flipper_application = NULL;
    app->tool_scene_plugin = NULL;

    //Load the models database, get the count of the models for the list.
    if(shared_plugin_load(app, ProtoPirateSharedPluginsConfig, NULL) && app->config_plugin) {
        app->car_models_count = app->config_plugin->car_model_get_count();
    } else {
        notification_message(app->notifications, &sequence_error);
        app->car_models_count = 0;
    }
    app->selected_model = malloc(sizeof(ProtoPirateCarModel));
    app->selected_model->name = furi_string_alloc();
    app->selected_model->preset = NULL; // important initialization
    app->selected_model->index = 0; // optional but clean
    app->variable_item_list = NULL;

    //Grab selected car model.
    if(app->config_plugin) {
        if(settings.car_model_index) {
            //Get the selected car model.
            app->config_plugin->car_model_get_by_index(
                app->selected_model, settings.car_model_index, app->car_models_count, app->setting);
            app->selected_model->last_preset_index = settings.preset_index;

            //Preset for the selected model...
            protopirate_preset_init(
                app,
                furi_string_get_cstr(app->selected_model->preset->name),
                app->selected_model->preset->frequency,
                app->selected_model->preset->data,
                app->selected_model->preset->data_size);
        } else {
            //This will return Select a model or No Models in Database
            app->config_plugin->car_model_get_by_index(
                app->selected_model, 0, app->car_models_count, app->setting);

            //Preset set in Config.
            protopirate_preset_init(app, preset_name, frequency, preset_data, preset_data_size);
        }

        //Kill the config plugin now.
        shared_plugin_unload(app, ProtoPirateSharedPluginsConfig);
    } else {
        //Preset set in Config.
        protopirate_preset_init(app, preset_name, frequency, preset_data, preset_data_size);
    }

    // Apply hopping state from settings
    app->txrx->hopper_state = settings.hopping_enabled ? ProtoPirateHopperStateRunning :
                                                         ProtoPirateHopperStateOFF;
    app->txrx->hopper_idx_frequency = 0;
    app->txrx->hopper_timeout = 0;
    app->txrx->idx_menu_chosen = 0;

    app->radio_initialized = false;

    return app;
}

void protopirate_app_free(ProtoPirateApp* app) {
    furi_check(app);

    FURI_LOG_I(TAG, "=== protopirate_app_free called ===");
    FURI_LOG_D(TAG, "State: radio_initialized=%d", app->radio_initialized);

    // Save settings before exiting
    ProtoPirateSettings settings;
    settings.frequency = app->txrx->preset->frequency;
    settings.auto_save = app->auto_save;
    settings.sound = app->sound;
    settings.check_saved = app->check_saved;
    settings.tx_power = app->tx_power;
    settings.datetime_filenames = app->datetime_filenames;
    settings.hopping_enabled = (app->txrx->hopper_state != ProtoPirateHopperStateOFF);
#ifdef ENABLE_EMULATE_FEATURE
    settings.emulate_feature_enabled = app->emulate_feature_enabled;
#else
    settings.emulate_feature_enabled = false;
#endif

    //Get the selected Model, and get the preset to save.
    if(app->selected_model && app->selected_model->index) {
        //Get Preset Index before model was selected.
        settings.car_model_index = app->selected_model->index;
        settings.preset_index = app->selected_model->last_preset_index;
    } else {
        // Find current preset index
        settings.car_model_index = 0; //Clear the selected model.
        settings.preset_index = 0;
        const char* current_preset = furi_string_get_cstr(app->txrx->preset->name);
        for(uint8_t i = 0; i < subghz_setting_get_preset_count(app->setting); i++) {
            if(strcmp(subghz_setting_get_preset_name(app->setting, i), current_preset) == 0) {
                settings.preset_index = i;
                break;
            }
        }
    }

    //Free the Model Name
    furi_string_free(app->selected_model->name);
    app->selected_model->index = 0;

    //Free the preset information.
    if(app->selected_model->preset) {
        //Free the preset data
        if((app->selected_model)->preset->data) {
            free(app->selected_model->preset->data);
            app->selected_model->preset->data = NULL;
        }

        //Free the Preset name
        furi_string_free(app->selected_model->preset->name);

        //Free the preset.
        free(app->selected_model->preset);
        app->selected_model->preset = NULL;
    }

    //Free the Model.
    free(app->selected_model);
    app->selected_model = NULL;

    FURI_LOG_I(
        TAG,
        "Saving settings: freq=%lu, preset=%u, auto_save=%d, hopping=%d, emulate=%d",
        settings.frequency,
        settings.preset_index,
        settings.auto_save,
        settings.hopping_enabled,
        settings.emulate_feature_enabled);

    protopirate_settings_save(&settings);

    protopirate_tool_scene_plugin_release(app);
#ifdef ENABLE_EMULATE_FEATURE
    protopirate_emulate_context_release(app);
#endif

    FURI_LOG_D(TAG, "Calling radio_deinit");
    protopirate_radio_deinit(app);

    if(app->loaded_file_path) {
        FURI_LOG_D(TAG, "Freeing loaded_file_path");
        furi_string_free(app->loaded_file_path);
        app->loaded_file_path = NULL;
    }

    protopirate_views_free(app);

    if(app->save_filename) {
        free(app->save_filename);
        app->save_filename = NULL;
    }

    if(app->file_path) {
        FURI_LOG_D(TAG, "Freeing file_path");
        furi_string_free(app->file_path);
        app->file_path = NULL;
    }

    if(app->save_protocol) {
        furi_string_free(app->save_protocol);
        app->save_protocol = NULL;
    }

    protopirate_psa_bf_context_release(app);

    FURI_LOG_D(TAG, "Freeing subghz_setting");
    subghz_setting_free(app->setting);

    FURI_LOG_D(TAG, "Freeing preset");
    furi_string_free(app->txrx->preset->name);
    free(app->txrx->preset);

    free(app->txrx);

    FURI_LOG_D(TAG, "Freeing view_dispatcher and scene_manager");
    view_dispatcher_free(app->view_dispatcher);
    scene_manager_free(app->scene_manager);

    FURI_LOG_D(TAG, "Closing dialogs record");
    furi_record_close(RECORD_DIALOGS);
    app->dialogs = NULL;

    FURI_LOG_D(TAG, "Closing notifications record");
    furi_record_close(RECORD_NOTIFICATION);
    app->notifications = NULL;

    FURI_LOG_D(TAG, "Closing GUI record");
    furi_record_close(RECORD_GUI);

    FURI_LOG_I(TAG, "App free complete");
    free(app);
}

int32_t protopirate_app(char* p) {
    //Stop charging while running the app.
    furi_hal_power_suppress_charge_enter();

    ProtoPirateApp* protopirate_app = protopirate_app_alloc();
    if(!protopirate_app) {
        furi_hal_power_suppress_charge_exit();
        return -1;
    }

    // Handle Command line PSF that may have been passed to us
    bool load_saved = (p && strlen(p));
    if(load_saved) protopirate_app->loaded_file_path = furi_string_alloc_set(p);

//We now jump straight to emulate scene from Browser.
#ifdef ENABLE_EMULATE_FEATURE
    scene_manager_next_scene(
        protopirate_app->scene_manager,
        (load_saved) ? ((protopirate_app->emulate_feature_enabled) ? ProtoPirateSceneEmulate :
                                                                     ProtoPirateSceneSavedInfo) :
                       ProtoPirateSceneStart);
#else
    scene_manager_next_scene(
        protopirate_app->scene_manager,
        (load_saved) ? ProtoPirateSceneSavedInfo : ProtoPirateSceneStart);
#endif
    //Pop up the beep if we are startng emulate.
    if(load_saved && protopirate_app->emulate_feature_enabled) {
        notification_message(protopirate_app->notifications, &sequence_success);
    }

    //Run the App
    view_dispatcher_run(protopirate_app->view_dispatcher);

    //Free the App and allow chargin again.
    protopirate_app_free(protopirate_app);
    furi_hal_power_suppress_charge_exit();
    return 0;
}
