/* The game: clocks, the engine, moves, turns, the end; the board view callbacks. */

#include "app.h"
#include "problems.h"

/* ---- clock ---- */

/* Bit 0: legal moves are hidden. Above it, illegal moves are refused (0), lose (1), or pieces
 * go anywhere and illegal moves lose (2). In a network game the host decides. */
int own_move_rule(void)
{
    return (setting_int("ShowMoves", 1) ? 0 : 1) | CLAMP(setting_int("IllegalLoses", 0), 0, 2) << 1;
}

static int move_rule(void)
{
    if (app.net || app.net_suspended)
        return app.net_rule;
    /* A server judges the moves itself: only legal ones go to it. */
    return server_game() ? own_move_rule() & 1 : own_move_rule();
}

gboolean show_moves(void) { return !(move_rule() & 1); }

int illegal_rule(void) { return move_rule() >> 1; }

const TimeControl *tc(void)
{
    if (server_game())
        return &app.online_tc;
    return app.time_index < NTIMES ? &TIMES[app.time_index] : &app.custom_tc;
}

const char *tc_label_of(int index, const TimeControl *custom)
{
    if (index < NTIMES)
        return tr(TIMES[index].label);
    static char label[96];
    long main_s = custom->main_s;
    snprintf(label, sizeof label, tr("%s (%ld:%02ld + %d s byoyomi + %d s increment)"), tr("Custom"), main_s / 60,
             main_s % 60, custom->byoyomi_s, custom->increment_s);
    return label;
}

const char *tc_label(void) { return tc_label_of(app.time_index, &app.custom_tc); }

static gboolean timed(void) { return !app.problem_plies && !lesson_playing() && (tc()->main_s || tc()->byoyomi_s); }

/* Lishogi runs no clock before both first moves (the game is aborted if one is not made). */
static long elapsed_ms(void)
{
    if (app.mode == MODE_ONLINE && app.game.count < 2)
        return 0;
    if (app.paused)
        return app.paused_ms;
    return (long) ((g_get_monotonic_time() - app.turn_start) / 1000);
}

static void format_clock(long ms, char *buf, int size)
{
    long s = (ms + 999) / 1000;
    snprintf(buf, size, "%ld:%02ld", s / 60, s % 60);
}

/* ---- game flow ---- */

const char *player_name(int color)
{
    if (app.players[color] == PLAYER_ENGINE && problem_playing())
        return problem_title();
    if (app.players[color] == PLAYER_ENGINE) {
        /* "Fairy-Stockfish 051126 LB" -> "Fairy-Stockfish" */
        static char name[96];
        snprintf(name, sizeof name, "%s", engine_name(app.engine));
        name[strcspn(name, " ")] = 0;
        snprintf(name + strlen(name), sizeof name - strlen(name), " (%s %d)", tr("Level"), engine_level(color));
        return name;
    }
    if (app.players[color] == PLAYER_REMOTE)
        return app.remote_name ? app.remote_name : tr("Opponent");
    /* A loaded record keeps its players' names. */
    if (app.mode == MODE_TWO_PLAYERS)
        return app.loaded_record && *app.game.names[color] ? app.game.names[color] : tr(SIDE_NAMES[color]);
    static char name[64];
    char *own = g_strstrip(setting_string("PlayerName", ""));
    snprintf(name, sizeof name, "%s", *own ? own : tr("You"));
    g_free(own);
    return name;
}

void update_move_label(void)
{
    int ply = view_ply(app.view);
    if (ply < 0)
        ply = app.game.count;
    char text[256] = "";
    if (view_ply(app.view) < 0 && app.game.result != RESULT_NONE) {
        snprintf(text, sizeof text, "%s%s%s", result_text(), app.peer_left ? "  " : "",
                 app.peer_left ? tr("The other player left.") : "");
    } else {
        /* The move list shows the move: the bar keeps to notices. */
        if (replay_paused())
            snprintf(text, sizeof text, "%s · %s", tr("Replay"), tr("Paused"));
        else if (replaying())
            snprintf(text, sizeof text, "▶ %s", tr("Replay"));
        else if (ply > 0 && app.game.gave_check[ply - 1] && show_moves())
            snprintf(text, sizeof text, "%s", tr("Check"));
    }
    analysis_text(ply, text, sizeof text);
    set_move_text(text);
    chat_presets_update();
}

static guint autosave_timer;

