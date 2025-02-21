#include "../metroflip_i.h"

void metroflip_scene_start_submenu_callback(void* context, uint32_t index) {
    Metroflip* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void metroflip_scene_start_on_enter(void* context) {
    Metroflip* app = context;
    Submenu* submenu = app->submenu;

    submenu_set_header(submenu, "Metroflip");

    submenu_add_item(
        submenu, "Scan Card", MetroflipSceneAuto, metroflip_scene_start_submenu_callback, app);

    submenu_add_item(
        submenu,
        "OV-Chipkaart (unstable)",
        MetroflipSceneOVC,
        metroflip_scene_start_submenu_callback,
        app);

    submenu_add_item(
        submenu, "Saved", MetroflipSceneLoad, metroflip_scene_start_submenu_callback, app);

    submenu_add_item(
        submenu,
        "Supported Cards",
        MetroflipSceneSupported,
        metroflip_scene_start_submenu_callback,
        app);

    submenu_add_item(
        submenu, "About", MetroflipSceneAbout, metroflip_scene_start_submenu_callback, app);

    submenu_add_item(
        submenu, "Credits", MetroflipSceneCredits, metroflip_scene_start_submenu_callback, app);

    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(app->scene_manager, MetroflipSceneStart));

    view_dispatcher_switch_to_view(app->view_dispatcher, MetroflipViewSubmenu);
}

bool metroflip_scene_start_on_event(void* context, SceneManagerEvent event) {
    Metroflip* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        scene_manager_set_scene_state(app->scene_manager, MetroflipSceneStart, event.event);
        consumed = true;
        scene_manager_next_scene(app->scene_manager, event.event);
    }

    return consumed;
}

void metroflip_scene_start_on_exit(void* context) {
    Metroflip* app = context;
    submenu_reset(app->submenu);
}
