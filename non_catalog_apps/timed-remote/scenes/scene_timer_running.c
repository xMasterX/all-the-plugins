#include "../helpers/ir_helper.h"
#include "../helpers/time_helper.h"
#include "../timed_remote.h"
#include "timed_remote_scene.h"

static void timer_callback(void *context) {
  TimedRemoteApp *app = context;
  view_dispatcher_send_custom_event(app->view_dispatcher,
                                    TimedRemoteEventTimerTick);
}

static void update_display(TimedRemoteApp *app) {
  uint8_t h, m, s;
  time_helper_seconds_to_hms(app->seconds_remaining, &h, &m, &s);

  char time_str[16];
  snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", h, m, s);

  widget_reset(app->widget);
  widget_add_string_element(app->widget, 64, 5, AlignCenter, AlignTop,
                            FontSecondary, app->signal_name);
  widget_add_string_element(app->widget, 64, 25, AlignCenter, AlignTop,
                            FontBigNumbers, time_str);

  if (app->repeat_count > 0 && app->repeat_count < 255) {
    /* Fixed repeat count (1-99) */
    char repeat_str[24];
    snprintf(repeat_str, sizeof(repeat_str), "Repeat: %d/%d",
             app->repeat_count - app->repeats_remaining + 1, app->repeat_count);
    widget_add_string_element(app->widget, 64, 52, AlignCenter, AlignTop,
                              FontSecondary, repeat_str);
  } else if (app->repeat_count == 255) {
    /* Unlimited repeat */
    widget_add_string_element(app->widget, 64, 52, AlignCenter, AlignTop,
                              FontSecondary, "Repeat: Unlimited");
  }
}

void timed_remote_scene_timer_running_on_enter(void *context) {
  TimedRemoteApp *app = context;

  /* Calculate initial remaining time */
  if (app->timer_mode == TimerModeCountdown) {
    app->seconds_remaining =
        time_helper_hms_to_seconds(app->hours, app->minutes, app->seconds);
  } else {
    /* Scheduled mode - calculate time until target */
    app->seconds_remaining =
        time_helper_seconds_until(app->hours, app->minutes, app->seconds);
    if (app->seconds_remaining == 0) {
      /* Target time already passed - fire immediately */
      view_dispatcher_send_custom_event(app->view_dispatcher,
                                        TimedRemoteEventTimerFired);
      return;
    }
  }

  /* Initialize repeat tracking for non-repeat or scheduled */
  if (app->repeat_count == 0) {
    app->repeats_remaining = 1;
  }

  update_display(app);
  view_dispatcher_switch_to_view(app->view_dispatcher, TimedRemoteViewWidget);

  /* Start 1-second timer */
  app->timer = furi_timer_alloc(timer_callback, FuriTimerTypePeriodic, app);
  furi_timer_start(app->timer, furi_kernel_get_tick_frequency()); /* 1 second */
}

bool timed_remote_scene_timer_running_on_event(void *context,
                                               SceneManagerEvent event) {
  TimedRemoteApp *app = context;
  bool consumed = false;

  if (event.type == SceneManagerEventTypeCustom) {
    if (event.event == TimedRemoteEventTimerTick) {
      if (app->seconds_remaining > 0) {
        app->seconds_remaining--;
        update_display(app);
      }

      if (app->seconds_remaining == 0) {
        /* Timer fired! */
        view_dispatcher_send_custom_event(app->view_dispatcher,
                                          TimedRemoteEventTimerFired);
      }
      consumed = true;
    } else if (event.event == TimedRemoteEventTimerFired) {
      /* Transmit IR signal */
      if (app->ir_signal) {
        ir_helper_transmit(app->ir_signal);
      }

      if (app->repeat_count == 255) {
        /* Unlimited repeat */
        app->seconds_remaining =
            time_helper_hms_to_seconds(app->hours, app->minutes, app->seconds);
        update_display(app);
      } else {
        app->repeats_remaining--;

        if (app->repeat_count != 0 && app->repeats_remaining > 0) {
          /* Reset countdown for next repeat */
          app->seconds_remaining = time_helper_hms_to_seconds(
              app->hours, app->minutes, app->seconds);
          update_display(app);
        } else {
          /* Done - show confirmation */
          scene_manager_next_scene(app->scene_manager, TimedRemoteSceneConfirm);
        }
      }
      consumed = true;
    }
  } else if (event.type == SceneManagerEventTypeBack) {
    /* User pressed Back - cancel timer */
    scene_manager_search_and_switch_to_previous_scene(app->scene_manager,
                                                      TimedRemoteSceneIrBrowse);
    consumed = true;
  }

  return consumed;
}

void timed_remote_scene_timer_running_on_exit(void *context) {
  TimedRemoteApp *app = context;

  /* Stop and free timer */
  if (app->timer) {
    furi_timer_stop(app->timer);
    furi_timer_free(app->timer);
    app->timer = NULL;
  }

  widget_reset(app->widget);
}
