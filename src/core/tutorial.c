/* The tutorial (☰ → Help → Tutorial): the lessons of lessons.c in a list, and a lesson played on
 * the board, its step in a passive drawer beside it (as the game over notice: the board stays in
 * use). A lesson is a game between two players from the step's position; its moves come here
 * instead of play.c's, and lessons.c says whether they were right. */

#include "app.h"
#include "lessons.h"
#include "texts.h"

enum { RESPONSE_AGAIN = 1, RESPONSE_HINT, RESPONSE_SKIP, RESPONSE_NEXT = GTK_RESPONSE_ACCEPT }; /* Next drawn as the main one */

static LessonBook book;
static gboolean loaded;

static struct {
    int lesson, step; /* -1: no lesson */
    gboolean skipped; /* a step of the lesson skipped: it is not passed */
    LessonRun run;
    gboolean done, failed, pending;
    GtkWidget *dialog, *where, *text, *status, *hint, *skip, *next;
} T = {.lesson = -1};

static void load(void)
{
    if (loaded)
        return;
    loaded = TRUE;
    for (int v = 0; v < V_COUNT; v++) {
        char *name = v == V_STANDARD ? g_strdup("lessons.txt") : g_strdup_printf("lessons-%s.txt", variant_engine_name(v));
        char *path = g_build_filename(app.base, "lessons", name, NULL), *text = NULL;
        if (g_file_get_contents(path, &text, NULL, NULL))
            lessons_add(&book, text, v);
        g_free(text);
        g_free(path);
        g_free(name);
    }
}

gboolean lesson_playing(void) { return T.lesson >= 0 && !T.pending; }

static const Lesson *lesson(void) { return &book.lessons[T.lesson]; }
static const LessonStep *step(void) { return &lesson()->steps[T.step]; }

static char *done_ids(void) { return setting_string("LessonsDone", ""); }

/* Large, as the moves left must be seen at a glance. */
static void set_status(const char *text, gboolean good)
{
    char *s = g_markup_printf_escaped(good ? "<span size=\"x-large\" weight=\"bold\">✓ %s</span>" : "<span size=\"x-large\" weight=\"bold\">%s</span>", text);
    gtk_label_set_markup(GTK_LABEL(T.status), s);
    g_free(s);
}

/* While the step goes on: Hint and Skip; once it is done, Next, the main button. */
static void show_buttons(void)
{
    gtk_widget_set_visible(T.hint, !T.done);
    gtk_widget_set_visible(T.skip, !T.done);
    gtk_widget_set_visible(T.next, T.done);
    if (T.done) /* Space and Enter go on */
        gtk_widget_grab_focus(T.next);
    else /* nothing of the drawer in focus, so that Space does not skip the step */
        gtk_window_set_focus(GTK_WINDOW(app.window), NULL);
}

/* The step's moves when it counts them: against the limit where the other side passes, the ones
 * left in a mate. */
static void show_moves_left(void)
{
    const LessonStep *s = step();
    char *moves = s->goal == GOAL_MATE && s->mate > 1 ? g_strdup_printf(tr("Moves left: %d"), s->mate - T.run.moves)
                  : s->goal == GOAL_BRINK            ? g_strdup_printf(tr("Moves left: %d"), 2 - T.run.moves)
                  : s->limit                         ? g_strdup_printf(tr("Moves: %d of %d"), T.run.moves, s->limit)
                                                     : g_strdup("");
    set_status(moves, FALSE);
    g_free(moves);
}

static void show_goals(void)
{
    int sq[LESSON_TARGETS], n = lesson_targets_left(&T.run, sq);
    view_set_goals(app.view, sq, T.done ? 0 : n);
}

/* A step's text is translated a paragraph (a `text` line) at a time; free the result. */
static char *tr_paragraphs(const char *text)
{
    char **lines = g_strsplit(text ? text : "", "\n", -1);
    for (int i = 0; lines[i]; i++) {
        char *t = g_strdup(tr(lines[i]));
        g_free(lines[i]);
        lines[i] = t;
    }
    char *all = g_strjoinv("\n", lines);
    g_strfreev(lines);
    return all;
}

