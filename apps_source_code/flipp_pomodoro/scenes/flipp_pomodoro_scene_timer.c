#include <furi.h>
#include <gui/scene_manager.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include "flipp_pomodoro_scene.h"
#include "../flipp_pomodoro_app.h"
#include "../views/flipp_pomodoro_timer_view.h"
#include "../modules/flipp_pomodoro_settings.h"
#include "../modules/flipp_pomodoro.h"
#include "../helpers/notifications.h"
#include "../helpers/time.h"
#include "../helpers/hints.h"
#include <notification/notification.h>

enum {
    SceneEventConusmed = true,
    SceneEventNotConusmed = false
};

static void send_notification_sequence(const NotificationSequence* seq);

static char* random_string_of_list(char** hints, size_t num_hints) {
    int random_index = rand() % num_hints;
    return hints[random_index];
}

// anti duplicates
static bool g_stage_complete_sent = false; // StageComplete sent only once
static bool g_notification_started = false; // track first notification dispatch per stage
static uint8_t g_notification_repeats_left = 0; // generic counter for repeated modes

// Do not duplicate notifications while the previous one is playing.
// Rough estimate of the duration of the standard sequence ~2.5–3s -> we take 3s.
static uint32_t g_notification_cooldown_until =
    0; // timestamp, before which we do not send the next one
static bool g_flash_backlight_on = false;

// In Slide, notification at the start of the NEXT stage - we simulate it at the stop in Once/Naggy.
// REUSE the standard sequences stage_start_notification_sequence_map[*].
static void notify_like_slide_next_stage(FlippPomodoroApp* app) {
    const uint32_t now = time_now();
    if(now < g_notification_cooldown_until) {
        return; // don't put a new one in the queue while the previous one is still playing
    }

    PomodoroStage cur = flipp_pomodoro__get_stage(app->state);
    uint8_t pos = app->state->current_stage_index % 8;
    PomodoroStage next;
    if(cur == FlippPomodoroStageFocus) {
        next = (pos == 6) ? FlippPomodoroStageLongBreak : FlippPomodoroStageRest;
    } else {
        next = FlippPomodoroStageFocus;
    }
    const NotificationSequence* seq = stage_start_notification_sequence_map[next];

    send_notification_sequence(seq);

    // block repetitions for the duration of the standard sequence playback
    g_notification_cooldown_until = now + 3;
}

// Instantly mute sound/vibration (without inventing new patterns - only off-messages).
static void stop_all_notifications(void) {
    NotificationApp* n = furi_record_open(RECORD_NOTIFICATION);
    static const NotificationSequence seq_stop = {
        &message_sound_off,
        &message_vibro_off,
        &message_green_0,
        &message_red_0,
        &message_display_backlight_on,
        NULL,
    };
    notification_message(n, &seq_stop);
    furi_record_close(RECORD_NOTIFICATION);
}

// Complete stop of any repetitive notification pattern + jam
static void notification_stop(void) {
    g_notification_repeats_left = 0;
    // a small "umbrella" so that the tick that comes immediately after the stop does not have time to put a new one
    g_notification_cooldown_until = time_now() + 1;
    g_notification_started = false;
    g_flash_backlight_on = false;
    stop_all_notifications();
}

static void send_notification_sequence(const NotificationSequence* seq) {
    if(!seq) {
        return;
    }
    NotificationApp* n = furi_record_open(RECORD_NOTIFICATION);
    notification_message(n, seq);
    furi_record_close(RECORD_NOTIFICATION);
}

static const NotificationSequence seq_flash_on = {
    &message_display_backlight_on,
    &message_green_255,
    NULL,
};

static const NotificationSequence seq_flash_off = {
    &message_display_backlight_off,
    &message_green_0,
    NULL,
};

static const NotificationSequence seq_vibrate = {
    &message_vibro_on,
    &message_delay_250,
    &message_vibro_off,
    NULL,
};

static const NotificationSequence seq_beep_soft = {
    &message_display_backlight_on,
    &message_note_d5,
    &message_delay_250,
    &message_sound_off,
    NULL,
};

static const NotificationSequence seq_beep_loud = {
    &message_display_backlight_on,
    &message_note_d5,
    &message_delay_250,
    &message_note_b5,
    &message_delay_250,
    &message_note_d5,
    &message_delay_250,
    &message_sound_off,
    NULL,
};

