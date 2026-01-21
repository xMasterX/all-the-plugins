// scenes/protopirate_scene_saved.c
#include "../protopirate_app_i.h"
#include "../helpers/protopirate_storage.h"

#define TAG "ProtoPirateSceneSaved"

typedef enum {
    SubmenuIndexBack = 0xFF,
} SavedMenuIndex;

static void protopirate_scene_saved_submenu_callback(void* context, uint32_t index) {
    ProtoPirateApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void protopirate_scene_saved_on_enter(void* context) {
    ProtoPirateApp* app = context;

    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Saved Captures");

    FURI_LOG_I(TAG, "Entering saved captures scene");

    uint32_t file_count = protopirate_storage_get_file_count();
    FURI_LOG_I(TAG, "File count: %lu", file_count);

    if(file_count == 0) {
        submenu_add_item(
            app->submenu,
            "No saved captures",
            SubmenuIndexBack,
            protopirate_scene_saved_submenu_callback,
            app);
    } else {
        FuriString* name = furi_string_alloc();
        FuriString* path = furi_string_alloc();

        for(uint32_t i = 0; i < file_count && i < 50; i++) {
            if(protopirate_storage_get_file_by_index(i, path, name)) {
                FURI_LOG_D(TAG, "Adding menu item: %s", furi_string_get_cstr(name));
                submenu_add_item(
                    app->submenu,
                    furi_string_get_cstr(name),
                    i,
                    protopirate_scene_saved_submenu_callback,
                    app);
            }
        }

        furi_string_free(name);
        furi_string_free(path);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, ProtoPirateViewSubmenu);
}

bool protopirate_scene_saved_on_event(void* context, SceneManagerEvent event) {
    ProtoPirateApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubmenuIndexBack) {
            // Just go back
            consumed = true;
        } else {
            // Load and display the selected file
            FuriString* path = furi_string_alloc();
            FuriString* name = furi_string_alloc();

            if(protopirate_storage_get_file_by_index(event.event, path, name)) {
                FURI_LOG_I(TAG, "Loading file: %s", furi_string_get_cstr(path));

                // Store path for the info scene to use
                if(app->loaded_file_path) {
                    furi_string_free(app->loaded_file_path);
                }
                app->loaded_file_path = furi_string_alloc_set(path);

                scene_manager_next_scene(app->scene_manager, ProtoPirateSceneSavedInfo);
            }

            furi_string_free(path);
            furi_string_free(name);
            consumed = true;
        }
    }

    return consumed;
}

void protopirate_scene_saved_on_exit(void* context) {
    ProtoPirateApp* app = context;
    submenu_reset(app->submenu);

    // Free file list cache to save memory
    protopirate_storage_free_file_list();
}
