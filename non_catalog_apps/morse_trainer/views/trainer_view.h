#pragma once

#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <notification/notification.h>
#include "../helpers/progress.h"

typedef enum {
    TrainerScreenTeach,
    TrainerScreenEncode,
    TrainerScreenDecode,
    TrainerScreenPractice,
} TrainerScreen;

/* Snapshot the quiz engine pushes into the view. The engine (scene code)
 * owns the authoritative state; the view only renders this copy, which keeps
 * GUI-thread draws race-free via the view model lock. */
typedef struct {
    TrainerScreen screen;
    char title[24]; /* top-left, e.g. "Teach 1/2" or "Q 3/10" */
    char prompt[10]; /* big centered text: letter or word */
    uint8_t prompt_hilite; /* index of active letter in word, 0xFF = none */
    char pattern[8]; /* ./- string drawn as shapes */
    bool show_pattern;
    char choices[4];
    uint8_t choice_count;
    uint8_t choice_sel;
    char footer[28]; /* hint line at the bottom */
    char feedback[28]; /* non-empty = feedback banner, input frozen */
    bool feedback_good;
    uint16_t key_held_ms; /* live OK-hold duration; drives the dot/dash bar */
    /* Letter-hint overlay (encode questions): two lines of "X .-" pairs.
     * Unlike feedback, it does NOT freeze input — keying stays fluent. */
    char hint1[26];
    char hint2[26];
} TrainerModel;

typedef struct TrainerView TrainerView;

TrainerView* trainer_view_alloc(
    ViewDispatcher* view_dispatcher,
    const MorseSettings* settings,
    NotificationApp* notifications);
void trainer_view_free(TrainerView* trainer_view);
View* trainer_view_get_view(TrainerView* trainer_view);

/* Copy a new snapshot in and request a redraw. */
void trainer_view_set_model(TrainerView* trainer_view, const TrainerModel* model);
