#include "t5577_mw_tally_view.h"

#include "../protocols/EM41XX.h"

#include <furi.h>
#include <gui/elements.h>
#include <input/input.h>

/** Where a run has got to. Listening is zero, so a reset model starts there. */
typedef enum {
    T5577MwTallyListening,
    T5577MwTallyDone, // reached its target, so the counts cover whole cycles
    T5577MwTallyQuiet, // stopped collecting before it got there
} T5577MwTallyState;

typedef struct {
    T5577MwTally tally;
    uint16_t target;
    T5577MwTallyState state;
} T5577MwTallyViewModel;

struct T5577MwTallyView {
    View* view;
    T5577MwTallyViewCallback callback;
    void* context;
};

static void t5577_mw_tally_view_draw(Canvas* canvas, void* context) {
    const T5577MwTallyViewModel* model = context;
    const T5577MwTally* tally = &model->tally;
    char line[32];

    canvas_set_font(canvas, FontPrimary);
    switch(model->state) {
    case T5577MwTallyDone:
        snprintf(line, sizeof(line), "Done - %u frames", tally->reads);
        break;
    case T5577MwTallyQuiet:
        // A run that stopped collecting, not only one that never started: the count stays in
        // the line because the frames it did gather are the result, partial or not.
        snprintf(line, sizeof(line), "No tag - %u frames", tally->reads);
        break;
    default:
        snprintf(line, sizeof(line), "Listening %u/%u", tally->reads, model->target);
        break;
    }
    canvas_draw_str_aligned(canvas, 64, 0, AlignCenter, AlignTop, line);

    canvas_set_font(canvas, FontSecondary);

    if(tally->keys == 0) {
        canvas_draw_str_aligned(
            canvas,
            64,
            28,
            AlignCenter,
            AlignTop,
            model->state == T5577MwTallyListening ? "Hold the tag still" : "Nothing read");
    }

    for(uint8_t i = 0; i < tally->keys; i++) {
        const uint8_t y = 14 + i * 10;
        const uint64_t key = tally->key[i];

        em41xx_key_format(key, line, sizeof(line));
        canvas_draw_str_aligned(canvas, 2, y, AlignLeft, AlignTop, line);

        snprintf(line, sizeof(line), "%u", tally->count[i]);
        canvas_draw_str_aligned(canvas, 126, y, AlignRight, AlignTop, line);
    }

    if(tally->crowded > 0) {
        // A key the screen had no room for is still a finding, not noise to be dropped. It goes
        // above the button rather than instead of it: the run whose tally overflowed is the one
        // whose result is least trustworthy, and offering it no way to run again was the worst
        // place to leave the user.
        //
        // Rows 44-51, between the last key row and the button's box at 52. That is one body
        // height and no descender, so this string must not grow one.
        snprintf(line, sizeof(line), "+%u more not shown", tally->crowded);
        canvas_draw_str_aligned(canvas, 64, 44, AlignCenter, AlignTop, line);
    }

    // Offered whenever the run is over, which includes one that stopped collecting part way:
    // those counts are still worth a second attempt, and Back would throw them away.
    if(model->state != T5577MwTallyListening) {
        // The framed button every other screen in the app gets from the widget, so this one
        // does not have its own idea of what an OK affordance looks like.
        elements_button_center(canvas, "Again");
    }
}

static bool t5577_mw_tally_view_input(InputEvent* event, void* context) {
    T5577MwTallyView* instance = context;

    if(event->type != InputTypeShort || event->key != InputKeyOk) return false;

    bool over = false;
    with_view_model(
        instance->view,
        T5577MwTallyViewModel * model,
        { over = model->state != T5577MwTallyListening; },
        false);

    // Only once the run is over: restarting mid-run would throw away the counts that are the
    // whole point, and OK is the easiest key to hit by accident while holding a tag down. A run
    // that went quiet counts as over - it is not coming back on its own.
    if(!over || instance->callback == NULL) return false;

    instance->callback(instance->context);

    return true;
}

T5577MwTallyView* t5577_mw_tally_view_alloc(void) {
    T5577MwTallyView* instance = malloc(sizeof(T5577MwTallyView));

    instance->callback = NULL;
    instance->context = NULL;

    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(T5577MwTallyViewModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, t5577_mw_tally_view_draw);
    view_set_input_callback(instance->view, t5577_mw_tally_view_input);

    return instance;
}

void t5577_mw_tally_view_free(T5577MwTallyView* instance) {
    furi_assert(instance);

    view_free(instance->view);
    free(instance);
}

View* t5577_mw_tally_view_get_view(T5577MwTallyView* instance) {
    furi_assert(instance);

    return instance->view;
}

void t5577_mw_tally_view_set_callback(
    T5577MwTallyView* instance,
    T5577MwTallyViewCallback callback,
    void* context) {
    furi_assert(instance);

    instance->callback = callback;
    instance->context = context;
}

void t5577_mw_tally_view_reset(T5577MwTallyView* instance, uint16_t target) {
    furi_assert(instance);

    with_view_model(
        instance->view,
        T5577MwTallyViewModel * model,
        {
            memset(model, 0, sizeof(*model));
            model->target = target;
        },
        true);
}

void t5577_mw_tally_view_update(
    T5577MwTallyView* instance,
    const T5577MwTally* tally,
    uint16_t target) {
    furi_assert(instance);
    furi_assert(tally);

    // Committed only on a change, for the same reason as finish() below - except this one is
    // called on a 150ms timer for the whole run, so an unconditional commit repaints the screen
    // about seven times a second with identical pixels while a quiet tag is on the pad.
    //
    // reads is the exact test for the counts: every mutation of the tally happens alongside
    // reads++, so the counter moves whenever anything else does. The target moves at most twice
    // a run as the key count settles - 50, then 51 at three keys, 52 at four - and is compared
    // outright.
    bool changed = false;
    with_view_model(
        instance->view,
        T5577MwTallyViewModel * model,
        {
            changed = model->tally.reads != tally->reads || model->target != target;
            model->tally = *tally;
            model->target = target;
        },
        changed);
}

void t5577_mw_tally_view_finish(T5577MwTallyView* instance, bool complete) {
    furi_assert(instance);

    const T5577MwTallyState state = complete ? T5577MwTallyDone : T5577MwTallyQuiet;

    // Committed only on a change: the state moves once a run, and every commit is a mutex round
    // trip and a repaint flag on the GUI service.
    bool changed = false;
    with_view_model(
        instance->view,
        T5577MwTallyViewModel * model,
        {
            changed = model->state != state;
            model->state = state;
        },
        changed);
}