void autosave(void)
{
    if (autosave_timer)
        g_source_remove(autosave_timer);
    autosave_timer = 0;
    if (lesson_playing()) /* the game before the tutorial stays saved */
        return;
    char *path = g_build_filename(app.home, "autosave." RECORD_EXT, NULL);
    record_save(path, FALSE);
    g_free(path);
    g_key_file_set_integer(app.settings, "Game", "Mode", app.mode);
    g_key_file_set_integer(app.settings, "Game", "SenteEngine", app.players[FIRST] == PLAYER_ENGINE);
    g_key_file_set_integer(app.settings, "Game", "GoteEngine", app.players[SECOND] == PLAYER_ENGINE);
    g_key_file_set_integer(app.settings, "Game", "Level", app.level);
    g_key_file_set_integer(app.settings, "Game", "LevelFirst", app.level_first);
    g_key_file_set_integer(app.settings, "Game", "Record", app.loaded_record);
    g_key_file_set_integer(app.settings, "Game", "Time", app.time_index);
    /* The clock of this game (the Sandbox's untimed one, a LAN host's), not the last New Game's. */
    g_key_file_set_integer(app.settings, "Game", "ClockMain", (int) app.custom_tc.main_s);
    g_key_file_set_integer(app.settings, "Game", "ClockByoyomi", (int) app.custom_tc.byoyomi_s);
    g_key_file_set_integer(app.settings, "Game", "ClockIncrement", (int) app.custom_tc.increment_s);
    g_key_file_set_integer(app.settings, "Game", "SenteMs", (int) app.remaining[FIRST]);
    g_key_file_set_integer(app.settings, "Game", "GoteMs", (int) app.remaining[SECOND]);
    /* A host coming back to the game (host_rejoin()) needs the room as it was. */
    if (app.mode == MODE_HOST) {
        g_key_file_set_integer(app.settings, "Game", "NetColor", app.net_color);
        g_key_file_set_integer(app.settings, "Game", "NetRule", app.net_rule);
        g_key_file_set_integer(app.settings, "Game", "Handicap", app.handicap);
        g_key_file_set_string(app.settings, "Game", "NetStart", app.net_start ? app.net_start : "");
        g_key_file_set_string(app.settings, "Game", "RoomPassword", app.room_password ? app.room_password : "");
    } else if (app.mode == MODE_JOIN && app.net_password) { /* rejoin() gives it again */
        g_key_file_set_string(app.settings, "Game", "RoomPassword", app.net_password);
    } else {
        g_key_file_remove_key(app.settings, "Game", "RoomPassword", NULL);
    }
    save_settings();
}

static gboolean autosave_cb(gpointer data)
{
    (void) data;
    autosave_timer = 0;
    autosave();
    return FALSE;
}

/* Saves a little after the last move: the Kindle's storage takes half a second to write the two
 * files, which right after a tap held up the move on the screen. */
static void autosave_later(void)
{
    if (autosave_timer)
        g_source_remove(autosave_timer);
    autosave_timer = g_timeout_add_seconds(2, autosave_cb, NULL);
}

static int engine_for = -1;  /* the variant the engine was started for */
static char *engine_from;    /* and its program */

static void engine_ready_cb(void *data)
{
    (void) data;
    for (int c = 0; c < 2; c++)
        if (app.players[c] == PLAYER_ENGINE)
            snprintf(app.game.names[c], sizeof app.game.names[c], "%s (%s %d)", engine_name(app.engine), tr("Level"),
                     engine_level(c));
    view_refresh_clocks(app.view);
}

/* The attacker's moves still to make in a problem, the one just made included (the attacker moves first). */
static int moves_left(void) { return problem_moves(app.problem_plies) - app.game.count / 2; }

/* The solution of the problem on the board: the engine searches it from the problem's own
 * position and its best line is shown as moves once the search ends. */
static gboolean solution_pending;
static char solution_pv[1024];

/* The search's last word on the score, for a problem's check: its mate in plies (negative: the
 * side to move is mated), 0 when it is not a mate. */
static void engine_info_cb(const char *line, void *data)
{
    (void) data;
    const char *mate = strstr(line, " score mate ");
    if (strstr(line, " score "))
        app.problem_mate = mate ? atoi(mate + 12) : 0;
    const char *pv = strstr(line, " pv ");
    if (solution_pending && mate && pv && !strstr(line, "bound"))
        snprintf(solution_pv, sizeof solution_pv, "%s", pv + 4);
}

/* Lishogi's way with a wrong move in a puzzle: the move goes back at once and a short "not the
 * move" stands where the length was, until the next try; no window to close. */
static gboolean problem_note;

static void problem_wrong(void)
{
    app.problem_check = FALSE;
    play_sound("wrong");
    game_undo(&app.game);
    problem_note = TRUE;
    next_turn();
}

/* The solution's line as the move list writes it, in a message. */
static void solution_show(void)
{
    Position p = app.game.start;
    GString *line = g_string_new("");
    char **tok = g_strsplit(solution_pv, " ", 0);
    int prev = -1;
    for (int i = 0; tok[i] && tok[i][0]; i++) {
        Move m;
        char text[64];
        if (!game_move_from_engine(&p, tok[i], &m))
            break;
        move_notation(&p, &m, prev, notation_ja(), text, sizeof text);
        g_string_append_printf(line, "%s%s", i ? "  " : "", text);
        prev = m.to;
        pos_make(&p, &m);
    }
    g_strfreev(tok);
    message(line->len ? line->str : tr("No solution found."));
    g_string_free(line, TRUE);
}

static void engine_bestmove_cb(const char *usi, void *data)
{
    (void) data;
    app.engine_thinking = FALSE;
    Move m;
    if (solution_pending) {
        solution_pending = FALSE;
        solution_show();
        view_refresh_clocks(app.view);
        return;
    }
    /* The defender's search says whether the attacker's move was right: it must be mated within what is left. */
    if (app.problem_check && !app.hint_pending && app.game.result == RESULT_NONE) {
        if (app.problem_mate >= 0 || -app.problem_mate > 2 * (moves_left() - 1))
            problem_wrong();
        else {
            app.problem_check = FALSE;
            if (game_move_from_engine(&app.game.pos, usi, &m))
                play_move(&m);
        }
        return;
    }
    if (app.hint_pending) {
        app.hint_pending = FALSE;
        if (game_move_from_engine(&app.game.pos, usi, &m))
            view_set_hint(app.view, &m);
        view_refresh_clocks(app.view);
        return;
    }
    int side = app.game.pos.side;
    if (app.game.result != RESULT_NONE || app.players[side] != PLAYER_ENGINE || app.paused)
        return;
    if (!strcmp(usi, "resign")) {
        game_end(&app.game, side == FIRST ? RESULT_SECOND_WIN : RESULT_FIRST_WIN, REASON_RESIGN);
        next_turn();
    } else if (!strcmp(usi, "win")) {
        if (!game_declare(&app.game))
            game_end(&app.game, side == FIRST ? RESULT_SECOND_WIN : RESULT_FIRST_WIN, REASON_ILLEGAL);
        next_turn();
    } else if (game_move_from_engine(&app.game.pos, usi, &m)) {
        play_move(&m);
    } else {
        game_end(&app.game, side == FIRST ? RESULT_SECOND_WIN : RESULT_FIRST_WIN, REASON_ILLEGAL);
        next_turn();
    }
}

