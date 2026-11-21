#ifndef LESSONS_H
#define LESSONS_H

#include <stdbool.h>

#include "rules.h"

/* The tutorial's lessons: data/lessons/lessons.txt for the standard game and lessons-<variant>.txt
 * (the engine's name of the variant) for the others, read by lessons_add(); the format is in
 * docs/CODE.md. Texts are English, translated with tr() where they are shown. A step is played by
 * a LessonRun: the player's move goes in, and out come whether it was right, the reply and when the
 * step is done. No GTK. */

typedef enum {
    GOAL_ANY,     /* any legal move */
    GOAL_STARS,   /* reach every target square, the other side passing */
    GOAL_CAPTURE, /* take every piece of the other side, which passes */
    GOAL_SHAPE,   /* the player's pieces where `shape` has them, the other side passing */
    GOAL_LINE,    /* the moves of `line`, the player's and the replies by turns */
    GOAL_MATE,    /* checkmate in `mate` moves, each giving check; the other side defends as long as it can */
    GOAL_CHECK,   /* a move that gives check */
    GOAL_BRINK,   /* 必至: a quiet move after which every defence loses to a mate in one, then that mate */
    GOAL_QUIZ,    /* tap a target square (reading notation and kanji); nothing moves */
} LessonGoal;

#define LESSON_TARGETS 32
#define LESSON_LINE 64
#define LESSON_MATE_MAX 4

typedef struct {
    char *title, *text, *done, *position, *shape, *hint;
    char *solution; /* the player's moves that solve it, for the tests (make test plays them) */
    LessonGoal goal;
    char *targets[LESSON_TARGETS]; /* squares as the game writes them (5d) */
    int ntargets;
    char *line[LESSON_LINE]; /* moves as the engine writes them; the player's may be a/b/c */
    int nline;
    int limit; /* the most moves where the other side passes (past them the step fails), 0: no limit */
    int mate;  /* GOAL_MATE: in how many of the player's moves (1 to LESSON_MATE_MAX) */
} LessonStep;

typedef struct {
    char *id, *title, *about, *chapter;
    int variant;
    LessonStep *steps;
    int nsteps;
} Lesson;

typedef struct {
    Lesson *lessons;
    int n;
} LessonBook;

/* Adds the lessons of `text`, all of `variant`; lines it does not know are left out. */
void lessons_add(LessonBook *b, const char *text, int variant);
void lessons_free(LessonBook *b);
/* The lesson with this id, or -1. */
int lessons_find(const LessonBook *b, const char *id);
/* `done` keeps the passed lessons' ids separated by commas (an "id:n" from the old stars counts
 * as passed). Whether `id` is there; and `done` with `id` added if it was not (free it). */
bool lesson_passed(const char *done, const char *id);
char *lesson_mark_passed(const char *done, const char *id);

typedef enum { LESSON_GOING, LESSON_WRONG, LESSON_DONE, LESSON_FAILED } LessonResult;

typedef struct {
    const LessonStep *step;
    int player, moves, at; /* the player's color, moves made, the next move of a line */
    int targets[LESSON_TARGETS];
    bool hit[LESSON_TARGETS];
    Position shape;
} LessonRun;

/* Starts `step` (the variant already set) with its position in *p; FALSE if it cannot be read. */
bool lesson_start(LessonRun *r, const LessonStep *step, Position *p);
/* The player's move `m` on *p (any move a piece can make, legal or not). WRONG leaves *p as it was
 * and says why (English, NULL for the step's own "not this one"); GOING and DONE play it, and a
 * reply if there is one (*replied, *reply). FAILED: the moves ran out, *p played; start again. */
LessonResult lesson_move(LessonRun *r, Position *p, const Move *m, Move *reply, bool *replied, const char **why);
/* A tap on `sq` in a GOAL_QUIZ step: DONE on a target, else WRONG. */
LessonResult lesson_tap(LessonRun *r, int sq);
/* The targets not reached yet (squares), as many as there are. */
int lesson_targets_left(const LessonRun *r, int *squares);
/* The move to suggest: the step's hint, the line's next move or the mate's; FALSE for none. */
bool lesson_hint(const LessonRun *r, Position *p, Move *m);

#endif
