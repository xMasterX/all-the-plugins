#include <flipper_application.h>
#include "../../metroflip_i.h"

#include <nfc/protocols/mf_classic/mf_classic_poller_sync.h>
#include <nfc/protocols/mf_classic/mf_classic.h>
#include <nfc/protocols/mf_classic/mf_classic_poller.h>
#include "../../api/metroflip/metroflip_api.h"

#include <dolphin/dolphin.h>
#include <bit_lib.h>
#include <furi_hal.h>
#include <nfc/nfc.h>
#include <nfc/nfc_device.h>
#include <nfc/nfc_listener.h>
#include "../../api/metroflip/metroflip_api.h"
#include "../../metroflip_plugins.h"

#define TAG "Metroflip:Scene:Troika"

const MfClassicKeyPair troika_1k_keys[16] = {
    {.a = 0xa0a1a2a3a4a5, .b = 0xfbf225dc5d58},
    {.a = 0xa82607b01c0d, .b = 0x2910989b6880},
    {.a = 0x2aa05ed1856f, .b = 0xeaac88e5dc99},
    {.a = 0x2aa05ed1856f, .b = 0xeaac88e5dc99},
    {.a = 0x73068f118c13, .b = 0x2b7f3253fac5},
    {.a = 0xfbc2793d540b, .b = 0xd3a297dc2698},
    {.a = 0x2aa05ed1856f, .b = 0xeaac88e5dc99},
    {.a = 0xae3d65a3dad4, .b = 0x0f1c63013dba},
    {.a = 0xa73f5dc1d333, .b = 0xe35173494a81},
    {.a = 0x69a32f1c2f19, .b = 0x6b8bd9860763},
    {.a = 0x9becdf3d9273, .b = 0xf8493407799d},
    {.a = 0x08b386463229, .b = 0x5efbaecef46b},
    {.a = 0xcd4c61c26e3d, .b = 0x31c7610de3b0},
    {.a = 0xa82607b01c0d, .b = 0x2910989b6880},
    {.a = 0x0e8f64340ba4, .b = 0x4acec1205d75},
    {.a = 0x2aa05ed1856f, .b = 0xeaac88e5dc99},
};

const MfClassicKeyPair troika_4k_keys[40] = {
    {.a = 0xEC29806D9738, .b = 0xFBF225DC5D58}, //1
    {.a = 0xA0A1A2A3A4A5, .b = 0x7DE02A7F6025}, //2
    {.a = 0x2AA05ED1856F, .b = 0xEAAC88E5DC99}, //3
    {.a = 0x2AA05ED1856F, .b = 0xEAAC88E5DC99}, //4
    {.a = 0x73068F118C13, .b = 0x2B7F3253FAC5}, //5
    {.a = 0xFBC2793D540B, .b = 0xD3A297DC2698}, //6
    {.a = 0x2AA05ED1856F, .b = 0xEAAC88E5DC99}, //7
    {.a = 0xAE3D65A3DAD4, .b = 0x0F1C63013DBA}, //8
    {.a = 0xA73F5DC1D333, .b = 0xE35173494A81}, //9
    {.a = 0x69A32F1C2F19, .b = 0x6B8BD9860763}, //10
    {.a = 0x9BECDF3D9273, .b = 0xF8493407799D}, //11
    {.a = 0x08B386463229, .b = 0x5EFBAECEF46B}, //12
    {.a = 0xCD4C61C26E3D, .b = 0x31C7610DE3B0}, //13
    {.a = 0xA82607B01C0D, .b = 0x2910989B6880}, //14
    {.a = 0x0E8F64340BA4, .b = 0x4ACEC1205D75}, //15
    {.a = 0x2AA05ED1856F, .b = 0xEAAC88E5DC99}, //16
    {.a = 0x6B02733BB6EC, .b = 0x7038CD25C408}, //17
    {.a = 0x403D706BA880, .b = 0xB39D19A280DF}, //18
    {.a = 0xC11F4597EFB5, .b = 0x70D901648CB9}, //19
    {.a = 0x0DB520C78C1C, .b = 0x73E5B9D9D3A4}, //20
    {.a = 0x3EBCE0925B2F, .b = 0x372CC880F216}, //21
    {.a = 0x16A27AF45407, .b = 0x9868925175BA}, //22
    {.a = 0xABA208516740, .b = 0xCE26ECB95252}, //23
    {.a = 0xCD64E567ABCD, .b = 0x8F79C4FD8A01}, //24
    {.a = 0x764CD061F1E6, .b = 0xA74332F74994}, //25
    {.a = 0x1CC219E9FEC1, .b = 0xB90DE525CEB6}, //26
    {.a = 0x2FE3CB83EA43, .b = 0xFBA88F109B32}, //27
    {.a = 0x07894FFEC1D6, .b = 0xEFCB0E689DB3}, //28
    {.a = 0x04C297B91308, .b = 0xC8454C154CB5}, //29
    {.a = 0x7A38E3511A38, .b = 0xAB16584C972A}, //30
    {.a = 0x7545DF809202, .b = 0xECF751084A80}, //31
    {.a = 0x5125974CD391, .b = 0xD3EAFB5DF46D}, //32
    {.a = 0x7A86AA203788, .b = 0xE41242278CA2}, //33
    {.a = 0xAFCEF64C9913, .b = 0x9DB96DCA4324}, //34
    {.a = 0x04EAA462F70B, .b = 0xAC17B93E2FAE}, //35
    {.a = 0xE734C210F27E, .b = 0x29BA8C3E9FDA}, //36
    {.a = 0xD5524F591EED, .b = 0x5DAF42861B4D}, //37
    {.a = 0xE4821A377B75, .b = 0xE8709E486465}, //38
    {.a = 0x518DC6EEA089, .b = 0x97C64AC98CA4}, //39
    {.a = 0xBB52F8CCE07F, .b = 0x6B6119752C70}, //40
};

