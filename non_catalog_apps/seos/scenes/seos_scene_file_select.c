#include "../seos_i.h"
#include "../seos_emulator.h"

void seos_scene_file_select_on_enter(void* context) {
    Seos* seos = context;
    SeosEmulator* seos_emulator = seos->seos_emulator;
    // Process file_select return
    seos_emulator_set_loading_callback(seos_emulator, seos_show_loading_popup, seos);
    if(seos_emulator_file_select(seos_emulator)) {
        seos->flow_mode = FLOW_CRED;
        scene_manager_next_scene(seos->scene_manager, SeosSceneSavedMenu);
    } else {
        scene_manager_search_and_switch_to_previous_scene(seos->scene_manager, SeosSceneMainMenu);
    }
    seos_emulator_set_loading_callback(seos_emulator, NULL, seos);
}

bool seos_scene_file_select_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void seos_scene_file_select_on_exit(void* context) {
    UNUSED(context);
}
