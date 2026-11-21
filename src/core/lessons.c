#include "lessons.h"

#include <stdlib.h>
#include <string.h>

static char *dup(const char *s) { return strcpy(malloc(strlen(s) + 1), s); }

/* `*field` with `text` added as a new paragraph. */
static void append(char **field, const char *text)
{
    if (!*field) {
        *field = dup(text);
        return;
    }
    size_t n = strlen(*field);
    *field = realloc(*field, n + strlen(text) + 2);
    (*field)[n] = '\n';
    strcpy(*field + n + 1, text);
}

/* Splits `s` (changed in place) at spaces into at most `max` words. */
static int words(char *s, char **out, int max)
{
    int n = 0;
    for (char *w = strtok(s, " \t"); w && n < max; w = strtok(NULL, " \t"))
        out[n++] = dup(w);
    return n;
}

static void step_free(LessonStep *s)
{
    free(s->title), free(s->text), free(s->done), free(s->position), free(s->shape), free(s->hint), free(s->solution);
    for (int i = 0; i < s->ntargets; i++)
        free(s->targets[i]);
    for (int i = 0; i < s->nline; i++)
        free(s->line[i]);
}

static LessonStep *last_step(LessonBook *b)
{
    if (!b->n || !b->lessons[b->n - 1].nsteps)
        return NULL;
    Lesson *l = &b->lessons[b->n - 1];
    return &l->steps[l->nsteps - 1];
}

static void goal_line(LessonStep *s, char *rest)
{
    char *kind = strtok(rest, " \t"), *args = strtok(NULL, "");
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", args ? args : "");
    if (!kind)
        return;
    if (!strcmp(kind, "stars")) {
        s->goal = GOAL_STARS;
        s->ntargets = words(buf, s->targets, LESSON_TARGETS);
    } else if (!strcmp(kind, "capture")) {
        s->goal = GOAL_CAPTURE;
    } else if (!strcmp(kind, "shape")) {
        s->goal = GOAL_SHAPE;
        s->shape = dup(buf);
    } else if (!strcmp(kind, "line") || !strcmp(kind, "move")) {
        s->goal = GOAL_LINE;
        s->nline = words(buf, s->line, LESSON_LINE);
    } else if (!strcmp(kind, "mate")) {
        s->goal = GOAL_MATE;
        s->mate = atoi(buf) > 1 ? atoi(buf) : 1;
        if (s->mate > LESSON_MATE_MAX)
            s->mate = LESSON_MATE_MAX;
    } else if (!strcmp(kind, "check")) {
        s->goal = GOAL_CHECK;
    } else if (!strcmp(kind, "brink")) {
        s->goal = GOAL_BRINK;
    } else if (!strcmp(kind, "quiz")) {
        s->goal = GOAL_QUIZ;
        s->ntargets = words(buf, s->targets, LESSON_TARGETS);
    }
}

