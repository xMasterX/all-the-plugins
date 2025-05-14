// Methods for Sub-GHz transmission

#include <flipper_format/flipper_format_i.h>
#include <path.h>
#include <string.h>

#include "helpers/subghz_txrx.h"
#include <lib/subghz/blocks/custom_btn.h>
#include <lib/subghz/protocols/raw.h>

#include "action_i.h"
#include "quac.h"

#define SUBGHZ_DIR_PATH EXT_PATH("subghz/")

typedef struct SubGhzNeedSaveContext {
    App* app;
    SubGhzTxRx* txrx;
    FuriString* file_path;
} SubGhzNeedSaveContext;

void action_subghz_need_save_callback(void* context) {
    FURI_LOG_I(TAG, "Saving udpated subghz signal");
    SubGhzNeedSaveContext* savectx = (SubGhzNeedSaveContext*)context;
    FlipperFormat* ff = subghz_txrx_get_fff_data(savectx->txrx);

    Stream* ff_stream = flipper_format_get_raw_stream(ff);
    flipper_format_delete_key(ff, "Repeat");
    //flipper_format_delete_key(ff, "Manufacture");

    do {
        if(!storage_simply_remove(
               savectx->app->storage, furi_string_get_cstr(savectx->file_path))) {
            FURI_LOG_E(TAG, "Failed to delete subghz file before re-save");
            break;
        }
        stream_seek(ff_stream, 0, StreamOffsetFromStart);
        stream_save_to_file(
            ff_stream,
            savectx->app->storage,
            furi_string_get_cstr(savectx->file_path),
            FSOM_CREATE_ALWAYS);
        if(storage_common_stat(
               savectx->app->storage, furi_string_get_cstr(savectx->file_path), NULL) != FSE_OK) {
            FURI_LOG_E(TAG, "Error verifying new subghz file after re-save");
            break;
        }
    } while(0);

    // Update original .sub file.
    //In case when rolling code was used in Quac we must update original .sub file with actual rolling code counter

    // Take file name from quac_app path
    FuriString* quac_filename = furi_string_alloc();
    furi_string_reset(quac_filename);
    path_extract_filename(savectx->file_path, quac_filename, false);
    FURI_LOG_I(TAG, "Extracted quac filename: %s", furi_string_get_cstr(quac_filename));

    //create new char string with full path (dir+filename) to original subghz folder
    char* full_subghz_file_name =
        malloc(1 + strlen(SUBGHZ_DIR_PATH) + strlen(furi_string_get_cstr(quac_filename)));
    strcpy(full_subghz_file_name, SUBGHZ_DIR_PATH);
    strcat(full_subghz_file_name, furi_string_get_cstr(quac_filename));
    FURI_LOG_I(TAG, "Full path to safe file: %s", full_subghz_file_name);

    //Save subghz file to original subghz location
    do {
        if(!storage_simply_remove(savectx->app->storage, full_subghz_file_name)) {
            FURI_LOG_E(
                TAG, "Failed to delete subghz file before re-save in original SUBGHZ location");
            break;
        }
        stream_seek(ff_stream, 0, StreamOffsetFromStart);
        stream_save_to_file(
            ff_stream, savectx->app->storage, full_subghz_file_name, FSOM_CREATE_ALWAYS);
        if(storage_common_stat(savectx->app->storage, full_subghz_file_name, NULL) != FSE_OK) {
            FURI_LOG_E(
                TAG, "Error verifying new subghz file after re-save in original SUBGHZ location");
            break;
        }
    } while(0);

    free(full_subghz_file_name);
    furi_string_free(quac_filename);
}

static void action_subghz_raw_end_callback(void* context) {
    FURI_LOG_I(TAG, "Stopping TX on RAW");
    furi_assert(context);
    App* app = context;

    app->raw_file_is_tx = false;
}

