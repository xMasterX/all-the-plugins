#include "src/scheduler_app_i.h"

void scheduler_scene_interval_on_enter(void* context) {
    SchedulerApp* app = context;
    // Scene state 1 = save on exit (OK), 0 = discard (Back)
    scene_manager_set_scene_state(app->scene_manager, SchedulerSceneInterval, 0);
    const int16_t line = app->edit_line;
    char title[24];

    if(line < 0) {
        scheduler_interval_view_configure(app->interval_view, "Set Interval", false, false);
        scheduler_interval_view_set_seconds(
            app->interval_view, scheduler_get_interval_seconds(app->scheduler));
    } else if(scheduler_get_schedule_type(app->scheduler) == SchedulerScheduleTimeOfDay) {
        snprintf(title, sizeof(title), "Line %d Time", line + 1);
        scheduler_interval_view_configure(app->interval_view, title, true, false);
        scheduler_interval_view_set_seconds(
            app->interval_view, scheduler_get_line_time(app->scheduler, line));
    } else {
        // Zero wait chains a line to the one before it
        snprintf(title, sizeof(title), "Line %d Wait", line + 1);
        scheduler_interval_view_configure(app->interval_view, title, false, true);
        scheduler_interval_view_set_seconds(
            app->interval_view, scheduler_get_line_interval(app->scheduler, line));
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, SchedulerAppViewInterval);
}

bool scheduler_scene_interval_on_event(void* context, SceneManagerEvent event) {
    SchedulerApp* app = context;

    // OK saves and closes, Back closes without saving
    if(event.type == SceneManagerEventTypeCustom && event.event == SchedulerEventIntervalDone) {
        scene_manager_set_scene_state(app->scene_manager, SchedulerSceneInterval, 1);
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }
    return false;
}

void scheduler_scene_interval_on_exit(void* context) {
    SchedulerApp* app = context;
    if(!scene_manager_get_scene_state(app->scene_manager, SchedulerSceneInterval)) {
        return;
    }
    const int16_t line = app->edit_line;

    uint32_t sec = scheduler_interval_view_get_seconds(app->interval_view);
    if(line < 0) {
        scheduler_set_interval_seconds(app->scheduler, sec);
    } else if(scheduler_get_schedule_type(app->scheduler) == SchedulerScheduleTimeOfDay) {
        scheduler_set_line_time(app->scheduler, line, sec);
    } else {
        scheduler_set_line_interval(app->scheduler, line, sec);
    }
}
