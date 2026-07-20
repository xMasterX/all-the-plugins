#include "morse_scene.h"
#include "../morse_trainer_i.h"

static const char* const about_text = "Morse Trainer\n"
                                      "Learn Morse from zero!\n"
                                      "\n"
                                      "= Encoding =\n"
                                      "OK short press: dot\n"
                                      "OK long press: dash\n"
                                      "Bar shows the dot/dash\n"
                                      "boundary while held.\n"
                                      "Pause commits letter\n"
                                      "Left: clear attempt\n"
                                      "Up: letter hint\n"
                                      "\n"
                                      "= Decoding =\n"
                                      "Listen to the beeps.\n"
                                      "Left/Right: pick answer\n"
                                      "(selection wraps around)\n"
                                      "OK: confirm\n"
                                      "Up: replay sound\n"
                                      "Down: hint (50/50)\n"
                                      "\n"
                                      "Hints: 2 per level\n"
                                      "\n"
                                      "= Levels =\n"
                                      "80% passes the level.\n"
                                      "80%=* 90%=** 100%=***\n"
                                      "Challenges: 3 words,\n"
                                      "max 3 mistakes.\n"
                                      "\n"
                                      "= Practice =\n"
                                      "Free keying sandbox.\n"
                                      "Keyed letters decode\n"
                                      "on screen. Up: hear it.\n"
                                      "\n"
                                      "Back x2: quit a level\n";

void morse_scene_about_on_enter(void* context) {
    MorseTrainerApp* app = context;
    widget_reset(app->widget);
    widget_add_text_scroll_element(app->widget, 0, 0, 128, 64, about_text);
    view_dispatcher_switch_to_view(app->view_dispatcher, MorseViewWidget);
}

bool morse_scene_about_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void morse_scene_about_on_exit(void* context) {
    MorseTrainerApp* app = context;
    widget_reset(app->widget);
}
