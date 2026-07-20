#include "morse_scene.h"
#include "../morse_trainer_i.h"
#include <furi_hal_random.h>
#include <stdio.h>

/* All quiz logic runs on the dispatcher thread: view input and timer
 * callbacks arrive here as custom events, so no extra locking is needed. */

static char quiz_current_char(QuizState* q) {
    return q->challenge ? q->target[q->letter_index] : q->target[0];
}

static bool quiz_current_is_decoding(QuizState* q) {
    return q->q_is_decoding;
}

static void quiz_play_current(MorseTrainerApp* app) {
    char text[2] = {quiz_current_char(&app->quiz), '\0'};
    morse_player_play_text(app->player, text);
}

static void quiz_build_choices(QuizState* q) {
    char target = quiz_current_char(q);
    uint8_t n = q->learned_count;
    uint8_t count = (n < 4) ? n : 4;
    q->choice_count = count;
    q->choice_sel = 0;

    char distractors[3];
    uint8_t d = 0;
    while(d < count - 1) {
        char c = q->learned[furi_hal_random_get() % n];
        if(c == target) continue;
        bool dup = false;
        for(uint8_t k = 0; k < d; k++) {
            if(distractors[k] == c) dup = true;
        }
        if(!dup) distractors[d++] = c;
    }
    uint8_t target_pos = furi_hal_random_get() % count;
    uint8_t j = 0;
    for(uint8_t i = 0; i < count; i++) {
        q->choices[i] = (i == target_pos) ? target : distractors[j++];
    }
}

static void quiz_render(MorseTrainerApp* app) {
    QuizState* q = &app->quiz;
    TrainerModel* m = &app->tm;
    memset(m, 0, sizeof(TrainerModel));
    q->hint_shown = false; /* any re-render clears the hint overlay */

    if(q->challenge) {
        snprintf(
            m->title,
            sizeof(m->title),
            "Word %u/%u  Miss:%u",
            (unsigned)(q->q_index + 1),
            (unsigned)CHALLENGE_WORDS,
            (unsigned)q->mistakes);
    } else {
        snprintf(
            m->title,
            sizeof(m->title),
            "Q %u/%u  OK:%u",
            (unsigned)(q->q_index + 1),
            (unsigned)q->total_questions,
            (unsigned)q->correct);
    }
    if(q->hints_left) {
        char hint[8];
        snprintf(hint, sizeof(hint), "  ?%u", (unsigned)q->hints_left);
        strlcat(m->title, hint, sizeof(m->title));
    }

    if(!quiz_current_is_decoding(q)) {
        m->screen = TrainerScreenEncode;
        strlcpy(m->prompt, q->target, sizeof(m->prompt));
        m->prompt_hilite = q->challenge ? q->letter_index : 0xFF;
        strlcpy(m->pattern, q->keyed, sizeof(m->pattern));
        strlcpy(m->footer, "OK: key    <: clear", sizeof(m->footer));
    } else {
        m->screen = TrainerScreenDecode;
        if(q->challenge) {
            /* Reveal answered letters, mask the rest. */
            size_t len = strlen(q->target);
            for(size_t i = 0; i < len && i < sizeof(m->prompt) - 1; i++) {
                m->prompt[i] = (i < q->letter_index) ? q->target[i] : '_';
            }
            m->prompt_hilite = q->letter_index;
        } else {
            strlcpy(m->prompt, "?", sizeof(m->prompt));
            m->prompt_hilite = 0xFF;
        }
        /* Visuals setting: 0 never, 1 auto (L1-L3 training wheels), 2 always. */
        m->show_pattern = (app->settings.visual == 2) ||
                          (app->settings.visual == 1 && app->level < 3);
        if(m->show_pattern) {
            strlcpy(m->pattern, morse_code_for(quiz_current_char(q)), sizeof(m->pattern));
        }
        memcpy(m->choices, q->choices, sizeof(m->choices));
        m->choice_count = q->choice_count;
        m->choice_sel = q->choice_sel;
    }

    trainer_view_set_model(app->trainer_view, m);
}

