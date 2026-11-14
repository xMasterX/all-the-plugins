#include "../nfc_magic_app_i.h"

void nfc_magic_scene_gen4_show_cfg_widget_callback(
    GuiButtonType result,
    InputType type,
    void* context) {
    NfcMagicApp* instance = context;

    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(instance->view_dispatcher, result);
    }
}

void nfc_magic_scene_gen4_show_cfg_on_enter(void* context) {
    NfcMagicApp* instance = context;
    Widget* widget = instance->widget;

    FuriString* output = furi_string_alloc();

    Gen4Config* config = &instance->gen4_data->config;
    size_t config_size = gen4_get_config_size_or_max(instance->gen4_data);

    // The dump can be an odd number of bytes long.
    for(size_t i = 0; i < config_size; i++) {
        if(i && !(i % 8)) furi_string_cat_printf(output, "\n");
        furi_string_cat_printf(output, "%02X", config->data_raw[i]);
        if(i % 2) furi_string_cat_printf(output, " ");
    }

    widget_add_string_element(widget, 3, 4, AlignLeft, AlignTop, FontPrimary, "Gen 4 Config");

    widget_add_text_scroll_element(widget, 3, 17, 124, 50, furi_string_get_cstr(output));

    furi_string_free(output);
    view_dispatcher_switch_to_view(instance->view_dispatcher, NfcMagicAppViewWidget);
}

bool nfc_magic_scene_gen4_show_cfg_on_event(void* context, SceneManagerEvent event) {
    NfcMagicApp* instance = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeBack) {
        consumed = scene_manager_search_and_switch_to_previous_scene(
            instance->scene_manager, NfcMagicSceneGen4ShowInfo);
    }
    return consumed;
}

void nfc_magic_scene_gen4_show_cfg_on_exit(void* context) {
    NfcMagicApp* instance = context;

    widget_reset(instance->widget);
}