static gboolean engine_died_idle(gpointer dead)
{
    if (dead != app.engine) /* already replaced */
        return FALSE;
    char *text = g_strdup_printf("%s\n%s", tr("The engine stopped:"), engine_from);
    engine_free(app.engine);
    app.engine = NULL;
    message(text);
    g_free(text);
    return FALSE;
}

static void engine_died_cb(void *data)
{
    (void) data;
    app.engine_thinking = FALSE;
    g_idle_add(engine_died_idle, app.engine);
}

const char *engine_variant(void) { return variant_engine_name(app.variant); }

/* The built-in engine, or for the standard game the USI program chosen in Preferences → Game. */
const char *engine_path(void)
{
    static char *own;
    g_free(own);
    own = NULL;
    if (OTHER_ENGINE && !g_getenv("DENBOKU_ENGINE") && app.variant == V_STANDARD && setting_int("Engine", 0) == 1)
        own = setting_string("EnginePath", "");
    return own && *own ? own : app.engine_path;
}

static gboolean engine_fresh; /* ensure_engine() just started it: variant, level and new game already sent */

gboolean ensure_engine(void)
{
    const char *path = engine_path();
    /* Another variant or program needs a fresh engine: Fairy-Stockfish applies USI_Variant at startup. */
    if (app.engine && (engine_for != app.variant || g_strcmp0(engine_from, path))) {
        engine_free(app.engine);
        app.engine = NULL;
        app.engine_thinking = FALSE;
    }
    if (app.engine)
        return TRUE;
    EngineCallbacks cb = {engine_ready_cb, engine_bestmove_cb, engine_died_cb, engine_info_cb};
    app.engine = engine_new(path, &cb, NULL);
    if (!app.engine) {
        char *text = g_strdup_printf("%s\n%s", tr("Could not start the engine:"), path);
        message(text);
        g_free(text);
        return FALSE;
    }
    g_free(engine_from);
    engine_from = g_strdup(path);
    engine_set_option(app.engine, "USI_Variant", engine_variant());
    engine_for = app.variant;
    engine_set_option(app.engine, "Threads", "1");
    /* Searches last at most 3 s: a small table costs no strength, and the Kindle is short of memory. */
    engine_set_option(app.engine, "Hash", "4");
    engine_set_option(app.engine, "USI_Ponder", "false");
    if (app.players[FIRST] == PLAYER_ENGINE || app.players[SECOND] == PLAYER_ENGINE)
        apply_level();
    engine_new_game(app.engine);
    engine_fresh = TRUE;
    return TRUE;
}

/* A game with the engine in it: start it if needed, at the level chosen. A game without it does
 * not keep it running (78 MB on a computer); a hint starts it again. */
void prepare_engine(void)
{
    if (app.players[FIRST] != PLAYER_ENGINE && app.players[SECOND] != PLAYER_ENGINE) {
        engine_free(app.engine);
        app.engine = NULL;
        app.engine_thinking = app.hint_pending = FALSE;
        return;
    }
    engine_fresh = FALSE;
    if (ensure_engine() && !engine_fresh) {
        engine_set_option(app.engine, "USI_Variant", engine_variant());
        apply_level();
        engine_new_game(app.engine);
    }
}

gboolean engines_game(void) { return app.players[FIRST] == PLAYER_ENGINE && app.players[SECOND] == PLAYER_ENGINE; }

/* Two engines may play at different levels: Sente's is its own. */
int engine_level(int side) { return engines_game() && side == FIRST ? app.level_first : app.level; }

/* A game between two engines stops and goes on (in the bar, where Hint is in other games). */
void pause_toggle(void)
{
    if (!engines_game() || app.game.result != RESULT_NONE)
        return;
    if (!app.paused) {
        app.paused_ms = elapsed_ms();
        app.paused = TRUE;
        if (app.engine)
            engine_cancel(app.engine);
        app.engine_thinking = FALSE;
    } else {
        app.paused = FALSE;
        app.turn_start = g_get_monotonic_time() - app.paused_ms * 1000;
        engine_search(FALSE);
    }
    view_refresh_clocks(app.view);
    view_queue_bar(app.view);
}

/* The level of the side to move. */
void apply_level(void)
{
    /* Fairy-Stockfish skill (-20..20) per level, from engine matches between neighbors:
     * evenly spaced values left two steps that played
     * like their neighbor (-9 and -6, -3 and 0) and others far apart. */
    static const int SKILL[LEVELS] = {-9, -4, -2, 1, 3, 5, 8, 12, 16, 20};
    char skill[16];
    snprintf(skill, sizeof skill, "%d", app.problem_plies ? 20 : SKILL[CLAMP(engine_level(app.game.pos.side), 1, LEVELS) - 1]);
    engine_set_option(app.engine, "Skill Level", skill);
}

