#include "morse_scene.h"
#include "../morse_trainer_i.h"

static const char* const on_off[] = {"OFF", "ON"};
static const char* const speed_names[] = {"Normal", "Relaxed"};
static const char* const visual_names[] = {"OFF", "Auto", "ON"};

enum {
    SettingsItemSound,
    SettingsItemVibro,
    SettingsItemSpeed,
    SettingsItemVisual,
    SettingsItemLed,
    SettingsItemReset,
};

/* Scene-local UI state for the two-press reset confirmation. */
static VariableItem* reset_item;
static bool reset_armed;

static void settings_sound_changed(VariableItem* item) {
    MorseTrainerApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, on_off[index]);
    app->settings.sound = index;
    if(index) morse_player_play_tone(app->player, MORSE_TONE_HZ, 80);
}

static void settings_vibro_changed(VariableItem* item) {
    MorseTrainerApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, on_off[index]);
    app->settings.vibro = index;
    if(index) {
        notification_message(app->notifications, &sequence_set_vibro_on);
        furi_delay_ms(60);
        notification_message(app->notifications, &sequence_reset_vibro);
    }
}

static void settings_enter_callback(void* context, uint32_t index) {
    MorseTrainerApp* app = context;
    if(index != SettingsItemReset) return;
    if(!reset_armed) {
        /* Destructive, so it takes a deliberate second OK press. */
        reset_armed = true;
        variable_item_set_current_value_text(reset_item, "sure?");
    } else {
        memset(&app->progress, 0, sizeof(MorseProgress));
        for(uint8_t m = 0; m < MODE_COUNT; m++)
            app->progress.unlocked[m] = 1;
        morse_progress_save(&app->progress);
        reset_armed = false;
        variable_item_set_current_value_text(reset_item, "done!");
    }
}

static void settings_speed_changed(VariableItem* item) {
    MorseTrainerApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, speed_names[index]);
    app->settings.farnsworth = index;
}

static void settings_visual_changed(VariableItem* item) {
    MorseTrainerApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, visual_names[index]);
    app->settings.visual = index;
}

static void settings_led_changed(VariableItem* item) {
    MorseTrainerApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, on_off[index]);
    app->settings.led = index;
    if(index) notification_message(app->notifications, &sequence_blink_green_10);
}

void morse_scene_settings_on_enter(void* context) {
    MorseTrainerApp* app = context;
    variable_item_list_reset(app->var_item_list);

    VariableItem* item;
    item = variable_item_list_add(app->var_item_list, "Sound", 2, settings_sound_changed, app);
    variable_item_set_current_value_index(item, app->settings.sound);
    variable_item_set_current_value_text(item, on_off[app->settings.sound]);

    item = variable_item_list_add(app->var_item_list, "Vibration", 2, settings_vibro_changed, app);
    variable_item_set_current_value_index(item, app->settings.vibro);
    variable_item_set_current_value_text(item, on_off[app->settings.vibro]);

    /* Relaxed = Farnsworth spacing: letters at full speed, longer pauses
     * between them. */
    item =
        variable_item_list_add(app->var_item_list, "Letter gaps", 2, settings_speed_changed, app);
    variable_item_set_current_value_index(item, app->settings.farnsworth);
    variable_item_set_current_value_text(item, speed_names[app->settings.farnsworth]);

    /* Auto = ./- patterns as training wheels on L1-L3 decode only;
     * ON = always visible; OFF = ear-only from the start. */
    item = variable_item_list_add(app->var_item_list, "Visuals", 3, settings_visual_changed, app);
    variable_item_set_current_value_index(item, app->settings.visual);
    variable_item_set_current_value_text(item, visual_names[app->settings.visual]);

    item = variable_item_list_add(app->var_item_list, "LED", 2, settings_led_changed, app);
    variable_item_set_current_value_index(item, app->settings.led);
    variable_item_set_current_value_text(item, on_off[app->settings.led]);

    reset_armed = false;
    reset_item = variable_item_list_add(app->var_item_list, "Reset progress", 1, NULL, NULL);
    variable_item_set_current_value_text(reset_item, "OK");
    variable_item_list_set_enter_callback(app->var_item_list, settings_enter_callback, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, MorseViewVarItemList);
}

bool morse_scene_settings_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false; /* Back pops to the menu; settings persist in on_exit */
}

void morse_scene_settings_on_exit(void* context) {
    MorseTrainerApp* app = context;
    morse_settings_save(&app->settings);
    variable_item_list_reset(app->var_item_list);
}
