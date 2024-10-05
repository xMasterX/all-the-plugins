#include "../nfc_eink_app_i.h"

void nfc_eink_scene_file_select_on_enter(void* context) {
    NfcEinkApp* instance = context;

    if(nfc_eink_load_from_file_select(instance)) {
        scene_manager_next_scene(instance->scene_manager, NfcEinkAppSceneScreenMenu);
    } else {
        scene_manager_previous_scene(instance->scene_manager);
    }
}

bool nfc_eink_scene_file_select_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    bool consumed = false;
    return consumed;
}

void nfc_eink_scene_file_select_on_exit(void* context) {
    UNUSED(context);
}
