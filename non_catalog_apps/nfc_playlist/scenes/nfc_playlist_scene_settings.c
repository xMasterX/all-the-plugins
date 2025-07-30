#include "../nfc_playlist.h"

typedef enum {
   NfcPlaylistSettings_Timeout,
   NfcPlaylistSettings_Delay,
   NfcPlaylistSettings_LedIndicator,
   NfcPlaylistSettings_SkipError,
   NfcPlaylistSettings_Loop,
   NfcPlaylistSettings_UserControls,
   NfcPlaylistSettings_Reset
} NfcPlaylistSettingsMenuSelection;

static void nfc_playlist_settings_menu_callback(void* context, uint32_t index) {
   furi_assert(context);
   NfcPlaylist* nfc_playlist = context;
   scene_manager_handle_custom_event(nfc_playlist->scene_manager, index);
}

static void nfc_playlist_settings_options_change_callback(VariableItem* item) {
   furi_assert(item);
   NfcPlaylist* nfc_playlist = variable_item_get_context(item);

   uint8_t current_option =
      variable_item_list_get_selected_item_index(nfc_playlist->views.variable_item_list);
   uint8_t option_value_index = variable_item_get_current_value_index(item);

   switch(current_option) {
   case NfcPlaylistSettings_Timeout: {
      nfc_playlist->worker_info.settings->emulate_timeout = option_value_index;
      FuriString* tmp_str = furi_string_alloc_printf(
         "%ds", options_emulate_timeout[nfc_playlist->worker_info.settings->emulate_timeout]);
      variable_item_set_current_value_text(item, furi_string_get_cstr(tmp_str));
      furi_string_free(tmp_str);
      break;
   }
   case NfcPlaylistSettings_Delay: {
      nfc_playlist->worker_info.settings->emulate_delay = option_value_index;
      FuriString* tmp_str = furi_string_alloc_printf(
         "%ds", options_emulate_delay[nfc_playlist->worker_info.settings->emulate_delay]);
      variable_item_set_current_value_text(item, furi_string_get_cstr(tmp_str));
      furi_string_free(tmp_str);
      break;
   }
   case NfcPlaylistSettings_LedIndicator:
      nfc_playlist->worker_info.settings->emulate_led_indicator = option_value_index;
      variable_item_set_current_value_text(
         item, nfc_playlist->worker_info.settings->emulate_led_indicator ? "ON" : "OFF");
      break;
   case NfcPlaylistSettings_SkipError:
      nfc_playlist->worker_info.settings->skip_error = option_value_index;
      variable_item_set_current_value_text(
         item, nfc_playlist->worker_info.settings->skip_error ? "ON" : "OFF");
      break;
   case NfcPlaylistSettings_Loop:
      nfc_playlist->worker_info.settings->loop = option_value_index;
      variable_item_set_current_value_text(
         item, nfc_playlist->worker_info.settings->loop ? "ON" : "OFF");
      break;
   case NfcPlaylistSettings_UserControls:
      nfc_playlist->worker_info.settings->user_controls = option_value_index;
      variable_item_set_current_value_text(
         item, nfc_playlist->worker_info.settings->user_controls ? "ON" : "OFF");
      break;
   default:
      break;
   }
}

void nfc_playlist_settings_scene_on_enter(void* context) {
   NfcPlaylist* nfc_playlist = context;
   FuriString* tmp_str = furi_string_alloc();

    //variable_item_list_set_header(nfc_playlist->views.variable_item_list, "Settings");

   VariableItem* emulation_timeout_setting = variable_item_list_add(
      nfc_playlist->views.variable_item_list,
      "Emulate time",
      (sizeof(options_emulate_timeout) / sizeof(options_emulate_timeout[0])),
      nfc_playlist_settings_options_change_callback,
      nfc_playlist);
   variable_item_set_current_value_index(
      emulation_timeout_setting, nfc_playlist->worker_info.settings->emulate_timeout);
   furi_string_printf(
      tmp_str,
      "%ds",
      options_emulate_timeout[nfc_playlist->worker_info.settings->emulate_timeout]);
   variable_item_set_current_value_text(emulation_timeout_setting, furi_string_get_cstr(tmp_str));

   VariableItem* emulation_delay_setting = variable_item_list_add(
      nfc_playlist->views.variable_item_list,
      "Delay time",
      (sizeof(options_emulate_delay) / sizeof(options_emulate_delay[0])),
      nfc_playlist_settings_options_change_callback,
      nfc_playlist);
   variable_item_set_current_value_index(
      emulation_delay_setting, nfc_playlist->worker_info.settings->emulate_delay);
   furi_string_printf(
      tmp_str, "%ds", options_emulate_delay[nfc_playlist->worker_info.settings->emulate_delay]);
   variable_item_set_current_value_text(emulation_delay_setting, furi_string_get_cstr(tmp_str));

   furi_string_free(tmp_str);

   VariableItem* emulation_led_indicator_setting = variable_item_list_add(
      nfc_playlist->views.variable_item_list,
      "LED Indicator",
      2,
      nfc_playlist_settings_options_change_callback,
      nfc_playlist);
   variable_item_set_current_value_index(
      emulation_led_indicator_setting, nfc_playlist->worker_info.settings->emulate_led_indicator);
   variable_item_set_current_value_text(
      emulation_led_indicator_setting,
      nfc_playlist->worker_info.settings->emulate_led_indicator ? "ON" : "OFF");

   VariableItem* emulation_skip_error_setting = variable_item_list_add(
      nfc_playlist->views.variable_item_list,
      "Skip Error",
      2,
      nfc_playlist_settings_options_change_callback,
      nfc_playlist);
   variable_item_set_current_value_index(
      emulation_skip_error_setting, nfc_playlist->worker_info.settings->skip_error);
   variable_item_set_current_value_text(
      emulation_skip_error_setting, nfc_playlist->worker_info.settings->skip_error ? "ON" : "OFF");

   VariableItem* loop_setting = variable_item_list_add(
      nfc_playlist->views.variable_item_list,
      "Loop",
      2,
      nfc_playlist_settings_options_change_callback,
      nfc_playlist);
   variable_item_set_current_value_index(loop_setting, nfc_playlist->worker_info.settings->loop);
   variable_item_set_current_value_text(
      loop_setting, nfc_playlist->worker_info.settings->loop ? "ON" : "OFF");

   VariableItem* user_controls_setting = variable_item_list_add(
      nfc_playlist->views.variable_item_list,
      "User Controls",
      2,
      nfc_playlist_settings_options_change_callback,
      nfc_playlist);
   variable_item_set_current_value_index(
      user_controls_setting, nfc_playlist->worker_info.settings->user_controls);
   variable_item_set_current_value_text(
      user_controls_setting, nfc_playlist->worker_info.settings->user_controls ? "ON" : "OFF");

   variable_item_list_add(nfc_playlist->views.variable_item_list, "Reset settings", 0, NULL, NULL);

   VariableItem* credits = variable_item_list_add(
      nfc_playlist->views.variable_item_list, "acegoal07, xtruan, WillyJL", 1, NULL, NULL);
   variable_item_set_current_value_text(credits, "Credits");

   variable_item_list_set_enter_callback(
      nfc_playlist->views.variable_item_list, nfc_playlist_settings_menu_callback, nfc_playlist);

   view_dispatcher_switch_to_view(nfc_playlist->view_dispatcher, NfcPlaylistView_VariableItemList);
}

