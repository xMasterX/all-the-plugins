#include "src/core/cfs_ui.h"
#include "src/data/cfs_shades.h"

void cfs_ui_screen_callback(void* context, uint32_t event) {
    CfsApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, event);
}

void cfs_ui_material_name(const CfsTagData* data, char* out, size_t out_size) {
    const CfsFilamentInfo* info = cfs_filament_lookup(data->filament_id);
    if(info) {
        snprintf(out, out_size, "%s", info->name);
    } else if(data->filament_type != CfsFilamentUnknown) {
        snprintf(out, out_size, "%s", cfs_filament_type_to_name(data->filament_type));
    } else {
        snprintf(out, out_size, "ID %s", data->filament_id);
    }
}

void cfs_ui_fill_card(const CfsTagData* data, size_t weight_index, CfsCardModel* card) {
    memset(card, 0, sizeof(*card));
    cfs_ui_material_name(data, card->title, sizeof(card->title));
    snprintf(card->brand, sizeof(card->brand), "%s", cfs_resolve_brand_name(data));
    snprintf(
        card->color_name,
        sizeof(card->color_name),
        "%s",
        cfs_color_nearest_name(data->color_r, data->color_g, data->color_b));
    snprintf(
        card->weight,
        sizeof(card->weight),
        "%s  %u m",
        cfs_weight_label(weight_index),
        data->length_m);
    snprintf(card->serial, sizeof(card->serial), "SN %s", data->serial);
    card->r = data->color_r;
    card->g = data->color_g;
    card->b = data->color_b;
}

void cfs_ui_show_working(
    CfsApp* app,
    const char* title,
    const char* text,
    const CfsTagData* color,
    bool winding) {
    CfsStatusModel status = {.kind = CfsStatusWorking, .winding = winding};
    snprintf(status.title, sizeof(status.title), "%s", title);
    snprintf(status.text, sizeof(status.text), "%s", text);
    if(color) {
        status.has_color = true;
        status.r = color->color_r;
        status.g = color->color_g;
        status.b = color->color_b;
    }
    cfs_screen_show_status(app->screen, &status, 0, 0, 0);
    view_dispatcher_switch_to_view(app->view_dispatcher, CfsViewScreen);
}

void cfs_ui_finish_op(CfsApp* app, bool success, const char* title, const char* text) {
    CfsStatusModel status = {.kind = success ? CfsStatusOk : CfsStatusFail};
    snprintf(status.title, sizeof(status.title), "%s", title);
    snprintf(status.text, sizeof(status.text), "%s", text);
    if(success) {
        cfs_screen_show_status(app->screen, &status, 0, 1800, CfsCustomEventScreenDone);
    } else {
        status.button_center = "Retry";
        cfs_screen_show_status(app->screen, &status, CfsCustomEventButtonRetry, 0, 0);
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, CfsViewScreen);
    notification_message(app->notifications, success ? &sequence_success : &sequence_error);
}
