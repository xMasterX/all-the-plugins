#include "../timed_remote.h"
#include "timed_remote_scene.h"

enum {
  TimerConfigIndexMode,
  TimerConfigIndexHours,
  TimerConfigIndexMinutes,
  TimerConfigIndexSeconds,
  TimerConfigIndexRepeat,
  TimerConfigIndexConfirm,
};

static void timer_config_mode_change(VariableItem *item) {
  TimedRemoteApp *app = variable_item_get_context(item);
  uint8_t index = variable_item_get_current_value_index(item);
  app->timer_mode = (index == 0) ? TimerModeCountdown : TimerModeScheduled;
  variable_item_set_current_value_text(
      item, app->timer_mode == TimerModeCountdown ? "Countdown" : "Scheduled");

  /* Disable repeat in scheduled mode */
  if (app->timer_mode == TimerModeScheduled) {
    app->repeat_count = 0;
  }

  /* Trigger rebuild to show/hide repeat options */
  view_dispatcher_send_custom_event(app->view_dispatcher,
                                    TimedRemoteEventModeChanged);
}

static void timer_config_hours_change(VariableItem *item) {
  TimedRemoteApp *app = variable_item_get_context(item);
  uint8_t index = variable_item_get_current_value_index(item);
  app->hours = index;
  char buf[4];
  snprintf(buf, sizeof(buf), "%02d", app->hours);
  variable_item_set_current_value_text(item, buf);
}

static void timer_config_minutes_change(VariableItem *item) {
  TimedRemoteApp *app = variable_item_get_context(item);
  uint8_t index = variable_item_get_current_value_index(item);
  app->minutes = index;
  char buf[4];
  snprintf(buf, sizeof(buf), "%02d", app->minutes);
  variable_item_set_current_value_text(item, buf);
}

static void timer_config_seconds_change(VariableItem *item) {
  TimedRemoteApp *app = variable_item_get_context(item);
  uint8_t index = variable_item_get_current_value_index(item);
  app->seconds = index;
  char buf[4];
  snprintf(buf, sizeof(buf), "%02d", app->seconds);
  variable_item_set_current_value_text(item, buf);
}

static void timer_config_repeat_change(VariableItem *item) {
  TimedRemoteApp *app = variable_item_get_context(item);
  uint8_t index = variable_item_get_current_value_index(item);

  char buf[16];
  if (index == 0) {
    /* Off */
    app->repeat_count = 0;
    snprintf(buf, sizeof(buf), "Off");
  } else if (index == 1) {
    /* Unlimited */
    app->repeat_count = 255;
    snprintf(buf, sizeof(buf), "Unlimited");
  } else {
    /* 1, 2, 3, ... 99 */
    app->repeat_count = index - 1;
    snprintf(buf, sizeof(buf), "%d", app->repeat_count);
  }
  variable_item_set_current_value_text(item, buf);
}

static void timer_config_enter_callback(void *context, uint32_t index) {
  TimedRemoteApp *app = context;
  /* In countdown mode, confirm is at index 5, in scheduled mode it's at index 4 */
  uint32_t confirm_index = app->timer_mode == TimerModeCountdown
                               ? TimerConfigIndexConfirm
                               : (TimerConfigIndexSeconds + 1);
  if (index == confirm_index) {
    view_dispatcher_send_custom_event(app->view_dispatcher,
                                      TimedRemoteEventTimerConfigured);
  }
}