void engine_search(gboolean hint)
{
    if (!ensure_engine())
        return;
    /* A hint is the engine's best move, not one weakened for the level the opponent plays at; the level
     * comes back for the engine's own move. */
    if (hint)
        engine_set_option(app.engine, "Skill Level", "20");
    else
        apply_level();
    char *position = g_malloc(16384);
    game_engine_position(&app.game, position, 16384);
    char go[128];
    /* Always a fixed time per move: given the clock, the engine would spend far longer
     * than the level calls for, which is slow on the Kindle. */
    long think = hint ? (app.kindle ? 2500 : 1500) : 400 + engine_level(app.game.pos.side) * 250;
    if (app.problem_plies) /* long enough to see the mate to the end, as far as the problem goes */
        think = (app.kindle ? 2000 : 1000) + moves_left() * 300;
    app.problem_mate = 0;
    if (timed() && !hint) {
        int side = app.game.pos.side;
        long left = app.remaining[side] - elapsed_ms();
        long budget = MAX(left, 0) / 30 + tc()->byoyomi_s * 1000L / 2 + tc()->increment_s * 1000L / 2;
        think = CLAMP(budget, 200, think);
    }
    snprintf(go, sizeof go, "movetime %ld", think);
    app.engine_thinking = TRUE;
    app.hint_pending = hint;
    /* The move just made goes on screen before the engine takes the processor. */
    gdk_window_process_all_updates();
    gdk_flush();
    engine_go(app.engine, position, go);
    g_free(position);
    view_refresh_clocks(app.view);
}

/* Searches the problem on the board from its own position, for the line shown by solution_show(). */
void problem_solution(void)
{
    if (!problem_playing() || app.engine_thinking || !ensure_engine())
        return;
    Game *g = g_memdup(&app.game, sizeof *g);
    while (g->count)
        game_undo(g);
    char *position = g_malloc(16384), go[64];
    game_engine_position(g, position, 16384);
    g_free(g);
    solution_pv[0] = 0;
    solution_pending = app.engine_thinking = TRUE;
    snprintf(go, sizeof go, "movetime %d", (app.kindle ? 3000 : 1500) + problem_moves(app.problem_plies) * 600);
    engine_go(app.engine, position, go);
    g_free(position);
    view_refresh_clocks(app.view);
}

const char *game_result_text(const Game *g, const PlayerType *players)
{
    static char buf[256];
    const char *why = reason_text(g);
    if (g->result == RESULT_ABORTED) {
        snprintf(buf, sizeof buf, "%s", tr("Game aborted"));
    } else if (g->result == RESULT_DRAW) {
        snprintf(buf, sizeof buf, "%s — %s", tr("Draw"), why);
    } else {
        int winner = g->result == RESULT_FIRST_WIN ? FIRST : SECOND;
        const char *who;
        /* "You" when one side is played here and the other is not (the engine, a network or online player). */
        if (players && players[winner] == PLAYER_HUMAN && players[!winner] != PLAYER_HUMAN)
            who = tr("You win");
        else if (players && players[winner] != PLAYER_HUMAN && players[!winner] == PLAYER_HUMAN)
            who = tr("You lose");
        else
            who = tr(SIDE_WINS[winner]);
        snprintf(buf, sizeof buf, "%s — %s", who, why);
    }
    return buf;
}

const char *result_text(void) { return game_result_text(&app.game, app.players); }

static void over_close(void)
{
    GtkWidget *d = app.over_dialog;
    app.over_label = app.over_rematch = app.over_dialog = NULL;
    if (d)
        gtk_widget_destroy(d);
}

static gboolean new_game_idle(gpointer data)
{
    (void) data;
    new_game_dialog();
    return FALSE;
}

static gboolean analysis_idle(gpointer data)
{
    (void) data;
    analysis_dialog();
    return FALSE;
}

#define RESPONSE_ANALYZE 1
#define RESPONSE_ROOM 2
#define RESPONSE_PROBLEMS 4
#define RESPONSE_SOLUTION 5

static gboolean next_problem_idle(gpointer data)
{
    (void) data;
    problem_next();
    return FALSE;
}

static gboolean problems_idle(gpointer data)
{
    (void) data;
    problems_dialog();
    return FALSE;
}

static gboolean room_idle(gpointer data)
{
    (void) data;
    room_settings_after_game();
    return FALSE;
}

static void over_row_cb(GtkButton *b, gpointer response)
{
    gtk_dialog_response(GTK_DIALOG(g_object_get_data(G_OBJECT(b), "dialog")), GPOINTER_TO_INT(response));
}

static void over_row(GtkWidget *d, GtkWidget *card, const char *label, int response)
{
    GtkWidget *b = row_add(card, row_choice(label, "", NULL));
    g_object_set_data(G_OBJECT(b), "dialog", d);
    g_signal_connect(b, "clicked", G_CALLBACK(over_row_cb), GINT_TO_POINTER(response));
}

/* Rematch: in a network game both must ask; here the same game again, the sides swapped
 * against the engine (not with a handicap, where they are not alike). */
