#include "../passy_i.h"
#include "../passy_reader.h"
#include <dolphin/dolphin.h>

#define TAG "PassySceneRead"

static PassyReader* passy_reader = NULL;

void passy_scene_read_on_enter(void* context) {
    Passy* passy = context;
    dolphin_deed(DolphinDeedNfcRead);

    // Setup view
    Popup* popup = passy->popup;
    popup_set_header(popup, "Reading", 68, 30, AlignLeft, AlignTop);
    popup_set_icon(popup, 0, 3, &I_RFIDDolphinReceive_97x61);

    passy->poller = nfc_poller_alloc(passy->nfc, NfcProtocolIso14443_4b);
    passy_reader = passy_reader_alloc(passy);
    nfc_poller_start(passy->poller, passy_reader_poller_callback, passy_reader);
    passy->bytes_total = 0;

    passy_blink_start(passy);

    view_dispatcher_switch_to_view(passy->view_dispatcher, PassyViewPopup);
}

bool passy_scene_read_on_event(void* context, SceneManagerEvent event) {
    Passy* passy = context;
    bool consumed = false;
    Popup* popup = passy->popup;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == PassyCustomEventReaderSuccess) {
            if(passy->read_type == PassyReadCOM) {
                scene_manager_next_scene(passy->scene_manager, PassySceneAdvancedMenu);
            } else {
                scene_manager_next_scene(passy->scene_manager, PassySceneReadSuccess);
            }
            consumed = true;
        } else if(event.event == PassyCustomEventReaderError) {
            passy->last_sw = passy_reader->last_sw;
            scene_manager_next_scene(passy->scene_manager, PassySceneReadError);
            consumed = true;
        } else if(event.event == PassyCustomEventReaderDetected) {
            popup_set_header(popup, "Detected", 68, 30, AlignLeft, AlignTop);
        } else if(event.event == PassyCustomEventReaderAuthenticated) {
            popup_set_header(popup, "Authenticated", 68, 30, AlignLeft, AlignTop);
        } else if(event.event == PassyCustomEventReaderReading) {
            if(passy->bytes_total == 0) {
                popup_set_header(popup, "Reading", 68, 30, AlignLeft, AlignTop);
            } else {
                // Update the header with the current bytes read
                char header[32];
                snprintf(
                    header,
                    sizeof(header),
                    "Reading\n%d/%dk",
                    passy->offset,
                    (passy->bytes_total / 1024));
                popup_set_header(popup, header, 68, 30, AlignLeft, AlignTop);
            }
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        scene_manager_search_and_switch_to_previous_scene(
            passy->scene_manager, PassySceneMainMenu);
        consumed = true;
    }

    return consumed;
}

void passy_scene_read_on_exit(void* context) {
    Passy* passy = context;

    if(passy_reader) {
        passy_reader_free(passy_reader);
        passy_reader = NULL;
    }
    if(passy->poller) {
        nfc_poller_stop(passy->poller);
        nfc_poller_free(passy->poller);
        passy->poller = NULL;
    }

    // Clear view
    popup_reset(passy->popup);

    passy_blink_stop(passy);
}
