#include "nearby_files_scene.h"
#include "../nearby_files.h"

typedef enum {
    NearbyFilesGpsSourceMenuItemNmea,
    NearbyFilesGpsSourceMenuItemRpc,
} NearbyFilesGpsSourceMenuItem;

static void nearby_files_scene_gps_source_show_result(DialogsApp* dialogs, GpsProtocol protocol) {
    DialogMessage* message = dialog_message_alloc();
    const char* text = (protocol == GpsProtocolRpc) ? "Using companion\nGPS (USB/BLE)" :
                                                      "Using UART\nGPS module";

    dialog_message_set_header(message, "GPS Source", 64, 4, AlignCenter, AlignTop);
    dialog_message_set_text(message, text, 64, 30, AlignCenter, AlignCenter);
    dialog_message_set_buttons(message, "OK", NULL, NULL);
    dialog_message_show(dialogs, message);

    dialog_message_free(message);
}

static void nearby_files_scene_gps_source_submenu_callback(void* context, uint32_t index) {
    NearbyFilesApp* app = context;

    switch(index) {
    case NearbyFilesGpsSourceMenuItemNmea:
        view_dispatcher_send_custom_event(
            app->view_dispatcher, NearbyFilesCustomEventSetSourceNmea);
        break;
    case NearbyFilesGpsSourceMenuItemRpc:
        view_dispatcher_send_custom_event(
            app->view_dispatcher, NearbyFilesCustomEventSetSourceRpc);
        break;
    }
}

void nearby_files_scene_gps_source_on_enter(void* context) {
    NearbyFilesApp* app = context;
    const GpsProtocol current = gps_reader_get_protocol(app->gps_reader);

    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Select GPS Source");

    submenu_add_item(
        app->submenu,
        "UART module (NMEA)",
        NearbyFilesGpsSourceMenuItemNmea,
        nearby_files_scene_gps_source_submenu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Companion app (RPC)",
        NearbyFilesGpsSourceMenuItemRpc,
        nearby_files_scene_gps_source_submenu_callback,
        app);

    submenu_set_selected_item(
        app->submenu,
        (current == GpsProtocolRpc) ? NearbyFilesGpsSourceMenuItemRpc :
                                      NearbyFilesGpsSourceMenuItemNmea);

    view_dispatcher_switch_to_view(app->view_dispatcher, NearbyFilesViewSubmenu);
}

bool nearby_files_scene_gps_source_on_event(void* context, SceneManagerEvent event) {
    NearbyFilesApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        bool has_selection = false;
        GpsProtocol new_protocol = GpsProtocolNmea;

        switch(event.event) {
        case NearbyFilesCustomEventSetSourceNmea:
            new_protocol = GpsProtocolNmea;
            has_selection = true;
            break;
        case NearbyFilesCustomEventSetSourceRpc:
            new_protocol = GpsProtocolRpc;
            has_selection = true;
            break;
        }

        if(has_selection) {
            if(gps_reader_set_protocol(app->gps_reader, new_protocol)) {
                nearby_files_save_config(app);
                nearby_files_scene_gps_source_show_result(app->dialogs, new_protocol);
            }
            scene_manager_previous_scene(app->scene_manager);
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        scene_manager_previous_scene(app->scene_manager);
        consumed = true;
    }

    return consumed;
}

void nearby_files_scene_gps_source_on_exit(void* context) {
    NearbyFilesApp* app = context;
    submenu_reset(app->submenu);
}