static void over_response_cb(GtkDialog *d, int r, gpointer data)
{
    (void) d, (void) data;
    if (r == RESPONSE_ROOM) {
        g_idle_add(room_idle, NULL);
        return; /* the notice stays */
    }
    if (r == RESPONSE_SOLUTION) {
        problem_solution();
        return; /* the notice stays */
    }
    if (r == RESPONSE_PROBLEMS || (r == GTK_RESPONSE_ACCEPT && problem_playing())) { /* Next problem is the main answer */
        over_close();
        g_idle_add(r == RESPONSE_PROBLEMS ? problems_idle : next_problem_idle, NULL);
        return;
    }
    if (r == GTK_RESPONSE_ACCEPT && app.net && !app.net_suspended) {
        rematch();
        return; /* waiting for the other player, or the host started it (and closed this) */
    }
    over_close();
    if (r == GTK_RESPONSE_ACCEPT) {
        Mode m = app.mode;
        if (m == MODE_FIRST || m == MODE_SECOND || m == MODE_RANDOM) {
            gboolean first = app.players[FIRST] == PLAYER_HUMAN;
            m = (app.handicap == HANDICAP_NONE) == first ? MODE_SECOND : MODE_FIRST;
        }
        start_game(app.handicap, m);
    } else if (r == GTK_RESPONSE_OK) {
        g_idle_add(new_game_idle, NULL);
    } else if (r == RESPONSE_ANALYZE) {
        g_idle_add(analysis_idle, NULL);
    }
}

/* The result beside the board, which stays in use (the moves can be gone through): Analyze and New
 * Game as rows, Close and Rematch, the main one, below. */
static void game_over(void)
{
    if (app.over_dialog)
        return;
    GtkWidget *d = app.over_dialog = dialog_new(tr("Game over"));
    drawer_set_passive(d);
    gboolean problem = problem_playing(), solved = problem && app.game.result == RESULT_FIRST_WIN;
    GtkWidget *label = gtk_label_new(solved ? tr("Solved!") : result_text()), *head = gtk_vbox_new(FALSE, 8);
    GtkWidget *middle = gtk_alignment_new(0.5, 0.5, 1, 0);
    gtk_container_add(GTK_CONTAINER(middle), head);
    gtk_box_pack_start(GTK_BOX(head), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), middle, TRUE, TRUE, 16);
    if (app.game.reason == REASON_ILLEGAL && app.game.count > 0) { /* which rule the last move broke */
        Position before;
        Move last = app.game.moves[app.game.count - 1];
        game_position_at(&app.game, app.game.count - 1, &before);
        const char *why = move_problem(&before, &last);
        if (why) {
            GtkWidget *rule = gtk_label_new(why);
            gtk_label_set_line_wrap(GTK_LABEL(rule), TRUE);
            gtk_label_set_justify(GTK_LABEL(rule), GTK_JUSTIFY_CENTER);
            gtk_box_pack_start(GTK_BOX(head), rule, FALSE, FALSE, 0);
        }
    }
    gboolean lan = app.net && !app.net_suspended;
    gboolean local = !app.net && !app.net_suspended && !server_game() && !app.loaded_record && !problem;
    if (lan) {
        app.over_label = gtk_label_new("");
        gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), app.over_label, FALSE, FALSE, 6);
    }
    /* Two buttons fit the foot: the others are rows. */
    GtkWidget *card = rows_card(GTK_DIALOG(d)->vbox);
    if (problem)
        over_row(d, card, tr("Solution"), RESPONSE_SOLUTION);
    if (problem)
        over_row(d, card, tr("Mate problems…"), RESPONSE_PROBLEMS);
    if (analysis_possible())
        over_row(d, card, tr("Analyze Game"), RESPONSE_ANALYZE);
    if (lan && app.mode == MODE_HOST && !app.peer_left)
        over_row(d, card, tr("Room settings"), RESPONSE_ROOM);
    if (!problem)
        over_row(d, card, tr("New Game"), GTK_RESPONSE_OK);
    gtk_dialog_add_button(GTK_DIALOG(d), tr("Close"), GTK_RESPONSE_CLOSE);
    if (solved) { /* the main answer, in focus */
        gtk_dialog_add_button(GTK_DIALOG(d), tr("Next problem"), GTK_RESPONSE_ACCEPT);
        gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_ACCEPT);
    }
    if (lan || local) { /* the main answer, in focus */
        app.over_rematch = gtk_dialog_add_button(GTK_DIALOG(d), tr("Rematch"), GTK_RESPONSE_ACCEPT);
        gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_ACCEPT);
    }
    g_signal_connect(d, "response", G_CALLBACK(over_response_cb), NULL);
    gtk_widget_show_all(d);
    if (lan && app.peer_left) {
        gtk_label_set_text(GTK_LABEL(app.over_label), tr("The other player left."));
        gtk_widget_set_sensitive(app.over_rematch, FALSE);
    }
    rematch_check();
}

void show_result(void)
{
    if (app.game.result != RESULT_NONE)
        game_over();
}

static guint over_idle;

/* ---- turns ---- */

/* Our turn came: the premove is played if it is legal now, else dropped. */
static gboolean premove_idle(gpointer data)
{
    (void) data;
    Move m;
    if (app.game.result == RESULT_NONE && app.players[app.game.pos.side] == PLAYER_HUMAN &&
        view_take_premove(app.view, &m)) {
        Move legal = m;
        if (pos_is_legal(&app.game.pos, &legal) && legal.promote == m.promote)
            play_move(&legal);
    }
    return FALSE;
}

/* No game-over notice for a game given up to start another one, and the one shown goes. */
void skip_game_over(void)
{
    if (over_idle)
        g_source_remove(over_idle);
    over_idle = 0;
    over_close();
}

