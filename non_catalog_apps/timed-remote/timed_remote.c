#include "timed_remote.h"
#include "scenes/timed_remote_scene.h"

extern const SceneManagerHandlers timed_remote_scene_handlers;

/* View dispatcher navigation callback */
static bool timed_remote_navigation_callback(void *context) {
  TimedRemoteApp *app = context;
  return scene_manager_handle_back_event(app->scene_manager);
}

/* View dispatcher custom event callback */
static bool timed_remote_custom_event_callback(void *context,
                                               uint32_t custom_event) {
  TimedRemoteApp *app = context;
  return scene_manager_handle_custom_event(app->scene_manager, custom_event);
}

TimedRemoteApp *timed_remote_app_alloc(void) {
  TimedRemoteApp *app = malloc(sizeof(TimedRemoteApp));
  memset(app, 0, sizeof(TimedRemoteApp));

  /* Open GUI record */
  app->gui = furi_record_open(RECORD_GUI);

  /* Allocate view dispatcher */
  app->view_dispatcher = view_dispatcher_alloc();
  view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
  view_dispatcher_set_navigation_event_callback(
      app->view_dispatcher, timed_remote_navigation_callback);
  view_dispatcher_set_custom_event_callback(app->view_dispatcher,
                                            timed_remote_custom_event_callback);
  view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui,
                                ViewDispatcherTypeFullscreen);

  /* Allocate scene manager */
  app->scene_manager = scene_manager_alloc(&timed_remote_scene_handlers, app);

  /* Allocate views */
  app->submenu = submenu_alloc();
  view_dispatcher_add_view(app->view_dispatcher, TimedRemoteViewSubmenu,
                           submenu_get_view(app->submenu));

  app->variable_item_list = variable_item_list_alloc();
  view_dispatcher_add_view(
      app->view_dispatcher, TimedRemoteViewVariableItemList,
      variable_item_list_get_view(app->variable_item_list));

  app->text_input = text_input_alloc();
  view_dispatcher_add_view(app->view_dispatcher, TimedRemoteViewTextInput,
                           text_input_get_view(app->text_input));

  app->widget = widget_alloc();
  view_dispatcher_add_view(app->view_dispatcher, TimedRemoteViewWidget,
                           widget_get_view(app->widget));

  app->popup = popup_alloc();
  view_dispatcher_add_view(app->view_dispatcher, TimedRemoteViewPopup,
                           popup_get_view(app->popup));

  return app;
}

void timed_remote_app_free(TimedRemoteApp *app) {
  /* Free timer if still running */
  if (app->timer) {
    furi_timer_stop(app->timer);
    furi_timer_free(app->timer);
  }

  /* Free IR signal if allocated */
  if (app->ir_signal) {
    infrared_signal_free(app->ir_signal);
  }

  /* Remove and free views */
  view_dispatcher_remove_view(app->view_dispatcher, TimedRemoteViewSubmenu);
  submenu_free(app->submenu);

  view_dispatcher_remove_view(app->view_dispatcher,
                              TimedRemoteViewVariableItemList);
  variable_item_list_free(app->variable_item_list);

  view_dispatcher_remove_view(app->view_dispatcher, TimedRemoteViewTextInput);
  text_input_free(app->text_input);

  view_dispatcher_remove_view(app->view_dispatcher, TimedRemoteViewWidget);
  widget_free(app->widget);

  view_dispatcher_remove_view(app->view_dispatcher, TimedRemoteViewPopup);
  popup_free(app->popup);

  /* Free scene manager */
  scene_manager_free(app->scene_manager);

  /* Free view dispatcher */
  view_dispatcher_free(app->view_dispatcher);

  /* Close GUI record */
  furi_record_close(RECORD_GUI);

  free(app);
}

int32_t timed_remote_app(void *p) {
  UNUSED(p);

  TimedRemoteApp *app = timed_remote_app_alloc();

  /* Start with file browser scene */
  scene_manager_next_scene(app->scene_manager, TimedRemoteSceneIrBrowse);

  /* Run event loop */
  view_dispatcher_run(app->view_dispatcher);

  /* Cleanup */
  timed_remote_app_free(app);

  return 0;
}
