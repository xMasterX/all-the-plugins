#include "usb_cdrom_app_i.h"
#include <furi.h>
#include <storage/storage.h>

static bool usb_cdrom_app_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    UsbCdromApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool usb_cdrom_app_back_event_callback(void* context) {
    furi_assert(context);
    UsbCdromApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void usb_cdrom_app_tick_event_callback(void* context) {
    furi_assert(context);
    UsbCdromApp* app = context;
    scene_manager_handle_tick_event(app->scene_manager);
}

void usb_cdrom_app_show_loading_popup(UsbCdromApp* app, bool show) {
    if(show) {
        // Raise timer priority so that animations can play
        furi_timer_set_thread_priority(FuriTimerThreadPriorityElevated);
        view_dispatcher_switch_to_view(app->view_dispatcher, UsbCdromAppViewLoading);
    } else {
        // Restore default timer priority
        furi_timer_set_thread_priority(FuriTimerThreadPriorityNormal);
    }
}

bool usb_cdrom_app_browse(UsbCdromApp* app) {
    // Firmware forks such as RogueMaster add fields to the end of DialogsFileBrowserOptions,
    // and their dialog_file_browser_set_basic_options writes them. Leave zeroed room for
    // those, or it writes past the struct and over the stack.
    union {
        DialogsFileBrowserOptions options;
        uint8_t room[sizeof(DialogsFileBrowserOptions) + 32];
    } browser = {0};
    dialog_file_browser_set_basic_options(&browser.options, USB_CDROM_APP_EXTENSION, &I_cd_10px);
    browser.options.base_path = USB_CDROM_APP_PATH_FOLDER;
    browser.options.hide_ext = false;

    // Input events and views are managed by file_select
    return dialog_file_browser_show(
        app->dialogs, app->file_path, app->file_path, &browser.options);
}

uint32_t usb_cdrom_app_image_blocks(UsbCdromApp* app, const char* path) {
    FileInfo info;
    if(storage_common_stat(app->fs_api, path, &info) != FSE_OK || file_info_is_dir(&info)) {
        return 0;
    }
    if(!info.size || info.size % CD_BLOCK_SIZE ||
       info.size / CD_BLOCK_SIZE > USB_CDROM_MAX_BLOCKS) {
        return 0;
    }
    return info.size / CD_BLOCK_SIZE;
}

void usb_cdrom_app_open_image(UsbCdromApp* app) {
    if(!usb_cdrom_app_image_blocks(app, furi_string_get_cstr(app->file_path))) {
        scene_manager_next_scene(app->scene_manager, UsbCdromSceneBadImage);
    } else if(furi_hal_usb_is_locked()) {
        scene_manager_next_scene(app->scene_manager, UsbCdromSceneUsbLocked);
    } else {
        scene_manager_next_scene(app->scene_manager, UsbCdromSceneWork);
    }
}

bool usb_cdrom_app_back_to_menu(UsbCdromApp* app) {
    bool consumed = scene_manager_search_and_switch_to_previous_scene(
        app->scene_manager, UsbCdromSceneFileSelect);
    if(!consumed) {
        consumed = scene_manager_search_and_switch_to_previous_scene(
            app->scene_manager, UsbCdromSceneStart);
    }
    return consumed;
}

UsbCdromApp* usb_cdrom_app_alloc(char* arg) {
    UsbCdromApp* app = malloc(sizeof(UsbCdromApp));
    app->file_path = furi_string_alloc();
    app->disc_type = UsbCdromDiscTypeAuto;
    app->disc_mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    if(arg != NULL) {
        furi_string_set_str(app->file_path, arg);
    } else {
        furi_string_set_str(app->file_path, USB_CDROM_APP_PATH_FOLDER);
    }

    app->gui = furi_record_open(RECORD_GUI);
    app->fs_api = furi_record_open(RECORD_STORAGE);
    app->dialogs = furi_record_open(RECORD_DIALOGS);

    app->view_dispatcher = view_dispatcher_alloc();

    app->scene_manager = scene_manager_alloc(&usb_cdrom_scene_handlers, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, usb_cdrom_app_tick_event_callback, 500);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, usb_cdrom_app_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, usb_cdrom_app_back_event_callback);

    app->cd_view = cd_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, UsbCdromAppViewWork, cd_view_get_view(app->cd_view));

    app->loading = loading_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, UsbCdromAppViewLoading, loading_get_view(app->loading));

    app->variable_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        UsbCdromAppViewStart,
        variable_item_list_get_view(app->variable_item_list));

    app->widget = widget_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, UsbCdromAppViewWidget, widget_get_view(app->widget));

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    if(arg != NULL && storage_file_exists(app->fs_api, arg)) {
        usb_cdrom_app_open_image(app);
    } else {
        scene_manager_next_scene(app->scene_manager, UsbCdromSceneStart);
    }

    return app;
}

void usb_cdrom_app_free(UsbCdromApp* app) {
    furi_assert(app);

    // Views
    view_dispatcher_remove_view(app->view_dispatcher, UsbCdromAppViewWork);
    view_dispatcher_remove_view(app->view_dispatcher, UsbCdromAppViewStart);
    view_dispatcher_remove_view(app->view_dispatcher, UsbCdromAppViewLoading);
    view_dispatcher_remove_view(app->view_dispatcher, UsbCdromAppViewWidget);

    cd_view_free(app->cd_view);
    variable_item_list_free(app->variable_item_list);
    loading_free(app->loading);
    widget_free(app->widget);

    // View dispatcher
    view_dispatcher_free(app->view_dispatcher);
    scene_manager_free(app->scene_manager);

    furi_string_free(app->file_path);
    furi_mutex_free(app->disc_mutex);

    // Close records
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_DIALOGS);

    free(app);
}

int32_t usb_cdrom_app(void* p) {
    UsbCdromApp* usb_cdrom_app = usb_cdrom_app_alloc((char*)p);
    view_dispatcher_run(usb_cdrom_app->view_dispatcher);
    usb_cdrom_app_free(usb_cdrom_app);
    return 0;
}