static void quiz_setup_question_type(MorseTrainerApp* app) {
    QuizState* q = &app->quiz;
    if(app->mode == TrainModeEncoding) {
        q->q_is_decoding = false;
    } else if(app->mode == TrainModeDecoding) {
        q->q_is_decoding = true;
    } else {
        /* Mixed: challenge words alternate, quiz questions are random. */
        q->q_is_decoding = q->challenge ? (q->q_index % 2 == 1) : (furi_hal_random_get() & 1);
    }
}

/* Review questions favor letters with a bad track record: weight 1 + up to
 * 5 extra per recorded miss, so a letter you keep failing shows up more. */
static char quiz_pick_review_char(MorseTrainerApp* app) {
    QuizState* q = &app->quiz;
    uint16_t total = 0;
    for(uint8_t i = 0; i < q->learned_count; i++) {
        int mi = morse_char_index(q->learned[i]);
        uint8_t miss = (mi >= 0) ? app->progress.misses[mi] : 0;
        total += 1 + ((miss > 5) ? 5 : miss);
    }
    uint16_t r = furi_hal_random_get() % total;
    for(uint8_t i = 0; i < q->learned_count; i++) {
        int mi = morse_char_index(q->learned[i]);
        uint8_t miss = (mi >= 0) ? app->progress.misses[mi] : 0;
        uint16_t w = 1 + ((miss > 5) ? 5 : miss);
        if(r < w) return q->learned[i];
        r -= w;
    }
    return q->learned[q->learned_count - 1];
}

static void quiz_setup_regular_question(MorseTrainerApp* app) {
    QuizState* q = &app->quiz;
    uint8_t new_count = strlen(q->new_chars);
    char c;
    if(q->q_index < new_count * QUIZ_REPS_PER_NEW) {
        /* Guarantee every new letter appears QUIZ_REPS_PER_NEW times before
         * the mixed-review tail — repetition first, then recall. The cycling
         * order never repeats back-to-back on its own. */
        c = q->new_chars[q->q_index % new_count];
    } else {
        /* Reroll so the same char never comes up twice in a row. */
        uint8_t tries = 0;
        do {
            if(new_count && (furi_hal_random_get() % 100) < 60) {
                c = q->new_chars[furi_hal_random_get() % new_count];
            } else {
                c = quiz_pick_review_char(app);
            }
        } while(c == q->last_char && ++tries < 8);
    }
    q->last_char = c;
    q->target[0] = c;
    q->target[1] = '\0';
    q->keyed[0] = '\0';
    quiz_setup_question_type(app);
    if(q->q_is_decoding) quiz_build_choices(q);
}

/* Draw CHALLENGE_WORDS distinct words from the level's pool, so each run
 * sees a different selection. */
static void quiz_pick_words(MorseTrainerApp* app) {
    QuizState* q = &app->quiz;
    const MorseLevel* level = curriculum_get(app->level);
    for(uint8_t i = 0; i < CHALLENGE_WORDS; i++) {
        uint8_t p;
        bool dup;
        do {
            p = furi_hal_random_get() % level->word_count;
            dup = false;
            for(uint8_t j = 0; j < i; j++) {
                if(q->word_pick[j] == p) dup = true;
            }
        } while(dup);
        q->word_pick[i] = p;
    }
}

static void quiz_setup_challenge_word(MorseTrainerApp* app) {
    QuizState* q = &app->quiz;
    const MorseLevel* level = curriculum_get(app->level);
    strlcpy(q->target, level->word_pool[q->word_pick[q->q_index]], sizeof(q->target));
    q->letter_index = 0;
    q->keyed[0] = '\0';
    quiz_setup_question_type(app);
    if(q->q_is_decoding) quiz_build_choices(q);
}

