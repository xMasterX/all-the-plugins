#pragma once

#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>

#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>

#include <notification/notification_messages.h>
#include <infrared/worker/infrared_worker.h>


#include "scenes/lidaremulator_scene.h"

#include "view_hijacker.h"

struct LidarEmulatorApp {
    SceneManager* scene_manager;
    ViewDispatcher* view_dispatcher;

    Gui* gui;

    Submenu* submenu;

    ViewInputCallback tmp_input_callback;

    ViewHijacker* view_hijacker;
};

typedef struct LidarEmulatorApp LidarEmulatorApp;


//Enumeration of all used view types.
typedef enum {
    LidarEmulatorViewSubmenu,
} LidarEmulatorView;

