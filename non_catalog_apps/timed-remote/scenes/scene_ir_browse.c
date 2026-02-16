#include "../helpers/ir_helper.h"
#include "../timed_remote.h"
#include "timed_remote_scene.h"

#define IR_DIR_PATH "/ext/infrared"

static FuriString **file_list = NULL;
static size_t file_count = 0;

static void ir_browse_callback(void *context, uint32_t index) {
  TimedRemoteApp *app = context;
  if (index < file_count) {
    /* Store selected file path */
    snprintf(app->selected_file_path, sizeof(app->selected_file_path), "%s/%s",
             IR_DIR_PATH, furi_string_get_cstr(file_list[index]));
    view_dispatcher_send_custom_event(app->view_dispatcher,
                                      TimedRemoteEventFileSelected);
  }
}

void timed_remote_scene_ir_browse_on_enter(void *context) {
  TimedRemoteApp *app = context;

  submenu_reset(app->submenu);
  submenu_set_header(app->submenu, "Select IR File");

  /* Get list of .ir files */
  if (ir_helper_list_files(IR_DIR_PATH, &file_list, &file_count)) {
    if (file_count == 0) {
      submenu_add_item(app->submenu, "(No IR files found)", 0, NULL, NULL);
    } else {
      for (size_t i = 0; i < file_count; i++) {
        submenu_add_item(app->submenu, furi_string_get_cstr(file_list[i]), i,
                         ir_browse_callback, app);
      }
    }
  } else {
    submenu_add_item(app->submenu, "(Error reading directory)", 0, NULL, NULL);
  }

  view_dispatcher_switch_to_view(app->view_dispatcher, TimedRemoteViewSubmenu);
}

bool timed_remote_scene_ir_browse_on_event(void *context,
                                           SceneManagerEvent event) {
  TimedRemoteApp *app = context;
  bool consumed = false;

  if (event.type == SceneManagerEventTypeCustom) {
    if (event.event == TimedRemoteEventFileSelected) {
      scene_manager_next_scene(app->scene_manager, TimedRemoteSceneIrSelect);
      consumed = true;
    }
  }

  return consumed;
}

void timed_remote_scene_ir_browse_on_exit(void *context) {
  TimedRemoteApp *app = context;
  submenu_reset(app->submenu);

  /* Free file list */
  if (file_list) {
    ir_helper_free_file_list(file_list, file_count);
    file_list = NULL;
    file_count = 0;
  }
}