static void show_step(void)
{
    const LessonStep *s = step();
    char *where = g_strdup_printf(tr("Step %d of %d: %s"), T.step + 1, lesson()->nsteps, tr(s->title));
    gtk_label_set_text(GTK_LABEL(T.where), where);
    g_free(where);
    char *text = tr_paragraphs(s->text);
    gtk_label_set_text(GTK_LABEL(T.text), text);
    g_free(text);
    show_moves_left();
}

/* The step's position on the board, as a new game between two players. */
static void start_step(void)
{
    const LessonStep *s = step();
    net_stop(); /* a lesson is a game of one's own, as a problem */
    online_stop();
    server_stop();
    app.variant = lesson()->variant;
    g_free(app.start_sfen);
    app.start_sfen = g_strdup(s->position);
    T.pending = TRUE; /* start_game keeps the lesson */
    start_game(HANDICAP_NONE, MODE_TWO_PLAYERS);
    T.pending = FALSE;
    Position p;
    lesson_start(&T.run, s, &p);
    T.done = T.failed = FALSE;
    show_buttons();
    view_set_physical(app.view, 2); /* any square: a wrong move reaches the lesson, which says why */
    view_set_hint(app.view, NULL);
    show_goals();
    show_step();
}

int lesson_player(void) { return T.run.player; }

static void lesson_passed_now(void)
{
    char *ids = done_ids(), *now = lesson_mark_passed(ids, lesson()->id);
    g_key_file_set_string(app.settings, "Settings", "LessonsDone", now);
    save_settings();
    free(now);
    g_free(ids);
}

static void step_done(void)
{
    const LessonStep *s = step();
    T.done = TRUE;
    show_goals();
    show_buttons();
    gboolean last = T.step + 1 >= lesson()->nsteps;
    set_status(s->done ? tr(s->done) : !last ? tr("Well done!") : T.skipped ? tr("Lesson finished, but a step was skipped: not passed yet.") : tr("Lesson complete!"),
               !last || !T.skipped);
    if (last) {
        if (!T.skipped)
            lesson_passed_now();
        gtk_button_set_label(GTK_BUTTON(T.next), T.lesson + 1 < book.n ? tr("Next lesson") : tr("Lessons"));
    } else {
        gtk_button_set_label(GTK_BUTTON(T.next), tr("Next"));
    }
    play_sound(last ? "win" : "notify");
}

void tutorial_move(const Move *m)
{
    if (T.done || T.failed) {
        view_refresh(app.view); /* the piece back: the step waits for Next or Again */
        return;
    }
    Position p = app.game.pos;
    Move mv = *m, legal = *m, reply;
    gboolean ok = pos_is_legal(&app.game.pos, &legal);
    bool replied;
    const char *why;
    LessonResult r = lesson_move(&T.run, &p, &mv, &reply, &replied, &why);
    if (r == LESSON_WRONG) {
        set_status(why ? tr(why) : tr("Not this move: try another one."), FALSE);
        play_sound("wrong");
        view_refresh(app.view);
        return;
    }
    view_set_hint(app.view, NULL);
    if (step()->goal == GOAL_STARS || step()->goal == GOAL_CAPTURE || step()->goal == GOAL_SHAPE) {
        /* the other side passes: the move kept in the game (its count tells the view a tap moved,
         * and its squares stay marked), the position after it with the player's turn again */
        if (app.game.count < MAX_PLY) {
            app.game.moves[app.game.count] = legal;
            app.game.times[app.game.count] = 0;
            app.game.gave_check[app.game.count] = FALSE;
            app.game.count++;
            app.game.hashes[app.game.count] = p.hash;
        }
        app.game.pos = p;
    } else if (ok) {
        game_play(&app.game, &legal, 0);
        if (replied)
            game_play(&app.game, &reply, 0);
    }
    play_sound(legal.captured ? "capture" : "move");
    view_set_ply(app.view, -1);
    view_refresh(app.view);
    show_goals();
    if (r == LESSON_DONE)
        step_done();
    else if (r == LESSON_FAILED) {
        T.failed = TRUE; /* no more moves until Again */
        set_status(tr(why), FALSE);
        view_set_goals(app.view, NULL, 0);
    }
    else
        show_moves_left();
}