static bool troika_get_card_config(TroikaCardConfig* config, MfClassicType type) {
    bool success = true;

    if(type == MfClassicType1k) {
        config->data_sector = 11;
        config->keys = troika_1k_keys;
    } else if(type == MfClassicType4k) {
        config->data_sector = 8; // Further testing needed
        config->keys = troika_4k_keys;
    } else {
        success = false;
    }

    return success;
}

static bool troika_parse(FuriString* parsed_data, const MfClassicData* data) {
    bool parsed = false;

    do {
        // Verify card type
        TroikaCardConfig cfg = {};
        if(!troika_get_card_config(&cfg, data->type)) break;

        // Verify key
        const MfClassicSectorTrailer* sec_tr =
            mf_classic_get_sector_trailer_by_sector(data, cfg.data_sector);

        const uint64_t key =
            bit_lib_bytes_to_num_be(sec_tr->key_a.data, COUNT_OF(sec_tr->key_a.data));
        if(key != cfg.keys[cfg.data_sector].a) break;

        FuriString* metro_result = furi_string_alloc();
        FuriString* ground_result = furi_string_alloc();
        FuriString* tat_result = furi_string_alloc();

        bool is_metro_data_present =
            mosgortrans_parse_transport_block(&data->block[32], metro_result);
        bool is_ground_data_present =
            mosgortrans_parse_transport_block(&data->block[28], ground_result);
        bool is_tat_data_present = mosgortrans_parse_transport_block(&data->block[16], tat_result);

        furi_string_cat_printf(parsed_data, "\e#Troyka card\n");
        if(is_metro_data_present && !furi_string_empty(metro_result)) {
            render_section_header(parsed_data, "Metro", 22, 21);
            furi_string_cat_printf(parsed_data, "%s\n", furi_string_get_cstr(metro_result));
        }

        if(is_ground_data_present && !furi_string_empty(ground_result)) {
            render_section_header(parsed_data, "Ediny", 22, 22);
            furi_string_cat_printf(parsed_data, "%s\n", furi_string_get_cstr(ground_result));
        }

        if(is_tat_data_present && !furi_string_empty(tat_result)) {
            render_section_header(parsed_data, "TAT", 24, 23);
            furi_string_cat_printf(parsed_data, "%s\n", furi_string_get_cstr(tat_result));
        }

        furi_string_free(tat_result);
        furi_string_free(ground_result);
        furi_string_free(metro_result);

        parsed = is_metro_data_present || is_ground_data_present || is_tat_data_present;
    } while(false);

    return parsed;
}

bool checked = false;

