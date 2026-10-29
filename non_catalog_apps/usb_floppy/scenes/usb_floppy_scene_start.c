#include "../usb_floppy_app_i.h"

static const char* const transport_names[FloppyTransportNum] = {
    [FloppyTransportCbi] = "CBI",
    [FloppyTransportBot] = "Bulk-Only",
};

static const char* const write_protect_names[] = {"Off", "On"};

static void usb_floppy_item_select(void* context, uint32_t index) {
    UsbFloppyApp* app = context;
    if(index == 0) {
        view_dispatcher_send_custom_event(app->view_dispatcher, UsbFloppyCustomEventFileSelect);
    } else if(index == 3) {
        view_dispatcher_send_custom_event(app->view_dispatcher, UsbFloppyCustomEventAbout);
    }
}

static void usb_floppy_transport_change(VariableItem* item) {
    UsbFloppyApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, transport_names[index]);
    app->transport = index;
}

static void usb_floppy_write_protect_change(VariableItem* item) {
    UsbFloppyApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, write_protect_names[index]);
    app->write_protect = index;
}

void usb_floppy_scene_start_on_enter(void* context) {
    UsbFloppyApp* app = context;

    variable_item_list_add(app->variable_item_list, "Select disk image", 0, NULL, NULL);

    VariableItem* item = variable_item_list_add(
        app->variable_item_list, "Transport", FloppyTransportNum, usb_floppy_transport_change, app);
    variable_item_set_current_value_index(item, app->transport);
    variable_item_set_current_value_text(item, transport_names[app->transport]);

    item = variable_item_list_add(
        app->variable_item_list,
        "Write protect",
        COUNT_OF(write_protect_names),
        usb_floppy_write_protect_change,
        app);
    variable_item_set_current_value_index(item, app->write_protect);
    variable_item_set_current_value_text(item, write_protect_names[app->write_protect]);

    variable_item_list_add(app->variable_item_list, "About", 0, NULL, NULL);

    variable_item_list_set_enter_callback(app->variable_item_list, usb_floppy_item_select, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, UsbFloppyAppViewStart);
}

bool usb_floppy_scene_start_on_event(void* context, SceneManagerEvent event) {
    UsbFloppyApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == UsbFloppyCustomEventFileSelect) {
            scene_manager_next_scene(app->scene_manager, UsbFloppySceneFileSelect);
            return true;
        } else if(event.event == UsbFloppyCustomEventAbout) {
            scene_manager_next_scene(app->scene_manager, UsbFloppySceneAbout);
            return true;
        }
    }
    return false;
}

void usb_floppy_scene_start_on_exit(void* context) {
    UsbFloppyApp* app = context;
    variable_item_list_reset(app->variable_item_list);
}
