#include "../usb_cdrom_app_i.h"

void usb_cdrom_scene_about_on_enter(void* context) {
    UsbCdromApp* app = context;

    widget_add_text_scroll_element(
        app->widget,
        0,
        0,
        128,
        64,
        // the scroll element breaks lines mid word, so they're broken here to fit
        "\e#USB CD-ROM v" FAP_VERSION "\n"
        "Emulates a USB\n"
        "CD/DVD-ROM drive.\n"
        "The disc is an .iso\n"
        "image from the SD\n"
        "card.\n\n"
        "Put images in\n"
        "apps_data/usb_cdrom\n\n"
        "\e#Source code\n"
        "github.com/piersroberts/\n"
        "flippercd");

    view_dispatcher_switch_to_view(app->view_dispatcher, UsbCdromAppViewWidget);
}

bool usb_cdrom_scene_about_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void usb_cdrom_scene_about_on_exit(void* context) {
    UsbCdromApp* app = context;
    widget_reset(app->widget);
}
