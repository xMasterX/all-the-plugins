#include "t5577_multiwriter_i.h"

#include <bit_lib/bit_lib.h>

#define TAG "T5577Mw"

static void t5577_multiwriter_make_app_folder(LfRfid* app);

static bool t5577_multiwriter_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    LfRfid* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool t5577_multiwriter_back_event_callback(void* context) {
    furi_assert(context);
    LfRfid* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

// Every long operation runs as timed steps rather than in one go. A detect takes about a second
// and reading a block back about a second and a half, so a write that checked itself inside
// on_enter would hold the view dispatcher - and with it the GUI service - for the better part of
// ten seconds.
static void t5577_multiwriter_step_timer_callback(void* context) {
    LfRfid* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, LfRfidEventStep);
}

static LfRfid* t5577_multiwriter_alloc(void) {
    LfRfid* app = malloc(sizeof(LfRfid));

    // Zeroed wholesale rather than field by field. Every default this app wants happens to be
    // the zero value - T5577ReadClockUnsupported, T5577MwSlotUnread, T5577MwProtectLock - and a
    // struct this wide collects a field whose hand-written init gets forgotten, which the
    // compiler never mentions. Note T5577BlockHeld is also zero, so the result arrays start out
    // reading "verified"; every walk sets them in its on_enter before anything reads them.
    memset(app, 0, sizeof(LfRfid));

    app->storage = furi_record_open(RECORD_STORAGE);
    app->dialogs = furi_record_open(RECORD_DIALOGS);

    app->file_path = furi_string_alloc_set(LFRFID_APP_FOLDER);

    app->dict = protocol_dict_alloc(lfrfid_protocols, LFRFIDProtocolMax);
    app->listener = t5577_mw_listener_alloc(app->dict);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&t5577_multiwriter_scene_handlers, app);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, t5577_multiwriter_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, t5577_multiwriter_back_event_callback);

    app->step_timer =
        furi_timer_alloc(t5577_multiwriter_step_timer_callback, FuriTimerTypeOnce, app);

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, LfRfidViewSubmenu, submenu_get_view(app->submenu));

    app->popup = popup_alloc();
    view_dispatcher_add_view(app->view_dispatcher, LfRfidViewPopup, popup_get_view(app->popup));

    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, LfRfidViewWidget, widget_get_view(app->widget));

    app->byte_input = byte_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, LfRfidViewByteInput, byte_input_get_view(app->byte_input));

    app->work_view = t5577_mw_work_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, LfRfidViewWork, t5577_mw_work_view_get_view(app->work_view));

    app->tally_view = t5577_mw_tally_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, LfRfidViewTally, t5577_mw_tally_view_get_view(app->tally_view));

    return app;
} //-V773

static void t5577_multiwriter_free(LfRfid* app) {
    furi_assert(app);

    furi_string_free(app->file_path);

    t5577_mw_listener_free(app->listener);
    protocol_dict_free(app->dict);

    furi_timer_free(app->step_timer);

    view_dispatcher_remove_view(app->view_dispatcher, LfRfidViewSubmenu);
    submenu_free(app->submenu);

    view_dispatcher_remove_view(app->view_dispatcher, LfRfidViewPopup);
    popup_free(app->popup);

    view_dispatcher_remove_view(app->view_dispatcher, LfRfidViewWidget);
    widget_free(app->widget);

    view_dispatcher_remove_view(app->view_dispatcher, LfRfidViewByteInput);
    byte_input_free(app->byte_input);

    view_dispatcher_remove_view(app->view_dispatcher, LfRfidViewWork);
    t5577_mw_work_view_free(app->work_view);

    view_dispatcher_remove_view(app->view_dispatcher, LfRfidViewTally);
    t5577_mw_tally_view_free(app->tally_view);

    view_dispatcher_free(app->view_dispatcher);
    scene_manager_free(app->scene_manager);

    furi_record_close(RECORD_GUI);
    app->gui = NULL;

    furi_record_close(RECORD_NOTIFICATION);
    app->notifications = NULL;

    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_DIALOGS);

    free(app);
}

