#include <gui/canvas.h>
#include <input/input.h>

#include "panels.h"
#include "../iconedit.h"
#include "../utils/draw.h"

typedef enum {
    Draw_NONE,
    Draw_Point1_Set,
} DrawState;

static struct {
    size_t cursor_x;
    size_t cursor_y;
    DrawState draw_state;
    size_t p1x, p1y;
    uint8_t* double_buffer;

    // tool modifiers
    int pen_size;
} canvasModel;

void canvas_alloc_canvas(size_t width, size_t height) {
    // place the cursor in the middle-ish
    // keeping in mind the canvas size (64x64) and our lowest scale of 2x
    canvasModel.cursor_x = MAX((int)(width / 2) - 1, 0);
    if(width > 32) {
        canvasModel.cursor_x = 15;
    }
    canvasModel.cursor_y = MAX((int)(height / 2) - 1, 0);
    if(height > 32) {
        canvasModel.cursor_y = 15;
    }
    canvasModel.draw_state = Draw_NONE;
    canvasModel.p1x = 0;
    canvasModel.p1y = 0;
    canvasModel.double_buffer = malloc(width * height * sizeof(uint8_t));
}

void canvas_free_canvas() {
    if(canvasModel.double_buffer) {
        free(canvasModel.double_buffer);
    }
}