static NfcCommand troika_poller_callback(NfcGenericEvent event, void* context) {
    furi_assert(context);
    furi_assert(event.event_data);
    furi_assert(event.protocol == NfcProtocolMfClassic);

    NfcCommand command = NfcCommandContinue;
    const MfClassicPollerEvent* mfc_event = event.event_data;
    Metroflip* app = context;

    if(mfc_event->type == MfClassicPollerEventTypeCardDetected) {
        view_dispatcher_send_custom_event(app->view_dispatcher, MetroflipCustomEventCardDetected);
        command = NfcCommandContinue;
    } else if(mfc_event->type == MfClassicPollerEventTypeCardLost) {
        view_dispatcher_send_custom_event(app->view_dispatcher, MetroflipCustomEventCardLost);
        app->sec_num = 0;
        command = NfcCommandStop;
    } else if(mfc_event->type == MfClassicPollerEventTypeRequestMode) {
        mfc_event->data->poller_mode.mode = MfClassicPollerModeRead;

    } else if(mfc_event->type == MfClassicPollerEventTypeRequestReadSector) {
        MfClassicKey key = {0};
        MfClassicKeyType key_type = MfClassicKeyTypeA;
        bit_lib_num_to_bytes_be(troika_1k_keys[app->sec_num].a, COUNT_OF(key.data), key.data);
        if(!checked) {
            mfc_event->data->read_sector_request_data.sector_num = app->sec_num;
            mfc_event->data->read_sector_request_data.key = key;
            mfc_event->data->read_sector_request_data.key_type = key_type;
            mfc_event->data->read_sector_request_data.key_provided = true;
            app->sec_num++;
            checked = true;
        }
        nfc_device_set_data(
            app->nfc_device, NfcProtocolMfClassic, nfc_poller_get_data(app->poller));
        const MfClassicData* mfc_data = nfc_device_get_data(app->nfc_device, NfcProtocolMfClassic);
        if(mfc_data->type == MfClassicType1k) {
            bit_lib_num_to_bytes_be(troika_1k_keys[app->sec_num].a, COUNT_OF(key.data), key.data);

            mfc_event->data->read_sector_request_data.sector_num = app->sec_num;
            mfc_event->data->read_sector_request_data.key = key;
            mfc_event->data->read_sector_request_data.key_type = key_type;
            mfc_event->data->read_sector_request_data.key_provided = true;
            if(app->sec_num == 16) {
                mfc_event->data->read_sector_request_data.key_provided = false;
                app->sec_num = 0;
            }
            app->sec_num++;
        } else if(mfc_data->type == MfClassicType4k) {
            bit_lib_num_to_bytes_be(troika_4k_keys[app->sec_num].a, COUNT_OF(key.data), key.data);

            mfc_event->data->read_sector_request_data.sector_num = app->sec_num;
            mfc_event->data->read_sector_request_data.key = key;
            mfc_event->data->read_sector_request_data.key_type = key_type;
            mfc_event->data->read_sector_request_data.key_provided = true;
            if(app->sec_num == 40) {
                mfc_event->data->read_sector_request_data.key_provided = false;
                app->sec_num = 0;
            }
            app->sec_num++;
        }
    } else if(mfc_event->type == MfClassicPollerEventTypeSuccess) {
        const MfClassicData* mfc_data = nfc_device_get_data(app->nfc_device, NfcProtocolMfClassic);
        FuriString* parsed_data = furi_string_alloc();
        Widget* widget = app->widget;
        if(!troika_parse(parsed_data, mfc_data)) {
            furi_string_reset(app->text_box_store);
            FURI_LOG_I(TAG, "Unknown card type");
            furi_string_printf(parsed_data, "\e#Unknown card\n");
        }
        widget_add_text_scroll_element(widget, 0, 0, 128, 64, furi_string_get_cstr(parsed_data));

        widget_add_button_element(
            widget, GuiButtonTypeRight, "Exit", metroflip_exit_widget_callback, app);
        widget_add_button_element(
            widget, GuiButtonTypeCenter, "Save", metroflip_save_widget_callback, app);

        furi_string_free(parsed_data);
        view_dispatcher_switch_to_view(app->view_dispatcher, MetroflipViewWidget);
        metroflip_app_blink_stop(app);
        command = NfcCommandStop;
    } else if(mfc_event->type == MfClassicPollerEventTypeFail) {
        FURI_LOG_I(TAG, "fail");
        command = NfcCommandStop;
    }

    return command;
}

