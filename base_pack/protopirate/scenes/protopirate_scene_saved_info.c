// scenes/protopirate_scene_saved_info.c
#include "../protopirate_app_i.h"
#include "plugins/protopirate_saved_info_plugin.h"
#include "../helpers/protopirate_psa_bf_host.h"
#include "../helpers/protopirate_storage.h"

static const ProtoPirateSavedInfoSceneHostApi protopirate_saved_info_scene_host_api = {
    .ensure_widget = protopirate_ensure_widget,
    .psa_bf_plugin_ensure_loaded = protopirate_psa_bf_plugin_ensure_loaded,
    .psa_bf_plugin_unload_if_idle = protopirate_psa_bf_plugin_unload_if_idle,
    .protocol_catalog_can_tx = protopirate_protocol_catalog_can_tx,
    .storage_delete_file = protopirate_storage_delete_file,
    .protocol_catalog_offers_bruteforce = protopirate_protocol_catalog_offers_bruteforce,
};

void protopirate_scene_saved_info_on_enter(void* context) {
    ProtoPirateApp* app = context;

    if(!shared_plugin_load(app, ProtoPirateSharedPluginsSavedInfo)) {
        notification_message(app->notifications, &sequence_error);
        scene_manager_previous_scene(app->scene_manager);
        return;
    }

    app->saved_info_plugin->set_host_api(&protopirate_saved_info_scene_host_api);
    app->saved_info_plugin->on_enter(app);
}

bool protopirate_scene_saved_info_on_event(void* context, SceneManagerEvent event) {
    ProtoPirateApp* app = ((ProtoPirateApp*)context);

    //I can't set the next scene from inside the plugin, or it causes crazy crashes.
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == ProtoPirateCustomEventSavedInfoEmulateDelayedStart) {
//Start Emulate Scene.
#ifdef ENABLE_EMULATE_FEATURE
            scene_manager_next_scene(app->scene_manager, ProtoPirateSceneEmulate);
            return true;
#endif
        } else if(event.event == ProtoPirateCustomEventSavedInfoExit) {
            scene_manager_previous_scene(((ProtoPirateApp*)context)->scene_manager);
            return true;
        }
    }

    //Handle Saved Info event in plugin.
    return app->saved_info_plugin->on_event(app, event);
}

void protopirate_scene_saved_info_on_exit(void* context) {
    ProtoPirateApp* app = context;

    shared_plugin_unload(app, ProtoPirateSharedPluginsSavedInfo);
}
