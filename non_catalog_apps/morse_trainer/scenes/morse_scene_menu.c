#include "morse_scene.h"
#include "../morse_trainer_i.h"

typedef enum {
    MenuItemEncoding, /* == TrainModeEncoding */
    MenuItemDecoding,
    MenuItemMixed,
    MenuItemPractice,
    MenuItemProgress,
    MenuItemSettings,
    MenuItemAbout,
} MenuItem;

static void menu_callback(void* context, uint32_t index) {
    MorseTrainerApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void morse_scene_menu_on_enter(void* context) {
    MorseTrainerApp* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Morse Trainer");
    submenu_add_item(app->submenu, "Encoding", MenuItemEncoding, menu_callback, app);
    submenu_add_item(app->submenu, "Decoding", MenuItemDecoding, menu_callback, app);
    submenu_add_item(app->submenu, "Mixed", MenuItemMixed, menu_callback, app);
    submenu_add_item(app->submenu, "Practice", MenuItemPractice, menu_callback, app);
    submenu_add_item(app->submenu, "Progress", MenuItemProgress, menu_callback, app);
    submenu_add_item(app->submenu, "Settings", MenuItemSettings, menu_callback, app);
    submenu_add_item(app->submenu, "About", MenuItemAbout, menu_callback, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, MorseViewSubmenu);
}

bool morse_scene_menu_on_event(void* context, SceneManagerEvent event) {
    MorseTrainerApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    switch(event.event) {
    case MenuItemEncoding:
    case MenuItemDecoding:
    case MenuItemMixed:
        app->mode = event.event;
        scene_manager_next_scene(app->scene_manager, MorseSceneLevelSelect);
        return true;
    case MenuItemPractice:
        scene_manager_next_scene(app->scene_manager, MorseScenePractice);
        return true;
    case MenuItemProgress:
        scene_manager_next_scene(app->scene_manager, MorseSceneProgress);
        return true;
    case MenuItemSettings:
        scene_manager_next_scene(app->scene_manager, MorseSceneSettings);
        return true;
    case MenuItemAbout:
        scene_manager_next_scene(app->scene_manager, MorseSceneAbout);
        return true;
    }
    return false;
}

void morse_scene_menu_on_exit(void* context) {
    MorseTrainerApp* app = context;
    submenu_reset(app->submenu);
}
