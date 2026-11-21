/* The tutorial's lessons (lessons.c): reading, playing a step, and every lesson in data/lessons
 * solved by its own solution. No GTK. */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "i18n.h"
#include "lessons.h"
#include "texts.h"

static int failures;

#define EXPECT(cond)                                                                                            \
    do {                                                                                                       \
        if (!(cond)) {                                                                                         \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                             \
            failures++;                                                                                        \
        }                                                                                                      \
    } while (0)

static LessonResult move(LessonRun *r, Position *p, const char *usi, bool *replied)
{
    Move m, reply;
    const char *why;
    memset(&m, 0, sizeof m);
    if (!game_move_from_engine(p, usi, &m)) { /* not legal: the move as a piece can make it */
        Move all[MAX_MOVES];
        int n = pos_any_moves(p, all);
        char buf[16];
        for (int i = 0; i < n; i++) {
            move_to_usi(&all[i], buf);
            if (!strcmp(buf, usi))
                m = all[i];
        }
    }
    return lesson_move(r, p, &m, &reply, replied, &why);
}

static const char *BOOK =
    "# a comment\n"
    "chapter Basics\n"
    "lesson silver The silver\n"
    "about One step forward or diagonally\n"
    "step Stars\n"
    "text Collect the stars.\n"
    "text Second paragraph.\n"
    "position 9/9/9/9/9/4S4/9/9/9 b - 1\n"
    "goal stars 5e 4d\n"
    "limit 2\n"
    "hint 5f5e\n"
    "step No position: left out\n"
    "goal any\n"
    "lesson open An opening\n"
    "step Line\n"
    "position lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1\n"
    "goal line 7g7f/2g2f 3c3d 2g2f\n"
    "chapter Mates\n"
    "lesson mate1 Mate\n"
    "step Head gold\n"
    "position 4k4/9/4P4/9/9/9/9/9/4K4 b G 1\n"
    "goal mate\n"
    "solution G*5b\n"
    "step Capture\n"
    "position 9/9/9/9/4p4/9/4R4/9/9 b - 1\n"
    "goal capture\n"
    "step Shape\n"
    "position 9/9/9/9/9/9/9/9/4K4 b - 1\n"
    "goal shape 9/9/9/9/9/9/9/5K3/9 b - 1\n";

