#include "../usb_cdrom_app_i.h"
#include "../views/cd_view.h"
#include "../helpers/cd_usb.h"
#include <lib/toolbox/path.h>

#define TAG "UsbCdromSceneWork"

// a line under the disc details, kept in the scene state
typedef enum {
    WorkNoticeNone,
    WorkNoticeBadImage,
    WorkNoticeEjectRequested,
} WorkNotice;

static bool disc_is_dvd(UsbCdromApp* app, uint32_t blocks) {
    switch(app->disc_type) {
    case UsbCdromDiscTypeCd:
        return false;
    case UsbCdromDiscTypeDvd:
        return true;
    default:
        return blocks > USB_CDROM_CD_MAX_BLOCKS;
    }
}

// Opens the image at app->file_path and closes the tray on it
static bool disc_insert(UsbCdromApp* app) {
    const char* path = furi_string_get_cstr(app->file_path);
    uint32_t blocks = usb_cdrom_app_image_blocks(app, path);
    if(!blocks) return false;
    File* file = storage_file_alloc(app->fs_api);
    if(!storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        FURI_LOG_W(TAG, "can't open %s", path);
        storage_file_free(file);
        return false;
    }
    furi_check(furi_mutex_acquire(app->disc_mutex, FuriWaitForever) == FuriStatusOk);
    app->file = file;
    app->disc_blocks = blocks;
    app->disc_dvd = disc_is_dvd(app, blocks);
    app->disc_id++;
    furi_mutex_release(app->disc_mutex);
    FURI_LOG_D(TAG, "inserted %s, %lu blocks", path, blocks);
    return true;
}

static void disc_remove(UsbCdromApp* app) {
    furi_check(furi_mutex_acquire(app->disc_mutex, FuriWaitForever) == FuriStatusOk);
    if(app->file) {
        storage_file_free(app->file);
        app->file = NULL;
        app->disc_id++;
    }
    app->eject_request = false;
    furi_mutex_release(app->disc_mutex);
}

// The eject button. A real drive can't open while the host has it locked, so it
// asks the host to eject instead. Forcing it opens the tray anyway.
static void disc_eject_button(UsbCdromApp* app, bool force) {
    furi_check(furi_mutex_acquire(app->disc_mutex, FuriWaitForever) == FuriStatusOk);
    bool ask_host = app->locked && !force;
    if(ask_host) {
        app->eject_request = true;
    }
    furi_mutex_release(app->disc_mutex);

    if(ask_host) {
        scene_manager_set_scene_state(
            app->scene_manager, UsbCdromSceneWork, WorkNoticeEjectRequested);
    } else {
        disc_remove(app);
        scene_manager_set_scene_state(app->scene_manager, UsbCdromSceneWork, WorkNoticeNone);
    }
}

static void disc_insert_button(UsbCdromApp* app) {
    if(!usb_cdrom_app_browse(app)) return;
    WorkNotice notice = disc_insert(app) ? WorkNoticeNone : WorkNoticeBadImage;
    scene_manager_set_scene_state(app->scene_manager, UsbCdromSceneWork, notice);
}

static void work_update_view(UsbCdromApp* app) {
    furi_check(furi_mutex_acquire(app->disc_mutex, FuriWaitForever) == FuriStatusOk);
    bool loaded = app->file;
    bool locked = app->locked;
    uint32_t blocks = app->disc_blocks;
    bool dvd = app->disc_dvd;
    furi_mutex_release(app->disc_mutex);

    FuriString* name = furi_string_alloc();
    FuriString* info = furi_string_alloc();
    if(loaded) {
        path_extract_filename(app->file_path, name, true);
        uint32_t mb = blocks / (1024 * 1024 / CD_BLOCK_SIZE);
        furi_string_printf(info, "%s ", dvd ? "DVD" : "CD");
        if(mb < 1024) {
            furi_string_cat_printf(info, "%luM", mb);
        } else {
            furi_string_cat_printf(info, "%.1fG", (double)mb / 1024);
        }
        if(locked) {
            furi_string_cat_str(info, "  Locked");
        }
    } else {
        furi_string_set_str(name, "Tray open");
    }
    switch(scene_manager_get_scene_state(app->scene_manager, UsbCdromSceneWork)) {
    case WorkNoticeBadImage:
        furi_string_set_str(info, "Not an ISO image");
        break;
    case WorkNoticeEjectRequested:
        if(loaded) furi_string_set_str(info, "Eject sent to host");
        break;
    }
    cd_view_set_disc(app->cd_view, loaded, name, info);
    furi_string_free(name);
    furi_string_free(info);
}

static void disc_media(void* ctx, CdMedia* media) {
    UsbCdromApp* app = ctx;
    furi_check(furi_mutex_acquire(app->disc_mutex, FuriWaitForever) == FuriStatusOk);
    media->blocks = app->file ? app->disc_blocks : 0;
    media->dvd = app->disc_dvd;
    media->id = app->disc_id;
    media->eject_request = app->eject_request;
    app->eject_request = false;
    furi_mutex_release(app->disc_mutex);
}