void lessons_add(LessonBook *b, const char *text, int variant)
{
    char *all = dup(text), *save = NULL, *chapter = NULL;
    for (char *line = strtok_r(all, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        while (*line == ' ' || *line == '\t')
            line++;
        size_t n = strlen(line);
        while (n && (line[n - 1] == ' ' || line[n - 1] == '\r' || line[n - 1] == '\t'))
            line[--n] = 0;
        if (!*line || *line == '#')
            continue;
        char *key = line, *rest = strchr(line, ' ');
        if (rest) {
            *rest++ = 0;
            while (*rest == ' ')
                rest++;
        } else {
            rest = line + n;
        }
        LessonStep *s = last_step(b);
        if (!strcmp(key, "chapter")) {
            free(chapter);
            chapter = dup(rest);
        } else if (!strcmp(key, "lesson")) {
            char *id = strtok(rest, " "), *title = strtok(NULL, "");
            if (!id)
                continue;
            b->lessons = realloc(b->lessons, (b->n + 1) * sizeof *b->lessons);
            Lesson *l = &b->lessons[b->n++];
            memset(l, 0, sizeof *l);
            l->id = dup(id);
            l->title = dup(title ? title : id);
            l->chapter = dup(chapter ? chapter : "");
            l->variant = variant;
        } else if (!strcmp(key, "about") && b->n) {
            append(&b->lessons[b->n - 1].about, rest);
        } else if (!strcmp(key, "step") && b->n) {
            Lesson *l = &b->lessons[b->n - 1];
            l->steps = realloc(l->steps, (l->nsteps + 1) * sizeof *l->steps);
            s = &l->steps[l->nsteps++];
            memset(s, 0, sizeof *s);
            s->title = dup(rest);
        } else if (!s) {
            continue;
        } else if (!strcmp(key, "text")) {
            append(&s->text, rest);
        } else if (!strcmp(key, "done")) {
            append(&s->done, rest);
        } else if (!strcmp(key, "position")) {
            free(s->position);
            s->position = dup(rest);
        } else if (!strcmp(key, "goal")) {
            goal_line(s, rest);
        } else if (!strcmp(key, "limit")) {
            s->limit = atoi(rest);
        } else if (!strcmp(key, "solution")) {
            free(s->solution);
            s->solution = dup(rest);
        } else if (!strcmp(key, "hint")) {
            free(s->hint);
            s->hint = dup(rest);
        }
    }
    free(chapter);
    free(all);
    /* A step without a position cannot be played: left out, and the lessons left empty too. */
    for (int i = 0; i < b->n; i++) {
        Lesson *l = &b->lessons[i];
        int k = 0;
        for (int j = 0; j < l->nsteps; j++)
            if (l->steps[j].position)
                l->steps[k++] = l->steps[j];
            else
                step_free(&l->steps[j]);
        l->nsteps = k;
    }
}

void lessons_free(LessonBook *b)
{
    for (int i = 0; i < b->n; i++) {
        Lesson *l = &b->lessons[i];
        for (int j = 0; j < l->nsteps; j++)
            step_free(&l->steps[j]);
        free(l->steps), free(l->id), free(l->title), free(l->about), free(l->chapter);
    }
    free(b->lessons);
    memset(b, 0, sizeof *b);
}

int lessons_find(const LessonBook *b, const char *id)
{
    for (int i = 0; i < b->n; i++)
        if (!strcmp(b->lessons[i].id, id))
            return i;
    return -1;
}

/* The entry of `id` in `done` and its length, or NULL. */
static const char *entry(const char *done, const char *id, size_t *len)
{
    size_t n = strlen(id);
    for (const char *p = done ? done : ""; *p;) {
        const char *end = strchr(p, ',');
        *len = end ? (size_t) (end - p) : strlen(p);
        if (*len >= n && !strncmp(p, id, n) && (*len == n || p[n] == ':'))
            return p;
        if (!end)
            break;
        p = end + 1;
    }
    return NULL;
}

bool lesson_passed(const char *done, const char *id)
{
    size_t len;
    return entry(done, id, &len) != NULL;
}

char *lesson_mark_passed(const char *done, const char *id)
{
    if (lesson_passed(done, id))
        return strdup(done);
    char *s = malloc((done ? strlen(done) : 0) + strlen(id) + 2);
    sprintf(s, "%s%s%s", done && *done ? done : "", done && *done ? "," : "", id);
    return s;
}

/* ---- playing a step ---- */

bool lesson_start(LessonRun *r, const LessonStep *step, Position *p)
{
    memset(r, 0, sizeof *r);
    r->step = step;
    if (!pos_from_text(p, step->position))
        return false;
    r->player = p->side;
    for (int i = 0; i < step->ntargets; i++)
        r->targets[i] = square_from_text(step->targets[i]);
    if (step->goal == GOAL_SHAPE && !pos_from_text(&r->shape, step->shape))
        return false;
    return true;
}

/* Whether `m` (legal on p) is `text`, or one of a/b/c. */
static bool is_move(Position *p, const Move *m, const char *text)
{
    char one[32];
    for (const char *s = text; *s;) {
        size_t n = strcspn(s, "/");
        snprintf(one, sizeof one, "%.*s", (int) (n < sizeof one ? n : sizeof one - 1), s);
        Move want;
        if (game_move_from_engine(p, one, &want) && want.from == m->from && want.to == m->to &&
            !want.promote == !m->promote)
            return true;
        s += n + (s[n] == '/');
    }
    return false;
}

static bool solo(const LessonStep *s) { return s->goal == GOAL_STARS || s->goal == GOAL_CAPTURE || s->goal == GOAL_SHAPE; }

static bool reached(LessonRun *r, const Position *p)
{
    const LessonStep *s = r->step;
    switch (s->goal) {
    case GOAL_STARS:
        for (int i = 0; i < s->ntargets; i++)
            if (!r->hit[i])
                return false;
        return true;
    case GOAL_CAPTURE:
        for (int sq = 0; sq < SQUARES; sq++)
            if (p->board[sq] && COLOR(p->board[sq]) != r->player)
                return false;
        return true;
    case GOAL_SHAPE:
        for (int sq = 0; sq < SQUARES; sq++) {
            uint8_t want = r->shape.board[sq];
            if (want && COLOR(want) == r->player && p->board[sq] != want)
                return false;
        }
        return true;
    default:
        return false;
    }
}

/* Mates of a few moves, as tsume are solved: every move of the attacker (the side to move) gives
 * check. A move list per ply, kept here (MAX_MOVES each is too large for some stacks). */
static Move mate_moves[2 * LESSON_MATE_MAX + 1][MAX_MOVES];

/* Whether the side to move mates in `n` moves or fewer, from ply `d` of mate_moves. */
static bool mates(Position *p, int n, int d)
{
    int c = pos_legal_moves(p, mate_moves[d]);
    for (int i = 0; i < c; i++) {
        Move m = mate_moves[d][i];
        pos_make(p, &m);
        bool ok = false;
        if (pos_in_check(p)) {
            int k = pos_legal_moves(p, mate_moves[d + 1]);
            ok = !k;
            for (int j = 0; j < k && n > 1; j++) {
                Move reply = mate_moves[d + 1][j];
                pos_make(p, &reply);
                ok = mates(p, n - 1, d + 2);
                pos_unmake(p, &reply);
                if (!ok)
                    break;
            }
        }
        pos_unmake(p, &m);
        if (ok)
            return true;
    }
    return false;
}

/* The fewest moves the side to move mates in, up to `n`; 0 for none. */
static int mate_length(Position *p, int n, int d)
{
    for (int k = 1; k <= n; k++)
        if (mates(p, k, d))
            return k;
    return 0;
}

/* The defence that holds out longest against a mate in `n` (the player having just given check):
 * false if one escapes it. */
static bool longest_defence(Position *p, int n, Move *best)
{
    Move replies[MAX_MOVES];
    int k = pos_legal_moves(p, replies), most = 0;
    for (int j = 0; j < k; j++) {
        pos_make(p, &replies[j]);
        int len = mate_length(p, n, 0);
        pos_unmake(p, &replies[j]);
        if (!len)
            return false;
        if (len > most)
            most = len, *best = replies[j];
    }
    return most > 0;
}

/* The mates in one the side to move has (checks that leave no move). */
static int mates_in_one(Position *p, int d)
{
    int c = pos_legal_moves(p, mate_moves[d]), n = 0;
    for (int i = 0; i < c; i++) {
        Move m = mate_moves[d][i];
        pos_make(p, &m);
        if (pos_in_check(p) && !pos_has_legal_move(p))
            n++;
        pos_unmake(p, &m);
    }
    return n;
}

/* After the player's quiet move: whether every defence loses to a mate in one; *best the one that leaves
 * the fewest mates (the hardest to finish). */
static bool brink_holds(Position *p, Move *best)
{
    Move replies[MAX_MOVES];
    int k = pos_legal_moves(p, replies), fewest = 0;
    for (int j = 0; j < k; j++) {
        pos_make(p, &replies[j]);
        int n = mates_in_one(p, 0);
        pos_unmake(p, &replies[j]);
        if (!n)
            return false;
        if (!fewest || n < fewest)
            fewest = n, *best = replies[j];
    }
    return fewest > 0;
}

LessonResult lesson_move(LessonRun *r, Position *p, const Move *m, Move *reply, bool *replied, const char **why)
{
    const LessonStep *s = r->step;
    *replied = false;
    *why = NULL;
    Move mv = *m;
    if (!pos_is_legal(p, &mv)) {
        *why = move_problem(p, m);
        return LESSON_WRONG;
    }
    if (s->goal == GOAL_LINE && (r->at >= s->nline || !is_move(p, &mv, s->line[r->at]))) {
        return LESSON_WRONG;
    }
    pos_make(p, &mv);
    if (s->goal == GOAL_BRINK && r->moves == 0) {
        bool check = pos_in_check(p);
        if (!check && brink_holds(p, reply)) {
            r->moves++;
            pos_make(p, reply);
            *replied = true;
            return LESSON_GOING;
        }
        pos_unmake(p, &mv);
        *why = check ? "No check here: find a quiet move that leaves the king no defense."
                     : "After that the other side can still defend: look for another.";
        return LESSON_WRONG;
    }
    if (s->goal == GOAL_MATE || s->goal == GOAL_CHECK || s->goal == GOAL_BRINK) {
        bool check = pos_in_check(p);
        if (check && (s->goal == GOAL_CHECK || !pos_has_legal_move(p)))
            return LESSON_DONE;
        int left = s->goal == GOAL_MATE ? s->mate - r->moves - 1 : 0; /* a brink's mate is in one */
        if (check && left > 0 && longest_defence(p, left, reply)) {
            r->moves++;
            pos_make(p, reply);
            *replied = true;
            return LESSON_GOING;
        }
        pos_unmake(p, &mv);
        *why = !check   ? "That is not check: the move does not attack the king."
               : left ? "After that check the king gets away: look for another."
                      : "That is not mate yet: the king still has a way out.";
        return LESSON_WRONG;
    }
    r->moves++;
    if (s->goal == GOAL_ANY)
        return LESSON_DONE;
    if (s->goal == GOAL_LINE) {
        r->at++;
        if (r->at < s->nline && game_move_from_engine(p, s->line[r->at], reply)) {
            pos_make(p, reply);
            *replied = true;
            r->at++;
        }
        return r->at >= s->nline ? LESSON_DONE : LESSON_GOING;
    }
    p->side = r->player; /* the other side passes */
    for (int i = 0; i < s->ntargets; i++)
        if (r->targets[i] == mv.to)
            r->hit[i] = true;
    if (reached(r, p))
        return LESSON_DONE;
    if (solo(s) && s->limit && r->moves >= s->limit) {
        *why = "Out of moves. Try again with fewer.";
        return LESSON_FAILED;
    }
    return LESSON_GOING;
}

LessonResult lesson_tap(LessonRun *r, int sq)
{
    for (int i = 0; i < r->step->ntargets; i++)
        if (r->targets[i] == sq) {
            r->hit[i] = true;
            return LESSON_DONE;
        }
    return LESSON_WRONG;
}

int lesson_targets_left(const LessonRun *r, int *squares)
{
    int n = 0;
    for (int i = 0; r->step && i < r->step->ntargets; i++)
        if (r->step->goal == GOAL_QUIZ ? r->hit[i] : !r->hit[i] && r->targets[i] >= 0) /* a quiz shows only the answer */
            squares[n++] = r->targets[i];
    return n;
}

bool lesson_hint(const LessonRun *r, Position *p, Move *m)
{
    const LessonStep *s = r->step;
    if (s->goal == GOAL_LINE && r->at < s->nline) {
        char one[32];
        snprintf(one, sizeof one, "%.*s", (int) strcspn(s->line[r->at], "/"), s->line[r->at]);
        return game_move_from_engine(p, one, m);
    }
    if (s->hint && r->moves == 0)
        return game_move_from_engine(p, s->hint, m);
    if (s->goal == GOAL_MATE || (s->goal == GOAL_BRINK && r->moves > 0)) { /* the check that keeps the mate */
        Move all[MAX_MOVES];
        int c = pos_legal_moves(p, all), left = s->goal == GOAL_MATE ? s->mate - r->moves : 1;
        for (int i = 0; i < c; i++) {
            Move mv = all[i], reply;
            pos_make(p, &mv);
            bool ok = pos_in_check(p) && (!pos_has_legal_move(p) || (left > 1 && longest_defence(p, left - 1, &reply)));
            pos_unmake(p, &mv);
            if (ok) {
                *m = mv;
                return true;
            }
        }
    }
    return false;
}
