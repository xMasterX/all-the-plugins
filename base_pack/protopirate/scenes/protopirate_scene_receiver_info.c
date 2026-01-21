// scenes/protopirate_scene_receiver_info.c
#include "../protopirate_app_i.h"
#include "../helpers/protopirate_storage.h"

#define TAG "ProtoPirateReceiverInfo"

static void protopirate_scene_receiver_info_widget_callback(
    GuiButtonType result,
    InputType type,
    void* context) {
    ProtoPirateApp* app = context;
    if(type == InputTypeShort) {
        if(result == GuiButtonTypeRight) {
            view_dispatcher_send_custom_event(
                app->view_dispatcher, ProtoPirateCustomEventReceiverInfoSave);
        }
#ifdef ENABLE_EMULATE_FEATURE
        else if(result == GuiButtonTypeLeft) {
            view_dispatcher_send_custom_event(
                app->view_dispatcher, ProtoPirateCustomEventReceiverInfoEmulate);
        }
#endif
    }
}

void protopirate_scene_receiver_info_on_enter(void* context) {
    furi_assert(context);
    ProtoPirateApp* app = context;

    widget_reset(app->widget);

    FuriString* text;
    text = furi_string_alloc();

    protopirate_history_get_text_item_menu(app->txrx->history, text, app->txrx->idx_menu_chosen);

    widget_add_string_element(
        app->widget, 64, 0, AlignCenter, AlignTop, FontPrimary, furi_string_get_cstr(text));

    furi_string_reset(text);
    protopirate_history_get_text_item(app->txrx->history, text, app->txrx->idx_menu_chosen);

    // Skip the first line (protocol name + Xbits) since it's already shown as the title
    const char* text_str = furi_string_get_cstr(text);
    const char* first_newline = strchr(text_str, '\r');
    if(first_newline) {
        // Skip \r\n
        text_str = first_newline + 1;
        if(*text_str == '\n') {
            text_str++;
        }
    } else {
        // Try \n only
        first_newline = strchr(text_str, '\n');
        if(first_newline) {
            text_str = first_newline + 1;
        }
    }

    widget_add_string_multiline_element(
        app->widget, 0, 11, AlignLeft, AlignTop, FontSecondary, text_str);

#ifdef ENABLE_EMULATE_FEATURE
    // Add emulate button on the left
    widget_add_button_element(
        app->widget,
        GuiButtonTypeLeft,
        "Emulate",
        protopirate_scene_receiver_info_widget_callback,
        app);
#endif

    // Add save button on the right
    widget_add_button_element(
        app->widget,
        GuiButtonTypeRight,
        "Save",
        protopirate_scene_receiver_info_widget_callback,
        app);

    furi_string_free(text);

    view_dispatcher_switch_to_view(app->view_dispatcher, ProtoPirateViewWidget);
}

bool protopirate_scene_receiver_info_on_event(void* context, SceneManagerEvent event) {
    ProtoPirateApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == ProtoPirateCustomEventReceiverInfoSave) {
            // Get the flipper format from history
            FlipperFormat* ff =
                protopirate_history_get_raw_data(app->txrx->history, app->txrx->idx_menu_chosen);

            if(ff) {
                // Extract protocol name
                FuriString* protocol = furi_string_alloc();
                flipper_format_rewind(ff);
                if(!flipper_format_read_string(ff, "Protocol", protocol)) {
                    furi_string_set_str(protocol, "Unknown");
                }

                FuriString* saved_path = furi_string_alloc();
                if(protopirate_storage_save_capture(
                       ff, furi_string_get_cstr(protocol), saved_path)) {
                    // Show success notification
                    notification_message(app->notifications, &sequence_success);
                    FURI_LOG_I(TAG, "Saved to: %s", furi_string_get_cstr(saved_path));
                } else {
                    notification_message(app->notifications, &sequence_error);
                    FURI_LOG_E(TAG, "Save failed");
                }

                furi_string_free(protocol);
                furi_string_free(saved_path);
            }
            consumed = true;
        }
#ifdef ENABLE_EMULATE_FEATURE
        else if(event.event == ProtoPirateCustomEventReceiverInfoEmulate) {
            // Get the flipper format from history
            FlipperFormat* ff =
                protopirate_history_get_raw_data(app->txrx->history, app->txrx->idx_menu_chosen);

            if(ff) {
                // Save to temp file (will be deleted on emulate exit)
                if(protopirate_storage_save_temp(ff)) {
                    FURI_LOG_I(TAG, "Saved temp for emulate");
                    
                    // Set the temp file path for emulate scene
                    if(app->loaded_file_path) {
                        furi_string_free(app->loaded_file_path);
                    }
                    app->loaded_file_path = furi_string_alloc_set_str(PROTOPIRATE_TEMP_FILE);
                    
                    // Go to emulate scene
                    scene_manager_next_scene(app->scene_manager, ProtoPirateSceneEmulate);
                } else {
                    notification_message(app->notifications, &sequence_error);
                    FURI_LOG_E(TAG, "Failed to save temp for emulate");
                }
            } else {
                FURI_LOG_E(TAG, "No flipper format data for index %d", app->txrx->idx_menu_chosen);
                notification_message(app->notifications, &sequence_error);
            }
            consumed = true;
        }
#endif
    }

    return consumed;
}

void protopirate_scene_receiver_info_on_exit(void* context) {
    furi_assert(context);
    ProtoPirateApp* app = context;
    widget_reset(app->widget);
}