void canvas_draw(Canvas* canvas, void* context) {
    UNUSED(canvas);
    IconEdit* app = context;
    IEIcon* icon = app->icon;

    // minimum scale is always 2x - so we can actually SEE the pixels
    // also, let's constrain the canvas to the 64x64 area - always
    // but our drawing area needs to be a little less - like 60x60? <-- do we even do this?
    // anything larger needs a scrolling canvas!
    // Hitting Play will show it fullscreen too - even if 1 frame
    int scale = 2;
    bool scroll = false;
    int max_dim = MAX(icon->width, icon->height);
    if(max_dim <= 32) {
        scale = 64 / max_dim;
    } else {
        scroll = true;
    }

    // Determine where we're drawing our canvas, based on icon size
    int x_offset = 64; // top-left corner
    int y_offset = 0; // of our canvas area

    // our icon is too big for the canvas, even at the low 2x scale, or it fits
    // either way, we want it centered, accounting for both width and height
    if(!scroll || icon->width * scale <= 64) {
        x_offset += (64 - (icon->width * scale)) / 2;
    }
    if(!scroll || icon->height * scale <= 64) {
        y_offset += (64 - (icon->height * scale)) / 2;
    }

    // canvas dimensions
    int cw = x_offset + icon->width * scale;
    int ch = y_offset + icon->height * scale;

    // clear the canvas area - this helps clear any tabbar text
    // maybe we don't need to do the whole 64x64 box?
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, x_offset, y_offset, cw, ch);
    canvas_set_color(canvas, ColorBlack);

    // draw editor grid - the guide dots around the canvas border
    for(size_t i = 0; i < (icon->width + 1); ++i) {
        // top and bottom frame dots
        canvas_draw_dot(canvas, x_offset + i * scale, y_offset - 1);
        canvas_draw_dot(canvas, x_offset + i * scale, y_offset + (icon->height * scale) + 1);
    }
    for(size_t i = 0; i < (icon->height + 1); ++i) {
        // left and right frame dots
        canvas_draw_dot(canvas, x_offset - 1, y_offset + i * scale);
        canvas_draw_dot(canvas, x_offset + (icon->width * scale) + 1, y_offset + i * scale);
    }

    // define our scrolled offset coordinates. if we're scrolling, we're at a 2x scale
    // which means we are looking at a 32x32 view port of our icon
    // we want to slide that view if our cursor *approaches* the edge, but isn't quite there
    // and our view should be bounded by the icon dimensions
    // cursor position is in absolute coordinates on the icon
    size_t window_pad = 4;
    size_t vpx = 0;
    size_t vpy = 0;
    if(scroll) {
        if(canvasModel.cursor_x >= 32 - window_pad) {
            vpx = (canvasModel.cursor_x + window_pad) - 32;
            if(vpx > icon->width - 32) {
                vpx = icon->width - 32;
            }
        }
        if(canvasModel.cursor_y >= 32 - window_pad) {
            vpy = (canvasModel.cursor_y + window_pad) - 32;
            if(vpy > icon->height - 32) {
                vpy = icon->height - 32;
            }
        }
    }
    int cx = canvasModel.cursor_x - vpx;
    int cy = canvasModel.cursor_y - vpy;

    // draw scroll bars!
    if(scroll) {
        // X axis scrollbar
        if(icon->width * scale > 64) {
            int sw = (32 * 64) / icon->width;
            float xp = (vpx * 1.0) / (icon->width - 32);
            int so = (64 - sw) * xp;
            int ys = MIN(y_offset + (icon->height * scale) + 1, (size_t)63);
            canvas_draw_line(canvas, x_offset + so, ys, x_offset + so + sw, ys);
        }
        // Y axis scrollbar
        if(icon->height * scale > 64) {
            int sh = (32 * 64) / icon->height;
            float yp = (vpy * 1.0) / (icon->height - 32);
            int so = (64 - sh) * yp;
            canvas_draw_line(
                canvas, x_offset - 1, y_offset + so, x_offset - 1, y_offset + so + sh);
        }
    }

    // set display buffer
    Frame* f = ie_icon_get_current_frame(icon);
    for(size_t i = 0; i < icon->width * icon->height; ++i) {
        canvasModel.double_buffer[i] = f->data[i];
    }

    // if we're drawing a circle/rect/line, add that to our view
    // this is independent of our current view port
    if(canvasModel.draw_state == Draw_Point1_Set) {
        uint8_t x1 = canvasModel.p1x;
        uint8_t y1 = canvasModel.p1y;
        uint8_t x2 = canvasModel.cursor_x;
        uint8_t y2 = canvasModel.cursor_y;
        int bw = icon->width;
        int bh = icon->height;
        switch(tools_get_selected_tool()) {
        case Tool_Line:
            // draw line between pt1 and cursor
            ie_draw_line(canvasModel.double_buffer, x1, y1, x2, y2, bw, bh);
            break;
        case Tool_Circle:
            ie_draw_circle(canvasModel.double_buffer, x1, y1, x2, y2, bw, bh);
            break;
        case Tool_Rect:
            ie_draw_line(canvasModel.double_buffer, x1, y1, x2, y1, bw, bh);
            ie_draw_line(canvasModel.double_buffer, x2, y1, x2, y2, bw, bh);
            ie_draw_line(canvasModel.double_buffer, x2, y2, x1, y2, bw, bh);
            ie_draw_line(canvasModel.double_buffer, x1, y1, x1, y2, bw, bh);
            break;
        default:
            break;
        }
    }

    // Draw the icon in two ways:
    // 1. Main way: draw scaled editor view, taking into account a scrolled viewport
    for(size_t y = vpy; y < vpy + MIN((int)icon->height, 32); ++y) {
        for(size_t x = vpx; x < vpx + MIN((int)icon->width, 32); ++x) {
            if(canvasModel.double_buffer[y * icon->width + x]) {
                canvas_draw_box(
                    canvas,
                    x_offset + ((x - vpx) * scale),
                    y_offset + ((y - vpy) * scale),
                    scale,
                    scale);
            }
        }
    }

    // 2. Actual sized preview (ehhh, not enough room for this anymore)
    // for(size_t i = 0; i < icon->width * icon->height; ++i) {
    //     if(canvasModel.double_buffer[i]) {
    //         canvas_draw_dot(canvas, PREVIEW_X + (i % icon->width), PREVIEW_Y + (i / icon->width));
    //     }
    // }

    // draw the cursor
    if(app->panel == Panel_Canvas) {
        canvas_set_color(canvas, ColorXOR);
        canvas_draw_dot(canvas, x_offset + cx * scale, y_offset + cy * scale);
        canvas_draw_dot(canvas, x_offset + cx * scale + (scale - 1), y_offset + cy * scale);
        canvas_draw_dot(
            canvas, x_offset + cx * scale + (scale - 1), y_offset + cy * scale + (scale - 1));
        canvas_draw_dot(canvas, x_offset + cx * scale, y_offset + cy * scale + (scale - 1));
        // canvas_draw_frame(
        //     canvas,
        //     x_offset + canvasModel.cursor_x * scale,
        //     y_offset + canvasModel.cursor_y * scale,
        //     scale,
        //     scale);
        canvas_set_color(canvas, ColorBlack);
    }

    // draw cursor guides around border
    if(app->panel == Panel_Canvas) {
        canvas_draw_line(
            canvas,
            x_offset + cx * scale,
            y_offset - 1,
            x_offset + cx * scale + (scale - 1),
            y_offset - 1);
        canvas_draw_line(
            canvas,
            x_offset + cx * scale,
            y_offset + (icon->height * scale) + 1,
            x_offset + cx * scale + (scale - 1),
            y_offset + (icon->height * scale) + 1);
        canvas_draw_line(
            canvas,
            x_offset - 1,
            y_offset + cy * scale,
            x_offset - 1,
            y_offset + cy * scale + (scale - 1));
        canvas_draw_line(
            canvas,
            x_offset + (icon->width * scale) + 1,
            y_offset + cy * scale,
            x_offset + (icon->width * scale) + 1,
            y_offset + cy * scale + (scale - 1));
    }
}

