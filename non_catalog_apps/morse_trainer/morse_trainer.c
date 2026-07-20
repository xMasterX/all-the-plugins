#include "morse_trainer_i.h"
#include "scenes/morse_scene.h"

static bool morse_custom_event_callback(void* context, uint32_t event) {
    MorseTrainerApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool morse_navigation_event_callback(void* context) {
    MorseTrainerApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

/* Timers fire on the timer thread; they only post events into the dispatcher
 * queue, so all game-state mutation stays on the dispatcher thread. */
static void commit_timer_callback(void* context) {
    MorseTrainerApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, MTEventCommit);
}

static void feedback_timer_callback(void* context) {
    MorseTrainerApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, MTEventFeedbackDone);
}

static MorseTrainerApp* morse_trainer_app_alloc(void) {
    MorseTrainerApp* app = malloc(sizeof(MorseTrainerApp));

    morse_progress_load(&app->progress);
    morse_settings_load(&app->settings);

    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->player = morse_player_alloc(app->notifications, &app->settings);

    app->gui = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&morse_scene_handlers, app);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, morse_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, morse_navigation_event_callback);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, MorseViewSubmenu, submenu_get_view(app->submenu));
    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, MorseViewWidget, widget_get_view(app->widget));
    app->var_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        MorseViewVarItemList,
        variable_item_list_get_view(app->var_item_list));
    app->trainer_view =
        trainer_view_alloc(app->view_dispatcher, &app->settings, app->notifications);
    view_dispatcher_add_view(
        app->view_dispatcher, MorseViewTrainer, trainer_view_get_view(app->trainer_view));

    app->commit_timer = furi_timer_alloc(commit_timer_callback, FuriTimerTypeOnce, app);
    app->feedback_timer = furi_timer_alloc(feedback_timer_callback, FuriTimerTypeOnce, app);

    return app;
}

static void morse_trainer_app_free(MorseTrainerApp* app) {
    /* Free in reverse order of allocation: timers, views (removed before
     * their modules are freed), dispatcher/manager, records, player. */
    furi_timer_stop(app->commit_timer);
    furi_timer_stop(app->feedback_timer);
    furi_timer_free(app->commit_timer);
    furi_timer_free(app->feedback_timer);

    view_dispatcher_remove_view(app->view_dispatcher, MorseViewTrainer);
    trainer_view_free(app->trainer_view);
    view_dispatcher_remove_view(app->view_dispatcher, MorseViewVarItemList);
    variable_item_list_free(app->var_item_list);
    view_dispatcher_remove_view(app->view_dispatcher, MorseViewWidget);
    widget_free(app->widget);
    view_dispatcher_remove_view(app->view_dispatcher, MorseViewSubmenu);
    submenu_free(app->submenu);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);

    morse_player_free(app->player);
    furi_record_close(RECORD_NOTIFICATION);

    free(app);
}

int32_t morse_trainer_app(void* p) {
    UNUSED(p);
    MorseTrainerApp* app = morse_trainer_app_alloc();
    scene_manager_next_scene(app->scene_manager, MorseSceneMenu);
    view_dispatcher_run(app->view_dispatcher);
    morse_trainer_app_free(app);
    return 0;
}
