#include "../metroflip_i.h"
#include <dolphin/dolphin.h>
#include <furi.h>
#include <bit_lib.h>
#include <lib/nfc/protocols/nfc_protocol.h>
#include "../api/metroflip/metroflip_api.h"
#include "../api/suica/suica_loading.h"
#define TAG "Metroflip:Scene:Load"
#include "keys.h"
#include <nfc/protocols/mf_classic/mf_classic.h>

void metroflip_scene_load_on_enter(void* context) {
    Metroflip* app = (Metroflip*)context;
    // We initialized this to be false every time we enter
    app->data_loaded = false;
    bool has_card_type = false;
    // The same string we will use to direct parse scene which plugin to call
    // Extracted from the file
    FuriString* card_type_str = furi_string_alloc();
    FuriString* device_type = furi_string_alloc();

    // All the app_data browser stuff. Don't worry about this
    DialogsFileBrowserOptions browser_options;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, STORAGE_APP_DATA_PATH_PREFIX);
    dialog_file_browser_set_basic_options(&browser_options, METROFLIP_FILE_EXTENSION, &I_icon);
    browser_options.base_path = STORAGE_APP_DATA_PATH_PREFIX;
    FuriString* file_path = furi_string_alloc_set(browser_options.base_path);

    if(dialog_file_browser_show(app->dialogs, file_path, file_path, &browser_options)) {
        FlipperFormat* format = flipper_format_file_alloc(storage);
        do {
            if(!flipper_format_file_open_existing(format, furi_string_get_cstr(file_path))) break;
            if(!flipper_format_read_string(format, "Device type", device_type)) break;
            const char* protocol_name = furi_string_get_cstr(device_type);
            if(!flipper_format_read_string(format, "Card Type", card_type_str)) {
                flipper_format_file_close(format);
                flipper_format_file_open_existing(format, furi_string_get_cstr(file_path));

                if(strcmp(protocol_name, "Mifare Classic") == 0) {
                    MfClassicData* mfc_data = mf_classic_alloc();
                    if(!mf_classic_load(mfc_data, format, 2)) break;
                    app->data_loaded = true;
                    CardType card_type = determine_card_type(app->nfc, mfc_data, app->data_loaded);
                    app->mfc_card_type = card_type;
                    has_card_type = true;
                    switch(card_type) {
                    case CARD_TYPE_METROMONEY:
                        app->card_type = "metromoney";
                        FURI_LOG_I(TAG, "Detected: Metromoney\n");
                        break;
                    case CARD_TYPE_CHARLIECARD:
                        app->card_type = "charliecard";
                        FURI_LOG_I(TAG, "Detected: CharlieCard\n");
                        break;
                    case CARD_TYPE_SMARTRIDER:
                        app->card_type = "smartrider";
                        FURI_LOG_I(TAG, "Detected: SmartRider\n");
                        break;
                    case CARD_TYPE_TROIKA:
                        app->card_type = "troika";
                        FURI_LOG_I(TAG, "Detected: Troika\n");
                        break;
                    case CARD_TYPE_GOCARD:
                        app->card_type = "gocard";
                        FURI_LOG_I(TAG, "Detected: go card\n");
                        break;
                    case CARD_TYPE_UNKNOWN:
                        app->card_type = "unknown";
                        //popup_set_header(popup, "Unsupported\n card", 58, 31, AlignLeft, AlignTop);
                        break;
                    default:
                        app->card_type = "unknown";
                        FURI_LOG_I(TAG, "Detected: Unknown card type\n");
                        //popup_set_header(popup, "Unsupported\n card", 58, 31, AlignLeft, AlignTop);
                        break;
                    }
                    mf_classic_free(mfc_data);
                } else if(strcmp(protocol_name, "Mifare DESFire") == 0) {
                    MfDesfireData* data = mf_desfire_alloc();
                    if(!mf_desfire_load(data, format, 2)) break;
                    app->data_loaded = true;
                    if(clipper_verify(data)) {
                        app->card_type = "clipper";
                        FURI_LOG_I(TAG, "Detected: Clipper");
                    } else if(itso_verify(data)) {
                        app->card_type = "itso";
                        FURI_LOG_I(TAG, "Detected: ITSO");
                    } else if(myki_verify(data)) {
                        app->card_type = "myki";
                        FURI_LOG_I(TAG, "Detected: Myki");
                    } else if(opal_verify(data)) {
                        app->card_type = "opal";
                        FURI_LOG_I(TAG, "Detected: Opal");
                    } else {
                        app->card_type = "unknown";
                        FURI_LOG_I(TAG, "Detected: none");
                    }
                    mf_desfire_free(data);
                    has_card_type = true;
                } else {
                    has_card_type = true;
                }
                flipper_format_file_close(format);
            } else {
                has_card_type = false;
                if(furi_string_equal_str(card_type_str, "suica")) {
                    FURI_LOG_I(TAG, "Detected: Suica");
                    app->data_loaded = true;
                    app->card_type = "suica";
                    load_suica_data(app, format);
                }
            }
            app->file_path = furi_string_get_cstr(file_path);
            strncpy(
                app->delete_file_path,
                furi_string_get_cstr(file_path),
                sizeof(app->delete_file_path) - 1);
            app->delete_file_path[sizeof(app->delete_file_path) - 1] = '\0';

            app->data_loaded = true;
        } while(0);
        flipper_format_free(format);
    }

    if(app->data_loaded) {
        // Direct to the parsing screen just like the auto scene does
        if(!has_card_type) {
            app->card_type = furi_string_get_cstr(card_type_str);
            has_card_type = false;
        }
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, MetroflipSceneStart);
        scene_manager_next_scene(app->scene_manager, MetroflipSceneParse);
    } else {
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, MetroflipSceneStart);
    }
    furi_string_free(file_path);
    furi_string_free(card_type_str);
    furi_record_close(RECORD_STORAGE);
}

bool metroflip_scene_load_on_event(void* context, SceneManagerEvent event) {
    Metroflip* app = context;
    UNUSED(event);
    bool consumed = false;
    // If they don't select any file in the brwoser and press back button,
    // the data is not loaded
    if(!app->data_loaded) {
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, MetroflipSceneStart);
    }
    consumed = true;

    return consumed;
}

void metroflip_scene_load_on_exit(void* context) {
    Metroflip* app = context;
    UNUSED(app);
}