bool canvas_input(InputEvent* event, void* context) {
    IconEdit* app = context;
    bool consumed = true;
    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        switch(event->key) {
        case InputKeyUp: {
            canvasModel.cursor_y -= canvasModel.cursor_y > 0;
            break;
        }
        case InputKeyDown: {
            canvasModel.cursor_y += canvasModel.cursor_y < (app->icon->height - 1);
            break;
        }
        case InputKeyLeft: {
            canvasModel.cursor_x -= canvasModel.cursor_x > 0;
            break;
        }
        case InputKeyRight: {
            canvasModel.cursor_x += canvasModel.cursor_x < (app->icon->width - 1);
            break;
        }
        case InputKeyBack: {
            if(canvasModel.draw_state == Draw_Point1_Set) {
                canvasModel.draw_state = Draw_NONE;
            } else {
                consumed = false; // send control back to the Tools panel
                app->panel = Panel_Tools;
            }
            break;
        }
        case InputKeyOk: {
            EditorTool tool = tools_get_selected_tool();
            Frame* f = ie_icon_get_current_frame(app->icon);
            switch(tool) {
            case Tool_Draw: {
                size_t offset = canvasModel.cursor_y * app->icon->width + canvasModel.cursor_x;
                f->data[offset] = f->data[offset] ? 0 : 1;
                app->dirty = true;
                break;
            }
            case Tool_Line:
            case Tool_Circle:
            case Tool_Rect: {
                switch(canvasModel.draw_state) {
                case Draw_NONE:
                    canvasModel.p1x = canvasModel.cursor_x;
                    canvasModel.p1y = canvasModel.cursor_y;
                    canvasModel.draw_state = Draw_Point1_Set;
                    break;
                case Draw_Point1_Set: {
                    uint8_t x2 = canvasModel.cursor_x;
                    uint8_t y2 = canvasModel.cursor_y;
                    int bw = app->icon->width;
                    int bh = app->icon->height;
                    // actually draw the line to icon image data
                    if(tool == Tool_Line) {
                        ie_draw_line(f->data, canvasModel.p1x, canvasModel.p1y, x2, y2, bw, bh);
                    } else if(tool == Tool_Circle) {
                        ie_draw_circle(f->data, canvasModel.p1x, canvasModel.p1y, x2, y2, bw, bh);
                    } else if(tool == Tool_Rect) {
                        ie_draw_line(
                            f->data, canvasModel.p1x, canvasModel.p1y, x2, canvasModel.p1y, bw, bh);
                        ie_draw_line(f->data, x2, canvasModel.p1y, x2, y2, bw, bh);
                        ie_draw_line(f->data, x2, y2, canvasModel.p1x, y2, bw, bh);
                        ie_draw_line(
                            f->data, canvasModel.p1x, canvasModel.p1y, canvasModel.p1x, y2, bw, bh);
                    }
                    // reset
                    canvasModel.draw_state = Draw_NONE;
                    app->dirty = true;
                    break;
                }
                default:
                    break;
                }
                break;
            }
            default:
                break;
            }
            break;
        }
        default:
            break;
        }
    }
    return consumed;
}