gboolean tutorial_tap(int sq)
{
    if (!lesson_playing() || T.lesson < 0 || step()->goal != GOAL_QUIZ)
        return FALSE;
    if (T.done || T.failed)
        return TRUE;
    if (lesson_tap(&T.run, sq) == LESSON_WRONG) {
        set_status(tr("Not this square: try another one."), FALSE);
        play_sound("wrong");
        return TRUE;
    }
    show_goals();
    step_done();
    return TRUE;
}

void tutorial_hint(void)
{
    Move m;
    Position p = app.game.pos;
    if (T.failed)
        return;
    if (!T.done && lesson_hint(&T.run, &p, &m)) {
        view_set_hint(app.view, &m);
    } else if (!T.done) {
        set_status(tr("No hint here: read the step again."), FALSE);
    }
}

void tutorial_again(void)
{
    if (T.lesson >= 0)
        start_step();
}

static void close_card(void)
{
    if (T.dialog) {
        GtkWidget *d = T.dialog;
        T.dialog = NULL;
        gtk_widget_destroy(d);
    }
}

void lesson_end(void)
{
    if (T.pending || T.lesson < 0)
        return;
    T.lesson = -1;
    view_set_goals(app.view, NULL, 0);
    view_set_physical(app.view, illegal_rule());
    close_card();
}

static gboolean after_lesson; /* the list opened from a lesson: closing it plays a game again */

static gboolean list_idle(gpointer data)
{
    (void) data;
    after_lesson = TRUE;
    tutorial_dialog();
    return FALSE;
}

static void open_lesson(int i);

static void card_response_cb(GtkDialog *d, int r, gpointer data)
{
    (void) d, (void) data;
    if (r == RESPONSE_AGAIN) {
        start_step();
    } else if (r == RESPONSE_HINT) {
        tutorial_hint();
    } else if (r == RESPONSE_NEXT || r == RESPONSE_SKIP) {
        if (r == RESPONSE_SKIP)
            T.skipped = TRUE;
        if (T.step + 1 < lesson()->nsteps) {
            T.step++;
            start_step();
        } else if (T.lesson + 1 < book.n) {
            open_lesson(T.lesson + 1);
        } else {
            lesson_end();
            g_idle_add(list_idle, NULL);
        }
    } else { /* closed: back to the lessons */
        lesson_end();
        g_idle_add(list_idle, NULL);
    }
}

static void open_lesson(int i)
{
    close_card();
    T.lesson = i;
    T.step = 0;
    T.skipped = FALSE;
    GtkWidget *d = T.dialog = dialog_new(tr(lesson()->title));
    drawer_set_passive(d);
    GtkWidget *box = GTK_DIALOG(d)->vbox;
    T.where = row_text("", TRUE, FALSE, TRUE);
    gtk_box_pack_start(GTK_BOX(box), T.where, FALSE, FALSE, 4);
    T.text = row_para("");
    gtk_box_pack_start(GTK_BOX(box), T.text, FALSE, FALSE, 4);
    T.status = row_para(""); /* long messages wrap instead of widening the drawer */
    gtk_box_pack_start(GTK_BOX(box), T.status, FALSE, FALSE, 4);
    gtk_dialog_add_button(GTK_DIALOG(d), tr("Again"), RESPONSE_AGAIN);
    T.hint = gtk_dialog_add_button(GTK_DIALOG(d), tr("Hint"), RESPONSE_HINT);
    T.skip = gtk_dialog_add_button(GTK_DIALOG(d), tr("Skip"), RESPONSE_SKIP);
    T.next = gtk_dialog_add_button(GTK_DIALOG(d), tr("Next"), RESPONSE_NEXT);
    g_signal_connect(d, "response", G_CALLBACK(card_response_cb), NULL);
    gtk_widget_show_all(d);
    start_step();
}

typedef struct {
    GtkWidget *dialog;
    int chosen;
} List;