static void toggle_flash_pattern(void) {
    send_notification_sequence(g_flash_backlight_on ? &seq_flash_off : &seq_flash_on);
    g_flash_backlight_on = !g_flash_backlight_on;
}

void flipp_pomodoro_scene_timer_sync_view_state(void* ctx) {
    furi_assert(ctx);

    FlippPomodoroApp* app = ctx;

    flipp_pomodoro_view_timer_set_state(
        flipp_pomodoro_view_timer_get_view(app->timer_view), app->state);
}

void flipp_pomodoro_scene_timer_on_next_stage(void* ctx) {
    furi_assert(ctx);
    FlippPomodoroApp* app = ctx;
    notification_stop(); // stop "spam" when moving to the next timer
    const bool expired = flipp_pomodoro__is_stage_expired(app->state);
    view_dispatcher_send_custom_event(
        app->view_dispatcher,
        expired ? FlippPomodoroAppCustomEventStageComplete : FlippPomodoroAppCustomEventStageSkip);
}

void flipp_pomodoro_scene_timer_on_left(void* ctx) {
    FlippPomodoroApp* app = ctx;
    notification_stop(); // stop "spam" when going to settings
    scene_manager_next_scene(app->scene_manager, FlippPomodoroSceneConfig);
}

void flipp_pomodoro_scene_timer_on_ask_hint(void* ctx) {
    FlippPomodoroApp* app = ctx;
    notification_stop(); // stop spam on center click
    view_dispatcher_send_custom_event(
        app->view_dispatcher, FlippPomodoroAppCustomEventTimerAskHint);
}

void flipp_pomodoro_scene_timer_on_enter(void* ctx) {
    furi_assert(ctx);

    FlippPomodoroApp* app = ctx;
    g_stage_complete_sent = false;
    g_notification_started = false;
    g_notification_repeats_left = 0;
    g_notification_cooldown_until = 0;
    g_flash_backlight_on = false;

    // If the stage has already expired:
    // - Slide: start over (as before);
    // - Once/Naggy: stay at 00:00 and DO NOT re-notify after returning.
    if(flipp_pomodoro__is_stage_expired(app->state)) {
        FlippPomodoroSettings s;
        flipp_pomodoro_settings_load(&s);
        if(s.buzz_mode == FlippPomodoroBuzzSlide) {
            flipp_pomodoro__destroy(app->state);
            app->state = flipp_pomodoro__new();
        } else {
            notification_stop();
            g_notification_started =
                true; // prohibition of repeated start of notifications on the first tick
        }
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, FlippPomodoroAppViewTimer);
    flipp_pomodoro_scene_timer_sync_view_state(app);

    flipp_pomodoro_view_timer_set_callback_context(app->timer_view, app);

    // hint here
    flipp_pomodoro_view_timer_set_on_ok_cb(
        app->timer_view, flipp_pomodoro_scene_timer_on_ask_hint);

    flipp_pomodoro_view_timer_set_on_right_cb(
        app->timer_view, flipp_pomodoro_scene_timer_on_next_stage);

    flipp_pomodoro_view_timer_set_on_left_cb(app->timer_view, flipp_pomodoro_scene_timer_on_left);
}

char* flipp_pomodoro_scene_timer_get_contextual_hint(FlippPomodoroApp* app) {
    switch(flipp_pomodoro__get_stage(app->state)) {
    case FlippPomodoroStageFocus:
        return random_string_of_list(work_hints, sizeof(work_hints) / sizeof(work_hints[0]));
    case FlippPomodoroStageRest:
    case FlippPomodoroStageLongBreak:
        return random_string_of_list(break_hints, BREAK_HINTS_COUNT);
    default:
        return "What's up?";
    }
}

static void handle_buzz_slide(FlippPomodoroApp* app) {
    if(!g_stage_complete_sent) {
        g_stage_complete_sent = true;
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FlippPomodoroAppCustomEventStageComplete);
    }
}

static void handle_buzz_once(FlippPomodoroApp* app) {
    if(!g_notification_started) {
        g_notification_started = true;
        notify_like_slide_next_stage(app);
    }
}

static void handle_buzz_annoying(FlippPomodoroApp* app) {
    if(!g_notification_started) {
        g_notification_started = true;
        g_notification_repeats_left = 10;
    }
    if(g_notification_repeats_left > 0) {
        notify_like_slide_next_stage(app);
        g_notification_repeats_left--;
        if(g_notification_repeats_left == 0) {
            stop_all_notifications();
        }
    }
}

