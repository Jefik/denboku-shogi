/* Game analysis, as on lishogi and lichess: Fairy-Stockfish at full strength looks at each
 * position of the game in turn, in the background. Each move gets the evaluation after it, the
 * best moves before it (one per line, MultiPV) and a mark when it lost winning chances, judged as
 * lishogi does (modules/analyse, Advice.scala and Accuracy.scala); each side gets lichess's
 * accuracy (AccuracyPercent.scala) on lishogi's winning chances. The options (time, CPUs, memory,
 * lines, arrows, marks on the board) are lichess's analysis menu. */

#include "app.h"

#include <math.h>
#ifndef G_OS_WIN32
#include <unistd.h>
#endif

#define MATE 100000 /* a mate in n plies scores MATE - n */
#define CEILING 5500 /* lishogi's limit on centipawns, and what a mate counts as in the loss */
#define MAX_LINES 5

typedef struct {
    gboolean done, has_best;
    int score; /* the first player's view */
    int side;  /* to move */
    Move best;
    Move lines[MAX_LINES]; /* the first move of each line, best first */
    int nlines;
} Eval;

/* The options, kept in settings.ini. */
static const int SECONDS[] = {1, 2, 5, 10};
static const int MEMORY_MB[] = {16, 64, 256, 512};
static int option(const char *key, int fallback, int count) { return CLAMP(setting_int(key, fallback), 0, count - 1); }
static int search_ms(void) { return SECONDS[option("AnalysisTime", 0, G_N_ELEMENTS(SECONDS))] * 1000; }
static int lines_wanted(void) { return option("AnalysisLines", 0, MAX_LINES) + 1; }
static gboolean show_arrows(void) { return setting_int("AnalysisArrows", 1); }

static Engine *engine;
static Game *snap; /* the game analyzed */
static Eval *evals;
static int npos, next; /* positions to look at, the one being looked at */
static int info_score;
static gboolean has_info;
static char info_pv[MAX_LINES][16]; /* the first move of each line, as the engine spells it */
static int searching_lines;
static GtkWidget *dialog, *progress, *graph, *times_graph;
/* The summary: per side, the inaccuracy, mistake and blunder buttons, the loss and the accuracy. */
static GtkWidget *mark_buttons[2][3], *loss_labels[2], *accuracy_labels[2], *side_labels[2];

static gboolean running(void) { return engine != NULL; }

/* The analysis of the position after `ply` moves, if it is this game's. */
static const Eval *eval_at(int ply)
{
    if (!snap || ply < 0 || ply >= npos || ply > app.game.count || !evals[ply].done ||
        snap->hashes[ply] != app.game.hashes[ply])
        return NULL;
    return &evals[ply];
}

static gboolean is_mate(int s) { return abs(s) > MATE - 1000; }
static int ceiled(int s) { return CLAMP(s, -CEILING, CEILING); }
static double winning_chances(int s) { return 2 / (1 + exp(-0.0007 * ceiled(s))) - 1; }

/* The mark for move `ply` (1 based): 0 none, 1 inaccuracy, 2 mistake, 3 blunder. */
static int judge(int ply)
{
    const Eval *a = eval_at(ply - 1), *b = eval_at(ply);
    if (!a || !b)
        return 0;
    int sign = a->side == FIRST ? 1 : -1, p = sign * a->score, n = sign * b->score;
    if (!is_mate(p) && !is_mate(n)) {
        double delta = winning_chances(p) - winning_chances(n);
        return delta >= 0.3 ? 3 : delta >= 0.2 ? 2 : delta >= 0.1 ? 1 : 0;
    }
    if (!is_mate(p) && n < 0) /* walked into a mate */
        return p < -4000 ? 1 : p < -1000 ? 2 : 3;
    if (is_mate(p) && p > 0 && (!is_mate(n) || n < 0)) /* let a mate go */
        return is_mate(n) ? 3 : n > 4000 ? 1 : n > 1000 ? 2 : 3;
    return 0;
}

