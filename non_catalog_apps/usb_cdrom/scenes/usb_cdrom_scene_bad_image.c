#include "../usb_cdrom_app_i.h"

void usb_cdrom_scene_bad_image_on_enter(void* context) {
    UsbCdromApp* app = context;

    widget_add_string_multiline_element(
        app->widget, 64, 2, AlignCenter, AlignTop, FontPrimary, "Not an ISO image");
    widget_add_string_multiline_element(
        app->widget,
        64,
        20,
        AlignCenter,
        AlignTop,
        FontSecondary,
        "Image size must be a\nmultiple of 2048 bytes\nand no more than 4 GB");

    view_dispatcher_switch_to_view(app->view_dispatcher, UsbCdromAppViewWidget);
}

bool usb_cdrom_scene_bad_image_on_event(void* context, SceneManagerEvent event) {
    UsbCdromApp* app = context;
    if(event.type == SceneManagerEventTypeBack) {
        return usb_cdrom_app_back_to_menu(app);
    }
    return false;
}

void usb_cdrom_scene_bad_image_on_exit(void* context) {
    UsbCdromApp* app = context;
    widget_reset(app->widget);
}
