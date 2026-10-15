#pragma once
//#include "../../protopirate_app_i.h"
#include "helpers/protopirate_models.h"
#include "helpers/protopirate_settings.h"

#include "helpers/variable_item_list.h"

enum ProtoPirateSettingIndex {
#ifdef ENABLE_MODELS_DATABASE
    ProtoPirateSettingIndexCarModel,
#endif
    ProtoPirateSettingIndexFrequency,
    ProtoPirateSettingIndexHopping,
    ProtoPirateSettingIndexModulation,
#ifdef ENABLE_EMULATE_FEATURE
    ProtoPirateSettingIndexTXPower,
#endif
    ProtoPirateSettingIndexAutoSave,
    ProtoPirateSettingIndexCheckSaved,
    ProtoPirateSettingIndexDateTimeFilenames,
    ProtoPirateSettingIndexSound,
    ProtoPirateSettingIndexLock,
};

typedef struct ProtoPirateApp ProtoPirateApp;
typedef struct ProtoPirateSharedPluginHostApi ProtoPirateSharedPluginHostApi;
typedef struct ProtoPirateConfigPlugin {
    const char* plugin_name;
#ifdef ENABLE_MODELS_DATABASE
    bool (*car_model_get_by_index)(
        ProtoPirateCarModel* car_model,
        uint16_t index,
        uint16_t model_count,
        SubGhzSetting* app_settings);
    uint16_t (*car_model_get_count)(void);
#endif
    bool (*on_enter)(void* app, bool show_lock_keyboard);
    void (*on_exit)(void* app);
    void (*set_host_api)(const ProtoPirateSharedPluginHostApi* host_api);
    void (*protopirate_settings_load)(ProtoPirateSettings* settings);
    void (*protopirate_settings_save)(ProtoPirateSettings* settings);
    void (*protopirate_settings_set_defaults)(ProtoPirateSettings* settings);
} ProtoPirateConfigPlugin;