static gboolean game_over_idle(gpointer data)
{
    (void) data;
    over_idle = 0;
    if (app.game.result != RESULT_NONE) {
        /* Won or lost for the one human at the board; otherwise neutral. */
        int human = app.players[FIRST] == PLAYER_HUMAN && app.players[SECOND] != PLAYER_HUMAN   ? FIRST
                    : app.players[SECOND] == PLAYER_HUMAN && app.players[FIRST] != PLAYER_HUMAN ? SECOND
                                                                                              : -1;
        Result r = app.game.result;
        gboolean won = r == (human == FIRST ? RESULT_FIRST_WIN : RESULT_SECOND_WIN);
        gboolean lost = r == (human == FIRST ? RESULT_SECOND_WIN : RESULT_FIRST_WIN);
        if (!app.loaded_record)
            history_add();
        play_sound(human < 0 || r == RESULT_DRAW || r == RESULT_ABORTED ? "draw" : won ? "win" : lost ? "lose" : "draw");
        game_over();
    }
    return FALSE;
}

static guint label_later;

static gboolean label_later_cb(gpointer data)
{
    (void) data;
    label_later = 0;
    update_move_label();
    return FALSE;
}

static gboolean tick_cb(gpointer data);

/* The move count the turn's clock started at: a turn starts again only with a move made or taken back, or
 * another game, not when a takeback is asked or declined or the Sandbox is left without a change. */
static int clock_count = -1;

void turn_clock_new(void) { clock_count = -1; }

void next_turn(void)
{
    /* A game that ends without a move (resignation, time, an abort) takes the time of its last
     * turn off that clock, which otherwise went back to where the turn began. */
    static int running_count = -1;
    if (app.game.result != RESULT_NONE && running_count == app.game.count && timed() && !app.loaded_record) {
        int side = app.game.pos.side;
        app.remaining[side] = MAX(0, app.remaining[side] - elapsed_ms());
    }
    running_count = app.game.result == RESULT_NONE ? app.game.count : -1;
    /* The clocks' timer runs only while a game has one: without it nothing wakes the app. */
    if (!app.tick && app.game.result == RESULT_NONE && timed())
        app.tick = g_timeout_add_seconds(1, tick_cb, NULL);
    apply_variant_style();
    view_refresh(app.view);
    /* The top bar's move text a moment after the board, as a separate small e-ink update. */
    if (app.kindle) {
        if (!label_later)
            label_later = g_timeout_add(200, label_later_cb, NULL);
    } else {
        update_move_label();
    }
    autosave_later();
    if (app.game.count != clock_count || app.game.result != RESULT_NONE) {
        app.turn_start = g_get_monotonic_time();
        clock_count = app.game.count;
    }
    gboolean online = app.net || app.net_suspended || server_game();
    app.hint_visible = !online;
    if (app.game.count != app.takeback_refused_mine)
        app.takeback_refused_mine = 0;
    if (app.game.count != app.takeback_refused_theirs)
        app.takeback_refused_theirs = 0;
    update_undo();
    chat_layout();
    app.can_hint = app.game.result == RESULT_NONE && !illegal_rule() && !online &&
                   app.players[app.game.pos.side] == PLAYER_HUMAN;
    view_queue_bar(app.view);
    if (app.game.result != RESULT_NONE) {
        if (app.engine)
            engine_cancel(app.engine);
        app.engine_thinking = FALSE;
        /* From the main loop, not from inside a network or engine callback, and once the last
         * move is on screen (an illegal one too: it is shown before it loses). */
        if (!over_idle) {
            gdk_window_process_all_updates();
            over_idle = g_timeout_add(300, game_over_idle, NULL);
        }
        return;
    }
    skip_game_over(); /* the game goes on (a move taken back) */
    if (app.players[app.game.pos.side] == PLAYER_ENGINE && !app.paused)
        engine_search(FALSE);
    else if (app.players[app.game.pos.side] == PLAYER_HUMAN && !app.edit_saved)
        g_idle_add(premove_idle, NULL);
}

/* Undo, or in a network game the takeback request, and the other player's request in the bar. */
void update_undo(void)
{
    gboolean lan = app.net || app.net_suspended, running = app.game.result == RESULT_NONE && !app.net_suspended;
    app.undo_label = lan ? "Takeback" : "Undo";
    app.can_undo = lan ? app.game.count > 0 && running && !app.takeback_mine &&
                             app.game.count != app.takeback_refused_mine
                       : app.game.count > 0 && !server_game();
    if (app.view)
        view_queue_bar(app.view);
}

/* Whether this side's time running out is decided here: in a network game (version 5 on) each side
 * decides on its own player's clock and says so (%TIME_UP), so that the other's view of it, a round
 * trip ahead, does not end the game on its own and the two never disagree. */
static gboolean flags_here(int side) { return !(app.net && app.peer_version >= 5 && app.players[side] == PLAYER_REMOTE); }

static void lose_on_time(int side)
{
    game_end(&app.game, side == FIRST ? RESULT_SECOND_WIN : RESULT_FIRST_WIN, REASON_TIME);
    if (app.net && app.peer_version >= 5)
        net_send(app.net, "%TIME_UP");
    next_turn();
}

/* An illegal move is played too, to be seen on the board, and loses. */
/* The last move's sound: "mate" for the move that won on the board (the result's tune follows). */
void move_sound(gboolean illegal)
{
    Reason why = app.game.reason;
    if (app.game.result != RESULT_NONE && !illegal &&
        (why == REASON_CHECKMATE || why == REASON_TRY || why == REASON_KING || why == REASON_CHECK))
        play_sound("mate");
    else if (app.game.result == RESULT_NONE || illegal)
        play_sound(app.game.gave_check[app.game.count - 1] && show_moves() ? "check"
                   : app.game.moves[app.game.count - 1].captured ? "capture" : "move");
}

