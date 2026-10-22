#include "../t5577_multiwriter_i.h"
#include <dolphin/dolphin.h>

typedef enum {
    SubmenuIndexCheck,
    SubmenuIndexWriteFirstKey,
    SubmenuIndexWriteSecondKey,
    SubmenuIndexWriteThirdKey,
    SubmenuIndexTest,
    SubmenuIndexPassword,
} SubmenuIndex;

static void t5577_multiwriter_scene_start_submenu_callback(void* context, uint32_t index) {
    LfRfid* app = context;

    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void t5577_multiwriter_scene_start_on_enter(void* context) {
    LfRfid* app = context;
    Submenu* submenu = app->submenu;

    submenu_add_item(
        submenu,
        "Check tag",
        SubmenuIndexCheck,
        t5577_multiwriter_scene_start_submenu_callback,
        app);
    submenu_add_item(
        submenu,
        "Write first key",
        SubmenuIndexWriteFirstKey,
        t5577_multiwriter_scene_start_submenu_callback,
        app);
    submenu_add_item(
        submenu,
        "Write second key",
        SubmenuIndexWriteSecondKey,
        t5577_multiwriter_scene_start_submenu_callback,
        app);
    submenu_add_item(
        submenu,
        "Write third key",
        SubmenuIndexWriteThirdKey,
        t5577_multiwriter_scene_start_submenu_callback,
        app);
    submenu_add_item(
        submenu, "Test tag", SubmenuIndexTest, t5577_multiwriter_scene_start_submenu_callback, app);
    submenu_add_item(
        submenu,
        "Password",
        SubmenuIndexPassword,
        t5577_multiwriter_scene_start_submenu_callback,
        app);

    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(app->scene_manager, LfRfidSceneStart));

    view_dispatcher_switch_to_view(app->view_dispatcher, LfRfidViewSubmenu);
}

bool t5577_multiwriter_scene_start_on_event(void* context, SceneManagerEvent event) {
    LfRfid* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;

    scene_manager_set_scene_state(app->scene_manager, LfRfidSceneStart, event.event);

    switch(event.event) {
    case SubmenuIndexCheck:
        scene_manager_next_scene(app->scene_manager, LfRfidSceneCheck);
        dolphin_deed(DolphinDeedRfidRead);
        return true;
    case SubmenuIndexWriteFirstKey:
    case SubmenuIndexWriteSecondKey:
    case SubmenuIndexWriteThirdKey:
        app->slot = event.event - SubmenuIndexWriteFirstKey;
        scene_manager_next_scene(app->scene_manager, LfRfidSceneSelectKey);
        return true;
    case SubmenuIndexTest:
        scene_manager_next_scene(app->scene_manager, LfRfidSceneTest);
        dolphin_deed(DolphinDeedRfidRead);
        return true;
    case SubmenuIndexPassword:
        scene_manager_next_scene(app->scene_manager, LfRfidScenePasswordMenu);
        return true;
    default:
        return false;
    }
}

void t5577_multiwriter_scene_start_on_exit(void* context) {
    LfRfid* app = context;

    submenu_reset(app->submenu);
}
