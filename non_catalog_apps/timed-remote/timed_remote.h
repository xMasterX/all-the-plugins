#pragma once

#include <furi.h>
#include <gui/gui.h>
#include <gui/modules/popup.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_input.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <gui/scene_manager.h>
#include <gui/view_dispatcher.h>
#include <lib/infrared/signal/infrared_signal.h>

/* Timer mode enumeration */
typedef enum {
  TimerModeCountdown,
  TimerModeScheduled,
} TimerMode;

/* View IDs for ViewDispatcher */
typedef enum {
  TimedRemoteViewSubmenu,
  TimedRemoteViewVariableItemList,
  TimedRemoteViewTextInput,
  TimedRemoteViewWidget,
  TimedRemoteViewPopup,
} TimedRemoteView;

/* Maximum lengths */
#define SIGNAL_NAME_MAX_LEN 32
#define FILE_PATH_MAX_LEN 256

/* Main application state */
typedef struct {
  /* Core Flipper components */
  Gui *gui;
  ViewDispatcher *view_dispatcher;
  SceneManager *scene_manager;

  /* Views */
  Submenu *submenu;
  VariableItemList *variable_item_list;
  TextInput *text_input;
  Widget *widget;
  Popup *popup;

  /* IR state */
  InfraredSignal *ir_signal;
  char signal_name[SIGNAL_NAME_MAX_LEN];
  char selected_file_path[FILE_PATH_MAX_LEN];

  /* Timer configuration */
  TimerMode timer_mode;
  uint8_t hours;
  uint8_t minutes;
  uint8_t seconds;

  /* Repeat options (Countdown mode only) */
  uint8_t repeat_count; /* 0 = off, 255 = unlimited, 1-99 = count */
  uint8_t repeats_remaining;

  /* Timer runtime state */
  FuriTimer *timer;
  uint32_t seconds_remaining;

  /* Text input buffer */
  char text_input_buffer[SIGNAL_NAME_MAX_LEN];
} TimedRemoteApp;

/* App lifecycle */
TimedRemoteApp *timed_remote_app_alloc(void);
void timed_remote_app_free(TimedRemoteApp *app);