/* Centipawns move `ply` gave away, -1 when it is not known. */
static int loss(int ply)
{
    const Eval *a = eval_at(ply - 1), *b = eval_at(ply);
    if (!a || !b)
        return -1;
    int diff = ceiled(b->score) - ceiled(a->score);
    return MAX(0, a->side == FIRST ? -diff : diff);
}

/* lichess's accuracy (AccuracyPercent.scala) on lishogi's winning chances: each move's accuracy
 * from the fall of its side's win percentage, then for the game the mean of a mean weighted by how
 * much the position was swinging and of the harmonic mean. FALSE until enough is analyzed. */
static double win_percent(int s) { return 50 + 50 * winning_chances(s); }

static double move_accuracy(double before, double after)
{
    if (after >= before)
        return 100;
    return CLAMP(103.1668100711649 * exp(-0.04354415386753951 * (before - after)) - 3.166924740191411 + 1, 0, 100);
}

static gboolean accuracy(int color, double *out)
{
    static double wp[MAX_PLY + 1];
    int n = 0;
    while (n <= app.game.count && eval_at(n))
        wp[n] = win_percent(eval_at(n)->score), n++;
    if (n < 2)
        return FALSE;
    int window = CLAMP(n / 10, 2, 8);
    double weighted = 0, weights = 0, inverse = 0;
    int moves = 0;
    for (int i = 0; i + 1 < n; i++) {
        if (eval_at(i)->side != color)
            continue;
        int start = i < window - 2 ? 0 : MIN(i - (window - 2), n - window);
        double mean = 0, var = 0;
        for (int j = start; j < start + window; j++)
            mean += wp[j] / window;
        for (int j = start; j < start + window; j++)
            var += (wp[j] - mean) * (wp[j] - mean) / window;
        double w = CLAMP(sqrt(var), 0.5, 12);
        double before = color == FIRST ? wp[i] : 100 - wp[i], after = color == FIRST ? wp[i + 1] : 100 - wp[i + 1];
        double a = move_accuracy(before, after);
        weighted += a * w, weights += w, inverse += 1 / MAX(a, 1), moves++;
    }
    if (!moves)
        return FALSE;
    *out = (weighted / weights + moves / inverse) / 2;
    return TRUE;
}

static void format_score(int s, char *buf, int size)
{
    if (is_mate(s))
        snprintf(buf, size, "#%s%d", s < 0 ? "-" : "", MATE - abs(s));
    else
        snprintf(buf, size, "%+.1f", s / 100.0);
}

/* The score in words, as Japanese commentary says it (互角, 有利, 優勢, 勝勢): even within 3 pawns, then
 * slightly better, better and winning past 8 and 20; a mate found is winning. */
static void score_words(int s, char *buf, int size)
{
    int a = abs(s);
    if (!is_mate(s) && a < 300) {
        snprintf(buf, size, "%s", tr("Equal"));
        return;
    }
    const char *side = tr(SIDE_NAMES[s > 0 ? FIRST : SECOND]);
    snprintf(buf, size, tr(is_mate(s) || a >= 2000 ? "%s is winning" : a >= 800 ? "%s is better" : "%s is slightly better"), side);
}

static void best_move_text(int ply, const Move *m, char *buf, int size)
{
    Position p;
    game_position_at(&app.game, ply, &p);
    Move copy = *m;
    move_notation(&p, &copy, ply ? app.game.moves[ply - 1].to : -1, notation_ja(), buf, size);
}

static int cores(void)
{
#ifdef G_OS_WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int) si.dwNumberOfProcessors;
#else
    return (int) sysconf(_SC_NPROCESSORS_ONLN);
#endif
}

static void search(void)
{
    char *position = g_malloc(16384);
    int count = snap->count;
    snap->count = next;
    game_engine_position(snap, position, 16384);
    snap->count = count;
    has_info = FALSE;
    memset(info_pv, 0, sizeof info_pv);
    char go[32];
    snprintf(go, sizeof go, "movetime %d", search_ms());
    engine_go(engine, position, go);
    g_free(position);
}

static gboolean free_engine_cb(gpointer data)
{
    engine_free(data);
    return FALSE;
}

