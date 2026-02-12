// views/protopirate_receiver_info.c
#include "protopirate_receiver_info.h"
#include "../protopirate_app_i.h"
#include <input/input.h>
#include <gui/elements.h>
#include <furi.h>

struct ProtoPirateReceiverInfo {
    View* view;
    ProtoPirateReceiverInfoCallback callback;
    void* context;
};

typedef struct {
    FuriString* protocol_name;
    FuriString* info_text;
} ProtoPirateReceiverInfoModel;

void protopirate_view_receiver_info_set_callback(
    ProtoPirateReceiverInfo* receiver_info,
    ProtoPirateReceiverInfoCallback callback,
    void* context) {
    furi_check(receiver_info);
    receiver_info->callback = callback;
    receiver_info->context = context;
}

void protopirate_view_receiver_info_draw(Canvas* canvas, ProtoPirateReceiverInfoModel* model) {
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(
        canvas, 64, 0, AlignCenter, AlignTop, furi_string_get_cstr(model->protocol_name));

    canvas_draw_line(canvas, 0, 12, 127, 12);

    canvas_set_font(canvas, FontSecondary);
    elements_multiline_text_aligned(
        canvas, 0, 16, AlignLeft, AlignTop, furi_string_get_cstr(model->info_text));
}

bool protopirate_view_receiver_info_input(InputEvent* event, void* context) {
    furi_check(context);
    ProtoPirateReceiverInfo* receiver_info = context;

    if(event->type == InputTypeShort && event->key == InputKeyBack) {
        if(receiver_info->callback) {
            receiver_info->callback(
                ProtoPirateCustomEventViewReceiverBack, receiver_info->context);
        }
        return true;
    }
    return false;
}

void protopirate_view_receiver_info_enter(void* context) {
    furi_check(context);
    UNUSED(context);
}

void protopirate_view_receiver_info_exit(void* context) {
    furi_check(context);
    ProtoPirateReceiverInfo* receiver_info = context;
    with_view_model(
        receiver_info->view,
        ProtoPirateReceiverInfoModel * model,
        {
            furi_string_reset(model->protocol_name);
            furi_string_reset(model->info_text);
        },
        false);
}

ProtoPirateReceiverInfo* protopirate_view_receiver_info_alloc() {
    ProtoPirateReceiverInfo* receiver_info = malloc(sizeof(ProtoPirateReceiverInfo));
    furi_check(receiver_info);

    receiver_info->view = view_alloc();
    view_allocate_model(
        receiver_info->view, ViewModelTypeLocking, sizeof(ProtoPirateReceiverInfoModel));
    view_set_context(receiver_info->view, receiver_info);
    view_set_draw_callback(
        receiver_info->view, (ViewDrawCallback)protopirate_view_receiver_info_draw);
    view_set_input_callback(receiver_info->view, protopirate_view_receiver_info_input);
    view_set_enter_callback(receiver_info->view, protopirate_view_receiver_info_enter);
    view_set_exit_callback(receiver_info->view, protopirate_view_receiver_info_exit);

    with_view_model(
        receiver_info->view,
        ProtoPirateReceiverInfoModel * model,
        {
            model->protocol_name = furi_string_alloc();
            model->info_text = furi_string_alloc();
        },
        true);

    return receiver_info;
}

void protopirate_view_receiver_info_free(ProtoPirateReceiverInfo* receiver_info) {
    furi_check(receiver_info);

    with_view_model(
        receiver_info->view,
        ProtoPirateReceiverInfoModel * model,
        {
            furi_string_free(model->protocol_name);
            furi_string_free(model->info_text);
        },
        false);

    view_free(receiver_info->view);
    free(receiver_info);
}

View* protopirate_view_receiver_info_get_view(ProtoPirateReceiverInfo* receiver_info) {
    furi_check(receiver_info);
    return receiver_info->view;
}

void protopirate_view_receiver_info_set_protocol_name(
    ProtoPirateReceiverInfo* receiver_info,
    const char* protocol_name) {
    furi_check(receiver_info);
    with_view_model(
        receiver_info->view,
        ProtoPirateReceiverInfoModel * model,
        { furi_string_set_str(model->protocol_name, protocol_name); },
        true);
}

void protopirate_view_receiver_info_set_info_text(
    ProtoPirateReceiverInfo* receiver_info,
    const char* info_text) {
    furi_check(receiver_info);
    with_view_model(
        receiver_info->view,
        ProtoPirateReceiverInfoModel * model,
        { furi_string_set_str(model->info_text, info_text); },
        true);
}