int32_t t5577_multiwriter_app(void* p) {
    UNUSED(p);

    LfRfid* app = t5577_multiwriter_alloc();

    t5577_multiwriter_make_app_folder(app);

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    scene_manager_next_scene(app->scene_manager, LfRfidSceneStart);

    view_dispatcher_run(app->view_dispatcher);

    // No t5577_field_reset() here, however tempting it looks as a safety net. It calls
    // furi_hal_rfid_tim_read_stop() unconditionally, which disables TIM1's clock - and TIM1 is
    // on APB2, whose furi_hal_bus_disable() branch keeps a live furi_check that the clock was
    // enabled in the first place. An app closed without ever touching the field therefore
    // crashes the device on the way out.
    //
    // Nothing needs the net anyway: every t5577 call this app makes raises and drops the field
    // inside that call, and the listener drives the HAL itself and tears it down in its own
    // thread before that thread returns.
    t5577_multiwriter_free(app);

    return 0;
}

static bool t5577_multiwriter_load_key_data(LfRfid* app, FuriString* path) {
    const ProtocolId id = lfrfid_dict_file_load(app->dict, furi_string_get_cstr(path));

    if(id != LFRFIDProtocolEM4100) {
        dialog_message_show_storage_error(app->dialogs, "Unsupported\nlfrfid protocol!");
        return false;
    }

    // Kept as the key itself rather than left in the dictionary: a write composes blocks from
    // it, and reading one back compares against the same number.
    const size_t size = protocol_dict_get_data_size(app->dict, LFRFIDProtocolEM4100);
    uint8_t data[8] = {0};

    // The dictionary's own idea of an EM4100 key is five bytes; anything else is this app
    // built against a firmware that changed the protocol out from under it.
    furi_check(size <= sizeof(data));

    protocol_dict_get_data(app->dict, LFRFIDProtocolEM4100, data, size);
    app->key = bit_lib_bytes_to_num_be(data, size);

    return true;
}

bool t5577_multiwriter_load_key_from_file_select(LfRfid* app) {
    furi_assert(app);

    DialogsFileBrowserOptions browser_options;
    dialog_file_browser_set_basic_options(
        &browser_options, LFRFID_APP_FILENAME_EXTENSION, &I_125_10px);
    browser_options.base_path = LFRFID_APP_FOLDER;

    if(!dialog_file_browser_show(app->dialogs, app->file_path, app->file_path, &browser_options)) {
        return false;
    }

    return t5577_multiwriter_load_key_data(app, app->file_path);
}

static void t5577_multiwriter_make_app_folder(LfRfid* app) {
    furi_assert(app);

    if(!storage_simply_mkdir(app->storage, LFRFID_APP_FOLDER)) {
        dialog_message_show_storage_error(app->dialogs, "Cannot create\napp folder");
    }
}

void t5577_multiwriter_popup_timeout_callback(void* context) {
    LfRfid* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, LfRfidEventPopupClosed);
}

void t5577_multiwriter_widget_callback(GuiButtonType result, InputType type, void* context) {
    LfRfid* app = context;
    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->view_dispatcher, result);
    }
}

void t5577_multiwriter_step_later(LfRfid* app, uint32_t delay_ms) {
    furi_assert(app);
    furi_timer_start(app->step_timer, furi_ms_to_ticks(delay_ms));
}

void t5577_multiwriter_step_stop(LfRfid* app) {
    furi_assert(app);
    furi_timer_stop(app->step_timer);
}

void t5577_multiwriter_forget_password(LfRfid* app) {
    furi_assert(app);

    app->password = 0;
    app->password_set = false;
    app->password_confirmed = false;
    app->password_try = false;
    memset(app->password_bytes, 0, sizeof(app->password_bytes));
}