/* Stops looking (the results so far stay). */
static void stop(void)
{
    if (!engine)
        return;
    engine_cancel(engine);
    /* Not from inside the engine's own callbacks. */
    g_idle_add(free_engine_cb, engine);
    engine = NULL;
}

static void update_dialog(void)
{
    if (!dialog)
        return;
    char text[160] = "";
    if (running())
        snprintf(text, sizeof text, "%s %d / %d", tr("Analyzing…"), next, npos);
    gtk_label_set_text(GTK_LABEL(progress), text);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), RESPONSE_DELETE, running());
    /* Per side: inaccuracies, mistakes, blunders, average centipawn loss, accuracy. */
    int marks[2][4] = {{0}}, lost[2] = {0}, moves[2] = {0};
    for (int ply = 1; ply <= app.game.count; ply++) {
        const Eval *a = eval_at(ply - 1);
        int l = loss(ply);
        if (!a || l < 0)
            continue;
        marks[a->side][judge(ply)]++;
        lost[a->side] += l;
        moves[a->side]++;
    }
    for (int c = 0; c < 2; c++) {
        char label[96];
        snprintf(label, sizeof label, "%s %s", SIDE_SYMBOLS[c], app.game.names[c]);
        gtk_label_set_text(GTK_LABEL(side_labels[c]), label);
        for (int k = 0; k < 3; k++) {
            snprintf(label, sizeof label, "%d", marks[c][k + 1]);
            gtk_button_set_label(GTK_BUTTON(mark_buttons[c][k]), label);
            gtk_widget_set_sensitive(mark_buttons[c][k], marks[c][k + 1] > 0);
        }
        if (moves[c])
            snprintf(text, sizeof text, "%d", lost[c] / moves[c]);
        else
            snprintf(text, sizeof text, "—");
        gtk_label_set_text(GTK_LABEL(loss_labels[c]), text);
        double acc;
        if (accuracy(c, &acc))
            snprintf(text, sizeof text, "%.0f%%", acc);
        else
            snprintf(text, sizeof text, "—");
        gtk_label_set_text(GTK_LABEL(accuracy_labels[c]), text);
    }
    gtk_widget_queue_draw(times_graph);
    gtk_widget_queue_draw(graph);
}

/* The move list, the top bar and the dialog follow the analysis. */
static void refresh(void)
{
    update_move_label();
    update_dialog();
}

/* "info … multipv 2 score cp 31 … pv 7g7f 3c3d": line 1's score, and each line's first move. */
static void engine_info_cb(const char *line, void *data)
{
    (void) data;
    char **tok = g_strsplit(line, " ", -1);
    int pv = 1;
    gboolean bound = FALSE, scored = FALSE;
    int score = 0;
    for (int i = 0; tok[i]; i++) {
        if (!strcmp(tok[i], "multipv") && tok[i + 1])
            pv = atoi(tok[i + 1]);
        else if (!strcmp(tok[i], "lowerbound") || !strcmp(tok[i], "upperbound"))
            bound = TRUE;
        else if (!strcmp(tok[i], "score") && tok[i + 1] && tok[i + 2]) {
            int v = atoi(tok[i + 2]);
            if (!strcmp(tok[i + 1], "cp"))
                score = v, scored = TRUE;
            else if (!strcmp(tok[i + 1], "mate"))
                score = v > 0 ? MATE - v : -(MATE + v), scored = TRUE; /* mate 0: mated */
        } else if (!strcmp(tok[i], "pv") && tok[i + 1] && !bound && pv >= 1 && pv <= MAX_LINES) {
            snprintf(info_pv[pv - 1], sizeof info_pv[0], "%s", tok[i + 1]);
            break;
        }
    }
    if (scored && !bound && pv == 1)
        info_score = score, has_info = TRUE;
    g_strfreev(tok);
}