static void build_timer_config_list(TimedRemoteApp *app) {
  VariableItem *item;
  char buf[16];

  variable_item_list_reset(app->variable_item_list);

  /* Mode: Countdown / Scheduled */
  item = variable_item_list_add(app->variable_item_list, "Mode", 2,
                                timer_config_mode_change, app);
  variable_item_set_current_value_index(
      item, app->timer_mode == TimerModeCountdown ? 0 : 1);
  variable_item_set_current_value_text(
      item, app->timer_mode == TimerModeCountdown ? "Countdown" : "Scheduled");

  /* Hours: 0-23 */
  item = variable_item_list_add(app->variable_item_list, "Hours", 24,
                                timer_config_hours_change, app);
  variable_item_set_current_value_index(item, app->hours);
  snprintf(buf, sizeof(buf), "%02d", app->hours);
  variable_item_set_current_value_text(item, buf);

  /* Minutes: 0-59 */
  item = variable_item_list_add(app->variable_item_list, "Minutes", 60,
                                timer_config_minutes_change, app);
  variable_item_set_current_value_index(item, app->minutes);
  snprintf(buf, sizeof(buf), "%02d", app->minutes);
  variable_item_set_current_value_text(item, buf);

  /* Seconds: 0-59 */
  item = variable_item_list_add(app->variable_item_list, "Seconds", 60,
                                timer_config_seconds_change, app);
  variable_item_set_current_value_index(item, app->seconds);
  snprintf(buf, sizeof(buf), "%02d", app->seconds);
  variable_item_set_current_value_text(item, buf);

  /* Repeat options - only for countdown mode */
  if (app->timer_mode == TimerModeCountdown) {
    /* Repeat: Off, Unlimited, 1, 2, 3, ... 99 (total 101 values) */
    item = variable_item_list_add(app->variable_item_list, "Repeat", 101,
                                  timer_config_repeat_change, app);

    /* Convert repeat_count to index */
    uint8_t repeat_index;
    if (app->repeat_count == 0) {
      repeat_index = 0; /* Off */
    } else if (app->repeat_count == 255) {
      repeat_index = 1; /* Unlimited */
    } else {
      repeat_index = app->repeat_count + 1; /* 1-99 */
    }
    variable_item_set_current_value_index(item, repeat_index);

    /* Set display text */
    if (app->repeat_count == 0) {
      variable_item_set_current_value_text(item, "Off");
    } else if (app->repeat_count == 255) {
      variable_item_set_current_value_text(item, "Unlimited");
    } else {
      snprintf(buf, sizeof(buf), "%d", app->repeat_count);
      variable_item_set_current_value_text(item, buf);
    }
  }

  /* Confirm button */
  variable_item_list_add(app->variable_item_list, ">> Start Timer <<", 0, NULL,
                         NULL);

  variable_item_list_set_enter_callback(app->variable_item_list,
                                        timer_config_enter_callback, app);
}

void timed_remote_scene_timer_config_on_enter(void *context) {
  TimedRemoteApp *app = context;
  build_timer_config_list(app);
  view_dispatcher_switch_to_view(app->view_dispatcher,
                                 TimedRemoteViewVariableItemList);
}

bool timed_remote_scene_timer_config_on_event(void *context,
                                               SceneManagerEvent event) {
  TimedRemoteApp *app = context;
  bool consumed = false;

  if (event.type == SceneManagerEventTypeCustom) {
    if (event.event == TimedRemoteEventModeChanged) {
      /* Rebuild the list to show/hide repeat options */
      build_timer_config_list(app);
      consumed = true;
    } else if (event.event == TimedRemoteEventTimerConfigured) {
      /* Initialize repeats remaining based on repeat_count encoding */
      if (app->repeat_count == 0) {
        /* Off - single execution */
        app->repeats_remaining = 1;
      } else if (app->repeat_count == 255) {
        /* Unlimited */
        app->repeats_remaining = 255;
      } else {
        /* Fixed count (1-99): initial + N repeats = N+1 total executions */
        app->repeats_remaining = app->repeat_count + 1;
      }
      scene_manager_next_scene(app->scene_manager,
                               TimedRemoteSceneTimerRunning);
      consumed = true;
    }
  }

  return consumed;
}

void timed_remote_scene_timer_config_on_exit(void *context) {
  TimedRemoteApp *app = context;
  variable_item_list_reset(app->variable_item_list);
}
