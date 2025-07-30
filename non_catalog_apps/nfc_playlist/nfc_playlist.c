#include "nfc_playlist.h"

static bool nfc_playlist_custom_callback(void* context, uint32_t custom_event) {
   furi_assert(context);
   NfcPlaylist* nfc_playlist = context;
   return scene_manager_handle_custom_event(nfc_playlist->scene_manager, custom_event);
}

static bool nfc_playlist_back_event_callback(void* context) {
   furi_assert(context);
   NfcPlaylist* nfc_playlist = context;
   return scene_manager_handle_back_event(nfc_playlist->scene_manager);
}

static void nfc_playlist_tick_event_callback(void* context) {
   furi_assert(context);
   NfcPlaylist* nfc_playlist = context;
   scene_manager_handle_tick_event(nfc_playlist->scene_manager);
}

static NfcPlaylist* nfc_playlist_alloc() {
   NfcPlaylist* nfc_playlist = malloc(sizeof(NfcPlaylist));
   furi_assert(nfc_playlist);

   nfc_playlist->scene_manager = scene_manager_alloc(&nfc_playlist_scene_handlers, nfc_playlist);
   nfc_playlist->view_dispatcher = view_dispatcher_alloc();

   nfc_playlist->views.submenu = submenu_alloc();
   nfc_playlist->views.widget = widget_alloc();
   nfc_playlist->views.file_browser.output = furi_string_alloc();
   nfc_playlist->views.file_browser.view =
      file_browser_alloc(nfc_playlist->views.file_browser.output);
   nfc_playlist->views.variable_item_list = variable_item_list_alloc();
   nfc_playlist->views.text_input.view = text_input_alloc();
   nfc_playlist->views.dialog = dialog_ex_alloc();

   nfc_playlist->notification_app = furi_record_open(RECORD_NOTIFICATION);

   Storage* storage = furi_record_open(RECORD_STORAGE);
   storage_simply_mkdir(storage, PLAYLIST_DIR);
   furi_record_close(RECORD_STORAGE);

   nfc_playlist->worker_info.settings = malloc(sizeof(NfcPlaylistWorkerSettings));
   furi_assert(nfc_playlist->worker_info.settings);

   nfc_playlist->worker_info.settings->playlist_path = furi_string_alloc();
   nfc_playlist->worker_info.settings->emulate_timeout = default_emulate_timeout;
   nfc_playlist->worker_info.settings->emulate_delay = default_emulate_delay;
   nfc_playlist->worker_info.settings->emulate_led_indicator = default_emulate_led_indicator;
   nfc_playlist->worker_info.settings->skip_error = default_skip_error;
   nfc_playlist->worker_info.settings->loop = default_loop;
   nfc_playlist->worker_info.settings->user_controls = default_user_controls;

   view_dispatcher_set_event_callback_context(nfc_playlist->view_dispatcher, nfc_playlist);
   view_dispatcher_set_custom_event_callback(
      nfc_playlist->view_dispatcher, nfc_playlist_custom_callback);
   view_dispatcher_set_navigation_event_callback(
      nfc_playlist->view_dispatcher, nfc_playlist_back_event_callback);
   view_dispatcher_set_tick_event_callback(
      nfc_playlist->view_dispatcher, nfc_playlist_tick_event_callback, 100);

   view_dispatcher_add_view(
      nfc_playlist->view_dispatcher,
      NfcPlaylistView_Submenu,
      submenu_get_view(nfc_playlist->views.submenu));
   view_dispatcher_add_view(
      nfc_playlist->view_dispatcher,
      NfcPlaylistView_Widget,
      widget_get_view(nfc_playlist->views.widget));
   view_dispatcher_add_view(
      nfc_playlist->view_dispatcher,
      NfcPlaylistView_FileBrowser,
      file_browser_get_view(nfc_playlist->views.file_browser.view));
   view_dispatcher_add_view(
      nfc_playlist->view_dispatcher,
      NfcPlaylistView_VariableItemList,
      variable_item_list_get_view(nfc_playlist->views.variable_item_list));
   view_dispatcher_add_view(
      nfc_playlist->view_dispatcher,
      NfcPlaylistView_TextInput,
      text_input_get_view(nfc_playlist->views.text_input.view));
   view_dispatcher_add_view(
      nfc_playlist->view_dispatcher,
      NfcPlaylistView_Dialog,
      dialog_ex_get_view(nfc_playlist->views.dialog));

   return nfc_playlist;
}

static void nfc_playlist_free(NfcPlaylist* nfc_playlist) {
   furi_assert(nfc_playlist);

   view_dispatcher_remove_view(nfc_playlist->view_dispatcher, NfcPlaylistView_Submenu);
   view_dispatcher_remove_view(nfc_playlist->view_dispatcher, NfcPlaylistView_Widget);
   view_dispatcher_remove_view(nfc_playlist->view_dispatcher, NfcPlaylistView_VariableItemList);
   view_dispatcher_remove_view(nfc_playlist->view_dispatcher, NfcPlaylistView_FileBrowser);
   view_dispatcher_remove_view(nfc_playlist->view_dispatcher, NfcPlaylistView_TextInput);
   view_dispatcher_remove_view(nfc_playlist->view_dispatcher, NfcPlaylistView_Dialog);

   scene_manager_free(nfc_playlist->scene_manager);
   view_dispatcher_free(nfc_playlist->view_dispatcher);
   furi_record_close(RECORD_NOTIFICATION);

   submenu_free(nfc_playlist->views.submenu);
   widget_free(nfc_playlist->views.widget);
   file_browser_free(nfc_playlist->views.file_browser.view);
   variable_item_list_free(nfc_playlist->views.variable_item_list);
   text_input_free(nfc_playlist->views.text_input.view);
   dialog_ex_free(nfc_playlist->views.dialog);

   furi_string_free(nfc_playlist->worker_info.settings->playlist_path);
   free(nfc_playlist->worker_info.settings);

   furi_string_free(nfc_playlist->views.file_browser.output);
   free(nfc_playlist);
}

static inline void nfc_playlist_set_log_level() {
#ifdef FURI_DEBUG
   furi_log_set_level(FuriLogLevelTrace);
#else
   furi_log_set_level(FuriLogLevelInfo);
#endif
}

int32_t nfc_playlist_main(void* p) {
   UNUSED(p);

   NfcPlaylist* nfc_playlist = nfc_playlist_alloc();

   nfc_playlist_set_log_level();

   Gui* gui = furi_record_open(RECORD_GUI);
   view_dispatcher_attach_to_gui(nfc_playlist->view_dispatcher, gui, ViewDispatcherTypeFullscreen);
   scene_manager_next_scene(nfc_playlist->scene_manager, NfcPlaylistScene_MainMenu);
   view_dispatcher_run(nfc_playlist->view_dispatcher);

   furi_record_close(RECORD_GUI);
   nfc_playlist_free(nfc_playlist);

   return 0;
}
