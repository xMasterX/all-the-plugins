/*
 * Suica Scene
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "../../api/suica/suica_drawings.h"
#include "../../metroflip_plugins.h"

#include <lib/nfc/protocols/felica/felica.h>
#include <lib/nfc/protocols/felica/felica_poller.h>
// #include <lib/nfc/protocols/felica/felica_poller_i.h>
#include <lib/bit_lib/bit_lib.h>

#include <applications/services/locale/locale.h>
#include <datetime.h>

// Probably not needed after upstream include this in their suica_i.h

#define TAG "Metroflip:Scene:Suica"

const char* suica_service_names[] = {
    "Travel History",
    "Taps Log",
};

static void suica_model_initialize(SuicaHistoryViewModel* model, size_t initial_capacity) {
    model->travel_history =
        (uint8_t*)malloc(initial_capacity * FELICA_DATA_BLOCK_SIZE); // Each entry is 16 bytes
    model->size = 0;
    model->capacity = initial_capacity;
    model->entry = 1;
    model->page = 0;
    model->animator_tick = 0;
    model->history.entry_station.name = furi_string_alloc_set("Unknown");
    model->history.entry_station.jr_header = furi_string_alloc_set("0");
    model->history.exit_station.name = furi_string_alloc_set("Unknown");
    model->history.exit_station.jr_header = furi_string_alloc_set("0");
    model->history.entry_line = RailwaysList[SUICA_RAILWAY_NUM];
    model->history.exit_line = RailwaysList[SUICA_RAILWAY_NUM];
}

static void suica_model_initialize_after_load(SuicaHistoryViewModel* model) {
    model->entry = 1;
    model->page = 0;
    model->animator_tick = 0;
    model->history.entry_station.name = furi_string_alloc_set("Unknown");
    model->history.entry_station.jr_header = furi_string_alloc_set("0");
    model->history.exit_station.name = furi_string_alloc_set("Unknown");
    model->history.exit_station.jr_header = furi_string_alloc_set("0");
    model->history.entry_line = RailwaysList[SUICA_RAILWAY_NUM];
    model->history.exit_line = RailwaysList[SUICA_RAILWAY_NUM];
}

static void suica_add_entry(SuicaHistoryViewModel* model, const uint8_t* entry) {
    if(model->size <= 0) {
        suica_model_initialize(model, 3);
    }
    // Check if resizing is needed
    if(model->size == model->capacity) {
        size_t new_capacity = model->capacity * 2; // Double the capacity
        uint8_t* new_data =
            (uint8_t*)realloc(model->travel_history, new_capacity * FELICA_DATA_BLOCK_SIZE);
        model->travel_history = new_data;
        model->capacity = new_capacity;
    }

    // Copy the 16-byte entry to the next slot
    for(size_t i = 0; i < FELICA_DATA_BLOCK_SIZE; i++) {
        model->travel_history[(model->size * FELICA_DATA_BLOCK_SIZE) + i] = entry[i];
    }

    model->size++;
}

static void suica_parse_train_code(
    uint8_t line_code,
    uint8_t station_code,
    SuicaTrainRideType ride_type,
    SuicaHistoryViewModel* model) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    Stream* stream = file_stream_alloc(storage);
    FuriString* line = furi_string_alloc();

    FuriString* line_code_str = furi_string_alloc();
    FuriString* line_and_station_code_str = furi_string_alloc();

    furi_string_printf(line_code_str, "0x%02X", line_code);
    furi_string_printf(line_and_station_code_str, "0x%02X,0x%02X", line_code, station_code);

    FuriString* line_candidate = furi_string_alloc_set(SUICA_RAILWAY_UNKNOWN_NAME);
    FuriString* station_candidate = furi_string_alloc_set(SUICA_RAILWAY_UNKNOWN_NAME);
    FuriString* station_num_candidate = furi_string_alloc_set("0");
    FuriString* station_JR_header_candidate = furi_string_alloc_set("0");
    FuriString* line_copy = furi_string_alloc();
    size_t line_comma_ind = 0;
    size_t station_comma_ind = 0;
    size_t station_num_comma_ind = 0;
    size_t station_JR_header_comma_ind = 0;

    bool station_found = false;
    FuriString* file_name = furi_string_alloc();
    furi_string_printf(file_name, "%s0x%02X.txt", SUICA_STATION_LIST_PATH, line_code);
    if(file_stream_open(stream, furi_string_get_cstr(file_name), FSAM_READ, FSOM_OPEN_EXISTING)) {
        while(stream_read_line(stream, line) && !station_found) {
            // file is in csv format: station_group_id,station_id,station_sub_id,station_name
            // search for the station
            furi_string_replace_all(line, "\r", "");
            furi_string_replace_all(line, "\n", "");
            furi_string_set(line_copy, line); // 0xD5,0x02,Keikyu Main,Shinagawa,1,0

            if(furi_string_start_with(line, line_code_str)) {
                // set line name here
                furi_string_right(line_copy, 10); // Keikyu Main,Shinagawa,1,0
                furi_string_set(line_candidate, line_copy);
                line_comma_ind = furi_string_search_char(line_candidate, ',', 0);
                furi_string_left(line_candidate, line_comma_ind); // Keikyu Main
                // we cut the line and station code in the line line copy
                // and we leave only the line name for the line candidate
                if(furi_string_start_with(line, line_and_station_code_str)) {
                    furi_string_set(station_candidate, line_copy); // Keikyu Main,Shinagawa,1,0
                    furi_string_right(station_candidate, line_comma_ind + 1);
                    station_comma_ind =
                        furi_string_search_char(station_candidate, ',', 0); // Shinagawa,1,0
                    furi_string_left(station_candidate, station_comma_ind); //  Shinagawa
                    station_found = true;
                    break;
                }
            }
        }
    } else {
        FURI_LOG_E(TAG, "Failed to open stations.txt");
    }

    furi_string_set(station_num_candidate, line_copy); // Keikyu Main,Shinagawa,1,0
    furi_string_right(station_num_candidate, line_comma_ind + station_comma_ind + 2); // 1,0
    station_num_comma_ind = furi_string_search_char(station_num_candidate, ',', 0);
    furi_string_left(station_num_candidate, station_num_comma_ind); // 1

    furi_string_set(station_JR_header_candidate, line_copy); // Keikyu Main,Shinagawa,1,0
    furi_string_right(
        station_JR_header_candidate,
        line_comma_ind + station_comma_ind + station_num_comma_ind + 3); // 0
    station_JR_header_comma_ind = furi_string_search_char(station_JR_header_candidate, ',', 0);
    furi_string_left(station_JR_header_candidate, station_JR_header_comma_ind); // 0

    switch(ride_type) {
    case SuicaTrainRideEntry:
        model->history.entry_station.name = furi_string_alloc_set("Unknown");
        model->history.entry_station.jr_header = furi_string_alloc_set("0");
        model->history.entry_line = RailwaysList[SUICA_RAILWAY_NUM];
        for(size_t i = 0; i < SUICA_RAILWAY_NUM; i++) {
            if(furi_string_equal_str(line_candidate, RailwaysList[i].long_name)) {
                model->history.entry_line = RailwaysList[i];
                furi_string_set(model->history.entry_station.name, station_candidate);
                model->history.entry_station.station_number =
                    atoi(furi_string_get_cstr(station_num_candidate));
                furi_string_set(
                    model->history.entry_station.jr_header, station_JR_header_candidate);
                break;
            }
        }
        break;
    case SuicaTrainRideExit:
        model->history.exit_station.name = furi_string_alloc_set("Unknown");
        model->history.exit_station.jr_header = furi_string_alloc_set("0");
        model->history.exit_line = RailwaysList[SUICA_RAILWAY_NUM];
        for(size_t i = 0; i < SUICA_RAILWAY_NUM; i++) {
            if(furi_string_equal_str(line_candidate, RailwaysList[i].long_name)) {
                model->history.exit_line = RailwaysList[i];
                furi_string_set(model->history.exit_station.name, station_candidate);
                model->history.exit_station.station_number =
                    atoi(furi_string_get_cstr(station_num_candidate));
                furi_string_set(
                    model->history.exit_station.jr_header, station_JR_header_candidate);
                break;
            }
        }
        break;
    default:
        UNUSED(model);
        break;
    }

    furi_string_free(line);
    furi_string_free(line_copy);
    furi_string_free(line_code_str);
    furi_string_free(line_and_station_code_str);
    furi_string_free(line_candidate);
    furi_string_free(station_candidate);
    furi_string_free(station_num_candidate);
    furi_string_free(station_JR_header_candidate);
    file_stream_close(stream);
    stream_free(stream);
    furi_record_close(RECORD_STORAGE);
}

static void suica_parse(SuicaHistoryViewModel* my_model) {
    uint8_t current_block[FELICA_DATA_BLOCK_SIZE];
    // Parse the current block/entry
    for(size_t i = 0; i < FELICA_DATA_BLOCK_SIZE; i++) {
        current_block[i] = my_model->travel_history[((my_model->entry - 1) * 16) + i];
    }

    if(((uint8_t)current_block[4] + (uint8_t)current_block[5]) != 0) {
        my_model->history.year = ((uint8_t)current_block[4] & 0xFE) >> 1;
        my_model->history.month = (((uint8_t)current_block[4] & 0x01) << 3) |
                                  (((uint8_t)current_block[5] & 0xE0) >> 5);
        my_model->history.day = (uint8_t)current_block[5] & 0x1F;
    } else {
        my_model->history.year = 0;
        my_model->history.month = 0;
        my_model->history.day = 0;
    }
    my_model->history.balance = ((uint16_t)current_block[11] << 8) | (uint16_t)current_block[10];
    my_model->history.area_code = current_block[15];
    if((uint8_t)current_block[0] >= TERMINAL_TICKET_VENDING_MACHINE &&
       (uint8_t)current_block[0] <= TERMINAL_IN_CAR_SUPP_MACHINE) {
        // Train rides
        // Will be overwritton is is ticket sale (TERMINAL_TICKET_VENDING_MACHINE)
        my_model->history.history_type = SuicaHistoryTrain;
        uint8_t entry_line = current_block[6];
        uint8_t entry_station = current_block[7];
        uint8_t exit_line = current_block[8];
        uint8_t exit_station = current_block[9];

        suica_parse_train_code(entry_line, entry_station, SuicaTrainRideEntry, my_model);

        if((uint8_t)current_block[14] != 0x01) {
            suica_parse_train_code(exit_line, exit_station, SuicaTrainRideExit, my_model);
        }

        if(((uint8_t)current_block[4] + (uint8_t)current_block[5]) != 0) {
            my_model->history.year = ((uint8_t)current_block[4] & 0xFE) >> 1;
            my_model->history.month = (((uint8_t)current_block[4] & 0x01) << 3) |
                                      (((uint8_t)current_block[5] & 0xE0) >> 5);
            my_model->history.day = (uint8_t)current_block[5] & 0x1F;
        }
    }
    switch((uint8_t)current_block[0]) {
    case TERMINAL_BUS:
        // 6 & 7 bus line code
        // 8 & 9 bus stop code
        my_model->history.history_type = SuicaHistoryBus;
        break;
    case TERMINAL_POS_AND_TAXI:
    case TERMINAL_VENDING_MACHINE:
        // 6 & 7 are hour and minute
        my_model->history.history_type = ((uint8_t)current_block[0] == TERMINAL_POS_AND_TAXI) ?
                                             SuicaHistoryPosAndTaxi :
                                             SuicaHistoryVendingMachine;
        my_model->history.hour = ((uint8_t)current_block[6] & 0xF8) >> 3;
        my_model->history.minute = (((uint8_t)current_block[6] & 0x07) << 3) |
                                   (((uint8_t)current_block[7] & 0xE0) >> 5);
        my_model->history.shop_code = (uint8_t*)malloc(2);
        my_model->history.shop_code[0] = current_block[8];
        my_model->history.shop_code[1] = current_block[9];
        break;
    case TERMINAL_MOBILE_PHONE:
        break;
    case TERMINAL_TICKET_VENDING_MACHINE:
        my_model->history.history_type = SuicaHistoryHappyBirthday;
        break;
    default:
        if((uint8_t)current_block[0] <= TERMINAL_NULL) {
            my_model->history.history_type = SuicaHistoryNull;
        }
        break;
    }
    if((uint8_t)current_block[14] == 0x01) {
        my_model->history.history_type = SuicaHistoryHappyBirthday;
    }
}

static void suica_parse_detail_callback(GuiButtonType result, InputType type, void* context) {
    Metroflip* app = context;
    UNUSED(result);
    if(type == InputTypeShort) {
        SuicaHistoryViewModel* my_model = view_get_model(app->suica_context->view_history);
        suica_parse(my_model);
        FURI_LOG_I(TAG, "Draw Callback: We have %d entries", my_model->size);
        view_dispatcher_switch_to_view(app->view_dispatcher, MetroflipViewCanvas);
    }
}

static NfcCommand suica_poller_callback(NfcGenericEvent event, void* context) {
    furi_assert(event.protocol == NfcProtocolFelica);
    NfcCommand command = NfcCommandContinue;
    MetroflipPollerEventType stage = MetroflipPollerEventTypeStart;

    Metroflip* app = context;
    FuriString* parsed_data = furi_string_alloc();
    SuicaHistoryViewModel* model = view_get_model(app->suica_context->view_history);

    Widget* widget = app->widget;

    const uint16_t service_code[2] = {SERVICE_CODE_HISTORY_IN_LE, SERVICE_CODE_TAPS_LOG_IN_LE};

    const FelicaPollerEvent* felica_event = event.event_data;
    FelicaPollerReadCommandResponse* rx_resp;
    rx_resp->SF1 = 0;
    rx_resp->SF2 = 0;
    uint8_t blocks[1] = {0x00};
    FelicaPoller* felica_poller = event.instance;
    const FelicaData* felica_data = nfc_poller_get_data(app->poller);
    FURI_LOG_I(TAG, "Poller set");
    if(felica_event->type == FelicaPollerEventTypeRequestAuthContext) {
        view_dispatcher_send_custom_event(app->view_dispatcher, MetroflipCustomEventCardDetected);
        command = NfcCommandContinue;

        if(stage == MetroflipPollerEventTypeStart) {
            nfc_device_set_data(
                app->nfc_device, NfcProtocolFelica, nfc_poller_get_data(app->poller));
            furi_string_printf(parsed_data, "\e#Suica\n");

            FelicaError error = FelicaErrorNone;
            int service_code_index = 0;
            // Authenticate with the card
            // Iterate through the two services
            while(service_code_index < 2 && error == FelicaErrorNone) {
                furi_string_cat_printf(
                    parsed_data, "%s: \n", suica_service_names[service_code_index]);
                rx_resp->SF1 = 0;
                rx_resp->SF2 = 0;
                blocks[0] = 0; // firmware api requires this to be a list
                while((rx_resp->SF1 + rx_resp->SF2) == 0 &&
                      blocks[0] < SUICA_MAX_HISTORY_ENTRIES && error == FelicaErrorNone) {
                    uint8_t block_data[16] = {0};
                    error = felica_poller_read_blocks(
                        felica_poller, 1, blocks, service_code[service_code_index], &rx_resp);
                    if(error != FelicaErrorNone) {
                        view_dispatcher_send_custom_event(
                            app->view_dispatcher, MetroflipCustomEventCardLost);
                        command = NfcCommandStop;
                        break;
                    }
                    furi_string_cat_printf(parsed_data, "Block %02X\n", blocks[0]);
                    blocks[0]++;
                    for(size_t i = 0; i < FELICA_DATA_BLOCK_SIZE; i++) {
                        furi_string_cat_printf(parsed_data, "%02X ", rx_resp->data[i]);
                        block_data[i] = rx_resp->data[i];
                    }
                    furi_string_cat_printf(parsed_data, "\n");
                    if(service_code_index == 0) {
                        FURI_LOG_I(
                            TAG,
                            "Service code %d, adding entry %x",
                            service_code_index,
                            model->size);
                        suica_add_entry(model, block_data);
                    }
                }
                service_code_index++;
            }
            metroflip_app_blink_stop(app);

            if(model->size == 1) { // Have to let the poller run once before knowing we failed
                furi_string_printf(
                    parsed_data,
                    "\e#Suica\nSorry, no data found.\nPlease let the developers know and we will add support.");
            }

            if(model->size == 1 && felica_data->pmm.data[1] != SUICA_IC_TYPE_CODE) {
                furi_string_printf(parsed_data, "\e#Suica\nSorry, not a Suica.\n");
            }
            widget_add_text_scroll_element(
                widget, 0, 0, 128, 64, furi_string_get_cstr(parsed_data));

            widget_add_button_element(
                widget, GuiButtonTypeRight, "Exit", metroflip_exit_widget_callback, app);
            widget_add_button_element(
                widget, GuiButtonTypeLeft, "Del", metroflip_delete_widget_callback, app);

            if(model->size > 1) {
                widget_add_button_element(
                    widget, GuiButtonTypeCenter, "Parse", suica_parse_detail_callback, app);
            }
            view_dispatcher_switch_to_view(app->view_dispatcher, MetroflipViewWidget);
        }
    }
    furi_string_free(parsed_data);
    command = NfcCommandStop;
    return command;
}

static bool suica_history_input_callback(InputEvent* event, void* context) {
    Metroflip* app = (Metroflip*)context;
    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        switch(event->key) {
        case InputKeyLeft: {
            bool redraw = true;
            with_view_model(
                app->suica_context->view_history,
                SuicaHistoryViewModel * model,
                {
                    if(model->entry > 1) {
                        model->entry--;
                    } 
                    suica_parse(model);
                    FURI_LOG_I(TAG, "Viewing entry %d", model->entry);
                },
                redraw);
            break;
        }
        case InputKeyRight: {
            bool redraw = true;
            with_view_model(
                app->suica_context->view_history,
                SuicaHistoryViewModel * model,
                {
                    if(model->entry < model->size) {
                        model->entry++;
                    }
                    suica_parse(model);
                    FURI_LOG_I(TAG, "Viewing entry %d", model->entry);
                },
                redraw);
            break;
        }
        case InputKeyUp: {
            bool redraw = true;
            with_view_model(
                app->suica_context->view_history,
                SuicaHistoryViewModel * model,
                {
                    if(model->page > 0) {
                        model->page--;
                    }
                },
                redraw);
            break;
        }
        case InputKeyDown: {
            bool redraw = true;
            with_view_model(
                app->suica_context->view_history,
                SuicaHistoryViewModel * model,
                {
                    if(model->page < HISTORY_VIEW_PAGE_NUM - 1) {
                        model->page++;
                    }
                },
                redraw);
            break;
        }
        default:
            // Handle other keys or do nothing
            break;
        }
    }

    return false;
}

static void suica_on_enter(Metroflip* app) {
    // Gui* gui = furi_record_open(RECORD_GUI);
    dolphin_deed(DolphinDeedNfcRead);

    if(app->data_loaded == false) {
        app->suica_context = malloc(sizeof(SuicaContext));
        app->suica_context->view_history = view_alloc();
        view_set_context(app->suica_context->view_history, app);
        view_allocate_model(
            app->suica_context->view_history,
            ViewModelTypeLockFree,
            sizeof(SuicaHistoryViewModel));
    }

    view_set_input_callback(app->suica_context->view_history, suica_history_input_callback);
    view_set_previous_callback(app->suica_context->view_history, suica_navigation_raw_callback);
    view_set_enter_callback(app->suica_context->view_history, suica_view_history_enter_callback);
    view_set_exit_callback(app->suica_context->view_history, suica_view_history_exit_callback);
    view_set_custom_callback(
        app->suica_context->view_history, suica_view_history_custom_event_callback);
    view_set_draw_callback(app->suica_context->view_history, suica_history_draw_callback);

    view_dispatcher_add_view(
        app->view_dispatcher, MetroflipViewCanvas, app->suica_context->view_history);

    if(app->data_loaded == false) {
        popup_set_header(app->popup, "Apply\n card to\nthe back", 68, 30, AlignLeft, AlignTop);
        popup_set_icon(app->popup, 0, 3, &I_RFIDDolphinReceive_97x61);
        view_dispatcher_switch_to_view(app->view_dispatcher, MetroflipViewPopup);

        nfc_scanner_alloc(app->nfc);
        app->poller = nfc_poller_alloc(app->nfc, NfcProtocolFelica);
        nfc_poller_start(app->poller, suica_poller_callback, app);
        FURI_LOG_I(TAG, "Poller started");

        metroflip_app_blink_start(app);
    } else {
        SuicaHistoryViewModel* model = view_get_model(app->suica_context->view_history);
        suica_model_initialize_after_load(model);
        Widget* widget = app->widget;
        FuriString* parsed_data = furi_string_alloc();
        furi_string_printf(parsed_data, "\e#Suica\n");

        for(uint8_t i = 0; i < model->size; i++) {
            furi_string_cat_printf(parsed_data, "Block %02X\n", i);
            for(size_t j = 0; j < FELICA_DATA_BLOCK_SIZE; j++) {
                furi_string_cat_printf(parsed_data, "%02X ", model->travel_history[i * 16 + j]);
            }
            furi_string_cat_printf(parsed_data, "\n");
        }
        widget_add_text_scroll_element(widget, 0, 0, 128, 64, furi_string_get_cstr(parsed_data));

        widget_add_button_element(
            widget, GuiButtonTypeRight, "Exit", metroflip_exit_widget_callback, app);
        widget_add_button_element(
            widget, GuiButtonTypeLeft, "Del", metroflip_delete_widget_callback, app);

        if(model->size > 1) {
            widget_add_button_element(
                widget, GuiButtonTypeCenter, "Parse", suica_parse_detail_callback, app);
        }
        view_dispatcher_switch_to_view(app->view_dispatcher, MetroflipViewWidget);
        furi_string_free(parsed_data);
    }
}

static bool suica_on_event(Metroflip* app, SceneManagerEvent event) {
    bool consumed = false;
    Popup* popup = app->popup;
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == MetroflipCustomEventCardDetected) {
            popup_set_header(popup, "DON'T\nMOVE", 68, 30, AlignLeft, AlignTop);
            consumed = true;
        } else if(event.event == MetroflipCustomEventCardLost) {
            popup_set_header(popup, "Card \n lost", 68, 30, AlignLeft, AlignTop);
            // popup_set_timeout(popup, 2000);
            // popup_enable_timeout(popup);
            // view_dispatcher_switch_to_view(app->view_dispatcher, SuicaViewPopup);
            // popup_disable_timeout(popup);
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, MetroflipSceneStart);
            consumed = true;
        } else if(event.event == MetroflipCustomEventWrongCard) {
            popup_set_header(popup, "WRONG \n CARD", 68, 30, AlignLeft, AlignTop);
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, MetroflipSceneStart);
            consumed = true;
        } else if(event.event == MetroflipCustomEventPollerFail) {
            popup_set_header(popup, "Failed", 68, 30, AlignLeft, AlignTop);
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, MetroflipSceneStart);
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        UNUSED(popup);
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, MetroflipSceneStart);
        consumed = true;
    }
    return consumed;
}

static void suica_on_exit(Metroflip* app) {
    widget_reset(app->widget);
    with_view_model(
        app->suica_context->view_history,
        SuicaHistoryViewModel * model,
        {
            if(model->travel_history) { // Check if memory was allocated
                free(model->travel_history);
                model->travel_history = NULL; // Set pointer to NULL to prevent dangling references
            }
        },
        false);
    view_free_model(app->suica_context->view_history);
    view_dispatcher_remove_view(app->view_dispatcher, MetroflipViewCanvas);
    view_free(app->suica_context->view_history);
    free(app->suica_context);
    if(app->poller && !app->data_loaded) {
        nfc_poller_stop(app->poller);
        nfc_poller_free(app->poller);
    }
}

/* Actual implementation of app<>plugin interface */
static const MetroflipPlugin suica_plugin = {
    .card_name = "Suica",
    .plugin_on_enter = suica_on_enter,
    .plugin_on_event = suica_on_event,
    .plugin_on_exit = suica_on_exit,

};

/* Plugin descriptor to comply with basic plugin specification */
static const FlipperAppPluginDescriptor suica_plugin_descriptor = {
    .appid = METROFLIP_SUPPORTED_CARD_PLUGIN_APP_ID,
    .ep_api_version = METROFLIP_SUPPORTED_CARD_PLUGIN_API_VERSION,
    .entry_point = &suica_plugin,
};

/* Plugin entry point - must return a pointer to const descriptor  */
const FlipperAppPluginDescriptor* suica_plugin_ep(void) {
    return &suica_plugin_descriptor;
}
