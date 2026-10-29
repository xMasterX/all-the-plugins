#include "cd_view.h"
#include "../usb_cdrom_app_i.h"
#include <gui/elements.h>

#define DISC_X      (19)
#define DISC_Y      (31)
#define DISC_RADIUS (14)
#define TEXT_X      (38)

struct CdView {
    View* view;
    CdViewButtonCallback button_cb;
    void* button_cb_context;
};

typedef struct {
    FuriString *name, *info, *status_string;
    bool loaded;
    uint32_t read_speed;
    uint32_t bytes_read;
    uint32_t update_time;
    uint8_t spin; // which way the disc's marker points, steps while reading
    bool connection_error;
} CdViewModel;

// unit vectors for the spinning marker, in 45 degree steps
static const int8_t spin_dx[] = {0, 7, 10, 7, 0, -7, -10, -7};
static const int8_t spin_dy[] = {-10, -7, 0, 7, 10, 7, 0, -7};

static void append_suffixed_byte_count(FuriString* string, uint32_t count) {
    if(count < 1024) {
        furi_string_cat_printf(string, "%luB", count);
    } else if(count < 1024 * 1024) {
        furi_string_cat_printf(string, "%luK", count / 1024);
    } else {
        furi_string_cat_printf(string, "%.3fM", (double)count / (1024 * 1024));
    }
}

static void draw_disc(Canvas* canvas, uint8_t spin) {
    canvas_draw_circle(canvas, DISC_X, DISC_Y, DISC_RADIUS);
    canvas_draw_circle(canvas, DISC_X, DISC_Y, 4);
    canvas_draw_dot(canvas, DISC_X, DISC_Y);
    int32_t dx = spin_dx[spin % COUNT_OF(spin_dx)];
    int32_t dy = spin_dy[spin % COUNT_OF(spin_dy)];
    canvas_draw_line(
        canvas,
        DISC_X + dx * 6 / 10,
        DISC_Y + dy * 6 / 10,
        DISC_X + dx * 12 / 10,
        DISC_Y + dy * 12 / 10);
}

// the eject symbol, a triangle over a bar
static void draw_eject(Canvas* canvas) {
    for(int32_t i = 0; i < 7; i++) {
        canvas_draw_line(canvas, DISC_X - i, DISC_Y - 6 + i, DISC_X + i, DISC_Y - 6 + i);
    }
    canvas_draw_box(canvas, DISC_X - 6, DISC_Y + 3, 13, 3);
}

static void cd_view_draw_callback(Canvas* canvas, void* _model) {
    CdViewModel* model = _model;

    if(model->connection_error) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(
            canvas, canvas_width(canvas) / 2, 12, AlignCenter, AlignCenter, "USB Error");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(
            canvas, canvas_width(canvas) / 2, 28, AlignCenter, AlignCenter, "Check your USB cable");
        canvas_draw_str_aligned(
            canvas, canvas_width(canvas) / 2, 42, AlignCenter, AlignCenter, "and try again");
        elements_button_left(canvas, "Back");
        return;
    }

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(
        canvas, canvas_width(canvas) / 2, 0, AlignCenter, AlignTop, "USB CD-ROM");

    canvas_draw_rframe(canvas, 0, 13, canvas_width(canvas), 37, 3);
    if(model->loaded) {
        draw_disc(canvas, model->spin);
    } else {
        draw_eject(canvas);
    }

    canvas_set_font(canvas, FontSecondary);
    elements_string_fit_width(canvas, model->name, canvas_width(canvas) - TEXT_X - 3);
    canvas_draw_str(canvas, TEXT_X, 23, furi_string_get_cstr(model->name));

    if(model->loaded) {
        furi_string_set_str(model->status_string, "R:");
        append_suffixed_byte_count(model->status_string, model->bytes_read);
        if(model->read_speed) {
            furi_string_cat_str(model->status_string, "; ");
            append_suffixed_byte_count(model->status_string, model->read_speed);
            furi_string_cat_str(model->status_string, "ps");
        }
        canvas_draw_str(canvas, TEXT_X, 34, furi_string_get_cstr(model->status_string));
    }

    canvas_draw_str(canvas, TEXT_X, 45, furi_string_get_cstr(model->info));

    elements_button_center(canvas, model->loaded ? "Eject" : "Insert");
}

static bool cd_view_input_callback(InputEvent* event, void* context) {
    CdView* cd_view = context;
    if(event->key != InputKeyOk) return false;
    if(cd_view->button_cb) {
        cd_view->button_cb(event->type, cd_view->button_cb_context);
    }
    return true;
}

CdView* cd_view_alloc(void) {
    CdView* cd_view = malloc(sizeof(CdView));

    cd_view->view = view_alloc();
    view_allocate_model(cd_view->view, ViewModelTypeLocking, sizeof(CdViewModel));
    with_view_model(
        cd_view->view,
        CdViewModel * model,
        {
            model->name = furi_string_alloc();
            model->info = furi_string_alloc();
            model->status_string = furi_string_alloc();
        },
        false);
    view_set_context(cd_view->view, cd_view);
    view_set_draw_callback(cd_view->view, cd_view_draw_callback);
    view_set_input_callback(cd_view->view, cd_view_input_callback);

    return cd_view;
}

void cd_view_free(CdView* cd_view) {
    furi_assert(cd_view);
    with_view_model(
        cd_view->view,
        CdViewModel * model,
        {
            furi_string_free(model->name);
            furi_string_free(model->info);
            furi_string_free(model->status_string);
        },
        false);
    view_free(cd_view->view);
    free(cd_view);
}

View* cd_view_get_view(CdView* cd_view) {
    furi_assert(cd_view);
    return cd_view->view;
}

void cd_view_set_button_callback(CdView* cd_view, CdViewButtonCallback cb, void* context) {
    furi_assert(cd_view);
    cd_view->button_cb = cb;
    cd_view->button_cb_context = context;
}

void cd_view_set_disc(CdView* cd_view, bool loaded, FuriString* name, FuriString* info) {
    furi_assert(name);
    furi_assert(info);
    with_view_model(
        cd_view->view,
        CdViewModel * model,
        {
            model->loaded = loaded;
            furi_string_set(model->name, name);
            furi_string_set(model->info, info);
        },
        true);
}

void cd_view_set_connection_error(CdView* cd_view) {
    with_view_model(cd_view->view, CdViewModel * model, { model->connection_error = true; }, true);
}

void cd_view_clear_connection_error(CdView* cd_view) {
    with_view_model(
        cd_view->view, CdViewModel * model, { model->connection_error = false; }, false);
}

void cd_view_set_stats(CdView* cd_view, uint32_t read) {
    with_view_model(
        cd_view->view,
        CdViewModel * model,
        {
            uint32_t now = furi_get_tick();
            // the count starts again at zero each time the drive starts
            model->read_speed = read < model->bytes_read ?
                                    0 :
                                    (read - model->bytes_read) * 1000 / (now - model->update_time);
            model->bytes_read = read;
            model->update_time = now;
            if(model->read_speed) model->spin++;
        },
        true);
}
