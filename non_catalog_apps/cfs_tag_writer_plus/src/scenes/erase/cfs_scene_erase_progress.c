#include "src/core/cfs_app_i.h"
#include "src/core/cfs_ui.h"

static void cfs_erase_start(CfsApp* app) {
    cfs_ui_show_working(app, "Erasing", "Hold the tag to\nthe back of\nyour Flipper", NULL, false);
    cfs_nfc_worker_start_erase(app->worker);
}

void cfs_scene_erase_progress_on_enter(void* context) {
    CfsApp* app = context;
    cfs_erase_start(app);
}

bool cfs_scene_erase_progress_on_event(void* context, SceneManagerEvent event) {
    CfsApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    switch(event.event) {
    case CfsCustomEventWorkerSuccess:
        cfs_ui_finish_op(app, true, "Tag erased", "It is blank now.");
        return true;
    case CfsCustomEventButtonRetry:
        cfs_erase_start(app);
        return true;
    case CfsCustomEventScreenDone:
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, CfsSceneStart);
        return true;
    case CfsCustomEventWorkerNotPresent:
        cfs_ui_finish_op(app, false, "Erase failed", "No tag found.\nHold it flat on\nthe back.");
        return true;
    case CfsCustomEventWorkerWrongType:
        cfs_ui_finish_op(app, false, "Erase failed", "Not a MIFARE\nClassic 1K tag.");
        return true;
    case CfsCustomEventWorkerWriteFailed:
        cfs_ui_finish_op(app, false, "Erase failed", "Could not write.\nTag locked?");
        return true;
    case CfsCustomEventWorkerVerifyFailed:
        cfs_ui_finish_op(app, false, "Verify failed", "Read-back did\nnot match.");
        return true;
    default:
        return false;
    }
}

void cfs_scene_erase_progress_on_exit(void* context) {
    CfsApp* app = context;
    cfs_nfc_worker_stop(app->worker);
    cfs_screen_reset(app->screen);
}