static void bestmove_cb(const char *usi, void *data)
{
    (void) data;
    Position p;
    game_position_at(snap, next, &p);
    Eval *e = &evals[next];
    e->side = p.side;
    /* No score when the game is over there: mated ("resign") or a won impasse ("win"). */
    int score = has_info ? info_score : strcmp(usi, "win") ? -MATE : MATE;
    e->score = p.side == FIRST ? score : -score;
    e->has_best = game_move_from_engine(&p, usi, &e->best);
    e->nlines = 0;
    if (e->has_best)
        e->lines[e->nlines++] = e->best;
    for (int k = 1; k < searching_lines; k++) {
        Position q = p;
        Move m;
        if (info_pv[k][0] && game_move_from_engine(&q, info_pv[k], &m) && memcmp(&m, &e->best, sizeof m))
            e->lines[e->nlines++] = m;
    }
    e->done = TRUE;
    next++;
    /* The game went another way (undo, a new game): nothing more of this one to look at. */
    if (next >= npos || next > app.game.count || snap->hashes[next] != app.game.hashes[next])
        stop();
    else
        search();
    refresh();
}

static void died_cb(void *data)
{
    (void) data;
    stop();
    refresh();
}

static void start(void)
{
    g_free(snap);
    g_free(evals);
    snap = g_memdup(&app.game, sizeof app.game);
    /* After an illegal move the last position cannot be searched. */
    npos = snap->count + (snap->reason == REASON_ILLEGAL || snap->reason == REASON_KING ? 0 : 1);
    evals = g_new0(Eval, npos);
    next = 0;
    EngineCallbacks cb = {NULL, bestmove_cb, died_cb, engine_info_cb};
    engine = engine_new(engine_path(), &cb, NULL);
    if (!engine) {
        char *text = g_strdup_printf("%s\n%s", tr("Could not start the engine:"), engine_path());
        message(text);
        g_free(text);
        return;
    }
    engine_set_option(engine, "USI_Variant", engine_variant());
    char threads[16], hash[16], lines[16];
    snprintf(threads, sizeof threads, "%d",
             app.kindle ? 1 : CLAMP(setting_int("AnalysisThreads", CLAMP(cores() - 1, 1, 4)), 1, cores()));
    snprintf(hash, sizeof hash, "%d", app.kindle ? 8 : MEMORY_MB[option("AnalysisMemory", 1, G_N_ELEMENTS(MEMORY_MB))]);
    searching_lines = lines_wanted();
    snprintf(lines, sizeof lines, "%d", searching_lines);
    engine_set_option(engine, "Threads", threads);
    engine_set_option(engine, "Hash", hash);
    engine_set_option(engine, "MultiPV", lines);
    engine_set_option(engine, "USI_Ponder", "false");
    engine_new_game(engine);
    search();
}

/* ---- the dialog ---- */

static void set_rgb(cairo_t *cr, const GdkColor *c, double alpha)
{
    cairo_set_source_rgba(cr, c->red / 65535.0, c->green / 65535.0, c->blue / 65535.0, alpha);
}

/* Sente's winning chances move by move, sente's side up; marks on the mistakes and a line at
 * the move shown. */
