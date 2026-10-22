#include "t5577_mw_work_view.h"

#include <furi.h>
#include <gui/elements.h>

#define T5577_MW_WORK_TEXT_SIZE 24

typedef struct {
    char title[T5577_MW_WORK_TEXT_SIZE];
    char status[T5577_MW_WORK_TEXT_SIZE];
    uint8_t done;
    uint8_t total;
} T5577MwWorkViewModel;

struct T5577MwWorkView {
    View* view;
};

static void t5577_mw_work_view_draw(Canvas* canvas, void* context) {
    const T5577MwWorkViewModel* model = context;

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 10, AlignCenter, AlignTop, model->title);

    // Guarded rather than assumed: a bar is only meaningful once there is something to count,
    // and a zero total here would divide by it.
    if(model->total > 0) {
        const float progress = (float)model->done / (float)model->total;
        char count[12];

        snprintf(count, sizeof(count), "%u/%u", model->done, model->total);
        elements_progress_bar_with_text(canvas, 8, 28, 112, MIN(progress, 1.0f), count);
    }

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 46, AlignCenter, AlignTop, model->status);
}

T5577MwWorkView* t5577_mw_work_view_alloc(void) {
    T5577MwWorkView* instance = malloc(sizeof(T5577MwWorkView));

    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(T5577MwWorkViewModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, t5577_mw_work_view_draw);

    return instance;
}

void t5577_mw_work_view_free(T5577MwWorkView* instance) {
    furi_assert(instance);

    view_free(instance->view);
    free(instance);
}

View* t5577_mw_work_view_get_view(T5577MwWorkView* instance) {
    furi_assert(instance);

    return instance->view;
}

void t5577_mw_work_view_start(T5577MwWorkView* instance, const char* title, uint8_t total) {
    furi_assert(instance);

    with_view_model(
        instance->view,
        T5577MwWorkViewModel * model,
        {
            strlcpy(model->title, title, sizeof(model->title));
            model->status[0] = '\0';
            model->done = 0;
            model->total = total;
        },
        true);
}

void t5577_mw_work_view_set_status(T5577MwWorkView* instance, const char* status, uint8_t done) {
    furi_assert(instance);

    with_view_model(
        instance->view,
        T5577MwWorkViewModel * model,
        {
            strlcpy(model->status, status, sizeof(model->status));
            model->done = done;
        },
        true);
}

void t5577_mw_work_view_finish(T5577MwWorkView* instance, const char* status) {
    furi_assert(instance);

    with_view_model(
        instance->view,
        T5577MwWorkViewModel * model,
        {
            strlcpy(model->status, status, sizeof(model->status));
            model->done = model->total;
        },
        true);
}
