#include "../usb_floppy_app_i.h"

static bool usb_floppy_file_select(UsbFloppyApp* app) {
    furi_assert(app);

    // Firmware forks such as RogueMaster add fields to the end of DialogsFileBrowserOptions,
    // and their dialog_file_browser_set_basic_options writes them. Leave zeroed room for
    // those, or it writes past the struct and over the stack.
    union {
        DialogsFileBrowserOptions options;
        uint8_t room[sizeof(DialogsFileBrowserOptions) + 32];
    } browser = {0};
    dialog_file_browser_set_basic_options(
        &browser.options, USB_FLOPPY_APP_EXTENSION, &I_floppy_10px);
    browser.options.base_path = USB_FLOPPY_APP_PATH_FOLDER;
    browser.options.hide_ext = false;

    // Input events and views are managed by file_select
    return dialog_file_browser_show(
        app->dialogs, app->file_path, app->file_path, &browser.options);
}

void usb_floppy_scene_file_select_on_enter(void* context) {
    UsbFloppyApp* app = context;

    if(usb_floppy_file_select(app)) {
        usb_floppy_app_open_image(app);
    } else {
        scene_manager_previous_scene(app->scene_manager);
    }
}

bool usb_floppy_scene_file_select_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void usb_floppy_scene_file_select_on_exit(void* context) {
    UNUSED(context);
}