static gboolean graph_expose_cb(GtkWidget *w, GdkEventExpose *ev, gpointer data)
{
    (void) ev, (void) data;
    GtkAllocation a;
    gtk_widget_get_allocation(w, &a);
    GtkStyle *st = gtk_widget_get_style(w);
    cairo_t *cr = gdk_cairo_create(gtk_widget_get_window(w));
    double W = a.width, H = a.height, mid = H / 2;
    int n = MAX(app.game.count, 1);
    set_rgb(cr, &st->base[GTK_STATE_NORMAL], 1);
    cairo_paint(cr);
    set_rgb(cr, &st->text[GTK_STATE_NORMAL], 1);
    cairo_set_line_width(cr, 2);
    cairo_rectangle(cr, 1, 1, W - 2, H - 2);
    cairo_stroke(cr);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, 0, mid);
    cairo_line_to(cr, W, mid);
    cairo_stroke(cr);
    /* Which side the curve favors: ▲ (the first player) above the zero line, △ below it. */
    PangoLayout *pl = gtk_widget_create_pango_layout(w, NULL);
    PangoAttrList *small = pango_attr_list_new();
    pango_attr_list_insert(small, pango_attr_scale_new(0.8));
    pango_layout_set_attributes(pl, small);
    pango_attr_list_unref(small);
    set_rgb(cr, &st->text[GTK_STATE_NORMAL], 0.55);
    for (int side = 0; side < 2; side++) {
        int tw, th;
        pango_layout_set_text(pl, SIDE_MARKS[side], -1);
        pango_layout_get_pixel_size(pl, &tw, &th);
        cairo_move_to(cr, 6, side ? H - th - 4 : 4);
        pango_cairo_show_layout(cr, pl);
    }
    g_object_unref(pl);
    set_rgb(cr, &st->text[GTK_STATE_NORMAL], 1);
    int last = -1;
    for (int ply = 0; ply <= app.game.count; ply++) {
        const Eval *e = eval_at(ply);
        if (!e)
            break;
        double x = ply * W / n, y = mid - winning_chances(e->score) * (mid - 3);
        if (last < 0)
            cairo_move_to(cr, x, mid);
        cairo_line_to(cr, x, y);
        last = ply;
    }
    if (last >= 0) {
        cairo_line_to(cr, last * W / n, mid);
        cairo_close_path(cr);
        set_rgb(cr, &st->text[GTK_STATE_NORMAL], 0.3);
        cairo_fill_preserve(cr);
        set_rgb(cr, &st->text[GTK_STATE_NORMAL], 1);
        cairo_set_line_width(cr, 2);
        cairo_stroke(cr);
    }
    for (int ply = 1; ply <= last; ply++) {
        int mark = judge(ply);
        if (mark < 2)
            continue;
        double x = ply * W / n, y = mid - winning_chances(eval_at(ply)->score) * (mid - 3);
        cairo_arc(cr, x, y, mark == 3 ? 5 : 3.5, 0, 2 * G_PI);
        cairo_fill(cr);
    }
    int shown = view_ply(app.view) < 0 ? app.game.count : view_ply(app.view);
    double dash[] = {4, 4};
    cairo_set_dash(cr, dash, 2, 0);
    cairo_set_line_width(cr, 2);
    cairo_move_to(cr, shown * W / n, 0);
    cairo_line_to(cr, shown * W / n, H);
    cairo_stroke(cr);
    cairo_destroy(cr);
    return TRUE;
}

/* A tap on the graph goes to that move. */
static gboolean graph_press_cb(GtkWidget *w, GdkEventButton *ev, gpointer d)
{
    GtkAllocation a;
    gtk_widget_get_allocation(w, &a);
    int ply = CLAMP((int) (ev->x * MAX(app.game.count, 1) / MAX(a.width, 1) + 0.5), 0, app.game.count);
    view_set_ply(app.view, ply == app.game.count ? -1 : ply);
    update_move_label();
    gtk_dialog_response(GTK_DIALOG(d), GTK_RESPONSE_CLOSE);
    return TRUE;
}

/* The time each move took, as on lichess: the first player's moves up, the second's down. */
static gboolean times_expose_cb(GtkWidget *w, GdkEventExpose *ev, gpointer data)
{
    (void) ev, (void) data;
    GtkAllocation a;
    gtk_widget_get_allocation(w, &a);
    GtkStyle *st = gtk_widget_get_style(w);
    cairo_t *cr = gdk_cairo_create(gtk_widget_get_window(w));
    double W = a.width, H = a.height, mid = H / 2;
    int n = MAX(app.game.count, 1), most = 1;
    for (int i = 0; i < app.game.count; i++)
        most = MAX(most, app.game.times[i]);
    set_rgb(cr, &st->base[GTK_STATE_NORMAL], 1);
    cairo_paint(cr);
    set_rgb(cr, &st->text[GTK_STATE_NORMAL], 1);
    cairo_set_line_width(cr, 2);
    cairo_rectangle(cr, 1, 1, W - 2, H - 2);
    cairo_stroke(cr);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, 0, mid);
    cairo_line_to(cr, W, mid);
    cairo_stroke(cr);
    double bar = MAX(1, W / n - 1);
    for (int i = 0; i < app.game.count; i++) {
        double h = (mid - 3) * app.game.times[i] / most;
        gboolean first = (app.game.start.side + i) % 2 == FIRST;
        cairo_rectangle(cr, (i + 0.5) * W / n - bar / 2, first ? mid - h : mid, bar, h);
    }
    set_rgb(cr, &st->text[GTK_STATE_NORMAL], 0.6);
    cairo_fill(cr);
    cairo_destroy(cr);
    return TRUE;
}

