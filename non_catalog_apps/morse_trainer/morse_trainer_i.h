#pragma once

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/widget.h>
#include <gui/modules/variable_item_list.h>
#include <notification/notification_messages.h>

#include "morse_events.h"
#include "helpers/morse.h"
#include "helpers/curriculum.h"
#include "helpers/progress.h"
#include "views/trainer_view.h"

#define MT_TAG "MorseTrainer"

/* How long after the last keyed symbol a letter auto-commits. The strict
 * 3-dot gap (240 ms) is too fast for beginners keying by hand, so this is
 * deliberately relaxed. */
#define COMMIT_PAUSE_MS   700
#define FEEDBACK_WRONG_MS 1500

/* Quiz length scales with how many new letters a level introduces:
 * 2 guaranteed reps of each new letter + review questions. A 2-letter
 * level is a short 8-question set, a 5-letter digit level goes to 14 —
 * beginners get more practice exactly where more material was added. */
#define QUIZ_REPS_PER_NEW  2
#define QUIZ_REVIEW_EXTRA  4
#define CHALLENGE_MAX_MISS 3
#define HINTS_PER_QUIZ     2

typedef enum {
    MorseSceneMenu,
    MorseSceneLevelSelect,
    MorseSceneTeach,
    MorseSceneQuiz,
    MorseSceneResults,
    MorseSceneProgress,
    MorseSceneSettings,
    MorseSceneAbout,
    MorseScenePractice,
    MorseSceneCount,
} MorseScene;

typedef enum {
    MorseViewSubmenu,
    MorseViewWidget,
    MorseViewTrainer,
    MorseViewVarItemList,
} MorseViewId;

typedef struct {
    bool challenge;
    bool q_is_decoding; /* current question type */
    uint8_t q_index; /* question index, or word 0..2 in challenges */
    uint8_t total_questions; /* regular quiz length for this level */
    uint8_t pass_score; /* 80% of total, rounded up */
    uint8_t star2_score; /* 90% of total, rounded up */
    uint8_t hints_left;
    uint8_t correct; /* regular quiz score */
    uint8_t mistakes; /* challenge mistake count */
    char target[MAX_WORD_LEN + 2]; /* current letter (len 1) or word */
    uint8_t letter_index; /* position within challenge word */
    uint8_t word_pick[CHALLENGE_WORDS]; /* indices drawn from the word pool */
    char last_char; /* previous question char; avoids back-to-back repeats */
    bool hint_shown; /* letter-hint overlay is up (timer running) */
    char keyed[8]; /* symbols keyed so far */
    char choices[4];
    uint8_t choice_count;
    uint8_t choice_sel;
    char learned[MAX_LEARNED];
    uint8_t learned_count;
    const char* new_chars;
    bool last_correct;
    bool quit_armed; /* first Back pressed; second Back within banner quits */
    bool finished; /* set before switching to results */
    bool passed;
    uint8_t stars;
} QuizState;

typedef struct {
    Gui* gui;
    NotificationApp* notifications;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    Submenu* submenu;
    Widget* widget;
    VariableItemList* var_item_list;
    TrainerView* trainer_view;
    MorsePlayer* player;
    FuriTimer* commit_timer; /* encode: end-of-letter pause */
    FuriTimer* feedback_timer; /* feedback banner duration */

    MorseProgress progress;
    MorseSettings settings;
    uint8_t mode; /* TrainMode */
    uint8_t level; /* 0-based */
    uint8_t teach_index;
    QuizState quiz;
    char practice_text[12]; /* decoded characters in free practice */
    char practice_keyed[8]; /* current keyed symbols in free practice */
    TrainerModel tm; /* scratch snapshot pushed into the trainer view */
} MorseTrainerApp;