static void test_book(void)
{
    LessonBook b = {0};
    lessons_add(&b, BOOK, V_STANDARD);
    EXPECT(b.n == 3);
    EXPECT(lessons_find(&b, "open") == 1 && lessons_find(&b, "none") == -1);
    Lesson *l = &b.lessons[0];
    EXPECT(!strcmp(l->chapter, "Basics") && !strcmp(l->title, "The silver") && !strcmp(l->about, "One step forward or diagonally"));
    EXPECT(l->nsteps == 1 && !strcmp(l->steps[0].text, "Collect the stars.\nSecond paragraph."));
    EXPECT(l->steps[0].goal == GOAL_STARS && l->steps[0].ntargets == 2 && l->steps[0].limit == 2);
    EXPECT(!strcmp(b.lessons[2].chapter, "Mates") && b.lessons[2].nsteps == 3);

    LessonRun r;
    Position p;
    bool replied;
    EXPECT(lesson_start(&r, &l->steps[0], &p));
    int sq[4];
    EXPECT(lesson_targets_left(&r, sq) == 2);
    Move h;
    EXPECT(lesson_hint(&r, &p, &h));
    EXPECT(move(&r, &p, "5f4f", &replied) == LESSON_WRONG); /* a silver does not step sideways */
    EXPECT(move(&r, &p, "5f5e", &replied) == LESSON_GOING && p.side == FIRST && lesson_targets_left(&r, sq) == 1);
    EXPECT(move(&r, &p, "5e4d", &replied) == LESSON_DONE);
    lesson_start(&r, &l->steps[0], &p);
    move(&r, &p, "5f6e", &replied);
    EXPECT(move(&r, &p, "5d4d", &replied) == LESSON_WRONG); /* not a move: the limit not reached */
    EXPECT(move(&r, &p, "6e5d", &replied) == LESSON_FAILED); /* the limit reached, a target still left */
    lesson_start(&r, &l->steps[0], &p);
    EXPECT(move(&r, &p, "5f4f", &replied) == LESSON_WRONG);
    EXPECT(move(&r, &p, "5f5e", &replied) == LESSON_GOING && r.moves == 1); /* a wrong move is not a move */

    const LessonStep *line = &b.lessons[1].steps[0];
    lesson_start(&r, line, &p);
    EXPECT(move(&r, &p, "6g6f", &replied) == LESSON_WRONG && p.side == FIRST);
    EXPECT(move(&r, &p, "7g7f", &replied) == LESSON_GOING && replied && p.side == FIRST);
    EXPECT(move(&r, &p, "7f7e", &replied) == LESSON_WRONG);
    EXPECT(lesson_hint(&r, &p, &h));
    EXPECT(move(&r, &p, "2g2f", &replied) == LESSON_DONE);

    const LessonStep *mate = &b.lessons[2].steps[0];
    lesson_start(&r, mate, &p);
    EXPECT(move(&r, &p, "G*4b", &replied) == LESSON_WRONG);
    EXPECT(move(&r, &p, "G*5b", &replied) == LESSON_DONE);
    lesson_start(&r, &b.lessons[2].steps[1], &p);
    EXPECT(move(&r, &p, "5g5e", &replied) == LESSON_DONE);
    lesson_start(&r, &b.lessons[2].steps[2], &p);
    EXPECT(move(&r, &p, "5i4h", &replied) == LESSON_DONE);

    /* Mate in 3: a check that lets the king go is wrong; the defences and the hints follow the mate. */
    LessonBook m3 = {0};
    lessons_add(&m3, "lesson m3 Mate in three\nstep Three\nposition 9/6k2/4B3R/9/9/9/9/9/9 b G 1\ngoal mate 3\n", V_STANDARD);
    lesson_start(&r, &m3.lessons[0].steps[0], &p);
    EXPECT(m3.lessons[0].steps[0].mate == 3);
    EXPECT(move(&r, &p, "1c3c", &replied) == LESSON_WRONG);
    EXPECT(move(&r, &p, "1c1b+", &replied) == LESSON_GOING && replied && p.side == FIRST);
    LessonResult res = LESSON_GOING;
    for (int i = 0; i < 2 && res == LESSON_GOING; i++) {
        char hint[16];
        EXPECT(lesson_hint(&r, &p, &h));
        move_to_usi(&h, hint);
        res = move(&r, &p, hint, &replied);
    }
    EXPECT(res == LESSON_DONE);
    lessons_free(&m3);

    /* Brinkmate: a check is wrong, a quiet move that leaves a defence is wrong, the brink then its mate. */
    LessonBook br = {0};
    lessons_add(&br, "lesson br Brink\nstep One\nposition 4k4/7R1/9/9/9/9/9/9/9 b S 1\ngoal brink\n", V_STANDARD);
    lesson_start(&r, &br.lessons[0].steps[0], &p);
    EXPECT(move(&r, &p, "2b5b", &replied) == LESSON_WRONG);
    EXPECT(move(&r, &p, "S*9i", &replied) == LESSON_WRONG);
    EXPECT(move(&r, &p, "S*5c", &replied) == LESSON_GOING && replied && p.side == FIRST);
    char hint2[16];
    EXPECT(lesson_hint(&r, &p, &h));
    move_to_usi(&h, hint2);
    EXPECT(move(&r, &p, hint2, &replied) == LESSON_DONE);
    lessons_free(&br);

    char *d = lesson_mark_passed(NULL, "a"), *d2 = lesson_mark_passed(d, "bc"), *d3 = lesson_mark_passed(d2, "a");
    EXPECT(!strcmp(d2, "a,bc") && !strcmp(d2, d3) && lesson_passed(d2, "bc") && !lesson_passed(d2, "b"));
    EXPECT(lesson_passed("x:1,ab:3", "ab") && !lesson_passed("x:1,ab:3", "a")); /* the old stars */
    free(d), free(d2), free(d3);
    lessons_free(&b);
}

static void test_problem_texts(void)
{
    rules_set_variant(V_STANDARD);
    Position p;
    Move all[MAX_MOVES];
    pos_from_text(&p, "4k4/9/9/9/9/9/4P4/9/4K4 b P 1");
    int n = pos_any_moves(&p, all);
    int nifu = 0;
    for (int i = 0; i < n; i++)
        if (IS_DROP(all[i]) && COL(all[i].to) == 4 && move_problem(&p, &all[i]) && strstr(move_problem(&p, &all[i]), "二歩"))
            nifu++;
    EXPECT(nifu > 0);
    pos_from_text(&p, "9/9/9/9/9/4S4/9/9/9 b - 1");
    Move side = {.from = SQ(5, 4), .to = SQ(5, 5)};
    EXPECT(!strcmp(move_problem(&p, &side), "That piece does not move that way."));
}

/* A text with no row in a language: tr() hands back the English itself (a title already in Japanese,
 * as a proverb's, needs none). */