/* A count in the summary: the next move of that side with that mark, after the one shown. */
static void jump_cb(GtkWidget *b, gpointer data)
{
    (void) b;
    int c = GPOINTER_TO_INT(data) / 4, kind = GPOINTER_TO_INT(data) % 4, count = app.game.count;
    int shown = view_ply(app.view) < 0 ? count : view_ply(app.view);
    for (int i = 1; i <= count; i++) {
        int ply = (shown + i - 1) % count + 1;
        const Eval *a = eval_at(ply - 1);
        if (a && a->side == c && judge(ply) == kind) {
            view_set_ply(app.view, ply == count ? -1 : ply);
            update_move_label();
            gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_CLOSE);
            return;
        }
    }
}

static void option_changed_cb(GtkWidget *w, gpointer key)
{
    int v = GTK_IS_SPIN_BUTTON(w) ? gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w))
            : GTK_IS_COMBO_BOX(w) ? gtk_combo_box_get_active(GTK_COMBO_BOX(w))
                                  : gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w));
    g_key_file_set_integer(app.settings, "Settings", key, v);
    save_settings();
    update_move_label(); /* the arrows and the marks on the board */
}

static void again_cb(GtkWidget *b, gpointer data)
{
    (void) b, (void) data;
    stop();
    start();
    refresh();
}

typedef struct {
    const char *key;
    int base; /* the value of the first chip */
} OptionChips;

static void option_chips_cb(int i, gpointer data)
{
    const OptionChips *o = data;
    g_key_file_set_integer(app.settings, "Settings", o->key, i + o->base);
    save_settings();
    update_move_label();
}

static GtkWidget *option_chips(GtkWidget *card, const char *label, const char *const *items, int n, const OptionChips *o, int value)
{
    return row_add(card, row_chips(tr(label), items, n, CLAMP(value - o->base, 0, n - 1), rows_width() - 20, option_chips_cb, (gpointer) o));
}

static GtkWidget *option_switch(GtkWidget *card, const char *label, const char *key, int fallback)
{
    GtkWidget *s = row_add(card, row_switch(tr(label), setting_int(key, fallback)));
    g_signal_connect(s, "toggled", G_CALLBACK(option_changed_cb), (gpointer) key);
    return s;
}

static void options_page_again_cb(GtkWidget *b, gpointer d)
{
    (void) b;
    again_cb(NULL, NULL);
    gtk_dialog_response(GTK_DIALOG(d), GTK_RESPONSE_CLOSE);
}

/* lichess's analysis menu, as a page over Analysis: how long and with what the engine looks, how many lines, and what
 * the board shows. Time, CPUs, memory and lines count from the next analysis ("Analyze again"). */
static void options_page(void)
{
    static const char *const times[] = {"1 s", "2 s", "5 s", "10 s"};
    static const char *const memory[] = {"16 MB", "64 MB", "256 MB", "512 MB"};
    static const char *const lines[] = {"1", "2", "3", "4", "5"};
    static const char *const cpu_names[] = {"1", "2", "3", "4", "5", "6", "7", "8"};
    static const OptionChips TIME = {"AnalysisTime", 0}, MEMORY = {"AnalysisMemory", 0}, LINES = {"AnalysisLines", 0},
                             CPUS = {"AnalysisThreads", 1};
    GtkWidget *d = dialog_new(tr("Options"));
    drawer_set_back(d);
    drawer_set_beside(d);
    GtkWidget *box = gtk_vbox_new(FALSE, 2), *card = rows_card(box);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), box, FALSE, FALSE, 0);
    option_chips(card, "Time per move", times, 4, &TIME, option("AnalysisTime", 0, 4));
    if (!app.kindle) {
        option_chips(card, "CPUs", cpu_names, MIN(cores(), 8), &CPUS,
                     CLAMP(setting_int("AnalysisThreads", CLAMP(cores() - 1, 1, 4)), 1, cores()));
        option_chips(card, "Memory", memory, 4, &MEMORY, option("AnalysisMemory", 1, 4));
    }
    option_chips(card, "Lines", lines, MAX_LINES, &LINES, lines_wanted() - 1);
    card = rows_card(box);
    option_switch(card, "Best move arrows", "AnalysisArrows", 1);
    option_switch(card, "Marks on the board", "AnalysisBoardMarks", 1);
    GtkWidget *again = row_add(rows_card(box), row_section("⭯", tr("Analyze again"), ""));
    g_signal_connect(again, "clicked", G_CALLBACK(options_page_again_cb), d);
    gtk_widget_show_all(d);
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}

