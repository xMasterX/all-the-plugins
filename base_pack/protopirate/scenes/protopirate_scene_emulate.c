// scenes/protopirate_scene_emulate.c
#include "../protopirate_app_i.h"

#ifdef ENABLE_EMULATE_FEATURE

#include "plugins/protopirate_emulate_plugin.h"
#include "../helpers/protopirate_storage.h"
#include "../protopirate_history.h"

#include <loader/firmware_api/firmware_api.h>

#define TAG "PPSceneEmulate"

static bool host_radio_init(void* app) {
    return protopirate_radio_init((ProtoPirateApp*)app);
}

static bool host_apply_protocol_registry_for_context(
    void* app,
    const char* preset_name,
    uint32_t frequency,
    const uint8_t* preset_data,
    size_t preset_data_size,
    const char* protocol_name) {
    return protopirate_apply_protocol_registry_for_context(
        (ProtoPirateApp*)app, preset_name, frequency, preset_data, preset_data_size, protocol_name);
}

static void host_rx_stack_suspend_for_tx(void* app) {
    protopirate_rx_stack_suspend_for_tx((ProtoPirateApp*)app);
}

static bool host_ensure_view_about(void* app) {
    return protopirate_ensure_view_about((ProtoPirateApp*)app);
}

static bool host_ensure_text_input(void* app) {
    return protopirate_ensure_text_input((ProtoPirateApp*)app);
}

static void host_idle(void* app) {
    protopirate_idle((ProtoPirateApp*)app);
}

static void host_history_release_scratch(void* app) {
    ProtoPirateApp* a = (ProtoPirateApp*)app;
    if(a && a->txrx && a->txrx->history) {
        protopirate_history_release_scratch(a->txrx->history);
    }
}

static void host_storage_delete_temp(void) {
    protopirate_storage_delete_temp();
}

static void protopirate_emulate_apply_pending_nav(ProtoPirateApp* app) {
    furi_check(app);

    const uint8_t nav = app->emulate_nav_pending;
    if(nav == EMULATE_NAV_NONE) {
        return;
    }

    app->emulate_nav_pending = EMULATE_NAV_NONE;

    if(nav == EMULATE_NAV_POP) {
        scene_manager_previous_scene(app->scene_manager);
    } else if(nav == EMULATE_NAV_STOP_APP) {
        scene_manager_stop(app->scene_manager);
        view_dispatcher_stop(app->view_dispatcher);
    }
}

static const ProtoPirateEmulateHostApi protopirate_emulate_host_api = {
    .radio_init = host_radio_init,
    .apply_protocol_registry_for_context = host_apply_protocol_registry_for_context,
    .rx_stack_suspend_for_tx = host_rx_stack_suspend_for_tx,
    .ensure_view_about = host_ensure_view_about,
    .ensure_text_input = host_ensure_text_input,
    .idle = host_idle,
    .history_release_scratch = host_history_release_scratch,
    .storage_delete_temp = host_storage_delete_temp,
};

void protopirate_emulate_context_release(ProtoPirateApp* app) {
    if(app->emulate_plugin && app->emulate_plugin->context_release) {
        app->emulate_plugin->context_release(app);
    }
    shared_plugin_unload(app, ProtoPirateSharedPluginsEmulate);
}

void protopirate_scene_emulate_on_enter(void* context) {
    ProtoPirateApp* app = context;

    app->emulate_nav_pending = EMULATE_NAV_NONE;

    if(!shared_plugin_load(app, ProtoPirateSharedPluginsEmulate, NULL)) {
        notification_message(app->notifications, &sequence_error);
        scene_manager_previous_scene(app->scene_manager);
        return;
    } else {
        app->emulate_plugin->set_host_api(&protopirate_emulate_host_api);
    }

    app->emulate_plugin->on_enter(app);
    protopirate_emulate_apply_pending_nav(app);
}

bool protopirate_scene_emulate_on_event(void* context, SceneManagerEvent event) {
    ProtoPirateApp* app = context;

    if(!app->emulate_plugin || !app->emulate_plugin->on_event) {
        return false;
    }

    const bool consumed = app->emulate_plugin->on_event(app, event);
    protopirate_emulate_apply_pending_nav(app);
    return consumed;
}

void protopirate_scene_emulate_on_exit(void* context) {
    ProtoPirateApp* app = context;

    if(app->emulate_plugin && app->emulate_plugin->on_exit) {
        app->emulate_plugin->on_exit(app);
    }
    protopirate_emulate_context_release(app);
}

#endif // ENABLE_EMULATE_FEATURE