static void handle_buzz_flash(FlippPomodoroApp* app) {
    UNUSED(app);
    const uint32_t now = time_now();
    if(now < g_notification_cooldown_until) {
        return;
    }
    toggle_flash_pattern();
    g_notification_started = true;
    g_notification_cooldown_until = now + 1;
}

static void handle_buzz_vibrate(FlippPomodoroApp* app) {
    UNUSED(app);
    const uint32_t now = time_now();
    if(now < g_notification_cooldown_until) {
        return;
    }
    send_notification_sequence(&seq_vibrate);
    toggle_flash_pattern();
    g_notification_started = true;
    g_notification_cooldown_until = now + 1;
}

static void handle_buzz_soft_beep(FlippPomodoroApp* app) {
    UNUSED(app);
    const uint32_t now = time_now();
    if(now < g_notification_cooldown_until) {
        return;
    }
    send_notification_sequence(&seq_beep_soft);
    g_notification_started = true;
    g_notification_cooldown_until = now + 2;
}

static void handle_buzz_loud_beep(FlippPomodoroApp* app) {
    UNUSED(app);
    const uint32_t now = time_now();
    if(now < g_notification_cooldown_until) {
        return;
    }
    send_notification_sequence(&seq_beep_loud);
    g_notification_started = true;
    g_notification_cooldown_until = now + 3;
}

static void reset_notification_flags(void) {
    g_stage_complete_sent = false;
    g_notification_started = false;
    g_notification_repeats_left = 0;
    g_notification_cooldown_until = 0;
    g_flash_backlight_on = false;
}

void flipp_pomodoro_scene_timer_handle_custom_event(
    FlippPomodoroApp* app,
    FlippPomodoroAppCustomEvent custom_event) {
    switch(custom_event) {
    case FlippPomodoroAppCustomEventTimerTick: {
        if(!flipp_pomodoro__is_stage_expired(app->state)) {
            reset_notification_flags();
            break;
        }
        FlippPomodoroSettings s;
        flipp_pomodoro_settings_load(&s);
        switch(s.buzz_mode) {
        case FlippPomodoroBuzzSlide:
            handle_buzz_slide(app);
            break;
        case FlippPomodoroBuzzOnce:
            handle_buzz_once(app);
            break;
        case FlippPomodoroBuzzAnnoying:
            handle_buzz_annoying(app);
            break;
        case FlippPomodoroBuzzFlash:
            handle_buzz_flash(app);
            break;
        case FlippPomodoroBuzzVibrate:
            handle_buzz_vibrate(app);
            break;
        case FlippPomodoroBuzzSoftBeep:
            handle_buzz_soft_beep(app);
            break;
        case FlippPomodoroBuzzLoudBeep:
            handle_buzz_loud_beep(app);
            break;
        default:
            handle_buzz_once(app);
            break;
        }
        break;
    }
    case FlippPomodoroAppCustomEventStateUpdated:
        // after changing the stage - the center remains a hint, and stop "spam"
        flipp_pomodoro_scene_timer_sync_view_state(app);
        flipp_pomodoro_view_timer_set_on_ok_cb(
            app->timer_view, flipp_pomodoro_scene_timer_on_ask_hint);
        reset_notification_flags();
        notification_stop();
        break;
    case FlippPomodoroAppCustomEventTimerAskHint:
        flipp_pomodoro_view_timer_display_hint(
            flipp_pomodoro_view_timer_get_view(app->timer_view),
            flipp_pomodoro_scene_timer_get_contextual_hint(app));
        break;
    default:
        break;
    }
}

bool flipp_pomodoro_scene_timer_on_event(void* ctx, SceneManagerEvent event) {
    furi_assert(ctx);
    FlippPomodoroApp* app = ctx;

    switch(event.type) {
    case SceneManagerEventTypeCustom:
        flipp_pomodoro_scene_timer_handle_custom_event(app, event.event);
        return SceneEventConusmed;
    case SceneManagerEventTypeBack:
        notification_stop(); // stop "spam" when exiting back
        scene_manager_next_scene(app->scene_manager, FlippPomodoroSceneInfo);
        return SceneEventConusmed;
    default:
        break;
    };
    return SceneEventNotConusmed;
}

void flipp_pomodoro_scene_timer_on_exit(void* ctx) {
    UNUSED(ctx);
    notification_stop();
}
