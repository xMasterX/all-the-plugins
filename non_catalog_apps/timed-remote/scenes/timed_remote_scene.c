#include "timed_remote_scene.h"

/* Scene handler tables */

void (*const timed_remote_scene_on_enter_handlers[])(void *) = {
    timed_remote_scene_ir_browse_on_enter,
    timed_remote_scene_ir_select_on_enter,
    timed_remote_scene_timer_config_on_enter,
    timed_remote_scene_timer_running_on_enter,
    timed_remote_scene_confirm_on_enter,
};

bool (*const timed_remote_scene_on_event_handlers[])(void *,
                                                     SceneManagerEvent) = {
    timed_remote_scene_ir_browse_on_event,
    timed_remote_scene_ir_select_on_event,
    timed_remote_scene_timer_config_on_event,
    timed_remote_scene_timer_running_on_event,
    timed_remote_scene_confirm_on_event,
};

void (*const timed_remote_scene_on_exit_handlers[])(void *) = {
    timed_remote_scene_ir_browse_on_exit,
    timed_remote_scene_ir_select_on_exit,
    timed_remote_scene_timer_config_on_exit,
    timed_remote_scene_timer_running_on_exit,
    timed_remote_scene_confirm_on_exit,
};

const SceneManagerHandlers timed_remote_scene_handlers = {
    .on_enter_handlers = timed_remote_scene_on_enter_handlers,
    .on_event_handlers = timed_remote_scene_on_event_handlers,
    .on_exit_handlers = timed_remote_scene_on_exit_handlers,
    .scene_num = TimedRemoteSceneCount,
};