static void lesson_clicked_cb(GtkButton *b, gpointer data)
{
    List *l = data;
    l->chosen = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "lesson"));
    gtk_dialog_response(GTK_DIALOG(l->dialog), GTK_RESPONSE_OK);
}

/* The lessons by chapter, the passed ones ticked; the variants' after the standard game's. */
void tutorial_dialog(void)
{
    load();
    if (!book.n) {
        message(tr("There are no lessons in this installation."));
        return;
    }
    List l = {dialog_new(tr("Tutorial")), -1};
    GtkWidget *page = GTK_DIALOG(l.dialog)->vbox, *card = NULL;
    if (PROVERB_COUNT) { /* a new one each day */
        rows_group(page, tr("Proverb of the day"));
        const char *const *p = PROVERBS[(time(NULL) / 86400) % PROVERB_COUNT];
        char *text = g_strdup_printf("%s\n%s", p[0], tr(p[1]));
        GtkWidget *para = row_add(rows_card(page), row_para(text));
        gtk_misc_set_padding(GTK_MISC(para), 10, 6);
        g_free(text);
    }
    char *ids = done_ids();
    const char *chapter = NULL;
    int variant = -1, first = -1;
    for (int i = 0; i < book.n; i++) {
        const Lesson *s = &book.lessons[i];
        if (!chapter || strcmp(chapter, s->chapter) || variant != s->variant) {
            char *title = s->variant == V_STANDARD ? g_strdup(tr(s->chapter))
                                                   : g_strdup_printf("%s: %s", variant_label(s->variant), tr(s->chapter));
            rows_group(page, title);
            g_free(title);
            card = rows_card(page);
            chapter = s->chapter;
            variant = s->variant;
        }
        gboolean passed = lesson_passed(ids, s->id);
        if (!passed && first < 0)
            first = i;
        char num[16];
        snprintf(num, sizeof num, "%d", i + 1);
        GtkWidget *b = row_add(card, row_section(passed ? "✓" : num, tr(s->title), s->about ? tr(s->about) : ""));
        g_object_set_data(G_OBJECT(b), "lesson", GINT_TO_POINTER(i));
        g_signal_connect(b, "clicked", G_CALLBACK(lesson_clicked_cb), &l);
        if (i == first)
            gtk_widget_grab_focus(b);
    }
    g_free(ids);
    gtk_dialog_add_button(GTK_DIALOG(l.dialog), tr("Close"), GTK_RESPONSE_CANCEL);
    gtk_widget_show_all(l.dialog);
    gtk_dialog_run(GTK_DIALOG(l.dialog));
    gtk_widget_destroy(l.dialog);
    gboolean back = after_lesson;
    after_lesson = FALSE;
    if (l.chosen >= 0) {
        open_lesson(l.chosen);
    } else if (back) { /* the board had the lesson's position: a game as at the start instead */
        Mode m = (Mode) CLAMP(setting_int("Mode", MODE_FIRST), 0, MODE_COUNT - 1);
        app.variant = CLAMP(setting_int("Variant", app.variant), 0, V_COUNT - 1);
        start_game((Handicap) CLAMP(setting_int("Handicap", 0), 0, HANDICAP_COUNT - 1), connected_mode(m) ? MODE_FIRST : m);
    }
}

void tutorial_offer(void)
{
    load();
    if (!book.n || setting_int("TutorialOffered", 0) || app.net || app.over_dialog)
        return;
    g_key_file_set_integer(app.settings, "Settings", "TutorialOffered", 1);
    save_settings();
    GtkWidget *d = dialog_new(tr("New to shōgi?"));
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox),
                       row_para(tr("Short lessons, a few minutes each, teach the moves on the board. They are also in the menu under Help → Tutorial.")),
                       FALSE, FALSE, 6);
    gtk_dialog_add_button(GTK_DIALOG(d), tr("Just play"), GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(d), tr("Start the tutorial"), GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
    gtk_widget_show_all(d);
    int r = gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
    if (r == GTK_RESPONSE_OK)
        open_lesson(0);
}

