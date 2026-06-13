#include "../nfc_magic_app_i.h"

void nfc_magic_scene_uscuid_ul_partial_widget_callback(
    GuiButtonType result,
    InputType type,
    void* context) {
    NfcMagicApp* instance = context;
    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(instance->view_dispatcher, result);
    }
}

void nfc_magic_scene_uscuid_ul_partial_on_enter(void* context) {
    NfcMagicApp* instance = context;
    Widget* widget = instance->widget;

    // Partial = data + UID wrote, some pages skipped -> still a success.
    notification_message(instance->notifications, &sequence_success);

    widget_add_string_element(
        widget, 64, 0, AlignCenter, AlignTop, FontPrimary, "Done, with skips");

    FuriString* message = furi_string_alloc();
    furi_string_printf(
        message,
        "Wrote %u/%u pages.\nNot written:",
        instance->write_progress_current,
        instance->write_progress_total);

    const uint8_t shown = (instance->write_failed_count < USCUID_UL_MAX_FAILED_PAGES) ?
                              instance->write_failed_count :
                              USCUID_UL_MAX_FAILED_PAGES;
    for(uint8_t i = 0; i < shown; i++) {
        furi_string_cat_printf(message, " %u", instance->write_failed_pages[i]);
    }
    if(instance->write_failed_count > shown) {
        furi_string_cat_str(message, " ...");
    }

    widget_add_text_box_element(
        widget, 0, 16, 128, 46, AlignLeft, AlignTop, furi_string_get_cstr(message), false);
    furi_string_free(message);

    widget_add_button_element(
        widget,
        GuiButtonTypeLeft,
        "Finish",
        nfc_magic_scene_uscuid_ul_partial_widget_callback,
        instance);

    view_dispatcher_switch_to_view(instance->view_dispatcher, NfcMagicAppViewWidget);
}

bool nfc_magic_scene_uscuid_ul_partial_on_event(void* context, SceneManagerEvent event) {
    NfcMagicApp* instance = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == GuiButtonTypeLeft) {
            consumed = scene_manager_search_and_switch_to_previous_scene(
                instance->scene_manager, NfcMagicSceneStart);
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        consumed = scene_manager_search_and_switch_to_previous_scene(
            instance->scene_manager, NfcMagicSceneStart);
    }
    return consumed;
}

void nfc_magic_scene_uscuid_ul_partial_on_exit(void* context) {
    NfcMagicApp* instance = context;
    widget_reset(instance->widget);
}