void action_subghz_tx(void* context, FuriString* action_path, FuriString* error) {
    App* app = context;
    const char* file_name = furi_string_get_cstr(action_path);

    FlipperFormat* fff_data_file = flipper_format_file_alloc(app->storage);

    SubGhzTxRx* txrx = subghz_txrx_alloc();

    SubGhzNeedSaveContext save_context = {app, txrx, action_path};
    subghz_txrx_set_need_save_callback(txrx, action_subghz_need_save_callback, &save_context);

    Stream* fff_data_stream = flipper_format_get_raw_stream(subghz_txrx_get_fff_data(txrx));
    stream_clean(fff_data_stream);

    FuriString* preset_name = furi_string_alloc();
    FuriString* protocol_name = furi_string_alloc();

    subghz_custom_btns_reset();

    app->raw_file_is_tx = false;

    FuriString* temp_str;
    temp_str = furi_string_alloc();
    uint32_t temp_data32;

    uint32_t frequency = 0;

    FURI_LOG_I(TAG, "SUBGHZ: Action starting...");

    do {
        if(!flipper_format_file_open_existing(fff_data_file, file_name)) {
            FURI_LOG_E(TAG, "Error opening %s", file_name);
            ACTION_SET_ERROR("SUBGHZ: Error opening %s", file_name);
            break;
        }

        if(!flipper_format_read_header(fff_data_file, temp_str, &temp_data32)) {
            FURI_LOG_E(TAG, "Missing or incorrect header");
            ACTION_SET_ERROR("SUBGHZ: Missing or incorrect header");
            break;
        }

        if(((!strcmp(furi_string_get_cstr(temp_str), SUBGHZ_KEY_FILE_TYPE)) ||
            (!strcmp(furi_string_get_cstr(temp_str), SUBGHZ_RAW_FILE_TYPE))) &&
           temp_data32 == SUBGHZ_KEY_FILE_VERSION) {
        } else {
            FURI_LOG_E(TAG, "Type or version mismatch");
            ACTION_SET_ERROR("SUBGHZ: Type or version mismatch");
            break;
        }

        SubGhzSetting* setting = subghz_txrx_get_setting(txrx);
        if(!flipper_format_read_uint32(fff_data_file, "Frequency", &frequency, 1)) {
            FURI_LOG_W(TAG, "Missing Frequency. Setting default frequency");
            frequency = subghz_setting_get_default_frequency(setting);
        } else if(!subghz_txrx_radio_device_is_frequecy_valid(txrx, frequency)) {
            FURI_LOG_E(TAG, "Frequency not supported on the chosen radio module");
            ACTION_SET_ERROR("SUBGHZ: Frequency not supported");
            break;
        }

        if(!flipper_format_read_string(fff_data_file, "Preset", temp_str)) {
            FURI_LOG_E(TAG, "Missing Preset");
            ACTION_SET_ERROR("SUBGHZ: Missing preset");
            break;
        }

        furi_string_set_str(
            temp_str, subghz_txrx_get_preset_name(txrx, furi_string_get_cstr(temp_str)));
        if(!strcmp(furi_string_get_cstr(temp_str), "")) {
            FURI_LOG_E(TAG, "Unknown preset");
            ACTION_SET_ERROR("SUBGHZ: Unknown preset");
            break;
        }

        if(!strcmp(furi_string_get_cstr(temp_str), "CUSTOM")) {
            subghz_setting_delete_custom_preset(setting, furi_string_get_cstr(temp_str));
            if(!subghz_setting_load_custom_preset(
                   setting, furi_string_get_cstr(temp_str), fff_data_file)) {
                FURI_LOG_E(TAG, "Missing Custom preset");
                ACTION_SET_ERROR("SUBGHZ: Missing Custom preset");
                break;
            }
        }
        furi_string_set(preset_name, temp_str);
        size_t preset_index =
            subghz_setting_get_inx_preset_by_name(setting, furi_string_get_cstr(preset_name));
        subghz_txrx_set_preset(
            txrx,
            furi_string_get_cstr(preset_name),
            frequency,
            subghz_setting_get_preset_data(setting, preset_index),
            subghz_setting_get_preset_data_size(setting, preset_index));

        // Load Protocol
        if(!flipper_format_read_string(fff_data_file, "Protocol", protocol_name)) {
            FURI_LOG_E(TAG, "Missing protocol");
            ACTION_SET_ERROR("SUBGHZ: Missing protocol");
            break;
        }

        FlipperFormat* fff_data = subghz_txrx_get_fff_data(txrx);
        if(!strcmp(furi_string_get_cstr(protocol_name), "RAW")) {
            subghz_protocol_raw_gen_fff_data(
                fff_data, file_name, subghz_txrx_radio_device_get_name(txrx));
        } else {
            stream_copy_full(
                flipper_format_get_raw_stream(fff_data_file),
                flipper_format_get_raw_stream(fff_data));
        }

        if(subghz_txrx_load_decoder_by_name_protocol(txrx, furi_string_get_cstr(protocol_name))) {
            SubGhzProtocolStatus status =
                subghz_protocol_decoder_base_deserialize(subghz_txrx_get_decoder(txrx), fff_data);
            if(status != SubGhzProtocolStatusOk) {
                break;
            }
        } else {
            FURI_LOG_E(TAG, "Protocol not found: %s", furi_string_get_cstr(protocol_name));
            break;
        }
    } while(false);

    flipper_format_file_close(fff_data_file);
    flipper_format_free(fff_data_file);

    if(subghz_txrx_tx_start(txrx, subghz_txrx_get_fff_data(txrx)) != SubGhzTxRxStartTxStateOk) {
        FURI_LOG_E(TAG, "Failed to start TX");
    }

    bool skip_extra_stop = false;
    FURI_LOG_D(TAG, "Checking if file is RAW...");
    if(!strcmp(furi_string_get_cstr(protocol_name), "RAW")) {
        subghz_txrx_set_raw_file_encoder_worker_callback_end(
            txrx, action_subghz_raw_end_callback, app);
        app->raw_file_is_tx = true;
        skip_extra_stop = true;
    }
    do {
        furi_delay_ms(1);
    } while(app->raw_file_is_tx);

    if(!app->raw_file_is_tx && !skip_extra_stop) {
        // TODO: Should this be based on a Setting?
        furi_delay_ms(1500);
        subghz_txrx_stop(txrx);
    } else {
        // TODO: Should this be based on a Setting?
        furi_delay_ms(50);
        subghz_txrx_stop(txrx);
    }
    skip_extra_stop = false;

    FURI_LOG_I(TAG, "SUBGHZ: Action complete.");

    subghz_custom_btns_reset();

    subghz_txrx_free(txrx);
    furi_string_free(preset_name);
    furi_string_free(protocol_name);
    furi_string_free(temp_str);
}
