#include "../usb_cdrom_app_i.h"

void usb_cdrom_scene_usb_locked_on_enter(void* context) {
    UsbCdromApp* app = context;

    widget_add_icon_element(app->widget, 78, 0, &I_ActiveConnection_50x64);
    widget_add_string_multiline_element(
        app->widget, 3, 2, AlignLeft, AlignTop, FontPrimary, "Connection\nis active!");
    widget_add_string_multiline_element(
        app->widget,
        3,
        30,
        AlignLeft,
        AlignTop,
        FontSecondary,
        "Disconnect from\nPC or phone to\nuse this function.");

    view_dispatcher_switch_to_view(app->view_dispatcher, UsbCdromAppViewWidget);
}

bool usb_cdrom_scene_usb_locked_on_event(void* context, SceneManagerEvent event) {
    UsbCdromApp* app = context;
    if(event.type == SceneManagerEventTypeBack) {
        return usb_cdrom_app_back_to_menu(app);
    }
    return false;
}

void usb_cdrom_scene_usb_locked_on_exit(void* context) {
    UsbCdromApp* app = context;
    widget_reset(app->widget);
}