static bool disc_read(void* ctx, uint32_t lba, uint32_t count, uint8_t* out) {
    UsbCdromApp* app = ctx;
    FURI_LOG_T(TAG, "disc_read lba=%08lX count=%lu", lba, count);
    bool ok = false;
    furi_check(furi_mutex_acquire(app->disc_mutex, FuriWaitForever) == FuriStatusOk);
    if(!app->file) {
        FURI_LOG_W(TAG, "read with the tray open");
    } else if(!storage_file_seek(app->file, lba * CD_BLOCK_SIZE, true)) {
        FURI_LOG_W(TAG, "seek failed");
    } else {
        size_t len = count * CD_BLOCK_SIZE;
        size_t read = storage_file_read(app->file, out, len);
        app->bytes_read += read;
        ok = read == len;
    }
    furi_mutex_release(app->disc_mutex);
    return ok;
}

static void disc_eject(void* ctx) {
    UsbCdromApp* app = ctx;
    FURI_LOG_D(TAG, "EJECT");
    disc_remove(app);
    view_dispatcher_send_custom_event(app->view_dispatcher, UsbCdromCustomEventTrayChanged);
}

static void disc_lock(void* ctx, bool locked) {
    UsbCdromApp* app = ctx;
    furi_check(furi_mutex_acquire(app->disc_mutex, FuriWaitForever) == FuriStatusOk);
    app->locked = locked;
    furi_mutex_release(app->disc_mutex);
    view_dispatcher_send_custom_event(app->view_dispatcher, UsbCdromCustomEventTrayChanged);
}

static void usb_connection_status_cb(bool connected, void* ctx) {
    UNUSED(connected);
    UsbCdromApp* app = ctx;
    view_dispatcher_send_custom_event(app->view_dispatcher, UsbCdromCustomEventConnectionError);
}

static void work_button_cb(InputType type, void* ctx) {
    UsbCdromApp* app = ctx;
    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->view_dispatcher, UsbCdromCustomEventTrayButton);
    } else if(type == InputTypeLong) {
        view_dispatcher_send_custom_event(app->view_dispatcher, UsbCdromCustomEventForceEject);
    }
}

bool usb_cdrom_scene_work_on_event(void* context, SceneManagerEvent event) {
    UsbCdromApp* app = context;
    bool consumed = false;
    if(event.type == SceneManagerEventTypeCustom) {
        consumed = true;
        if(event.event == UsbCdromCustomEventTrayButton) {
            if(app->file) {
                disc_eject_button(app, false);
            } else {
                disc_insert_button(app);
            }
        } else if(event.event == UsbCdromCustomEventForceEject) {
            disc_eject_button(app, true);
        } else if(event.event == UsbCdromCustomEventConnectionError) {
            cd_view_set_connection_error(app->cd_view);
        } else if(event.event != UsbCdromCustomEventTrayChanged) {
            consumed = false;
        }
        work_update_view(app);
    } else if(event.type == SceneManagerEventTypeTick) {
        cd_view_set_stats(app->cd_view, app->bytes_read);
        work_update_view(app);
    } else if(event.type == SceneManagerEventTypeBack) {
        consumed = usb_cdrom_app_back_to_menu(app);
    }
    return consumed;
}

void usb_cdrom_scene_work_on_enter(void* context) {
    UsbCdromApp* app = context;
    app->bytes_read = 0;
    app->locked = false;
    app->eject_request = false;
    cd_view_clear_connection_error(app->cd_view);
    cd_view_set_button_callback(app->cd_view, work_button_cb, app);

    usb_cdrom_app_show_loading_popup(app, true);

    WorkNotice notice = disc_insert(app) ? WorkNoticeNone : WorkNoticeBadImage;
    scene_manager_set_scene_state(app->scene_manager, UsbCdromSceneWork, notice);
    work_update_view(app);

    CdDevice device = {
        .ctx = app,
        .media = disc_media,
        .read = disc_read,
        .eject = disc_eject,
        .lock = disc_lock,
    };

    app->usb = cd_usb_start(device);
    if(app->usb) {
        cd_usb_set_connection_status_callback(app->usb, usb_connection_status_cb, app);
    }

    usb_cdrom_app_show_loading_popup(app, false);
    view_dispatcher_switch_to_view(app->view_dispatcher, UsbCdromAppViewWork);
}

void usb_cdrom_scene_work_on_exit(void* context) {
    UsbCdromApp* app = context;
    usb_cdrom_app_show_loading_popup(app, true);

    if(app->usb) {
        cd_usb_set_connection_status_callback(app->usb, NULL, NULL);
        cd_usb_stop(app->usb);
        app->usb = NULL;
    }
    disc_remove(app);
    cd_view_set_button_callback(app->cd_view, NULL, NULL);
    usb_cdrom_app_show_loading_popup(app, false);
}
