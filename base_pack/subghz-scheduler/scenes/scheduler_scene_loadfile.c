#include "scheduler_scene_loadfile.h"
#include "helpers/scheduler_playlist.h"
#include "src/scheduler_app_i.h"
#include "views/scheduler_start_view_settings.h"

#include <flipper_format/flipper_format_i.h>
#include <types.h>

#define TAG "SubGHzSchedulerLoadFile"

bool check_file_extension(const char* filename) {
    const char* extension = strrchr(filename, '.');
    return extension && (!strcmp(extension, ".txt") || !strcmp(extension, ".sub"));
}

static bool load_protocol_from_file(SchedulerApp* app) {
    furi_assert(app);

    DialogsFileBrowserOptions browser_options;
    dialog_file_browser_set_basic_options(&browser_options, ".sub|.txt", &I_sub1_10px);
    browser_options.skip_assets = true;
    browser_options.hide_ext = false;
    browser_options.base_path = SUBGHZ_APP_FOLDER;

    FuriString* path = furi_string_alloc_set(app->tx_file_path);
    if(furi_string_empty(path)) {
        furi_string_set(path, SUBGHZ_APP_FOLDER);
    }

    // Input events and views are managed by file_select
    bool ok = dialog_file_browser_show(app->dialogs, path, path, &browser_options);

    if(ok) {
        const char* filestr = furi_string_get_cstr(path);
        const char* ext = strrchr(filestr, '.');
        int list_count = 0;

        // Only attempt count of TXT playlist files
        if(ext && strcmp(ext, ".txt") == 0) {
            Storage* storage = furi_record_open(RECORD_STORAGE);
            list_count = scheduler_playlist_count(storage, filestr);
            furi_record_close(RECORD_STORAGE);

            if(list_count <= 0) {
                dialog_message_show_storage_error(
                    app->dialogs, "Playlist is empty\nor unreadable!");
                ok = false;
            } else if(list_count > SCHEDULER_MAX_LINES) {
                dialog_message_show_storage_error(
                    app->dialogs, "Playlist has more\nthan 127 lines!");
                ok = false;
            }
        }

        if(ok) {
            furi_string_set(app->tx_file_path, path);
            scheduler_set_file(app->scheduler, filestr, (int8_t)list_count);
        }
    }

    furi_string_free(path);
    return ok;
}

void scheduler_scene_load_on_enter(void* context) {
    furi_assert(context);
    SchedulerApp* app = context;
    if(load_protocol_from_file(app)) {
        scene_manager_set_scene_state(
            app->scene_manager, SchedulerSceneStart, MenuIndexStartSchedule);
    }
    scene_manager_search_and_switch_to_previous_scene(app->scene_manager, SchedulerSceneStart);
}

void scheduler_scene_load_on_exit(void* context) {
    furi_assert(context);
    UNUSED(context);
}

bool scheduler_scene_load_on_event(void* context, SceneManagerEvent event) {
    furi_assert(context);
    SchedulerApp* app = context;
    UNUSED(app);
    UNUSED(event);
    return false;
}
