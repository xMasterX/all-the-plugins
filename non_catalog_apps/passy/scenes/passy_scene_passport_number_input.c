#include "../passy_i.h"
#include <gui/modules/validators.h>

#define PASSY_APP_FILE_PREFIX "Passy"

void passy_scene_passport_number_input_text_input_callback(void* context) {
    Passy* passy = context;

    view_dispatcher_send_custom_event(passy->view_dispatcher, PassyCustomEventTextInputDone);
}

void passy_scene_passport_number_input_on_enter(void* context) {
    Passy* passy = context;

    // Setup view
    TextInput* text_input = passy->text_input;

    text_input_set_header_text(text_input, "Passport Number");
    text_input_set_minimum_length(text_input, 9);
    if(passy->passport_number[0] != '\0') {
        strlcpy(passy->text_store, passy->passport_number, sizeof(passy->text_store));
    }
    text_input_set_result_callback(
        text_input,
        passy_scene_passport_number_input_text_input_callback,
        passy,
        passy->text_store,
        sizeof(passy->text_store),
        false);

    view_dispatcher_switch_to_view(passy->view_dispatcher, PassyViewTextInput);
}

bool passy_scene_passport_number_input_on_event(void* context, SceneManagerEvent event) {
    Passy* passy = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == PassyCustomEventTextInputDone) {
            strlcpy(passy->passport_number, passy->text_store, strlen(passy->text_store) + 1);
            scene_manager_next_scene(passy->scene_manager, PassySceneDoBInput);
            consumed = true;
        }
    }
    return consumed;
}

void passy_scene_passport_number_input_on_exit(void* context) {
    Passy* passy = context;

    // Clear view
    memset(passy->text_store, 0, sizeof(passy->text_store));
    text_input_reset(passy->text_input);
}