bool nfc_playlist_settings_scene_on_event(void* context, SceneManagerEvent event) {
   furi_assert(context);
   NfcPlaylist* nfc_playlist = context;
   bool consumed = false;
   if(event.type == SceneManagerEventTypeCustom) {
      switch(event.event) {
      case NfcPlaylistSettings_Reset: {
         FuriString* tmp_str = furi_string_alloc();

         nfc_playlist->worker_info.settings->emulate_timeout = default_emulate_timeout;
         VariableItem* emulation_timeout_setting = variable_item_list_get(
            nfc_playlist->views.variable_item_list, NfcPlaylistSettings_Timeout);
         variable_item_set_current_value_index(
            emulation_timeout_setting, nfc_playlist->worker_info.settings->emulate_timeout);
         furi_string_printf(
            tmp_str,
            "%ds",
            options_emulate_timeout[nfc_playlist->worker_info.settings->emulate_timeout]);
         variable_item_set_current_value_text(
            emulation_timeout_setting, furi_string_get_cstr(tmp_str));

         nfc_playlist->worker_info.settings->emulate_delay = default_emulate_delay;
         VariableItem* emulation_delay_setting = variable_item_list_get(
            nfc_playlist->views.variable_item_list, NfcPlaylistSettings_Delay);
         variable_item_set_current_value_index(
            emulation_delay_setting, nfc_playlist->worker_info.settings->emulate_delay);
         furi_string_printf(
            tmp_str,
            "%ds",
            options_emulate_delay[nfc_playlist->worker_info.settings->emulate_delay]);
         variable_item_set_current_value_text(
            emulation_delay_setting, furi_string_get_cstr(tmp_str));

         furi_string_free(tmp_str);

         nfc_playlist->worker_info.settings->emulate_led_indicator = default_emulate_led_indicator;
         VariableItem* emulation_led_indicator_setting = variable_item_list_get(
            nfc_playlist->views.variable_item_list, NfcPlaylistSettings_LedIndicator);
         variable_item_set_current_value_index(
            emulation_led_indicator_setting,
            nfc_playlist->worker_info.settings->emulate_led_indicator);
         variable_item_set_current_value_text(
            emulation_led_indicator_setting,
            nfc_playlist->worker_info.settings->emulate_led_indicator ? "ON" : "OFF");

         nfc_playlist->worker_info.settings->skip_error = default_skip_error;
         VariableItem* emulation_skip_error_setting = variable_item_list_get(
            nfc_playlist->views.variable_item_list, NfcPlaylistSettings_SkipError);
         variable_item_set_current_value_index(
            emulation_skip_error_setting, nfc_playlist->worker_info.settings->skip_error);
         variable_item_set_current_value_text(
            emulation_skip_error_setting,
            nfc_playlist->worker_info.settings->skip_error ? "ON" : "OFF");

         nfc_playlist->worker_info.settings->loop = default_loop;
         VariableItem* loop_setting = variable_item_list_get(
            nfc_playlist->views.variable_item_list, NfcPlaylistSettings_Loop);
         variable_item_set_current_value_index(
            loop_setting, nfc_playlist->worker_info.settings->loop);
         variable_item_set_current_value_text(
            loop_setting, nfc_playlist->worker_info.settings->loop ? "ON" : "OFF");

         nfc_playlist->worker_info.settings->user_controls = default_user_controls;
         VariableItem* user_controls_setting = variable_item_list_get(
            nfc_playlist->views.variable_item_list, NfcPlaylistSettings_UserControls);
         variable_item_set_current_value_index(
            user_controls_setting, nfc_playlist->worker_info.settings->user_controls);
         variable_item_set_current_value_text(
            user_controls_setting,
            nfc_playlist->worker_info.settings->user_controls ? "ON" : "OFF");

         consumed = true;
         break;
      }
      default:
         break;
      }
   }
   return consumed;
}

void nfc_playlist_settings_scene_on_exit(void* context) {
   furi_assert(context);
   NfcPlaylist* nfc_playlist = context;
   variable_item_list_reset(nfc_playlist->views.variable_item_list);
}
