
#include "helpers/scheduler_hms.h"
#include "scheduler_interval_view.h"
#include "src/scheduler_app_i.h"
#include "subghz_scheduler_icons.h"

typedef struct {
    SchedulerHMS time;
    uint8_t col;
    bool time_of_day; // clock time, hours wrap at 24
    bool allow_zero;
    char title[24];
} IntervalModel;

struct SchedulerIntervalView {
    View* view;
    SchedulerApp* app;
};

static inline bool is_selected(IntervalModel* model, uint8_t col) {
    return model->col == col;
}

static void draw_block(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    size_t w,
    size_t h,
    Font font,
    bool selected,
    const char* text) {
    canvas_set_color(canvas, ColorBlack);

    // The selected block is edited directly with up/down
    if(selected) {
        canvas_draw_icon(canvas, x + w / 2 - 2, y - 4, &I_SmallArrowUp_3x5);
        canvas_draw_icon(canvas, x + w / 2 - 2, y + h + 1, &I_SmallArrowDown_3x5);
        canvas_draw_rbox(canvas, x, y, w, h, 1);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_draw_rframe(canvas, x, y, w, h, 1);
    }

    canvas_set_font(canvas, font);
    canvas_draw_str_aligned(canvas, x + w / 2, y + h / 2, AlignCenter, AlignCenter, text);

    if(selected) {
        canvas_set_color(canvas, ColorBlack);
    }
}

static void interval_view_draw(Canvas* canvas, void* context) {
    IntervalModel* model = context;
    char buffer[4];

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(
        canvas, GUI_DISPLAY_WIDTH / 2, 4, AlignCenter, AlignTop, model->title);

    // Hours
    snprintf(buffer, sizeof(buffer), "%02u", model->time.h);
    draw_block(canvas, 14, 19, 28, 24, FontBigNumbers, is_selected(model, 0), buffer);

    // Hour/Minute colon
    canvas_draw_box(canvas, 45, 34, 2, 2);
    canvas_draw_box(canvas, 45, 28, 2, 2);

    // Minutes
    snprintf(buffer, sizeof(buffer), "%02u", model->time.m);
    draw_block(canvas, 50, 19, 28, 24, FontBigNumbers, is_selected(model, 1), buffer);

    // Minute/Second colon
    canvas_draw_box(canvas, 81, 34, 2, 2);
    canvas_draw_box(canvas, 81, 28, 2, 2);

    // Seconds
    snprintf(buffer, sizeof(buffer), "%02u", model->time.s);
    draw_block(canvas, 86, 19, 28, 24, FontBigNumbers, is_selected(model, 2), buffer);
}

static bool interval_view_input(InputEvent* event, void* context) {
    SchedulerIntervalView* view = context;
    furi_assert(view);
    SchedulerApp* app = view->app;
    furi_assert(app);
    bool consumed = false;
    bool done = false;

    with_view_model(
        view->view,
        IntervalModel * model,
        {
            if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
                const InputKey key = event->key;

                if(key == InputKeyLeft) {
                    if(model->col > 0) {
                        model->col--;
                    }
                    consumed = true;
                } else if(key == InputKeyRight) {
                    if(model->col < 2) {
                        model->col++;
                    }
                    consumed = true;
                } else if(key == InputKeyOk) {
                    // Save and close, the scene stores the value when it exits
                    done = true;
                    consumed = true;
                } else if(key == InputKeyUp || key == InputKeyDown) {
                    const bool up = (key == InputKeyUp);

                    if(model->col == 0) {
                        const uint8_t hours = model->time_of_day ? 24 : 100;
                        model->time.h =
                            up ? (uint8_t)((model->time.h + 1) % hours) :
                                 (model->time.h == 0 ? hours - 1 : (uint8_t)(model->time.h - 1));
                    } else if(model->col == 1) {
                        model->time.m =
                            up ? (uint8_t)((model->time.m + 1) % 60) :
                                 (model->time.m == 0 ? 59 : (uint8_t)(model->time.m - 1));
                    } else {
                        model->time.s =
                            up ? (uint8_t)((model->time.s + 1) % 60) :
                                 (model->time.s == 0 ? 59 : (uint8_t)(model->time.s - 1));
                    }
                    consumed = true;
                }
            }
        },
        true);

    if(done) {
        view_dispatcher_send_custom_event(app->view_dispatcher, SchedulerEventIntervalDone);
    }

    return consumed;
}

void scheduler_interval_view_set_seconds(SchedulerIntervalView* v, uint32_t seconds) {
    furi_assert(v);

    with_view_model(
        v->view,
        IntervalModel * m,
        {
            scheduler_seconds_to_hms(seconds, &m->time);
            m->col = 0;
        },
        true);
}

uint32_t scheduler_interval_view_get_seconds(SchedulerIntervalView* v) {
    furi_assert(v);
    uint32_t sec = 1;

    with_view_model(
        v->view,
        IntervalModel * m,
        {
            sec = scheduler_hms_to_seconds(&m->time);
            // Don't allow 00:00:00 for intervals unless zero is allowed, clamp to 00:00:01
            if(sec == 0 && !m->allow_zero && !m->time_of_day) {
                sec = 1;
            }
        },
        false);

    return sec;
}

SchedulerIntervalView* scheduler_interval_view_alloc() {
    SchedulerIntervalView* interval_view = calloc(1, sizeof(SchedulerIntervalView));
    interval_view->view = view_alloc();

    view_allocate_model(interval_view->view, ViewModelTypeLocking, sizeof(IntervalModel));
    view_set_context(interval_view->view, interval_view);
    view_set_draw_callback(interval_view->view, interval_view_draw);
    view_set_input_callback(interval_view->view, interval_view_input);

    with_view_model(
        interval_view->view,
        IntervalModel * m,
        {
            m->time.h = 0;
            m->time.m = 0;
            m->time.s = 1;
            m->time_of_day = false;
            m->allow_zero = false;
            snprintf(m->title, sizeof(m->title), "Set Interval");
            m->col = 0;
        },
        true);

    return interval_view;
}

View* scheduler_interval_view_get_view(SchedulerIntervalView* interval_view) {
    furi_assert(interval_view);
    return interval_view->view;
}

void scheduler_interval_view_free(SchedulerIntervalView* interval_view) {
    furi_assert(interval_view);
    view_free(interval_view->view);
    free(interval_view);
}

void scheduler_interval_view_set_app(SchedulerIntervalView* view, SchedulerApp* app) {
    furi_assert(view);
    view->app = app;
}

void scheduler_interval_view_configure(
    SchedulerIntervalView* v,
    const char* title,
    bool time_of_day,
    bool allow_zero) {
    furi_assert(v);

    with_view_model(
        v->view,
        IntervalModel * m,
        {
            snprintf(m->title, sizeof(m->title), "%s", title);
            m->time_of_day = time_of_day;
            m->allow_zero = allow_zero;
        },
        true);
}
