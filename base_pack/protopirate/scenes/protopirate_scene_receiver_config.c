// scenes/protopirate_scene_receiver_config.c
#include "protopirate_app_i.h"
#include "plugins/protopirate_config_plugin.h"
#include "../helpers/protopirate_protocol_plugin_host.h"

void protopirate_scene_receiver_config_on_enter(void* context) {
    ProtoPirateApp* app = context;

    if(!shared_plugin_load(
           &app->running_plugin_flipper_application,
           &app->running_plugin,
           ProtoPirateSharedPluginsConfig,
           NULL)) {
        notification_message(app->notifications, &sequence_error);
        scene_manager_previous_scene(app->scene_manager);
        return;
    }
    app->running_plugin.config_plugin->set_host_api(&protopirate_shared_plugin_host_api);

    //We have to grab this from the scene manager, from inside the plugin it just doesnt work!
    bool show_lock_keyboard =
        (scene_manager_get_scene_state(app->scene_manager, ProtoPirateSceneReceiverConfig) == 1);

    if(!app->running_plugin.config_plugin->on_enter(app, show_lock_keyboard)) {
        scene_manager_previous_scene(app->scene_manager);
    }
}

bool protopirate_scene_receiver_config_on_event(void* context, SceneManagerEvent event) {
    ProtoPirateApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == ProtoPirateCustomEventSceneSettingLock) {
            app->lock = ProtoPirateLockOn;
            scene_manager_previous_scene(app->scene_manager);
            consumed = true;
        }
    }
    return consumed;
}

void protopirate_scene_receiver_config_on_exit(void* context) {
    ProtoPirateApp* app = context;

    //Remove the View, and switch back to main menu view so the app wont close.
    view_dispatcher_switch_to_view(app->view_dispatcher, ProtoPirateViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, ProtoPirateViewVariableItemList);

    //Let the plugin clean up properly.
    app->running_plugin.config_plugin->on_exit(app);

    //Unload the plugin.
    shared_plugin_unload(&app->running_plugin_flipper_application, &app->running_plugin);
}