static void quiz_start_question(MorseTrainerApp* app) {
    quiz_render(app);
    if(quiz_current_is_decoding(&app->quiz)) quiz_play_current(app);
}

static void quiz_finish(MorseTrainerApp* app) {
    QuizState* q = &app->quiz;
    furi_timer_stop(app->commit_timer);
    furi_timer_stop(app->feedback_timer);
    q->finished = true;
    if(q->challenge) {
        bool completed = q->q_index >= CHALLENGE_WORDS;
        q->passed = completed && q->mistakes <= CHALLENGE_MAX_MISS;
        if(q->passed) {
            q->stars = (q->mistakes == 0) ? 3 : (q->mistakes == 1) ? 2 : 1;
        } else {
            q->stars = 0;
        }
    } else {
        q->passed = q->correct >= q->pass_score;
        if(!q->passed) {
            q->stars = 0;
        } else if(q->correct >= q->total_questions) {
            q->stars = 3;
        } else if(q->correct >= q->star2_score) {
            q->stars = 2;
        } else {
            q->stars = 1;
        }
    }
    scene_manager_next_scene(app->scene_manager, MorseSceneResults);
}

/* Shared bookkeeping for every answered question: lifetime stats, per-char
 * miss counts (drives adaptive review), and LED color feedback. */
static void quiz_record_answer(MorseTrainerApp* app, bool correct) {
    QuizState* q = &app->quiz;
    app->progress.total_answered++;
    if(correct) {
        app->progress.total_correct++;
    } else {
        int mi = morse_char_index(quiz_current_char(q));
        if(mi >= 0 && app->progress.misses[mi] < 250) app->progress.misses[mi]++;
    }
    if(app->settings.led) {
        notification_message(
            app->notifications, correct ? &sequence_blink_green_10 : &sequence_blink_red_10);
    }
}

static void quiz_advance(MorseTrainerApp* app);

static void quiz_show_wrong_feedback(MorseTrainerApp* app) {
    QuizState* q = &app->quiz;
    q->last_correct = false;
    q->hint_shown = false; /* real feedback supersedes an open hint overlay */
    TrainerModel* m = &app->tm;
    m->hint1[0] = '\0';
    m->hint2[0] = '\0';
    char c = quiz_current_char(q);
    snprintf(m->feedback, sizeof(m->feedback), "Wrong!  %c = %s", c, morse_code_for(c));
    m->feedback_good = false;
    morse_player_play_tone(app->player, MORSE_BAD_HZ, 300);
    furi_timer_start(app->feedback_timer, furi_ms_to_ticks(FEEDBACK_WRONG_MS));
    trainer_view_set_model(app->trainer_view, m);
}

/* Correct answers never show a banner: green LED (+ a short beep on encode)
 * and the next question appears instantly, keeping the flow. Wrong answers
 * still pause to show the right pattern — that's where learning happens. */
static void quiz_handle_answer(MorseTrainerApp* app, bool correct) {
    QuizState* q = &app->quiz;
    quiz_record_answer(app, correct);
    if(correct) {
        q->last_correct = true;
        q->hint_shown = false;
        /* No beep before decode playback: the next letter's audio IS the cue,
         * and a tone submitted first would just get aborted by it. */
        if(!q->q_is_decoding) morse_player_play_tone(app->player, MORSE_GOOD_HZ, 80);
        quiz_advance(app);
    } else {
        quiz_show_wrong_feedback(app);
    }
}

static void quiz_evaluate_encode(MorseTrainerApp* app) {
    QuizState* q = &app->quiz;
    if(q->keyed[0] == '\0') return;
    const char* expected = morse_code_for(quiz_current_char(q));
    bool correct = (expected != NULL) && (strcmp(q->keyed, expected) == 0);
    q->keyed[0] = '\0';
    if(q->challenge) {
        if(!correct) q->mistakes++;
    } else {
        if(correct) q->correct++;
    }
    quiz_handle_answer(app, correct);
}

