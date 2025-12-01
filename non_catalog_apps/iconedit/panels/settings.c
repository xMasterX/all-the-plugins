#include <gui/canvas.h>
#include <input/input.h>

#include "panels.h"
#include "utils/draw.h"
#include "../iconedit.h"

static struct {
    SettingType selected_item;
} settingsModel = {.selected_item = Setting_Include};

void settings_draw(Canvas* canvas, void* context) {
    UNUSED(canvas);
    UNUSED(context);
    IconEdit* app = context;

    // TODO: Fix all this up.. it's crap

    // Icon* checkbox;
    // Icon* checked = NULL;
    // Icon* unchecked = NULL;

    // checkbox = app->settings.include_icon_header ? checked : unchecked;
    // canvas_draw_icon(canvas, 1, 20, checkbox);
    ie_draw_str(canvas, 10, 20, AlignLeft, AlignTop, Font5x7, "Include icon_i.h");
    if(app->panel == Panel_Settings && settingsModel.selected_item == Setting_Include) {
        canvas_set_color(canvas, ColorXOR);
        canvas_draw_frame(canvas, 0, 19, 64, 9);
        canvas_set_color(canvas, ColorBlack);
    }

    // checkbox = app->settings.delete_auto_brace ? checked : unchecked;
    // canvas_draw_icon(canvas, 1, 30, checkbox);
    ie_draw_str(canvas, 10, 30, AlignLeft, AlignTop, Font5x7, "Delete auto brace");
    if(app->panel == Panel_Settings && settingsModel.selected_item == Setting_Delete_Brace) {
        canvas_set_color(canvas, ColorXOR);
        canvas_draw_frame(canvas, 0, 29, 64, 9);
        canvas_set_color(canvas, ColorBlack);
    }
}

bool settings_input(InputEvent* event, void* context) {
    IconEdit* app = context;
    bool consumed = true;
    if(event->type == InputTypeShort) {
        switch(event->key) {
        case InputKeyUp: {
            if(settingsModel.selected_item == Setting_Include) {
                app->panel = Panel_TabBar;
            } else {
                settingsModel.selected_item -= 1;
            }
            break;
        }
        case InputKeyDown: {
            SettingType next_setting = (SettingType)(settingsModel.selected_item + 1);
            if(next_setting <= Setting_COUNT - 1) {
                settingsModel.selected_item = next_setting;
            }
            break;
        }
        case InputKeyOk:
            if(settingsModel.selected_item == Setting_Include) {
                app->settings.include_icon_header = !app->settings.include_icon_header;
            }
            if(settingsModel.selected_item == Setting_Delete_Brace) {
                app->settings.delete_auto_brace = !app->settings.delete_auto_brace;
            }
            break;
        case InputKeyBack:
            app->panel = Panel_TabBar;
            break;
        default:
            break;
        }
    }
    return consumed;
}