static void options_cb(GtkWidget *b, gpointer data)
{
    (void) b, (void) data;
    options_page();
}

static void options_box(void)
{
    GtkWidget *b = row_add(rows_card(GTK_DIALOG(dialog)->vbox), row_section("✲", tr("Options"), ""));
    g_signal_connect(b, "clicked", G_CALLBACK(options_cb), NULL);
}

/* Two columns, one per side: the marks (buttons that go to the next one), the loss, the accuracy. */
static GtkWidget *summary_table(void)
{
    static const char *const ROWS[] = {"Inaccuracies", "Mistakes", "Blunders", "Average centipawn loss", "Accuracy"};
    GtkWidget *table = gtk_table_new(6, 3, FALSE);
    /* Within the drawer: the names wrap in 44% of it, each side's column gets a quarter. */
    int w = rows_width() - 20, side = MAX(60, w / 4 - 16);
    for (int r = 0; r < 5; r++) {
        GtkWidget *l = gtk_label_new(tr(ROWS[r]));
        gtk_misc_set_alignment(GTK_MISC(l), 0, 0.5);
        gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
        gtk_widget_set_size_request(l, w * 44 / 100, -1);
        gtk_table_attach(GTK_TABLE(table), l, 0, 1, r + 1, r + 2, GTK_FILL, GTK_FILL, 8, 2);
    }
    for (int c = 0; c < 2; c++) {
        side_labels[c] = gtk_label_new(NULL);
        gtk_label_set_ellipsize(GTK_LABEL(side_labels[c]), PANGO_ELLIPSIZE_END);
        gtk_widget_set_size_request(side_labels[c], side, -1);
        gtk_table_attach(GTK_TABLE(table), side_labels[c], c + 1, c + 2, 0, 1, GTK_FILL, GTK_FILL, 8, 4);
        for (int k = 0; k < 3; k++) {
            mark_buttons[c][k] = gtk_button_new_with_label("0");
            gtk_button_set_relief(GTK_BUTTON(mark_buttons[c][k]), GTK_RELIEF_NONE);
            g_signal_connect(mark_buttons[c][k], "clicked", G_CALLBACK(jump_cb), GINT_TO_POINTER(c * 4 + k + 1));
            gtk_table_attach(GTK_TABLE(table), mark_buttons[c][k], c + 1, c + 2, k + 1, k + 2, GTK_FILL, GTK_FILL, 8, 0);
        }
        loss_labels[c] = gtk_label_new(NULL);
        accuracy_labels[c] = gtk_label_new(NULL);
        gtk_table_attach(GTK_TABLE(table), loss_labels[c], c + 1, c + 2, 4, 5, GTK_FILL, GTK_FILL, 8, 2);
        gtk_table_attach(GTK_TABLE(table), accuracy_labels[c], c + 1, c + 2, 5, 6, GTK_FILL, GTK_FILL, 8, 2);
    }
    return table;
}

gboolean analysis_possible(void)
{
    return app.game.count > 0 && !problem_playing() && /* a problem has Solution instead */
           (app.game.result != RESULT_NONE || app.loaded_record ||
            (app.players[FIRST] == PLAYER_HUMAN && app.players[SECOND] == PLAYER_HUMAN && !app.net && !server_game()));
}

/* ☰ → Analysis: starts looking at the game unless it is already done or going on, and shows
 * the results as they come. */