static void quiz_evaluate_decode(MorseTrainerApp* app) {
    QuizState* q = &app->quiz;
    bool correct = q->choices[q->choice_sel] == quiz_current_char(q);
    if(q->challenge) {
        if(!correct) q->mistakes++;
    } else {
        if(correct) q->correct++;
    }
    quiz_handle_answer(app, correct);
}

static void quiz_advance(MorseTrainerApp* app) {
    QuizState* q = &app->quiz;
    if(!q->challenge) {
        q->q_index++;
        if(q->q_index >= q->total_questions) {
            quiz_finish(app);
            return;
        }
        quiz_setup_regular_question(app);
        quiz_start_question(app);
        return;
    }

    /* Challenge: too many mistakes ends the run immediately. */
    if(q->mistakes > CHALLENGE_MAX_MISS) {
        quiz_finish(app);
        return;
    }
    if(q->last_correct) {
        q->letter_index++;
        if(q->letter_index >= strlen(q->target)) {
            q->q_index++;
            if(q->q_index >= CHALLENGE_WORDS) {
                quiz_finish(app);
                return;
            }
            quiz_setup_challenge_word(app);
            quiz_start_question(app);
            return;
        }
        /* Next letter within the word. */
        if(q->q_is_decoding) quiz_build_choices(q);
    }
    /* Wrong answer: retry the same letter. */
    quiz_render(app);
    if(quiz_current_is_decoding(q)) quiz_play_current(app);
}

void morse_scene_quiz_on_enter(void* context) {
    MorseTrainerApp* app = context;
    QuizState* q = &app->quiz;
    memset(q, 0, sizeof(QuizState));

    const MorseLevel* level = curriculum_get(app->level);
    q->challenge = curriculum_is_challenge(app->level);
    q->new_chars = level->new_chars;
    curriculum_learned_upto(app->level, q->learned, sizeof(q->learned));
    q->learned_count = strlen(q->learned);
    q->hints_left = HINTS_PER_QUIZ;
    /* Speed ramp: graduates (L10+) hear slightly faster characters. */
    morse_player_set_dot_ms(app->player, (app->level >= 9) ? 65 : MORSE_DOT_MS);

    uint8_t new_count = strlen(q->new_chars);
    q->total_questions = new_count * QUIZ_REPS_PER_NEW + QUIZ_REVIEW_EXTRA;
    /* Pass at 80%, second star at 90%, third at a perfect run. */
    q->pass_score = (q->total_questions * 8 + 9) / 10;
    q->star2_score = (q->total_questions * 9 + 9) / 10;

    if(q->challenge) {
        quiz_pick_words(app);
        quiz_setup_challenge_word(app);
    } else {
        quiz_setup_regular_question(app);
    }
    quiz_start_question(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, MorseViewTrainer);
}

