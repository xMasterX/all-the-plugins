#pragma once

#include <gui/scene_manager.h>

/* Declare on_enter/on_event/on_exit for every scene listed in
 * morse_scene_config.h. The x-macro keeps the three handler arrays and the
 * scene enum permanently in sync. */
#define ADD_SCENE(name)                                                         \
    void morse_scene_##name##_on_enter(void* context);                          \
    bool morse_scene_##name##_on_event(void* context, SceneManagerEvent event); \
    void morse_scene_##name##_on_exit(void* context);
#include "morse_scene_config.h"
#undef ADD_SCENE

extern const SceneManagerHandlers morse_scene_handlers;
