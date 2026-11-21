/* Game records: replay as on lishogi, games opened from lishogi, records through the clipboard,
 * and the history of games played. */

#include "app.h"

/* ---- loading ---- */

/* A game read from a file, the clipboard or lishogi becomes the shown game, both sides human. */
void use_loaded_game(Game *g)
{
    replay_stop();
    if (app.engine)
        engine_cancel(app.engine);
    app.engine_thinking = app.hint_pending = FALSE;
    app.variant = CLAMP(rules_variant, 0, V_COUNT - 1);
    g_key_file_set_integer(app.settings, "Settings", "Variant", app.variant);
    app.game = *g;
    turn_clock_new();
    forget_redo();
    app.loaded_record = TRUE;
    app.problem_plies = 0;
    app.mode = MODE_TWO_PLAYERS;
    app.players[FIRST] = app.players[SECOND] = PLAYER_HUMAN;
    app.time_index = 0;
    view_set_flipped(app.view, FALSE);
    view_reload(app.view);
    view_set_ply(app.view, -1);
    next_turn();
    skip_game_over(); /* a finished record is shown, not announced */
}

gboolean load_record_text(const char *text)
{
    Game g;
    if (record_read_any_text(&g, text)) {
        use_loaded_game(&g);
        return TRUE;
    }
    rules_set_variant(app.variant); /* a failed read may have changed it */
    return FALSE;
}

/* Writes the game to a file beside `path` and renames it over: a crash or the power going in the
 * middle leaves the old file, not half of the new one. */
gboolean record_save(const char *path, gboolean alt)
{
    char *tmp = g_strconcat(path, ".new", NULL);
    FILE *f = g_fopen(tmp, "w");
    gboolean ok = f && (alt ? record_write_alt(&app.game, f) : record_write(&app.game, f));
    if (f)
        ok = !fclose(f) && ok;
    ok = ok && !g_rename(tmp, path);
    if (!ok)
        g_unlink(tmp);
    g_free(tmp);
    return ok;
}

void paste_game(void)
{
    char *text = gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
    if (!text || !load_record_text(text))
        toast(tr(RECORD_NONE_TEXT));
    g_free(text);
}

/* KIF, as lishogi's import page (lishogi.org/paste) and other apps read it. */
void copy_game(void)
{
    char *path = g_build_filename(app.home, "clipboard." RECORD_EXT, NULL);
    FILE *f = g_fopen(path, "w");
    char *text = NULL;
    if (f) {
        record_write(&app.game, f);
        fclose(f);
        g_file_get_contents(path, &text, NULL, NULL);
        g_remove(path);
    }
    g_free(path);
    if (text) {
        gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text, -1);
        toast(tr("Game copied"));
    }
    g_free(text);
}

/* ---- history ---- */

#define HISTORY_MAX 200

/* A finished game goes to the history as a KIF named after its start: the same game ended again
 * (after an undo, or resumed) replaces its file. The oldest go past HISTORY_MAX. */
void history_add(void)
{
    if (!app.game.count)
        return;
    g_mkdir_with_parents(app.history, 0755);
    char name[32];
    time_t t = (time_t) app.game.started;
    strftime(name, sizeof name, "%Y%m%d-%H%M%S." RECORD_EXT, localtime(&t));
    char *path = g_build_filename(app.history, name, NULL);
    record_save(path, FALSE);
    g_free(path);
    GPtrArray *files = g_ptr_array_new_with_free_func(g_free);
    GDir *dir = g_dir_open(app.history, 0, NULL);
    const char *file;
    while (dir && (file = g_dir_read_name(dir)))
        if (record_kind(file) == 1)
            g_ptr_array_add(files, g_strdup(file));
    if (dir)
        g_dir_close(dir);
    g_ptr_array_sort(files, compare_names_asc);
    for (guint i = 0; i + HISTORY_MAX < files->len; i++) {
        char *old = g_build_filename(app.history, g_ptr_array_index(files, i), NULL);
        g_remove(old);
        g_free(old);
    }
    g_ptr_array_free(files, TRUE);
}

/* 1 for a file in the game's own record format (RECORD_EXT, any case), 2 the other one, 0 neither. */
int record_kind(const char *name)
{
    const char *dot = strrchr(name, '.');
    return !dot ? 0 : !g_ascii_strcasecmp(dot + 1, RECORD_EXT) ? 1 : !g_ascii_strcasecmp(dot + 1, RECORD_ALT_EXT) ? 2 : 0;
}

/* The names of the record files in `folder`, in no order. */
GPtrArray *record_files(const char *folder)
{
    GPtrArray *files = g_ptr_array_new_with_free_func(g_free);
    GDir *dir = g_dir_open(folder, 0, NULL);
    const char *name;
    while (dir && (name = g_dir_read_name(dir)))
        if (record_kind(name))
            g_ptr_array_add(files, g_strdup(name));
    if (dir)
        g_dir_close(dir);
    return files;
}

static HistEntry *history_read(const char *file, const GStatBuf *st)
{
    char *path = g_build_filename(app.history, file, NULL);
    FILE *f = g_fopen(path, "r");
    g_free(path);
    Game *g = g_new(Game, 1);
    int variant = rules_variant;
    HistEntry *e = NULL;
    if (f && record_read(g, f)) {
        e = g_new(HistEntry, 1);
        *e = (HistEntry){g_strdup(file), st->st_mtime, st->st_size, g->started, rules_variant, g->result, g->reason, g->count,
                         {g_strdup(g->names[FIRST]), g_strdup(g->names[SECOND])}};
    }
    if (f)
        fclose(f);
    rules_set_variant(variant);
    g_free(g);
    return e;
}

