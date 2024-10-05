#include "../nfc_eink_app_i.h"

void nfc_eink_scene_error_popup_callback(void* context) {
    NfcEinkApp* nfc = context;
    view_dispatcher_send_custom_event(nfc->view_dispatcher, NfcEinkAppCustomEventTimerExpired);
}

void nfc_eink_scene_error_on_enter(void* context) {
    NfcEinkApp* instance = context;

    Popup* popup = instance->popup;
    popup_set_icon(popup, 10, 14, &I_WarningDolphin_45x42);
    popup_set_header(popup, "Error", 90, 26, AlignCenter, AlignCenter);
    ///TODO: Text error should be determined from some error variable or from EinkScreen event
    popup_set_text(popup, "Target Lost", 85, 40, AlignCenter, AlignCenter);

    popup_set_timeout(popup, 5000);
    popup_set_context(popup, instance);
    popup_set_callback(popup, nfc_eink_scene_error_popup_callback);
    popup_enable_timeout(popup);
    view_dispatcher_switch_to_view(instance->view_dispatcher, NfcEinkViewPopup);
}

bool nfc_eink_scene_error_on_event(void* context, SceneManagerEvent event) {
    NfcEinkApp* instance = context;

    bool consumed = false;
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == NfcEinkAppCustomEventTimerExpired) {
            consumed = scene_manager_search_and_switch_to_previous_scene(
                instance->scene_manager, NfcEinkAppSceneStart);
        }
    }

    return consumed;
}

void nfc_eink_scene_error_on_exit(void* context) {
    NfcEinkApp* nfc = context;
    // Clear view
    popup_reset(nfc->popup);
}
