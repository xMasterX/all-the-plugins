#include "../usb_floppy_app_i.h"

void usb_floppy_scene_bad_image_on_enter(void* context) {
    UsbFloppyApp* app = context;

    widget_add_string_multiline_element(
        app->widget, 64, 2, AlignCenter, AlignTop, FontPrimary, "Not a floppy image");
    widget_add_string_multiline_element(
        app->widget,
        64,
        20,
        AlignCenter,
        AlignTop,
        FontSecondary,
        "Image must be exactly\n720K (737280 bytes) or\n1.44M (1474560 bytes)");

    view_dispatcher_switch_to_view(app->view_dispatcher, UsbFloppyAppViewWidget);
}

bool usb_floppy_scene_bad_image_on_event(void* context, SceneManagerEvent event) {
    UsbFloppyApp* app = context;
    if(event.type == SceneManagerEventTypeBack) {
        return usb_floppy_app_back_to_menu(app);
    }
    return false;
}

void usb_floppy_scene_bad_image_on_exit(void* context) {
    UsbFloppyApp* app = context;
    widget_reset(app->widget);
}
