#include "../t5577_multiwriter_i.h"
#include <bit_lib/bit_lib.h>

static void t5577_multiwriter_scene_password_input_callback(void* context) {
    LfRfid* app = context;

    view_dispatcher_send_custom_event(app->view_dispatcher, LfRfidEventNext);
}

void t5577_multiwriter_scene_password_input_on_enter(void* context) {
    LfRfid* app = context;

    bit_lib_num_to_bytes_be(app->password, T5577_MW_PASSWORD_SIZE, app->password_bytes);

    byte_input_set_header_text(app->byte_input, "Password of the tag");
    byte_input_set_result_callback(
        app->byte_input,
        t5577_multiwriter_scene_password_input_callback,
        NULL,
        app,
        app->password_bytes,
        T5577_MW_PASSWORD_SIZE);

    view_dispatcher_switch_to_view(app->view_dispatcher, LfRfidViewByteInput);
}

bool t5577_multiwriter_scene_password_input_on_event(void* context, SceneManagerEvent event) {
    LfRfid* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event != LfRfidEventNext) return false;

    const uint32_t word = bit_lib_bytes_to_num_be(app->password_bytes, T5577_MW_PASSWORD_SIZE);
    const bool trying =
        scene_manager_get_scene_state(app->scene_manager, LfRfidScenePasswordInput) != 0;

    if(!trying) {
        app->password = word;
        app->password_set = true;
        app->password_confirmed = false;
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }

    // The warning belongs here, with the word already settled and the tag about to be
    // addressed: a password frame is the same length as a write, so a tag that turns out to
    // have no password takes this one into block 0 instead of answering with it.
    if(!t5577_multiwriter_ask(
           app,
           "Try this password?",
           "If the tag has none,\nthis frame overwrites\nits block 0",
           "Try")) {
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, LfRfidSceneStart);
        return true;
    }

    app->password = word;
    app->password_set = true;
    app->password_confirmed = false;
    app->password_try = true;

    scene_manager_search_and_switch_to_previous_scene(app->scene_manager, LfRfidSceneCheck);

    return true;
}

void t5577_multiwriter_scene_password_input_on_exit(void* context) {
    LfRfid* app = context;

    // Spent either way. A screen left by Back must not send the next check off with a word the
    // user walked away from.
    scene_manager_set_scene_state(app->scene_manager, LfRfidScenePasswordInput, 0);

    byte_input_set_result_callback(app->byte_input, NULL, NULL, NULL, NULL, 0);
    byte_input_set_header_text(app->byte_input, "");
}
