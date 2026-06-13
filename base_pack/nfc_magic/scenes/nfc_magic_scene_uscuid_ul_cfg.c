#include "../nfc_magic_app_i.h"

void nfc_magic_scene_uscuid_ul_cfg_on_enter(void* context) {
    NfcMagicApp* instance = context;
    Widget* widget = instance->widget;

    widget_add_string_element(widget, 0, 0, AlignLeft, AlignTop, FontPrimary, "Config");

    // Raw 16-byte config page, 4 bytes per line with offset. (Per-byte meaning: TODO.)
    FuriString* message = furi_string_alloc();
    for(size_t i = 0; i < USCUID_UL_CONFIG_SIZE; i += 4) {
        const uint8_t* c = &instance->uscuid_ul_data.config[i];
        furi_string_cat_printf(
            message, "%02X: %02X %02X %02X %02X\n", (unsigned)i, c[0], c[1], c[2], c[3]);
    }
    widget_add_text_box_element(
        widget, 0, 14, 128, 50, AlignLeft, AlignTop, furi_string_get_cstr(message), false);
    furi_string_free(message);

    view_dispatcher_switch_to_view(instance->view_dispatcher, NfcMagicAppViewWidget);
}

bool nfc_magic_scene_uscuid_ul_cfg_on_event(void* context, SceneManagerEvent event) {
    NfcMagicApp* instance = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeBack) {
        consumed = scene_manager_previous_scene(instance->scene_manager);
    }
    return consumed;
}

void nfc_magic_scene_uscuid_ul_cfg_on_exit(void* context) {
    NfcMagicApp* instance = context;
    widget_reset(instance->widget);
}
