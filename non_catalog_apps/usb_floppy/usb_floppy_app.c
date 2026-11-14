#include "usb_floppy_app_i.h"
#include <furi.h>
#include <storage/storage.h>

static bool usb_floppy_app_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    UsbFloppyApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool usb_floppy_app_back_event_callback(void* context) {
    furi_assert(context);
    UsbFloppyApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void usb_floppy_app_tick_event_callback(void* context) {
    furi_assert(context);
    UsbFloppyApp* app = context;
    scene_manager_handle_tick_event(app->scene_manager);
}

void usb_floppy_app_show_loading_popup(UsbFloppyApp* app, bool show) {
    if(show) {
        // Raise timer priority so that animations can play
        furi_timer_set_thread_priority(FuriTimerThreadPriorityElevated);
        view_dispatcher_switch_to_view(app->view_dispatcher, UsbFloppyAppViewLoading);
    } else {
        // Restore default timer priority
        furi_timer_set_thread_priority(FuriTimerThreadPriorityNormal);
    }
}

void usb_floppy_app_open_image(UsbFloppyApp* app) {
    FileInfo info;
    app->geometry = NULL;
    if(storage_common_stat(app->fs_api, furi_string_get_cstr(app->file_path), &info) == FSE_OK &&
       !file_info_is_dir(&info)) {
        app->geometry = ufi_geometry_find(info.size);
    }

    if(!app->geometry) {
        scene_manager_next_scene(app->scene_manager, UsbFloppySceneBadImage);
    } else if(furi_hal_usb_is_locked()) {
        scene_manager_next_scene(app->scene_manager, UsbFloppySceneUsbLocked);
    } else {
        scene_manager_next_scene(app->scene_manager, UsbFloppySceneWork);
    }
}

bool usb_floppy_app_back_to_menu(UsbFloppyApp* app) {
    bool consumed = scene_manager_search_and_switch_to_previous_scene(
        app->scene_manager, UsbFloppySceneFileSelect);
    if(!consumed) {
        consumed = scene_manager_search_and_switch_to_previous_scene(
            app->scene_manager, UsbFloppySceneStart);
    }
    return consumed;
}

UsbFloppyApp* usb_floppy_app_alloc(char* arg) {
    UsbFloppyApp* app = malloc(sizeof(UsbFloppyApp));
    app->file_path = furi_string_alloc();
    app->transport = FloppyTransportCbi;
    app->write_protect = false;

    if(arg != NULL) {
        furi_string_set_str(app->file_path, arg);
    } else {
        furi_string_set_str(app->file_path, USB_FLOPPY_APP_PATH_FOLDER);
    }

    app->gui = furi_record_open(RECORD_GUI);
    app->fs_api = furi_record_open(RECORD_STORAGE);
    app->dialogs = furi_record_open(RECORD_DIALOGS);

    app->view_dispatcher = view_dispatcher_alloc();

    app->scene_manager = scene_manager_alloc(&usb_floppy_scene_handlers, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, usb_floppy_app_tick_event_callback, 500);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, usb_floppy_app_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, usb_floppy_app_back_event_callback);

    app->floppy_view = floppy_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, UsbFloppyAppViewWork, floppy_view_get_view(app->floppy_view));

    app->loading = loading_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, UsbFloppyAppViewLoading, loading_get_view(app->loading));

    app->variable_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        UsbFloppyAppViewStart,
        variable_item_list_get_view(app->variable_item_list));

    app->widget = widget_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, UsbFloppyAppViewWidget, widget_get_view(app->widget));

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    if(arg != NULL && storage_file_exists(app->fs_api, arg)) {
        usb_floppy_app_open_image(app);
    } else {
        scene_manager_next_scene(app->scene_manager, UsbFloppySceneStart);
    }

    return app;
}

void usb_floppy_app_free(UsbFloppyApp* app) {
    furi_assert(app);

    // Views
    view_dispatcher_remove_view(app->view_dispatcher, UsbFloppyAppViewWork);
    view_dispatcher_remove_view(app->view_dispatcher, UsbFloppyAppViewStart);
    view_dispatcher_remove_view(app->view_dispatcher, UsbFloppyAppViewLoading);
    view_dispatcher_remove_view(app->view_dispatcher, UsbFloppyAppViewWidget);

    floppy_view_free(app->floppy_view);
    variable_item_list_free(app->variable_item_list);
    loading_free(app->loading);
    widget_free(app->widget);

    // View dispatcher
    view_dispatcher_free(app->view_dispatcher);
    scene_manager_free(app->scene_manager);

    furi_string_free(app->file_path);

    // Close records
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_DIALOGS);

    free(app);
}

int32_t usb_floppy_app(void* p) {
    UsbFloppyApp* usb_floppy_app = usb_floppy_app_alloc((char*)p);
    view_dispatcher_run(usb_floppy_app->view_dispatcher);
    usb_floppy_app_free(usb_floppy_app);
    return 0;
}
