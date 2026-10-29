#include "../usb_floppy_app_i.h"

void usb_floppy_scene_usb_locked_on_enter(void* context) {
    UsbFloppyApp* app = context;

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

    view_dispatcher_switch_to_view(app->view_dispatcher, UsbFloppyAppViewWidget);
}

bool usb_floppy_scene_usb_locked_on_event(void* context, SceneManagerEvent event) {
    UsbFloppyApp* app = context;
    if(event.type == SceneManagerEventTypeBack) {
        return usb_floppy_app_back_to_menu(app);
    }
    return false;
}

void usb_floppy_scene_usb_locked_on_exit(void* context) {
    UsbFloppyApp* app = context;
    widget_reset(app->widget);
}