bool morse_scene_quiz_on_event(void* context, SceneManagerEvent event) {
    MorseTrainerApp* app = context;
    QuizState* q = &app->quiz;

    if(event.type == SceneManagerEventTypeBack) {
        /* First Back arms a warning banner; a second Back while it shows (or
         * Back during answer feedback) abandons the run to level select,
         * skipping the teach scene sitting between them in history. */
        if(!q->quit_armed && !furi_timer_is_running(app->feedback_timer)) {
            q->quit_armed = true;
            TrainerModel* m = &app->tm;
            strlcpy(m->feedback, "Back again to quit", sizeof(m->feedback));
            m->feedback_good = false;
            trainer_view_set_model(app->trainer_view, m);
            furi_timer_start(app->feedback_timer, furi_ms_to_ticks(FEEDBACK_WRONG_MS));
            return true;
        }
        furi_timer_stop(app->commit_timer);
        furi_timer_stop(app->feedback_timer);
        morse_player_stop(app->player);
        scene_manager_search_and_switch_to_previous_scene(
            app->scene_manager, MorseSceneLevelSelect);
        return true;
    }
    if(event.type != SceneManagerEventTypeCustom) return false;

    switch(event.event) {
    case MTEventKeyDot:
    case MTEventKeyDash: {
        size_t len = strlen(q->keyed);
        if(len < sizeof(q->keyed) - 1) {
            q->keyed[len] = (event.event == MTEventKeyDot) ? '.' : '-';
            q->keyed[len + 1] = '\0';
        }
        furi_timer_restart(app->commit_timer, furi_ms_to_ticks(COMMIT_PAUSE_MS));
        quiz_render(app);
        return true;
    }
    case MTEventKeyClear:
        q->keyed[0] = '\0';
        furi_timer_stop(app->commit_timer);
        quiz_render(app);
        return true;
    case MTEventCommit:
        quiz_evaluate_encode(app);
        return true;
    case MTEventChoicePrev:
        /* Selection wraps at both ends. */
        if(q->choice_count)
            q->choice_sel = (q->choice_sel + q->choice_count - 1) % q->choice_count;
        quiz_render(app);
        return true;
    case MTEventChoiceNext:
        if(q->choice_count) q->choice_sel = (q->choice_sel + 1) % q->choice_count;
        quiz_render(app);
        return true;
    case MTEventHint: {
        if(!q->q_is_decoding) {
            /* Encode hint (Up): overlay the level's letters with their
             * patterns. Stays until Up again or any other action; doesn't
             * freeze input, so you can keep keying with it open. */
            if(q->hint_shown) {
                q->hint_shown = false;
                quiz_render(app);
                return true;
            }
            if(q->hints_left == 0) return true;
            q->hints_left--;
            TrainerModel* m = &app->tm;
            m->hint1[0] = '\0';
            m->hint2[0] = '\0';
            uint8_t new_count = strlen(q->new_chars);
            if(q->challenge || new_count == 0) {
                char c = quiz_current_char(q);
                snprintf(m->hint1, sizeof(m->hint1), "%c  %s", c, morse_code_for(c));
            } else {
                uint8_t half = (new_count + 1) / 2;
                for(uint8_t i = 0; i < new_count; i++) {
                    char* line = (i < half) ? m->hint1 : m->hint2;
                    char part[12];
                    snprintf(
                        part,
                        sizeof(part),
                        "%c %s   ",
                        q->new_chars[i],
                        morse_code_for(q->new_chars[i]));
                    strlcat(line, part, sizeof(m->hint1));
                }
            }
            trainer_view_set_model(app->trainer_view, m);
            q->hint_shown = true;
            return true;
        }
        /* Decode hint (Down): 50/50 — drop all but one wrong choice. Only
         * useful with >2 choices. */
        if(q->hints_left == 0 || q->choice_count <= 2) return true;
        q->hints_left--;
        char target = quiz_current_char(q);
        char keep;
        do {
            keep = q->choices[furi_hal_random_get() % q->choice_count];
        } while(keep == target);
        if(furi_hal_random_get() & 1) {
            q->choices[0] = target;
            q->choices[1] = keep;
        } else {
            q->choices[0] = keep;
            q->choices[1] = target;
        }
        q->choice_count = 2;
        q->choice_sel = 0;
        quiz_render(app);
        return true;
    }
    case MTEventChoiceConfirm:
        quiz_evaluate_decode(app);
        return true;
    case MTEventReplay:
        if(quiz_current_is_decoding(q)) quiz_play_current(app);
        return true;
    case MTEventFeedbackDone:
        if(q->quit_armed) {
            /* Quit warning expired without a second Back: resume play. */
            q->quit_armed = false;
            quiz_render(app);
            return true;
        }
        quiz_advance(app);
        return true;
    }
    return false;
}

void morse_scene_quiz_on_exit(void* context) {
    MorseTrainerApp* app = context;
    furi_timer_stop(app->commit_timer);
    furi_timer_stop(app->feedback_timer);
    morse_player_stop(app->player);
}
