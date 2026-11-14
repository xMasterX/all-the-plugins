#include "../usb_cdrom_app_i.h"

static const char* const disc_type_names[UsbCdromDiscTypeNum] = {
    [UsbCdromDiscTypeAuto] = "Auto",
    [UsbCdromDiscTypeCd] = "CD",
    [UsbCdromDiscTypeDvd] = "DVD",
};

static void usb_cdrom_item_select(void* context, uint32_t index) {
    UsbCdromApp* app = context;
    if(index == 0) {
        view_dispatcher_send_custom_event(app->view_dispatcher, UsbCdromCustomEventFileSelect);
    } else if(index == 2) {
        view_dispatcher_send_custom_event(app->view_dispatcher, UsbCdromCustomEventAbout);
    }
}

static void usb_cdrom_disc_type_change(VariableItem* item) {
    UsbCdromApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, disc_type_names[index]);
    app->disc_type = index;
}

void usb_cdrom_scene_start_on_enter(void* context) {
    UsbCdromApp* app = context;

    variable_item_list_add(app->variable_item_list, "Insert disc", 0, NULL, NULL);

    VariableItem* item = variable_item_list_add(
        app->variable_item_list, "Disc type", UsbCdromDiscTypeNum, usb_cdrom_disc_type_change, app);
    variable_item_set_current_value_index(item, app->disc_type);
    variable_item_set_current_value_text(item, disc_type_names[app->disc_type]);

    variable_item_list_add(app->variable_item_list, "About", 0, NULL, NULL);

    variable_item_list_set_enter_callback(app->variable_item_list, usb_cdrom_item_select, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, UsbCdromAppViewStart);
}

bool usb_cdrom_scene_start_on_event(void* context, SceneManagerEvent event) {
    UsbCdromApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == UsbCdromCustomEventFileSelect) {
            scene_manager_next_scene(app->scene_manager, UsbCdromSceneFileSelect);
            return true;
        } else if(event.event == UsbCdromCustomEventAbout) {
            scene_manager_next_scene(app->scene_manager, UsbCdromSceneAbout);
            return true;
        }
    }
    return false;
}

void usb_cdrom_scene_start_on_exit(void* context) {
    UsbCdromApp* app = context;
    variable_item_list_reset(app->variable_item_list);
}
