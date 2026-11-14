// helpers/protopirate_settings.c
#include "protopirate_app_i.h"
//#include "protopirate_settings_plugin_helpers.h"

//#include "protopirate_storage.h"
//#include <storage/storage.h>
//#include <flipper_format/flipper_format.h>
//#include <furi.h>
//#include "../protocols/protocols_common.h"
//#include <lib/flipper_application/flipper_application.h>
//#include "helpers/protopirate_plugins.h"

void protopirate_settings_set_defaults_from_plugin(ProtoPirateSettings* settings) {
    FlipperApplication* running_config_fal = NULL;
    ProtoPiratePlugin running_config_plugin = {.plugin_pointer = NULL};
    if(shared_plugin_load(
           &running_config_fal, &running_config_plugin, ProtoPirateSharedPluginsConfig, NULL) &&
       running_config_plugin.config_plugin) {
        //Call the function in the plugin.
        running_config_plugin.config_plugin->protopirate_settings_set_defaults(settings);

        //Kill the config plugin now.
        shared_plugin_unload(&running_config_fal, &running_config_plugin);
    }
}

void protopirate_settings_load_from_plugin(ProtoPirateSettings* settings) {
    FlipperApplication* running_config_fal = NULL;
    ProtoPiratePlugin running_config_plugin = {.plugin_pointer = NULL};
    if(shared_plugin_load(
           &running_config_fal, &running_config_plugin, ProtoPirateSharedPluginsConfig, NULL) &&
       running_config_plugin.config_plugin) {
        //Call the function in the plugin.
        running_config_plugin.config_plugin->protopirate_settings_load(settings);

        //Kill the config plugin now.
        shared_plugin_unload(&running_config_fal, &running_config_plugin);
    }
}

void protopirate_settings_save_from_plugin(ProtoPirateSettings* settings) {
    FlipperApplication* running_config_fal = NULL;
    ProtoPiratePlugin running_config_plugin = {.plugin_pointer = NULL};
    if(shared_plugin_load(
           &running_config_fal, &running_config_plugin, ProtoPirateSharedPluginsConfig, NULL) &&
       running_config_plugin.config_plugin) {
        //Call the function in the plugin.
        running_config_plugin.config_plugin->protopirate_settings_save(settings);

        //Kill the config plugin now.
        shared_plugin_unload(&running_config_fal, &running_config_plugin);
    }
}