static void play(const Move *m, gboolean illegal)
{
    if (app.game.count >= MAX_PLY) { /* a draw, as CSA's move limit (#MAX_MOVES): not a game that stops answering */
        if (app.game.result == RESULT_NONE) {
            game_end(&app.game, RESULT_DRAW, REASON_ABORT);
            next_turn();
        }
        toast(tr("This game is too long to go on (1024 moves)."));
        return;
    }
    long used = elapsed_ms();
    int side = app.game.pos.side;
    if (timed()) {
        long left = app.remaining[side] - used;
        if (left < 0) {
            if ((tc()->byoyomi_s && -left <= tc()->byoyomi_s * 1000L) || server_game() || !flags_here(side))
                left = 0; /* lishogi keeps the real clock, a network opponent its own */
            else {
                lose_on_time(side);
                return;
            }
        }
        app.remaining[side] = left + tc()->increment_s * 1000L;
    }
    view_set_hint(app.view, NULL);
    if (app.mode == MODE_ONLINE && app.players[side] == PLAYER_HUMAN && !illegal)
        online_send_move(m);
    if (app.mode == MODE_SERVER && app.players[side] == PLAYER_HUMAN && !illegal)
        server_send_move(m);
    if (app.net && app.players[side] == PLAYER_HUMAN) {
        takeback_decline_by_moving();
        char text[32] = "%ILLEGAL ";
        move_to_net(&app.game.pos, m, illegal ? text + strlen(text) : text);
        net_send(app.net, text);
    }
    if (illegal)
        game_play_illegal(&app.game, m, (int) used);
    else
        game_play(&app.game, m, (int) used);
    if (problem_playing() && app.players[side] == PLAYER_HUMAN && !illegal) {
        problem_note = FALSE;
        if (app.game.result == RESULT_FIRST_WIN || app.game.result == RESULT_SECOND_WIN)
            problem_solved_now();
        else if (app.game.result == RESULT_NONE && (moves_left() <= 1 || !app.game.gave_check[app.game.count - 1])) {
            problem_wrong(); /* the last move it had and no mate, or not a check (tsume: every move checks) */
            return;
        } else
            app.problem_check = TRUE;
    }
    move_sound(illegal);
    if (setting_int("FlashEveryMove", 0))
        view_flash(app.view);
    next_turn();
}

void play_move(const Move *m) { play(m, FALSE); }

void play_illegal_move(const Move *m)
{
    Move shown = *m;
    if (pos_is_possible(&app.game.pos, &shown)) {
        play(&shown, TRUE);
        return;
    }
    int side = app.game.pos.side;
    if (app.players[side] == PLAYER_HUMAN)
        net_send(app.net, "%ILLEGAL");
    game_end(&app.game, side == FIRST ? RESULT_SECOND_WIN : RESULT_FIRST_WIN, REASON_ILLEGAL);
    next_turn();
}

static gboolean tick_cb(gpointer data)
{
    (void) data;
    if (app.game.result != RESULT_NONE || !timed()) {
        app.tick = 0; /* next_turn() starts it again */
        return FALSE;
    }
    if (app.wait_dialog || app.net_suspended || app.edit_saved)
        return TRUE; /* clocks stop while a network game is set up or reconnecting, or while editing */
    int side = app.game.pos.side;
    long used = elapsed_ms(), main_left = app.remaining[side] - used, byoyomi = tc()->byoyomi_s * 1000L;
    if (!server_game() && main_left + byoyomi < 0 && flags_here(side)) {
        lose_on_time(side);
        return TRUE;
    }
    /* The last 10 s of our time tick, once a second: main time first, then byoyomi. */
    static long last_tick = -1;
    if (app.players[side] == PLAYER_HUMAN && app.players[!side] != PLAYER_HUMAN) {
        long left = main_left > 0 ? main_left : main_left + byoyomi, secs = left / 1000;
        if (left > 0 && secs < 10 && secs != last_tick)
            play_sound("tick");
        last_tick = secs;
    }
    /* E-ink: redrawing the clocks every second flickers; the user can slow it down. */
    int refresh = setting_int("ClockRefresh", 0);
    if (refresh == 0 || main_left < 10000 || (refresh == 1 && used / 1000 % 10 == 0))
        view_refresh_clocks(app.view);
    return TRUE;
}

static int pending_plies, pending_index;

void start_problem(int variant, int plies, int index, const char *position)
{
    net_stop(); /* a problem is a game of one's own: no room, chat or online game left over */
    online_stop();
    server_stop();
    g_free(app.start_sfen);
    app.start_sfen = g_strdup(position);
    app.variant = variant;
    pending_plies = plies;
    pending_index = index;
    start_game(HANDICAP_NONE, MODE_FIRST);
}

