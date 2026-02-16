#include "../helpers/ir_helper.h"
#include "../timed_remote.h"
#include "timed_remote_scene.h"

static IrSignalList *signal_list = NULL;

static void ir_select_callback(void *context, uint32_t index) {
  TimedRemoteApp *app = context;
  if (signal_list && index < signal_list->count) {
    /* Free any previous signal */
    if (app->ir_signal) {
      infrared_signal_free(app->ir_signal);
    }
    /* Copy the selected signal */
    app->ir_signal = infrared_signal_alloc();
    infrared_signal_set_signal(app->ir_signal,
                               signal_list->items[index].signal);

    /* Copy signal name */
    strncpy(app->signal_name,
            furi_string_get_cstr(signal_list->items[index].name),
            sizeof(app->signal_name) - 1);
    app->signal_name[sizeof(app->signal_name) - 1] = '\0';

    view_dispatcher_send_custom_event(app->view_dispatcher,
                                      TimedRemoteEventSignalSelected);
  }
}

void timed_remote_scene_ir_select_on_enter(void *context) {
  TimedRemoteApp *app = context;

  submenu_reset(app->submenu);
  submenu_set_header(app->submenu, "Select Signal");

  /* Load signals from selected file */
  signal_list = ir_signal_list_alloc();
  if (ir_helper_load_file(app->selected_file_path, signal_list)) {
    if (signal_list->count == 0) {
      submenu_add_item(app->submenu, "(No signals in file)", 0, NULL, NULL);
    } else {
      for (size_t i = 0; i < signal_list->count; i++) {
        submenu_add_item(app->submenu,
                         furi_string_get_cstr(signal_list->items[i].name), i,
                         ir_select_callback, app);
      }
    }
  } else {
    submenu_add_item(app->submenu, "(Error reading file)", 0, NULL, NULL);
  }

  view_dispatcher_switch_to_view(app->view_dispatcher, TimedRemoteViewSubmenu);
}

bool timed_remote_scene_ir_select_on_event(void *context,
                                           SceneManagerEvent event) {
  TimedRemoteApp *app = context;
  bool consumed = false;

  if (event.type == SceneManagerEventTypeCustom) {
    if (event.event == TimedRemoteEventSignalSelected) {
      scene_manager_next_scene(app->scene_manager,
                               TimedRemoteSceneTimerConfig);
      consumed = true;
    }
  }

  return consumed;
}

void timed_remote_scene_ir_select_on_exit(void *context) {
  TimedRemoteApp *app = context;
  submenu_reset(app->submenu);

  /* Free signal list */
  if (signal_list) {
    ir_signal_list_free(signal_list);
    signal_list = NULL;
  }
}