void analysis_dialog(void)
{
    if (!running() && !eval_at(MIN(npos - 1, app.game.count)))
        start();
    dialog = dialog_new(tr("Analysis"));
    drawer_set_beside(dialog);
    g_object_set_data(G_OBJECT(dialog), "browse-moves", GINT_TO_POINTER(TRUE)); /* LB/RB go through the moves */
    GtkWidget *box = GTK_DIALOG(dialog)->vbox;
    progress = gtk_label_new(NULL);
    gtk_box_pack_start(GTK_BOX(box), progress, FALSE, FALSE, 6);
    GtkWidget *eval_label = gtk_label_new(tr("Evaluation")); /* titled as the move times are */
    gtk_misc_set_alignment(GTK_MISC(eval_label), 0, 0.5);
    gtk_box_pack_start(GTK_BOX(box), eval_label, FALSE, FALSE, 0);
    graph = gtk_drawing_area_new();
    int w = rows_width() - 20;
    gtk_widget_set_size_request(graph, w, w / 3);
    gtk_widget_add_events(graph, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(graph, "expose-event", G_CALLBACK(graph_expose_cb), NULL);
    g_signal_connect(graph, "button-press-event", G_CALLBACK(graph_press_cb), dialog);
    gtk_box_pack_start(GTK_BOX(box), graph, FALSE, FALSE, 6);
    times_graph = gtk_drawing_area_new();
    gtk_widget_set_size_request(times_graph, w, w / 6);
    g_signal_connect(times_graph, "expose-event", G_CALLBACK(times_expose_cb), NULL);
    gboolean timed = FALSE;
    for (int i = 0; i < app.game.count; i++)
        timed |= app.game.times[i] > 0;
    if (timed) {
        GtkWidget *label = gtk_label_new(tr("Move times"));
        gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
        gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), times_graph, FALSE, FALSE, 6);
    }
    gtk_box_pack_start(GTK_BOX(box), summary_table(), FALSE, FALSE, 6);
    options_box();
    gtk_dialog_add_button(GTK_DIALOG(dialog), tr("Stop"), RESPONSE_DELETE);
    gtk_dialog_add_button(GTK_DIALOG(dialog), tr("Close"), GTK_RESPONSE_CLOSE);
    gtk_widget_show_all(dialog);
    if (!timed)
        gtk_widget_hide(times_graph);
    update_dialog();
    int r = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    dialog = NULL;
    if (r == RESPONSE_DELETE) {
        stop();
        refresh();
    }
}

const char *analysis_menu_label(void)
{
    return running() || eval_at(0) ? "Analysis" : "Analyze Game";
}

/* The evaluation after move `ply`, and for a marked move what it was and the best move instead;
 * the best move from that position goes on the board, and the marks on the move list. */
void analysis_text(int ply, char *buf, int size)
{
    char marks[MAX_PLY];
    for (int i = 0; i < app.game.count; i++)
        marks[i] = (char) judge(i + 1);
    view_set_marks(app.view, marks, app.game.count);
    view_set_board_marks(app.view, setting_int("AnalysisBoardMarks", 1));
    const Eval *e = eval_at(ply);
    view_set_best(app.view, e ? e->lines : NULL, e && show_arrows() ? e->nlines : 0, ply);
    if (!e)
        return;
    int n = (int) strlen(buf);
    char score[16], words[64];
    format_score(e->score, score, sizeof score);
    score_words(e->score, words, sizeof words);
    n += snprintf(buf + n, size - n, "%s%s (%s)", n ? "  " : "", score, words);
    int mark = ply > 0 ? judge(ply) : 0;
    const Eval *before = eval_at(ply - 1);
    if (mark && n < size) {
        static const char *const NAMES[] = {"", "Inaccuracy", "Mistake", "Blunder"};
        n += snprintf(buf + n, size - n, "  %s", tr(NAMES[mark]));
        if (before->has_best && n < size) {
            char mv[64];
            best_move_text(ply - 1, &before->best, mv, sizeof mv);
            snprintf(buf + n, size - n, " · %s %s", tr("Best:"), mv);
        }
    }
}
