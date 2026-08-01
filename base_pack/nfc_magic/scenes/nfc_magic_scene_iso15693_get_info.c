#include "../nfc_magic_app_i.h"
#include "../magic/protocols/iso15693/iso15693_poller.h"

static void nfc_magic_iso15693_get_info_popup_callback(void* context) {
    NfcMagicApp* instance = context;
    view_dispatcher_send_custom_event(instance->view_dispatcher, NfcMagicAppCustomEventViewExit);
}

// Info mode emits only Success or CardLost. The remaining values are matched explicitly rather than
// swept into a trailing else, so adding an event can't silently arrive here as a failure.
static void nfc_magic_iso15693_get_info_poller_callback(Iso15693PollerEvent event, void* context) {
    NfcMagicApp* instance = context;

    switch(event) {
    case Iso15693PollerEventSuccess:
        view_dispatcher_send_custom_event(
            instance->view_dispatcher, NfcMagicCustomEventIso15693CardDetected);
        break;
    case Iso15693PollerEventCardLost:
        // Bounded activation retries ran out: no card in the field, or it never answered.
        view_dispatcher_send_custom_event(
            instance->view_dispatcher, NfcMagicCustomEventIso15693CardDetectFailed);
        break;
    case Iso15693PollerEventPartial:
    case Iso15693PollerEventFail:
    case Iso15693PollerEventCardDetected:
    case Iso15693PollerEventWriteProgress:
        break; // not emitted in Info mode
    }
}

void nfc_magic_scene_iso15693_get_info_on_enter(void* context) {
    NfcMagicApp* app = context;
    Popup* popup = app->popup;

    // Setup the popup view to instruct the user
    popup_set_header(popup, "Detecting ISO15693", 68, 19, AlignCenter, AlignBottom);
    popup_set_text(popup, "Approach card to the back of Flipper", 68, 21, AlignCenter, AlignTop);
    popup_set_icon(popup, 0, 8, &I_NFC_manual_60x50);

    view_dispatcher_switch_to_view(app->view_dispatcher, NfcMagicAppViewPopup);

    // Allocate the poller here (not at app startup) so it doesn't hold the shared Nfc's
    // config across the scanner's run. Freed in on_exit.
    app->iso15693_poller = iso15693_poller_alloc(app->nfc);
    iso15693_poller_start(app->iso15693_poller, nfc_magic_iso15693_get_info_poller_callback, app);
    nfc_magic_app_blink_start(app);
}

bool nfc_magic_scene_iso15693_get_info_on_event(void* context, SceneManagerEvent event) {
    NfcMagicApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == NfcMagicCustomEventIso15693CardDetected) {
            // Keep the read result in the app so the info scene still has it after the
            // poller is freed in on_exit.
            iso15693_3_copy(app->iso15693_data, iso15693_poller_get_data(app->iso15693_poller));
            scene_manager_next_scene(app->scene_manager, NfcMagicSceneIso15693Info);
            consumed = true;
        } else if(event.event == NfcMagicCustomEventIso15693CardDetectFailed) {
            // Say why the popup is going away. Dropping straight back to the menu left the user with
            // no way to tell "nothing was on the antenna" from "the card was rejected", so they just
            // re-tapped with nothing to change.
            Popup* popup = app->popup;
            nfc_magic_app_blink_stop(app);
            notification_message(app->notifications, &sequence_error);
            popup_reset(popup);
            popup_set_header(popup, "No card detected", 64, 8, AlignCenter, AlignTop);
            popup_set_text(
                popup,
                "Hold the card flat\nagainst the back\nof the Flipper",
                64,
                24,
                AlignCenter,
                AlignTop);
            popup_set_timeout(popup, 2000);
            popup_set_context(popup, app);
            popup_set_callback(popup, nfc_magic_iso15693_get_info_popup_callback);
            popup_enable_timeout(popup);
            consumed = true;
        } else if(event.event == NfcMagicAppCustomEventViewExit) {
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, NfcMagicSceneIso15693);
            consumed = true;
        }
    }

    return consumed;
}

void nfc_magic_scene_iso15693_get_info_on_exit(void* context) {
    NfcMagicApp* app = context;

    iso15693_poller_stop(app->iso15693_poller);
    iso15693_poller_free(app->iso15693_poller);
    app->iso15693_poller = NULL;
    nfc_magic_app_blink_stop(app);

    // Reset the popup to a clean state for the next view
    popup_reset(app->popup);
}
