#include "../usb_cdrom_app_i.h"

void usb_cdrom_scene_file_select_on_enter(void* context) {
    UsbCdromApp* app = context;

    if(usb_cdrom_app_browse(app)) {
        usb_cdrom_app_open_image(app);
    } else {
        scene_manager_previous_scene(app->scene_manager);
    }
}

bool usb_cdrom_scene_file_select_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void usb_cdrom_scene_file_select_on_exit(void* context) {
    UNUSED(context);
}
