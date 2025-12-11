#include "../nfc_comparator.h"

static void
   nfc_comparator_finder_results_callback(GuiButtonType result, InputType type, void* context) {
   furi_assert(context);
   NfcComparator* nfc_comparator = context;
   if(type == InputTypeShort) {
      view_dispatcher_send_custom_event(nfc_comparator->view_dispatcher, result);
   }
}

void nfc_comparator_finder_results_scene_on_enter(void* context) {
   furi_assert(context);
   NfcComparator* nfc_comparator = context;

   nfc_comparator_led_worker_start(
      nfc_comparator->notification_app, NfcComparatorLedState_Complete);

   FuriString* temp_str = furi_string_alloc();

   bool match = false;
   switch(nfc_comparator->workers.compare_checks->type) {
   case NfcCompareChecksType_Digital:
      match = nfc_comparator->workers.compare_checks->uid &&
              nfc_comparator->workers.compare_checks->uid_length &&
              nfc_comparator->workers.compare_checks->protocol &&
              nfc_comparator->workers.compare_checks->nfc_data;
      break;
   case NfcCompareChecksType_Physical:
      match = nfc_comparator->workers.compare_checks->uid &&
              nfc_comparator->workers.compare_checks->uid_length &&
              nfc_comparator->workers.compare_checks->protocol;
      break;
   default:
      furi_string_printf(temp_str, "Unknown comparison type.");
      break;
   }
   if(match) {
      furi_string_printf(
         temp_str,
         "\e#Match found!\e#\n %s",
         furi_string_get_cstr(nfc_comparator->workers.compare_checks->nfc_card_path));
   } else {
      furi_string_printf(temp_str, "\e#No match found!\e#");
   }

   widget_add_text_box_element(
      nfc_comparator->views.widget,
      0,
      0,
      128,
      57,
      AlignCenter,
      AlignCenter,
      furi_string_get_cstr(temp_str),
      false);
   widget_add_button_element(
      nfc_comparator->views.widget,
      GuiButtonTypeLeft,
      "Again",
      nfc_comparator_finder_results_callback,
      nfc_comparator);
   widget_add_button_element(
      nfc_comparator->views.widget,
      GuiButtonTypeRight,
      "Exit",
      nfc_comparator_finder_results_callback,
      nfc_comparator);

   furi_string_free(temp_str);

   view_dispatcher_switch_to_view(nfc_comparator->view_dispatcher, NfcComparatorView_Widget);
}

bool nfc_comparator_finder_results_scene_on_event(void* context, SceneManagerEvent event) {
   NfcComparator* nfc_comparator = context;
   bool consumed = false;
   if(event.type == SceneManagerEventTypeCustom) {
      switch(event.event) {
      case GuiButtonTypeLeft:
         scene_manager_previous_scene(nfc_comparator->scene_manager);
         consumed = true;
         break;
      case GuiButtonTypeRight:
         scene_manager_search_and_switch_to_previous_scene(
            nfc_comparator->scene_manager, NfcComparatorScene_FinderMenu);
         consumed = true;
         break;
      default:
         break;
      }
   } else if(event.type == SceneManagerEventTypeBack) {
      scene_manager_search_and_switch_to_previous_scene(
         nfc_comparator->scene_manager, NfcComparatorScene_FinderMenu);
      consumed = true;
   }
   return consumed;
}

void nfc_comparator_finder_results_scene_on_exit(void* context) {
   furi_assert(context);
   NfcComparator* nfc_comparator = context;
   widget_reset(nfc_comparator->views.widget);
   nfc_comparator_led_worker_stop(nfc_comparator->notification_app);
   nfc_comparator_compare_checks_reset(nfc_comparator->workers.compare_checks);
}
