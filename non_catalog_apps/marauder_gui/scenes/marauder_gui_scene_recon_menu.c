#include "../marauder_gui_app_i.h"
#include <string.h>

/* Marauder's cross-hardware "Recon" mission mode, added in firmware v1.16.0
   ("recon wifi|ble|status|stop"). Each row runs the command and shows its output through the
   shared cmd_output scene. Order must match marauder_recon_menu_items below. */
static const char* const marauder_recon_commands[] = {
    "recon wifi",
    "recon ble",
    "recon status",
    "recon stop",
};

static const MarauderMenuItem marauder_recon_menu_items[] = {
    {"WiFi Recon",
     "WiFi Recon",
     "WiFi kesif gorevini baslatir - agları toplayip degerlendirir.",
     "Starts the WiFi recon mission - collects and profiles networks."},
    {"BLE Recon",
     "BLE Recon",
     "Bluetooth kesif gorevini baslatir - cihaz istihbarati toplar.",
     "Starts the Bluetooth recon mission - gathers device intelligence."},
    {"Durum",
     "Status",
     "Calisan kesif gorevinin durumunu gosterir.",
     "Shows the status of the running recon mission."},
    {"Durdur", "Stop", "Calisan kesif gorevini durdurur.", "Stops the running recon mission."},
};

void marauder_gui_scene_recon_menu_on_enter(void* context) {
    MarauderGuiApp* app = context;
    marauder_gui_menu_set_items(
        app,
        marauder_recon_menu_items,
        sizeof(marauder_recon_menu_items) / sizeof(marauder_recon_menu_items[0]),
        "Recon");
}

bool marauder_gui_scene_recon_menu_on_event(void* context, SceneManagerEvent event) {
    MarauderGuiApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        size_t count = sizeof(marauder_recon_commands) / sizeof(marauder_recon_commands[0]);
        if(event.event < count) {
            strncpy(
                app->terminal_cmd,
                marauder_recon_commands[event.event],
                sizeof(app->terminal_cmd) - 1);
            app->terminal_cmd[sizeof(app->terminal_cmd) - 1] = '\0';
            scene_manager_next_scene(app->scene_manager, MarauderGuiSceneCmdOutput);
            consumed = true;
        }
    }

    return consumed;
}

void marauder_gui_scene_recon_menu_on_exit(void* context) {
    MarauderGuiApp* app = context;
    app->tick_handler = NULL;
}