void start_game(Handicap h, Mode mode)
{
    lesson_end(); /* not when the tutorial starts its own */
    turn_clock_new();
    skip_game_over();
    app.paused = FALSE;
    app.loaded_record = FALSE;
    forget_redo();
    if (app.engine)
        engine_cancel(app.engine);
    app.engine_thinking = app.hint_pending = FALSE;
    app.mode = mode;
    app.handicap = h;
    app.peer_left = FALSE;
    app.problem_plies = pending_plies;
    app.problem_index = pending_index;
    app.problem_check = FALSE;
    solution_pending = FALSE;
    pending_plies = 0;
    problem_note = FALSE;
    rules_set_variant(app.variant);
    if (mode == MODE_RANDOM)
        mode = g_random_boolean() ? MODE_FIRST : MODE_SECOND;
    app.players[FIRST] = mode == MODE_SECOND || mode == MODE_ENGINES ? PLAYER_ENGINE : PLAYER_HUMAN;
    app.players[SECOND] = mode == MODE_FIRST || mode == MODE_ENGINES ? PLAYER_ENGINE : PLAYER_HUMAN;
    if (connected_mode(mode)) {
        app.players[app.net_color] = PLAYER_HUMAN;
        app.players[!app.net_color] = PLAYER_REMOTE;
    }
    game_new(&app.game, h);
    /* Debugging: start from any position. */
    const char *sfen = g_getenv("DENBOKU_SFEN");
    if (sfen && pos_from_text(&app.game.start, sfen)) {
        app.game.pos = app.game.start;
        app.game.hashes[0] = app.game.pos.hash;
    }
    /* A position from the editor; a network room sends its own to the joiner ("SFEN"). */
    const char *custom = mode == MODE_HOST || mode == MODE_JOIN ? app.net_start
                         : server_game()                       ? NULL
                                                               : app.start_sfen;
    if (custom && pos_from_text(&app.game.start, custom)) {
        app.game.pos = app.game.start;
        app.game.hashes[0] = app.game.pos.hash;
    }
    g_free(app.start_sfen);
    app.start_sfen = NULL;
    app.takeback_mine = app.takeback_theirs = FALSE;
    for (int c = 0; c < 2; c++) {
        app.remaining[c] = tc()->main_s * 1000L;
        snprintf(app.game.names[c], sizeof app.game.names[c], "%s", player_name(c));
    }
    prepare_engine();
    view_set_flipped(app.view, app.players[SECOND] == PLAYER_HUMAN && app.players[FIRST] != PLAYER_HUMAN);
    view_set_ply(app.view, -1);
    view_set_hint(app.view, NULL);
    view_clear_premove(app.view);
    play_sound("start");
    next_turn();
    autosave_later(); /* the mode too: a joiner whose app closes before a move still rejoins */
}

/* ---- view callbacks ---- */

void view_move_cb(View *v, const Move *m, void *data)
{
    (void) v, (void) data;
    if (lesson_playing()) {
        tutorial_move(m);
        return;
    }
    Move legal = *m;
    /* Official rules: an illegal move (二歩, 打ち歩詰め, 王手放置, dead piece) loses. */
    if (pos_is_legal(&app.game.pos, &legal))
        play_move(&legal);
    else if (!app.problem_plies) /* a problem takes no illegal move: it is not played */
        play_illegal_move(m);
}

/* Premoves while the engine or the other player is to move. */
gboolean view_can_premove_cb(View *v, int color, void *data)
{
    (void) v, (void) data;
    return app.players[color] == PLAYER_HUMAN && app.players[!color] != PLAYER_HUMAN && !app.net_suspended &&
           !app.edit_saved;
}

gboolean view_square_tap_cb(View *v, int sq, void *data)
{
    (void) v, (void) data;
    return tutorial_tap(sq);
}

gboolean view_can_move_cb(View *v, int color, void *data)
{
    (void) v, (void) data;
    if (lesson_playing())
        return color == lesson_player();
    return app.players[color] == PLAYER_HUMAN && !app.hint_pending && !app.net_suspended;
}

void view_text_cb(View *v, int color, char *label, char *clock, gboolean *active, void *data)
{
    (void) v, (void) data;
    gboolean ja = i18n_japanese();
    const char *mark = ja ? SIDE_MARKS[color] : SIDE_SYMBOLS[color];
    if (app.wait_dialog) { /* a network game's lobby: who is in, on the board of the game to come */
        const char *name = color == app.net_color ? local_name() : app.remote_name ? app.remote_name : "";
        snprintf(label, 128, "%s %s", mark, name);
        clock[0] = 0;
        *active = FALSE;
        return;
    }
    gboolean to_move = app.game.result == RESULT_NONE && app.game.pos.side == color;
    gboolean thinking = to_move && app.engine_thinking && !app.hint_pending;
    snprintf(label, 128, "%s %s", mark, thinking ? tr("Thinking…") : player_name(color));
    if (to_move && app.paused && timed())
        snprintf(label, 128, "%s %s", mark, tr("Paused"));
    *active = to_move;
    if (problem_playing() && to_move && app.players[color] == PLAYER_HUMAN && !app.hint_pending) {
        /* What is left of the mate (it shortens with each move that keeps it), or that the last try was not it. */
        char *what = problem_label(2 * moves_left() - 1);
        snprintf(clock, 64, "%s", problem_note ? tr("Not the move") : what);
        g_free(what);
        return;
    }
    if (!timed()) {
        clock[0] = 0;
        snprintf(clock, 64, "%s", !to_move ? "" : app.paused ? tr("Paused") : app.hint_pending ? tr("Hint…") : tr("To move"));
        return;
    }
    long left = app.remaining[color] - (to_move ? elapsed_ms() : 0);
    char t[48];
    if (left > 0 || !tc()->byoyomi_s) {
        format_clock(MAX(left, 0), t, sizeof t);
    } else {
        long byo = tc()->byoyomi_s * 1000L + (to_move ? left : 0);
        char b[24];
        format_clock(MAX(byo, 0), b, sizeof b);
        snprintf(t, sizeof t, "%s %s", ja ? "秒読み" : "B", b);
    }
    /* The side to move: its clock alone, in the view's pill. */
    snprintf(clock, 64, "%s", t);
}

void view_ply_cb(View *v, void *data)
{
    (void) v, (void) data;
    replay_stop();
    update_move_label();
}
