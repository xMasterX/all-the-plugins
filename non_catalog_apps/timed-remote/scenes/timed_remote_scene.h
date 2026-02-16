#pragma once

#include <gui/scene_manager.h>

/* Scene IDs */
typedef enum {
  TimedRemoteSceneIrBrowse,
  TimedRemoteSceneIrSelect,
  TimedRemoteSceneTimerConfig,
  TimedRemoteSceneTimerRunning,
  TimedRemoteSceneConfirm,
  TimedRemoteSceneCount,
} TimedRemoteScene;

/* Scene event IDs */
typedef enum {
  TimedRemoteSceneEventConsumed = true,
  TimedRemoteSceneEventNotConsumed = false,
} TimedRemoteSceneEvent;

/* Custom events */
typedef enum {
  /* File/signal selected */
  TimedRemoteEventFileSelected,
  TimedRemoteEventSignalSelected,
  /* Timer configuration */
  TimedRemoteEventModeChanged,
  TimedRemoteEventTimerConfigured,
  /* Timer events */
  TimedRemoteEventTimerTick,
  TimedRemoteEventTimerFired,
  /* Confirmation */
  TimedRemoteEventConfirmDone,
} TimedRemoteCustomEvent;

/* Scene handlers - declared extern, defined in individual scene files */
extern void timed_remote_scene_ir_browse_on_enter(void *context);
extern bool timed_remote_scene_ir_browse_on_event(void *context,
                                                  SceneManagerEvent event);
extern void timed_remote_scene_ir_browse_on_exit(void *context);

extern void timed_remote_scene_ir_select_on_enter(void *context);
extern bool timed_remote_scene_ir_select_on_event(void *context,
                                                  SceneManagerEvent event);
extern void timed_remote_scene_ir_select_on_exit(void *context);

extern void timed_remote_scene_timer_config_on_enter(void *context);
extern bool timed_remote_scene_timer_config_on_event(void *context,
                                                     SceneManagerEvent event);
extern void timed_remote_scene_timer_config_on_exit(void *context);

extern void timed_remote_scene_timer_running_on_enter(void *context);
extern bool timed_remote_scene_timer_running_on_event(void *context,
                                                      SceneManagerEvent event);
extern void timed_remote_scene_timer_running_on_exit(void *context);

extern void timed_remote_scene_confirm_on_enter(void *context);
extern bool timed_remote_scene_confirm_on_event(void *context,
                                                SceneManagerEvent event);
extern void timed_remote_scene_confirm_on_exit(void *context);