static void translated(const char *english, const char *where)
{
    if (!english || !english[strcspn(english, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz")])
        return;
    for (Language l = 1; l < LANGUAGE_COUNT; l++) {
        i18n_set_language(l);
        if (tr(english) == english) {
            printf("FAIL %s: no %s for \"%.60s\"\n", where, i18n_code(l), english);
            failures++;
        }
    }
    i18n_set_language(LANG_EN);
}

static void translated_paragraphs(const char *text, const char *where)
{
    if (!text)
        return;
    char *copy = strdup(text), *save = NULL;
    for (char *p = strtok_r(copy, "\n", &save); p; p = strtok_r(NULL, "\n", &save))
        translated(p, where);
    free(copy);
}

/* Every lesson of the game: each step starts, and its solution (or its line) solves it. */
static void test_data(void)
{
    DIR *dir = opendir("data/lessons");
    if (!dir)
        return;
    struct dirent *e;
    while ((e = readdir(dir))) {
        if (strncmp(e->d_name, "lessons", 7) || !strstr(e->d_name, ".txt"))
            continue;
        int variant = V_STANDARD;
        if (e->d_name[7] == '-')
            for (int v = 0; v < V_COUNT; v++)
                if (!strncmp(e->d_name + 8, variant_engine_name(v), strlen(variant_engine_name(v))) &&
                    e->d_name[8 + strlen(variant_engine_name(v))] == '.')
                    variant = v;
        char path[512];
        snprintf(path, sizeof path, "data/lessons/%s", e->d_name);
        FILE *f = fopen(path, "rb");
        if (!f)
            continue;
        static char text[1 << 20];
        size_t len = fread(text, 1, sizeof text - 1, f);
        text[len] = 0;
        fclose(f);
        rules_set_variant(variant);
        LessonBook b = {0};
        lessons_add(&b, text, variant);
        EXPECT(b.n > 0);
        for (int i = 0; i < b.n; i++) {
            translated(b.lessons[i].title, path), translated(b.lessons[i].about, path), translated(b.lessons[i].chapter, path);
            for (int j = 0; j < b.lessons[i].nsteps; j++) {
                const LessonStep *s = &b.lessons[i].steps[j];
                translated(s->title, path), translated_paragraphs(s->text, path), translated_paragraphs(s->done, path);
                LessonRun r;
                Position p;
                if (!lesson_start(&r, s, &p)) {
                    printf("FAIL %s: %s step %d: position\n", path, b.lessons[i].id, j + 1);
                    failures++;
                    continue;
                }
                const char *bad = setup_problem(&p); /* 二歩, a piece that could never move… (no kings is fine) */
                if (bad && strcmp(bad, "Each side needs one king.")) {
                    printf("FAIL %s: %s step %d: %s\n", path, b.lessons[i].id, j + 1, bad);
                    failures++;
                }
                char moves[2048];
                if (s->solution)
                    snprintf(moves, sizeof moves, "%s", s->solution);
                else if (s->goal == GOAL_LINE) { /* the player's moves of the line, the first of each a/b */
                    moves[0] = 0;
                    for (int k = 0; k < s->nline; k += 2)
                        snprintf(moves + strlen(moves), sizeof moves - strlen(moves), "%.*s ", (int) strcspn(s->line[k], "/"), s->line[k]);
                } else {
                    printf("FAIL %s: %s step %d: no solution\n", path, b.lessons[i].id, j + 1);
                    failures++;
                    continue;
                }
                LessonResult res = LESSON_GOING;
                bool replied;
                for (char *w = strtok(moves, " "); w && res == LESSON_GOING; w = strtok(NULL, " "))
                    res = s->goal == GOAL_QUIZ ? lesson_tap(&r, square_from_text(w)) : move(&r, &p, w, &replied);
                if (res != LESSON_DONE) {
                    printf("FAIL %s: %s step %d (%s): the solution gives %d\n", path, b.lessons[i].id, j + 1, s->title, res);
                    failures++;
                }
            }
        }
        lessons_free(&b);
    }
    closedir(dir);
}

int main(void)
{
    rules_init();
    rules_set_variant(V_STANDARD);
    test_book();
    test_problem_texts();
    test_data();
    for (int v = 0; v < V_COUNT; v++)
        translated(VARIANT_LABELS[v], "VARIANT_LABELS"), translated(VARIANT_RULES[v], "VARIANT_RULES");
    if (failures) {
        printf("%d lesson test(s) failed\n", failures);
        return 1;
    }
    printf("lesson tests passed\n");
    return 0;
}
