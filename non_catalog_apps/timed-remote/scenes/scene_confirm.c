#include "../timed_remote.h"
#include "timed_remote_scene.h"

static void confirm_popup_callback(void *context) {
  TimedRemoteApp *app = context;
  view_dispatcher_send_custom_event(app->view_dispatcher,
                                    TimedRemoteEventConfirmDone);
}

void timed_remote_scene_confirm_on_enter(void *context) {
  TimedRemoteApp *app = context;

  popup_reset(app->popup);
  popup_set_header(app->popup, "Signal Sent!", 64, 20, AlignCenter,
                   AlignCenter);
  popup_set_text(app->popup, app->signal_name, 64, 35, AlignCenter,
                 AlignCenter);
  popup_set_timeout(app->popup, 2000); /* 2 seconds */
  popup_set_context(app->popup, app);
  popup_set_callback(app->popup, confirm_popup_callback);
  popup_enable_timeout(app->popup);

  view_dispatcher_switch_to_view(app->view_dispatcher, TimedRemoteViewPopup);
}

bool timed_remote_scene_confirm_on_event(void *context,
                                         SceneManagerEvent event) {
  TimedRemoteApp *app = context;
  bool consumed = false;

  if (event.type == SceneManagerEventTypeCustom) {
    if (event.event == TimedRemoteEventConfirmDone) {
      /* Return to file browser */
      scene_manager_search_and_switch_to_previous_scene(
          app->scene_manager, TimedRemoteSceneIrBrowse);
      consumed = true;
    }
  }

  return consumed;
}

void timed_remote_scene_confirm_on_exit(void *context) {
  TimedRemoteApp *app = context;
  popup_reset(app->popup);
}