static void troika_on_enter(Metroflip* app) {
    dolphin_deed(DolphinDeedNfcRead);

    app->sec_num = 0;

    if(app->data_loaded) {
        Storage* storage = furi_record_open(RECORD_STORAGE);
        FlipperFormat* ff = flipper_format_file_alloc(storage);
        if(flipper_format_file_open_existing(ff, app->file_path)) {
            MfClassicData* mfc_data = mf_classic_alloc();
            mf_classic_load(mfc_data, ff, 2);
            FuriString* parsed_data = furi_string_alloc();
            Widget* widget = app->widget;

            furi_string_reset(app->text_box_store);
            if(!troika_parse(parsed_data, mfc_data)) {
                furi_string_reset(app->text_box_store);
                FURI_LOG_I(TAG, "Unknown card type");
                furi_string_printf(parsed_data, "\e#Unknown card\n");
            }
            widget_add_text_scroll_element(
                widget, 0, 0, 128, 64, furi_string_get_cstr(parsed_data));

            widget_add_button_element(
                widget, GuiButtonTypeRight, "Exit", metroflip_exit_widget_callback, app);
            widget_add_button_element(
                widget, GuiButtonTypeCenter, "Delete", metroflip_delete_widget_callback, app);
            mf_classic_free(mfc_data);
            furi_string_free(parsed_data);
            view_dispatcher_switch_to_view(app->view_dispatcher, MetroflipViewWidget);
        }
        flipper_format_free(ff);
    } else {
        // Setup view
        Popup* popup = app->popup;
        popup_set_header(popup, "Apply\n card to\nthe back", 68, 30, AlignLeft, AlignTop);
        popup_set_icon(popup, 0, 3, &I_RFIDDolphinReceive_97x61);

        // Start worker
        view_dispatcher_switch_to_view(app->view_dispatcher, MetroflipViewPopup);
        app->poller = nfc_poller_alloc(app->nfc, NfcProtocolMfClassic);
        nfc_poller_start(app->poller, troika_poller_callback, app);

        metroflip_app_blink_start(app);
    }
}

static bool troika_on_event(Metroflip* app, SceneManagerEvent event) {
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == MetroflipCustomEventCardDetected) {
            Popup* popup = app->popup;
            popup_set_header(popup, "DON'T\nMOVE", 68, 30, AlignLeft, AlignTop);
            consumed = true;
        } else if(event.event == MetroflipCustomEventCardLost) {
            Popup* popup = app->popup;
            popup_set_header(popup, "Card \n lost", 68, 30, AlignLeft, AlignTop);
            consumed = true;
        } else if(event.event == MetroflipCustomEventWrongCard) {
            Popup* popup = app->popup;
            popup_set_header(popup, "WRONG \n CARD", 68, 30, AlignLeft, AlignTop);
            consumed = true;
        } else if(event.event == MetroflipCustomEventPollerFail) {
            Popup* popup = app->popup;
            popup_set_header(popup, "Failed", 68, 30, AlignLeft, AlignTop);
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, MetroflipSceneStart);
        consumed = true;
    }

    return consumed;
}

static void troika_on_exit(Metroflip* app) {
    widget_reset(app->widget);

    if(app->poller && !app->data_loaded) {
        nfc_poller_stop(app->poller);
        nfc_poller_free(app->poller);
    }

    // Clear view
    popup_reset(app->popup);

    metroflip_app_blink_stop(app);
}

/* Actual implementation of app<>plugin interface */
static const MetroflipPlugin troika_plugin = {
    .card_name = "Troika",
    .plugin_on_enter = troika_on_enter,
    .plugin_on_event = troika_on_event,
    .plugin_on_exit = troika_on_exit,

};

/* Plugin descriptor to comply with basic plugin specification */
static const FlipperAppPluginDescriptor troika_plugin_descriptor = {
    .appid = METROFLIP_SUPPORTED_CARD_PLUGIN_APP_ID,
    .ep_api_version = METROFLIP_SUPPORTED_CARD_PLUGIN_API_VERSION,
    .entry_point = &troika_plugin,
};

/* Plugin entry point - must return a pointer to const descriptor  */
const FlipperAppPluginDescriptor* troika_plugin_ep(void) {
    return &troika_plugin_descriptor;
}