static gint compare_entries_desc(gconstpointer a, gconstpointer b)
{
    return -strcmp((*(HistEntry *const *) a)->file, (*(HistEntry *const *) b)->file);
}

/* The history's readable games, newest first. history/index.txt keeps them, so only the files new
 * or changed since it was written are read; it is written again when anything changed. */
GPtrArray *history_games(void)
{
    char *index = g_build_filename(app.history, "index.txt", NULL);
    GHashTable *known = histindex_load(index);
    GHashTable *now = g_hash_table_new(g_str_hash, g_str_equal);
    GPtrArray *files = record_files(app.history);
    GPtrArray *games = g_ptr_array_new_with_free_func((GDestroyNotify) histindex_free);
    guint read = 0, added = 0;
    for (guint i = 0; i < files->len; i++) {
        const char *file = g_ptr_array_index(files, i);
        char *path = g_build_filename(app.history, file, NULL);
        GStatBuf st;
        gboolean found = !g_stat(path, &st);
        g_free(path);
        HistEntry *e = found ? g_hash_table_lookup(known, file) : NULL;
        if (e && e->mtime == (gint64) st.st_mtime && e->size == (gint64) st.st_size) {
            g_hash_table_steal(known, file);
        } else if (found) {
            e = history_read(file, &st);
            read++;
            added += e != NULL;
        }
        if (e) {
            g_ptr_array_add(games, e);
            g_hash_table_insert(now, e->file, e);
        }
    }
    if (added || g_hash_table_size(known)) /* new games, or some changed or gone */
        histindex_save(index, now);
    if (g_getenv("DENBOKU_DEBUG"))
        g_printerr("history: %u games, %u files read\n", games->len, read);
    g_ptr_array_sort(games, compare_entries_desc);
    g_hash_table_destroy(now);
    g_hash_table_destroy(known);
    g_ptr_array_free(files, TRUE);
    g_free(index);
    return games;
}

/* ---- replay ---- */

/* lishogi's speeds (ui/analyse/src/action-menu.ts): a move a second, one every five seconds, or
 * each after the time it took in the game. */
static guint replay_timer; /* while it plays */
static gboolean replay_on; /* playing or paused: the bar's Undo and Hint are its speed and pause */
static ReplaySpeed replay_speed;

gboolean replaying(void) { return replay_on; }

gboolean replay_paused(void) { return replay_on && !replay_timer; }

gboolean replay_has_times(void)
{
    for (int i = 0; i < app.game.count; i++)
        if (app.game.times[i] > 0)
            return TRUE;
    return FALSE;
}

static guint replay_delay(int ply)
{
    if (replay_speed == REPLAY_FAST)
        return 1000;
    if (replay_speed == REPLAY_SLOW)
        return 5000;
    /* The move about to be shown waited this long; lishogi adds 130 ms for lag. */
    return ply < 2 ? 1000 : (guint) app.game.times[ply] + 130;
}

static gboolean replay_step(gpointer data);

static void replay_schedule(int ply)
{
    replay_timer = g_timeout_add(replay_delay(ply), replay_step, NULL);
}

static gboolean replay_step(gpointer data)
{
    (void) data;
    replay_timer = 0;
    int ply = view_ply(app.view);
    if (ply < 0 || ply >= app.game.count) {
        replay_on = FALSE;
        update_move_label();
        return FALSE;
    }
    Move m = app.game.moves[ply];
    ply++;
    view_set_ply(app.view, ply == app.game.count ? -1 : ply);
    play_sound(app.game.gave_check[ply - 1] && show_moves() ? "check" : m.captured ? "capture" : "move");
    if (ply < app.game.count)
        replay_schedule(ply);
    else
        replay_on = FALSE;
    update_move_label();
    view_queue_bar(app.view);
    return FALSE;
}

/* From the move shown, or from the start at the end of the game, as lishogi does. */
static void replay_play(void)
{
    int ply = view_ply(app.view);
    if (ply < 0 || ply >= app.game.count) {
        ply = 0;
        view_set_ply(app.view, 0);
    }
    replay_schedule(ply);
}

static void replay_changed(void)
{
    update_move_label();
    view_queue_bar(app.view);
}

void replay_start(ReplaySpeed speed)
{
    replay_stop();
    replay_speed = speed;
    replay_on = TRUE;
    replay_play();
    replay_changed();
}

void replay_stop(void)
{
    if (!replay_on)
        return;
    if (replay_timer)
        g_source_remove(replay_timer);
    replay_timer = 0;
    replay_on = FALSE;
    replay_changed();
}

void replay_pause_toggle(void)
{
    if (!replay_on)
        return;
    if (replay_timer) {
        g_source_remove(replay_timer);
        replay_timer = 0;
    } else {
        replay_play();
    }
    replay_changed();
}

/* Fast, slow, real time (when the game has its times), and fast again. */
void replay_next_speed(void)
{
    if (!replay_on)
        return;
    replay_speed = (replay_speed + 1) % 3;
    if (replay_speed == REPLAY_REALTIME && !replay_has_times())
        replay_speed = REPLAY_FAST;
    if (replay_timer) { /* the next move at the new speed */
        g_source_remove(replay_timer);
        replay_play();
    }
    replay_changed();
}

const char *replay_speed_label(void)
{
    static const char *const LABELS[] = {"Fast", "Slow", "Real time"};
    return tr(LABELS[replay_speed]);
}
