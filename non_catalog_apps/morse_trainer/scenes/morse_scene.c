#include "morse_scene.h"
#include "../morse_trainer_i.h"

#define ADD_SCENE(name) morse_scene_##name##_on_enter,
static void (*const on_enter_handlers[])(void*) = {
#include "morse_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(name) morse_scene_##name##_on_event,
static bool (*const on_event_handlers[])(void*, SceneManagerEvent) = {
#include "morse_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(name) morse_scene_##name##_on_exit,
static void (*const on_exit_handlers[])(void*) = {
#include "morse_scene_config.h"
};
#undef ADD_SCENE

const SceneManagerHandlers morse_scene_handlers = {
    .on_enter_handlers = on_enter_handlers,
    .on_event_handlers = on_event_handlers,
    .on_exit_handlers = on_exit_handlers,
    .scene_num = MorseSceneCount,
};
