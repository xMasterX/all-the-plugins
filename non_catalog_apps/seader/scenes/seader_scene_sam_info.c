#include "../seader_i.h"
#include <dolphin/dolphin.h>

#define TAG "SeaderSamInfoScene"

void seader_scene_sam_info_widget_callback(GuiButtonType result, InputType type, void* context) {
    Seader* seader = context;
    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(seader->view_dispatcher, result);
    }
}

void seader_scene_sam_info_on_enter(void* context) {
    Seader* seader = context;
    SeaderWorker* seader_worker = seader->worker;
    Widget* widget = seader->widget;

    // Use reusable string instead of allocating new one
    FuriString* fw_str = seader->temp_string1;

    furi_string_cat_printf(
        fw_str, "FW %d.%d", seader_worker->sam_version[0], seader_worker->sam_version[1]);

    widget_add_button_element(
        seader->widget, GuiButtonTypeLeft, "Back", seader_scene_sam_info_widget_callback, seader);

    widget_add_string_element(
        widget, 64, 5, AlignCenter, AlignCenter, FontSecondary, furi_string_get_cstr(fw_str));

    // No need to free fw_str as it's reused from seader struct

    view_dispatcher_switch_to_view(seader->view_dispatcher, SeaderViewWidget);
}

bool seader_scene_sam_info_on_event(void* context, SceneManagerEvent event) {
    Seader* seader = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == GuiButtonTypeLeft) {
            consumed = scene_manager_previous_scene(seader->scene_manager);
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        consumed = scene_manager_search_and_switch_to_previous_scene(
            seader->scene_manager, SeaderSceneSamPresent);
    }
    return consumed;
}

void seader_scene_sam_info_on_exit(void* context) {
    Seader* seader = context;

    // Clear views
    widget_reset(seader->widget);
}
