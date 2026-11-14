/**
 * @file t5577_mw_work_view.h
 * @brief What a write, a check or a lock is doing, step by step.
 *
 * Every one of those takes several seconds - a detect is about a second and reading a block
 * back is about a second and a half - so they run as timed steps with the screen in between,
 * and this is what the screen says while they do.
 */
#pragma once

#include <gui/view.h>

typedef struct T5577MwWorkView T5577MwWorkView;

T5577MwWorkView* t5577_mw_work_view_alloc(void);

void t5577_mw_work_view_free(T5577MwWorkView* instance);

View* t5577_mw_work_view_get_view(T5577MwWorkView* instance);

/** What the screen is for, and how many steps the bar is counting towards. */
void t5577_mw_work_view_start(T5577MwWorkView* instance, const char* title, uint8_t total);

/** Where it has got to: a line of its own, and a step count for the bar. */
void t5577_mw_work_view_set_status(T5577MwWorkView* instance, const char* status, uint8_t done);

/** The bar full and the last word on the line.
 *
 * The view already knows the total, so a walk that has finished should not have to restate it -
 * and the bar otherwise stops one step short of the end, because the status is set before each
 * step runs rather than after.
 */
void t5577_mw_work_view_finish(T5577MwWorkView* instance, const char* status);
