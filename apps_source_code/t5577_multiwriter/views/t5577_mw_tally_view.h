/**
 * @file t5577_mw_tally_view.h
 * @brief Which key a reader actually got, and how often.
 *
 * A multi-key tag sends its keys back to back with nothing to say where one ends, so a reader
 * takes whichever frame it happens to decode. Reading the tag once says only that *a* key is
 * there; counting many frames is what says every key is reachable, and in what proportion.
 */
#pragma once

#include <gui/view.h>

#include "../t5577_mw_listener.h"

typedef struct T5577MwTallyView T5577MwTallyView;

typedef void (*T5577MwTallyViewCallback)(void* context);

T5577MwTallyView* t5577_mw_tally_view_alloc(void);

void t5577_mw_tally_view_free(T5577MwTallyView* instance);

View* t5577_mw_tally_view_get_view(T5577MwTallyView* instance);

/** What OK does once the run has finished. */
void t5577_mw_tally_view_set_callback(
    T5577MwTallyView* instance,
    T5577MwTallyViewCallback callback,
    void* context);

/** Start a run counting towards `target` frames. */
void t5577_mw_tally_view_reset(T5577MwTallyView* instance, uint16_t target);

/** Show the counts as they stand, against a target that may have moved.
 *
 * The target is not known at the start of a run: it is rounded up to a whole number of cycles
 * once the run has seen how many keys the tag sends, so it settles after the first few frames.
 */
void t5577_mw_tally_view_update(
    T5577MwTallyView* instance,
    const T5577MwTally* tally,
    uint16_t target);

/** End the run.
 *
 * `complete` when it reached its target, so its counts cover whole cycles and every key had the
 * same number of turns; otherwise it stopped collecting before it got there and the counts are
 * whatever it managed. Either way the run is over and the screen offers to run it again - one
 * call rather than two flags, so the two endings cannot both be set.
 */
void t5577_mw_tally_view_finish(T5577MwTallyView* instance, bool complete);
