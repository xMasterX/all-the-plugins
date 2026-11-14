#include "helpers/scheduler_hms.h"
#include "helpers/scheduler_settings_io.h"
#include "src/scheduler_app_i.h"

#include <flipper_format/flipper_format.h>

#define TAG "SubGHzSchedulerLines"

static void scheduler_scene_lines_callback(void* context, uint32_t index) {
    furi_assert(context);
    SchedulerApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void scheduler_scene_lines_on_enter(void* context) {
    furi_assert(context);
    SchedulerApp* app = context;

    const bool time_of_day =
        scheduler_get_schedule_type(app->scheduler) == SchedulerScheduleTimeOfDay;
    const uint8_t count = scheduler_get_list_count(app->scheduler);

    submenu_reset(app->menu);
    submenu_set_header(app->menu, time_of_day ? "Line Times" : "Line Waits");

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    FuriString* sub_path = furi_string_alloc();
    FuriString* label = furi_string_alloc();
    char value[16];

    bool have_file =
        flipper_format_file_open_existing(ff, furi_string_get_cstr(app->tx_file_path));

    for(uint8_t i = 0; i < count; ++i) {
        if(have_file && !flipper_format_read_string(ff, "sub", sub_path)) {
            have_file = false;
        }

        if(time_of_day) {
            value[0] = '@';
            scheduler_seconds_to_hms_string(
                scheduler_get_line_time(app->scheduler, i), value + 1, sizeof(value) - 1);
        } else {
            scheduler_seconds_to_hms_string(
                scheduler_get_line_interval(app->scheduler, i), value, sizeof(value));
        }

        furi_string_printf(
            label,
            "%s %s",
            value,
            have_file ? path_basename(furi_string_get_cstr(sub_path)) : "?");
        submenu_add_item(
            app->menu, furi_string_get_cstr(label), i, scheduler_scene_lines_callback, app);
    }

    flipper_format_file_close(ff);
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
    furi_string_free(sub_path);
    furi_string_free(label);

    submenu_set_selected_item(
        app->menu, scene_manager_get_scene_state(app->scene_manager, SchedulerSceneLines));

    view_dispatcher_switch_to_view(app->view_dispatcher, SchedulerAppViewMenu);
}

bool scheduler_scene_lines_on_event(void* context, SceneManagerEvent event) {
    furi_assert(context);
    SchedulerApp* app = context;

    if(event.type == SceneManagerEventTypeCustom &&
       event.event < scheduler_get_list_count(app->scheduler)) {
        scene_manager_set_scene_state(app->scene_manager, SchedulerSceneLines, event.event);
        app->edit_line = (int16_t)event.event;
        scene_manager_next_scene(app->scene_manager, SchedulerSceneInterval);
        return true;
    }

    return false;
}

void scheduler_scene_lines_on_exit(void* context) {
    furi_assert(context);
    SchedulerApp* app = context;
    submenu_reset(app->menu);
}
