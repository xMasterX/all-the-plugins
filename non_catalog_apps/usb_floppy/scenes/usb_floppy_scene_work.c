#include "../usb_floppy_app_i.h"
#include "../views/floppy_view.h"
#include "../helpers/floppy_usb.h"
#include <lib/toolbox/path.h>

#define TAG "UsbFloppySceneWork"

static bool file_read(void* ctx, uint32_t lba, uint32_t count, uint8_t* out) {
    UsbFloppyApp* app = ctx;
    FURI_LOG_T(TAG, "file_read lba=%08lX count=%lu", lba, count);
    if(!storage_file_seek(app->file, lba * UFI_BLOCK_SIZE, true)) {
        FURI_LOG_W(TAG, "seek failed");
        return false;
    }
    size_t len = count * UFI_BLOCK_SIZE;
    size_t read = storage_file_read(app->file, out, len);
    app->bytes_read += read;
    return read == len;
}

static bool file_write(void* ctx, uint32_t lba, uint32_t count, const uint8_t* buf) {
    UsbFloppyApp* app = ctx;
    FURI_LOG_T(TAG, "file_write lba=%08lX count=%lu", lba, count);
    if(!storage_file_seek(app->file, lba * UFI_BLOCK_SIZE, true)) {
        FURI_LOG_W(TAG, "seek failed");
        return false;
    }
    size_t len = count * UFI_BLOCK_SIZE;
    size_t written = storage_file_write(app->file, buf, len);
    app->bytes_written += written;
    return written == len;
}

static void file_eject(void* ctx) {
    UsbFloppyApp* app = ctx;
    FURI_LOG_D(TAG, "EJECT");
    view_dispatcher_send_custom_event(app->view_dispatcher, UsbFloppyCustomEventEject);
}

static void usb_connection_status_cb(bool connected, void* ctx) {
    UNUSED(connected);
    UsbFloppyApp* app = ctx;
    view_dispatcher_send_custom_event(app->view_dispatcher, UsbFloppyCustomEventConnectionError);
}

bool usb_floppy_scene_work_on_event(void* context, SceneManagerEvent event) {
    UsbFloppyApp* app = context;
    bool consumed = false;
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == UsbFloppyCustomEventEject) {
            consumed = usb_floppy_app_back_to_menu(app);
        } else if(event.event == UsbFloppyCustomEventConnectionError) {
            floppy_view_set_connection_error(app->floppy_view);
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeTick) {
        floppy_view_set_stats(app->floppy_view, app->bytes_read, app->bytes_written);
    } else if(event.type == SceneManagerEventTypeBack) {
        consumed = usb_floppy_app_back_to_menu(app);
    }
    return consumed;
}

void usb_floppy_scene_work_on_enter(void* context) {
    UsbFloppyApp* app = context;
    app->bytes_read = app->bytes_written = 0;
    floppy_view_clear_connection_error(app->floppy_view);

    usb_floppy_app_show_loading_popup(app, true);

    FuriString* file_name = furi_string_alloc();
    path_extract_filename(app->file_path, file_name, true);
    floppy_view_set_file_name(app->floppy_view, file_name);

    FuriString* info = furi_string_alloc_printf(
        "%s  %s%s",
        app->geometry->name,
        app->transport == FloppyTransportCbi ? "CBI" : "BOT",
        app->write_protect ? "  WP" : "");
    floppy_view_set_info(app->floppy_view, info);
    furi_string_free(info);

    // write protect also opens the image read only, so the SD card copy is safe
    app->file = storage_file_alloc(app->fs_api);
    furi_check(storage_file_open(
        app->file,
        furi_string_get_cstr(app->file_path),
        app->write_protect ? FSAM_READ : (FSAM_READ | FSAM_WRITE),
        FSOM_OPEN_EXISTING));

    UfiDevice device = {
        .ctx = app,
        .geometry = app->geometry,
        .write_protect = app->write_protect,
        .read = file_read,
        .write = file_write,
        .eject = file_eject,
    };

    app->usb = floppy_usb_start(furi_string_get_cstr(file_name), app->transport, device);
    if(app->usb) {
        floppy_usb_set_connection_status_callback(app->usb, usb_connection_status_cb, app);
    }

    furi_string_free(file_name);

    usb_floppy_app_show_loading_popup(app, false);
    view_dispatcher_switch_to_view(app->view_dispatcher, UsbFloppyAppViewWork);
}

void usb_floppy_scene_work_on_exit(void* context) {
    UsbFloppyApp* app = context;
    usb_floppy_app_show_loading_popup(app, true);

    if(app->usb) {
        floppy_usb_set_connection_status_callback(app->usb, NULL, NULL);
        floppy_usb_stop(app->usb);
        app->usb = NULL;
    }
    if(app->file) {
        storage_file_free(app->file);
        app->file = NULL;
    }
    usb_floppy_app_show_loading_popup(app, false);
}
