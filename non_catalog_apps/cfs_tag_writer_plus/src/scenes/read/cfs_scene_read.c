#include "src/core/cfs_app_i.h"
#include "src/core/cfs_ui.h"

static void cfs_read_capture_result(CfsApp* app) {
    const CfsTagData* d = cfs_nfc_worker_get_tag_data(app->worker);
    app->tag_data = *d;
    memcpy(app->tag_plain, cfs_nfc_worker_get_read_plain(app->worker), CFS_PLAINTEXT_SIZE);
    app->has_tag_plain = true;
    size_t ul = 0;
    const uint8_t* u = cfs_nfc_worker_get_uid(app->worker, &ul);
    if(u) {
        if(ul > sizeof(app->uid)) ul = sizeof(app->uid);
        memcpy(app->uid, u, ul);
        app->uid_len = ul;
    }
}

static void cfs_read_show_error(CfsApp* app, const char* text) {
    cfs_ui_finish_op(app, false, "Read failed", text);
}

static void cfs_read_start(CfsApp* app) {
    cfs_ui_show_working(
        app, "Reading", "Hold a CFS tag\nto the back of\nyour Flipper", NULL, false);
    cfs_nfc_worker_start_read(app->worker);
}

void cfs_scene_read_on_enter(void* context) {
    CfsApp* app = context;
    app->result_read_only = false; // this is a live read
    cfs_read_start(app);
}

bool cfs_scene_read_on_event(void* context, SceneManagerEvent event) {
    CfsApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    switch(event.event) {
    case CfsCustomEventWorkerSuccess:
        cfs_read_capture_result(app);
        app->is_blank = false;
        notification_message(app->notifications, &sequence_success);
        scene_manager_next_scene(app->scene_manager, CfsSceneReadResult);
        return true;
    case CfsCustomEventWorkerBlankTag:
        cfs_read_capture_result(app);
        app->is_blank = true;
        scene_manager_next_scene(app->scene_manager, CfsSceneReadResult);
        return true;
    case CfsCustomEventButtonRetry:
        cfs_read_start(app);
        return true;
    case CfsCustomEventWorkerNotPresent:
        cfs_read_show_error(app, "No tag found.\nHold it flat on\nthe back.");
        return true;
    case CfsCustomEventWorkerWrongType:
        cfs_read_show_error(app, "Not a MIFARE\nClassic 1K tag.");
        return true;
    case CfsCustomEventWorkerAuthFailed:
        cfs_read_show_error(app, "Auth failed.\nNot a CFS tag?");
        return true;
    case CfsCustomEventWorkerReadFailed:
        cfs_read_show_error(app, "Could not decode\nthe tag data.");
        return true;
    default:
        return false;
    }
}

void cfs_scene_read_on_exit(void* context) {
    CfsApp* app = context;
    cfs_nfc_worker_stop(app->worker);
    cfs_screen_reset(app->screen);
}
