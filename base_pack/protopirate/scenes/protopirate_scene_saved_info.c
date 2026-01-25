// scenes/protopirate_scene_saved_info.c
#include "../protopirate_app_i.h"
#include "../helpers/protopirate_storage.h"

static void protopirate_scene_saved_info_widget_callback(
    GuiButtonType result,
    InputType type,
    void* context) {
    ProtoPirateApp* app = context;
    if(type == InputTypeShort) {
        if(result == GuiButtonTypeLeft) { // Emulate button
#ifdef ENABLE_EMULATE_FEATURE
            view_dispatcher_send_custom_event(
                app->view_dispatcher, ProtoPirateCustomEventSavedInfoEmulate);
#endif
        } else if(result == GuiButtonTypeRight) { // Delete button
            view_dispatcher_send_custom_event(
                app->view_dispatcher, ProtoPirateCustomEventSavedInfoDelete);
        }
    }
}

void protopirate_scene_saved_info_on_enter(void* context) {
    ProtoPirateApp* app = context;

    widget_reset(app->widget);

    if(app->loaded_file_path) {
        FlipperFormat* ff =
            protopirate_storage_load_file(furi_string_get_cstr(app->loaded_file_path));

        if(ff) {
            FuriString* info_str = furi_string_alloc();
            FuriString* temp_str = furi_string_alloc();
            uint32_t temp_data;

            // Protocol
            if(flipper_format_read_string(ff, "Protocol", temp_str)) {
                furi_string_cat_printf(info_str, "Protocol: %s\n", furi_string_get_cstr(temp_str));
            }

            // Frequency
            flipper_format_rewind(ff);
            if(flipper_format_read_uint32(ff, "Frequency", &temp_data, 1)) {
                furi_string_cat_printf(
                    info_str,
                    "Freq: %lu.%02lu MHz\n",
                    temp_data / 1000000,
                    (temp_data % 1000000) / 10000);
            }

            // Serial
            flipper_format_rewind(ff);
            if(flipper_format_read_uint32(ff, "Serial", &temp_data, 1)) {
                furi_string_cat_printf(info_str, "Serial: %08lX\n", temp_data);
            }

            // Button
            flipper_format_rewind(ff);
            if(flipper_format_read_uint32(ff, "Btn", &temp_data, 1)) {
                furi_string_cat_printf(info_str, "Button: %02X\n", (uint8_t)temp_data);
            }

            // Counter
            flipper_format_rewind(ff);
            if(flipper_format_read_uint32(ff, "Cnt", &temp_data, 1)) {
                furi_string_cat_printf(info_str, "Counter: %04lX\n", temp_data);
            }

            // BS
            flipper_format_rewind(ff);
            if(flipper_format_read_uint32(ff, "BS", &temp_data, 1)) {
                furi_string_cat_printf(info_str, "BS: %02X\n", (uint8_t)temp_data);
            }

            // BS Magic
            flipper_format_rewind(ff);
            if(flipper_format_read_uint32(ff, "BSMagic", &temp_data, 1)) {
                furi_string_cat_printf(info_str, "BS Magic: %02X\n", (uint8_t)temp_data);
            }

            // Protocol-specific fields
            flipper_format_rewind(ff);
            if(flipper_format_read_uint32(ff, "CRC", &temp_data, 1)) {
                furi_string_cat_printf(info_str, "CRC: %02X\n", (uint8_t)temp_data);
            }

            flipper_format_rewind(ff);
            if(flipper_format_read_uint32(ff, "Type", &temp_data, 1)) {
                furi_string_cat_printf(info_str, "Type: %02X\n", (uint8_t)temp_data);
            }

            flipper_format_rewind(ff);
            if(flipper_format_read_string(ff, "Manufacture", temp_str)) {
                furi_string_cat_printf(
                    info_str, "Manufacture: %s\n", furi_string_get_cstr(temp_str));
            }

            // Add text to the widget
            widget_add_text_scroll_element(
                app->widget, 0, 0, 128, 50, furi_string_get_cstr(info_str));

#ifdef ENABLE_EMULATE_FEATURE
            // Add buttons
            widget_add_button_element(
                app->widget,
                GuiButtonTypeLeft,
                "Emulate",
                protopirate_scene_saved_info_widget_callback,
                app);
#endif
            widget_add_button_element(
                app->widget,
                GuiButtonTypeRight,
                "Delete",
                protopirate_scene_saved_info_widget_callback,
                app);

            furi_string_free(temp_str);
            furi_string_free(info_str);
            protopirate_storage_close_file(ff);
        }
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, ProtoPirateViewWidget);
}

bool protopirate_scene_saved_info_on_event(void* context, SceneManagerEvent event) {
    ProtoPirateApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == ProtoPirateCustomEventSavedInfoDelete) {
            if(app->loaded_file_path) {
                protopirate_storage_delete_file(furi_string_get_cstr(app->loaded_file_path));
                scene_manager_previous_scene(app->scene_manager);
            }
            consumed = true;
        }
#ifdef ENABLE_EMULATE_FEATURE
        if(event.event == ProtoPirateCustomEventSavedInfoEmulate) {
            scene_manager_next_scene(app->scene_manager, ProtoPirateSceneEmulate);
            consumed = true;
        }
#endif
    }

    return consumed;
}

void protopirate_scene_saved_info_on_exit(void* context) {
    ProtoPirateApp* app = context;
    widget_reset(app->widget);
}