bool t5577_multiwriter_ask(LfRfid* app, const char* header, const char* text, const char* yes) {
    furi_assert(app);

    DialogMessage* message = dialog_message_alloc();
    dialog_message_set_header(message, header, 64, 2, AlignCenter, AlignTop);
    dialog_message_set_text(message, text, 64, 18, AlignCenter, AlignTop);
    dialog_message_set_buttons(message, "Cancel", NULL, yes);

    const DialogMessageButton result = dialog_message_show(app->dialogs, message);
    dialog_message_free(message);

    return result == DialogMessageButtonRight;
}

void t5577_multiwriter_show_error(LfRfid* app, const char* header, const char* text) {
    furi_assert(app);

    // Named in the log as well as on screen: this is the only record of which of the several
    // failures a walk ended on once the screen has been dismissed.
    FURI_LOG_W(TAG, "result: %s", header);

    Widget* widget = app->widget;

    widget_reset(widget);
    widget_add_icon_element(widget, 72, 17, &I_WarningDolphinFlip_45x42);
    widget_add_string_element(widget, 64, 3, AlignCenter, AlignTop, FontPrimary, header);
    widget_add_string_multiline_element(widget, 3, 17, AlignLeft, AlignTop, FontSecondary, text);

    view_dispatcher_switch_to_view(app->view_dispatcher, LfRfidViewWidget);
    notification_message(app->notifications, &sequence_error);
}

void t5577_multiwriter_show_success(LfRfid* app, const char* header) {
    furi_assert(app);

    FURI_LOG_I(TAG, "result: %s", header);

    // The budget is enforced by the buffer itself: strlcpy truncates, so an over-long header
    // comes out short rather than running into the artwork. The assert names the programmer
    // error on a debug build and compiles out of this one, which is the point - furi_check
    // would stay live and turn a cosmetic slip into a crash on someone's device.
    furi_assert(strlen(header) <= T5577_MW_POPUP_HEADER_MAX);

    strlcpy(app->popup_header, header, sizeof(app->popup_header));

    Popup* popup = app->popup;

    // The header sits at 75 because the dolphin's top right corner is empty there, which is
    // where lfrfid's write-success screen and the t5577 app's result screens put it. It is the
    // only text on the screen: below that corner the artwork runs out to the icon's own right
    // edge, x=90, and elements_get_max_chars_to_fit() measures to the screen edge knowing
    // nothing of the icon - so a second line lands on the dolphin rather than being clipped.
    // There is nothing to say there anyway; see show_success()'s declaration in the header.
    popup_set_header(popup, app->popup_header, 75, 10, AlignLeft, AlignTop);
    popup_set_icon(popup, 0, 9, &I_DolphinSuccess_91x55);
    popup_set_context(popup, app);
    popup_set_callback(popup, t5577_multiwriter_popup_timeout_callback);
    popup_set_timeout(popup, 1500);
    popup_enable_timeout(popup);

    view_dispatcher_switch_to_view(app->view_dispatcher, LfRfidViewPopup);
    notification_message(app->notifications, &sequence_success);
}

bool t5577_multiwriter_leave_to_start(LfRfid* app, SceneManagerEvent event) {
    furi_assert(app);

    const bool leaving =
        (event.type == SceneManagerEventTypeBack) ||
        ((event.type == SceneManagerEventTypeCustom) && (event.event == LfRfidEventPopupClosed));

    if(!leaving) return false;

    scene_manager_search_and_switch_to_previous_scene(app->scene_manager, LfRfidSceneStart);

    return true;
}

void t5577_multiwriter_say(LfRfid* app, const char* header, const char* text) {
    furi_assert(app);

    DialogMessage* message = dialog_message_alloc();
    dialog_message_set_header(message, header, 64, 2, AlignCenter, AlignTop);
    dialog_message_set_text(message, text, 64, 18, AlignCenter, AlignTop);
    dialog_message_set_buttons(message, NULL, "OK", NULL);

    dialog_message_show(app->dialogs, message);
    dialog_message_free(message);
}
