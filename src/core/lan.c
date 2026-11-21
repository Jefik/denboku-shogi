/* Network games: hosting, joining, the lobby, reconnection, rematch (the chat is in chat.c). */

#include "app.h"

static void net_connected_cb(void *data);
static void net_line_cb(const char *line, void *data);
static void net_closed_cb(const char *why, void *data);
static void connection_lost(const char *why);
static gboolean net_accept_cb(const char *address, void *data);
static void net_denied_cb(const char *address, void *data);
static void net_password_cb(void *data);
static void lobby_update(void);

/* ---- LAN game ---- */

/* The guest's side from New Game's choice: Sente, Gote or (2) either at random. */
static int pick_color(int side) { return side == 2 ? (g_random_boolean() ? FIRST : SECOND) : side; }

/* The port this device hosts on. Joining uses NET_PORT unless the address says "host:port",
 * as game servers do. */
int net_port(void) { return CLAMP(setting_int("Port", NET_PORT), 1024, 65535); }

#ifndef G_OS_WIN32
/* An iptables command, quietly: deleting a rule that is not there is the usual case. */
static gboolean iptables(const char *what, const char *proto, int port)
{
    char p[16];
    snprintf(p, sizeof p, "%d", port);
    char *argv[] = {"iptables", (char *) what, "INPUT", "-p", (char *) proto, "--dport", p, "-j", "ACCEPT", NULL};
    int status = 1;
    return g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                        NULL, NULL, NULL, NULL, &status, NULL) && status == 0;
}
#endif

/* The Kindle firewall drops all incoming traffic; open the game port while a network game
 * or a search for one is going on. */
static void kindle_firewall(gboolean open)
{
#ifdef G_OS_WIN32
    (void) open;
#else
    static int opened; /* the game port opened, 0 for none */
    if (!app.kindle || open == (opened != 0))
        return;
    int port = open ? net_port() : opened;
    opened = open ? port : 0;
    for (int i = 0; i < 2; i++) {
        const char *proto = i ? "udp" : "tcp";
        int p = i ? NET_PORT : port;
        /* The rules a killed game left (it never closed them) go first, then this one is added. */
        for (int n = 0; n < 32 && iptables("-D", proto, p); n++)
            if (!open)
                break; /* closing: only the one this game added */
        if (open)
            iptables("-I", proto, p);
    }
#endif
}

/* The name sent to the other player: the one set in Preferences, or the device's. */
const char *local_name(void)
{
    static char name[64];
    char *own = g_strstrip(setting_string("PlayerName", ""));
    const char *device = g_getenv("DENBOKU_DEVICE"); /* Android's device name: its host name is "localhost" */
    snprintf(name, sizeof name, "%s", *own ? own : app.kindle ? "Kindle" : device && *device ? device : g_get_host_name());
    g_free(own);
    return name;
}

/* A lost connection does not end a network game: the clocks stop, the host listens again, the
 * joiner keeps trying to reconnect, and on reconnecting the host sends the whole game again
 * ("MOVES"), which is also how a joiner that restarted the app gets back into the game. */

#define NET_TIMEOUT_S 45

static const NetCallbacks NET_CB = {net_connected_cb, net_line_cb, net_closed_cb, net_accept_cb, net_denied_cb, net_password_cb};

static gboolean ping_cb(gpointer data)
{
    (void) data;
    if (!app.net && !app.net_suspended) {
        app.ping = 0;
        return FALSE;
    }
    if (!app.net_suspended && g_get_monotonic_time() - app.net_heard > NET_TIMEOUT_S * G_USEC_PER_SEC)
        connection_lost(tr("The other player stopped answering."));
    else
        net_send(app.net, "PING");
    return TRUE;
}

static void close_lost_dialog(void)
{
    if (app.lost_dialog)
        gtk_widget_destroy(app.lost_dialog);
    app.lost_dialog = NULL;
}

static char *room_password(void) { return g_strdup(app.room_password ? app.room_password : ""); }

static const NetLimits LIMITS = {HANDICAP_COUNT - 1, TIME_CUSTOM, V_COUNT - 1};

/* Host: makes the room show up in the joiners' list, with its settings as numbers (each joiner
 * writes them in its own language: room_label). */
static NetAnnounce *announce_room(void)
{
    char *password = room_password();
    NetRoom r = {.password = *password != 0, .variant = app.variant, .clock = app.time_index,
                 .main_s = app.custom_tc.main_s, .byoyomi_s = app.custom_tc.byoyomi_s,
                 .increment_s = app.custom_tc.increment_s, .setup = app.net_start != NULL, .handicap = app.handicap};
    g_strlcpy(r.name, local_name(), sizeof r.name);
    char *text = net_room_text(&r);
    NetAnnounce *a = net_announce(text, net_port());
    g_free(text);
    g_free(password);
    return a;
}

/* A room of the joiners' list, in this device's language; free it. */
static char *room_label(const char *announced)
{
    NetRoom r;
    if (!net_room_parse(announced, &LIMITS, &r))
        return g_strdup(*r.name ? r.name : "?");
    TimeControl custom = {NULL, r.main_s, r.byoyomi_s, r.increment_s};
    GString *t = g_string_new(r.name);
    if (r.password)
        g_string_append_printf(t, " %s", tr("(password)"));
    g_string_append_printf(t, " — %s · %s", variant_label(r.variant), tc_label_of(r.clock, &custom));
    if (r.setup || r.handicap != HANDICAP_NONE)
        g_string_append_printf(t, " · %s", r.setup ? tr("set-up position") : handicap_label((Handicap) r.handicap));
    return g_string_free(t, FALSE);
}

/* Host: announced while it waits for the other player to come back to the game. */
static NetAnnounce *resume_announce;

static void stop_resume_announce(void)
{
    if (resume_announce)
        net_announce_free(resume_announce);
    resume_announce = NULL;
}

static guint welcome_timer; /* see welcome_cb() */

void net_stop(void)
{
    stop_resume_announce();
    gboolean was = app.net || app.net_suspended;
    if (was && app.mode == MODE_HOST)
        upnp_close();
    g_free(app.public_address);
    app.public_address = NULL;
    net_free(app.net);
    app.net = NULL;
    if (was)
        chat_clear();
    app.net_suspended = FALSE;
    app.takeback_mine = app.takeback_theirs = FALSE;
    app.peer_left = FALSE;
    if (welcome_timer) {
        g_source_remove(welcome_timer);
        welcome_timer = 0;
    }
    if (app.banned) {
        g_hash_table_destroy(app.banned);
        app.banned = NULL;
    }
    if (app.net_retry)
        g_source_remove(app.net_retry);
    if (app.ping)
        g_source_remove(app.ping);
    app.net_retry = app.ping = 0;
    close_lost_dialog();
    kindle_firewall(FALSE);
    if (was)
        apply_settings();
}

/* Milliseconds the side to move has used, not counting a pause. */
static long turn_used_ms(void)
{
    gint64 now = app.net_suspended ? app.pause_start : g_get_monotonic_time();
    return (long) ((now - app.turn_start) / 1000);
}

static void net_resume(void)
{
    if (!app.net_suspended)
        return;
    app.turn_start += g_get_monotonic_time() - app.pause_start;
    app.net_suspended = FALSE;
    stop_resume_announce();
    close_lost_dialog();
    update_undo();
    view_refresh_clocks(app.view);
}

/* "MOVES <sente ms> <gote ms> <ms used this turn> <CSA moves>…": the game so far. */
static void net_send_game(gboolean fresh)
{
    GString *s = g_string_new("MOVES");
    if (fresh) {
        long t = tc()->main_s * 1000L;
        g_string_append_printf(s, " %ld %ld 0", t, t);
    } else {
        g_string_append_printf(s, " %ld %ld %ld", app.remaining[FIRST], app.remaining[SECOND], turn_used_ms());
        Position p = app.game.start;
        for (int i = 0; i < app.game.count; i++) {
            Move m = app.game.moves[i];
            char text[16];
            move_to_net(&p, &m, text);
            pos_make(&p, &m);
            g_string_append_printf(s, " %s", text);
        }
    }
    net_send(app.net, s->str);
    g_string_free(s, TRUE);
}

/* Joiner: sets up the game the host sent. */
static void net_load_game(void)
{
    const char *fields = app.net_moves ? app.net_moves : "";
    long ms[3];
    int n = net_moves_fields(fields, ms), field = 0;
    char **f = g_strsplit(fields, " ", -1);
    app.net_suspended = FALSE;
    close_lost_dialog();
    start_game(app.handicap, MODE_JOIN);
    for (int i = 0; f[i]; i++) {
        Move m;
        if (!*f[i] || field++ < 3)
            continue;
        if (!move_from_net(&app.game.pos, f[i], &m))
            break;
        game_play(&app.game, &m, 0);
    }
    next_turn();
    if (n >= 3) {
        app.remaining[FIRST] = ms[0];
        app.remaining[SECOND] = ms[1];
        app.turn_start = g_get_monotonic_time() - ms[2] * (gint64) 1000;
    }
    g_strfreev(f);
    view_refresh_clocks(app.view);
}

/* "GAME <handicap> <clock> <joiner's color> <move rule>": the host's settings. */
static void net_send_settings(void)
{
    char line[64];
    snprintf(line, sizeof line, "GAME %d %d %d %d", app.handicap, app.time_index, !app.net_color, app.net_rule);
    net_send(app.net, line);
    if (app.time_index == TIME_CUSTOM) {
        const TimeControl *t = tc();
        snprintf(line, sizeof line, "CLOCK %d %d %d", t->main_s, t->byoyomi_s, t->increment_s);
        net_send(app.net, line);
    }
    snprintf(line, sizeof line, "VARIANT %d", app.variant);
    net_send(app.net, line);
    char *sfen = g_strconcat("SFEN ", app.net_start ? app.net_start : "", NULL);
    net_send(app.net, sfen);
    g_free(sfen);
}

/* Host: the joiner is in. The host decides the game: handicap, clock, who plays sente and the
 * move rule; in the lobby the game starts when the host presses Start. */
static void host_welcome(void)
{
    net_send_settings();
    if (app.wait_dialog) {
        lobby_update();
        return;
    }
    net_send_game(FALSE);
    net_resume();
}

/* The joiner's DENBOKU line comes right after it connects: wait for it (2 s at most, for one that
 * never sends it), so that the settings include what its version knows (VARIANT, CLOCK). */

static gboolean welcome_cb(gpointer data)
{
    (void) data;
    welcome_timer = 0;
    if (app.net && app.mode == MODE_HOST)
        host_welcome();
    return FALSE;
}

/* After a lobby/password dialog closes, the Kindle's window manager can leave the game window
 * behind its home; bring it back (the process itself never closes on Leave). */
static void back_to_game(void)
{
    if (!app.window)
        return;
    gtk_window_present(GTK_WINDOW(app.window));
    GdkWindow *w = gtk_widget_get_window(app.window);
    if (w)
        gdk_window_raise(w);
}

/* The lobby was hidden for the password prompt: gtk_dialog_run() ends on that, but the lobby goes on. */
static gboolean lobby_hidden;

/* Joiner: asks for the room password (from the main loop: it runs a dialog). */
static gboolean ask_password_idle(gpointer data)
{
    (void) data;
    if (!app.net || app.mode != MODE_JOIN)
        return FALSE;
    GtkWidget *d = dialog_new(tr("Network game"));
    /* Parent it to the game window, which is the active one, not to the lobby dialog behind
     * it: the window manager then gives the prompt (and its keyboard) the focus. Hide the
     * lobby while asking, too: a modal dialog over another modal one does not always get it. */
    gtk_window_set_transient_for(GTK_WINDOW(d), GTK_WINDOW(app.window));
    GtkWidget *lobby = app.wait_dialog;
    if (lobby) {
        lobby_hidden = TRUE;
        gtk_widget_hide(lobby);
    }
    GtkWidget *entry = text_entry();
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), gtk_label_new(tr("This room has a password:")), FALSE, FALSE, 6);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), entry, FALSE, FALSE, 6);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), show_password_check(entry), FALSE, FALSE, 0);
    gtk_dialog_add_button(GTK_DIALOG(d), tr("Leave"), GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(d), tr("Join"), GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
    gtk_widget_show_all(d);
    /* On the Kindle the on-screen keyboard opens on focus-in; wait for the window manager to
     * give the dialog focus, then focus the entry and ask for the keyboard again. Read the entry
     * before destroying the dialog (run_dialog destroys it). */
    dialog_focus_entry(entry);
    gboolean ok = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK;
    char *password = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
    gtk_widget_destroy(d);
    if (lobby && app.wait_dialog == lobby)
        gtk_widget_show(lobby);
    if (!app.wait_dialog) /* the lobby is still behind this prompt: keep it in front */
        back_to_game();
    if (ok) {
        g_free(app.net_password);
        app.net_password = password;
        net_password(app.net, password);
    } else {
        g_free(password);
        if (app.wait_dialog)
            gtk_dialog_response(GTK_DIALOG(app.wait_dialog), GTK_RESPONSE_REJECT);
    }
    return FALSE;
}

static gboolean denied_idle(gpointer data)
{
    (void) data;
    g_free(app.net_password);
    app.net_password = NULL;
    message(tr("Wrong password for this room."));
    if (app.wait_dialog)
        gtk_dialog_response(GTK_DIALOG(app.wait_dialog), GTK_RESPONSE_REJECT);
    else {
        net_stop();
        next_turn();
    }
    return FALSE;
}

static gboolean net_accept_cb(const char *address, void *data)
{
    (void) data;
    return !app.banned || GPOINTER_TO_INT(g_hash_table_lookup(app.banned, address)) < BANNED;
}

static void wrong_password(const char *from);

static void parse(const char *line, NetLine *out)
{
    net_parse(line, &LIMITS, out);
}

/* Joiner: the settings lines of this connection not taken during a game, and their turn to be. */
static GPtrArray *settings_held;
static gboolean settings_apply;

static void net_denied_cb(const char *address, void *data)
{
    (void) data;
    wrong_password(address);
}

/* Joiner: the room has a password; coming back, the one given before. */
static void net_password_cb(void *data)
{
    (void) data;
    if (app.net_password)
        net_password(app.net, app.net_password);
    else
        g_idle_add(ask_password_idle, NULL);
}

static void net_connected_cb(void *data)
{
    (void) data;
    app.net_heard = g_get_monotonic_time();
    app.peer_version = 0;
    chat_presets_update();
    app.peer_left = FALSE;
    if (settings_held)
        g_ptr_array_set_size(settings_held, 0);
    if (app.over_dialog) { /* the other player came back after leaving the game-over screen */
        if (app.over_label)
            gtk_label_set_text(GTK_LABEL(app.over_label), "");
        if (app.over_rematch)
            gtk_widget_set_sensitive(app.over_rematch, TRUE);
    }
    if (!app.ping)
        app.ping = g_timeout_add_seconds(15, ping_cb, NULL);
    char name[128];
    snprintf(name, sizeof name, "NAME %s", local_name());
    net_send(app.net, name);
    /* Both sides announce the protocol; the host waits for this before using newer lines. */
    char greeting[32];
    snprintf(greeting, sizeof greeting, "DENBOKU %d", NET_VERSION);
    net_send(app.net, greeting);
    if (app.mode != MODE_HOST)
        return;
    if (welcome_timer)
        g_source_remove(welcome_timer);
    welcome_timer = g_timeout_add(2000, welcome_cb, NULL);
}

static gboolean replaced_idle(gpointer data)
{
    (void) data;
    net_stop();
    next_turn();
    message(tr("The game continued on another connection."));
    return FALSE;
}

/* The joiner was removed by the host, or refused by its ban list. */
static gboolean lobby_reject_idle(gpointer data)
{
    (void) data;
    if (app.wait_dialog)
        gtk_dialog_response(GTK_DIALOG(app.wait_dialog), GTK_RESPONSE_REJECT);
    return FALSE;
}

/* Takes moves back until `side` is to move again (the last move as well when that side just
 * played), the takeback both sides agreed to. */
static void takeback_apply(int side)
{
    view_clear_premove(app.view);
    if (app.engine)
        engine_cancel(app.engine);
    app.engine_thinking = app.hint_pending = FALSE;
    view_set_ply(app.view, -1);
    do
        game_undo(&app.game);
    while (app.game.count > 0 && app.game.pos.side != side);
    view_set_hint(app.view, NULL);
    next_turn();
}

void takeback_answer(gboolean yes)
{
    app.takeback_theirs = FALSE;
    if (!app.net)
        return;
    net_send(app.net, yes ? "TAKEBACK 1" : "TAKEBACK 0");
    if (!yes)
        app.takeback_refused_theirs = app.game.count;
    if (yes)
        takeback_apply(!app.net_color);
    else
        next_turn();
}

/* The other player asked to take a move back and waits for the answer: the view's bar asks, in
 * its buttons' place, and the game goes on (a move declines it, as on lishogi). */
gboolean takeback_asked(void)
{
    return app.net && app.takeback_theirs && app.game.result == RESULT_NONE && !app.net_suspended;
}

void takeback_decline_by_moving(void)
{
    if (app.net && app.takeback_theirs) {
        app.takeback_theirs = FALSE;
        net_send(app.net, "TAKEBACK 0");
    }
}

/* Guessing the room password: the fifth wrong one bans the address from the room. The counts live
 * with the bans, for as long as the room. */
static void wrong_password(const char *from)
{
    if (!from || !*from)
        return;
    if (!app.banned)
        app.banned = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    int n = GPOINTER_TO_INT(g_hash_table_lookup(app.banned, from)) + 1;
    g_hash_table_insert(app.banned, g_strdup(from), GINT_TO_POINTER(n));
}

/* The host's settings as this side has them: a line said again (CLOCK on READY) changes nothing. */
static char *settings_key(void)
{
    const TimeControl *t = tc();
    return g_strdup_printf("%d %d %d %d %d %d %d %d %s", app.handicap, app.time_index, app.net_color, app.net_rule,
                           app.variant, t->main_s, t->byoyomi_s, t->increment_s, app.net_start ? app.net_start : "");
}

/* Joiner: a setting from the host taken; in the lobby a change means pressing Ready again. */
static void setting_taken(char *before)
{
    char *now = settings_key();
    if (strcmp(before, now) && app.lobby_button && GTK_IS_TOGGLE_BUTTON(app.lobby_button) &&
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app.lobby_button))) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.lobby_button), FALSE); /* sends READY 0 */
        toast(tr("The host changed the settings."));
    }
    g_free(now);
    g_free(before);
    lobby_update();
}

static void net_line_cb(const char *line, void *data)
{
    (void) data;
    gboolean join = app.mode == MODE_JOIN, host = app.mode == MODE_HOST;
    NetLine l;
    app.net_heard = g_get_monotonic_time();
    if (g_getenv("DENBOKU_DEBUG") && strcmp(line, "PING"))
        g_printerr("net: %s\n", line);
    parse(line, &l);
    /* The host sends its settings again on a reconnection: with a game going on they are the
     * same, and a change (the variant is the board's size) is not taken; they wait for a "MOVES"
     * that starts a new game. */
    if (join && !app.wait_dialog && app.game.result == RESULT_NONE && !settings_apply &&
        (l.kind == NL_GAME || l.kind == NL_VARIANT || l.kind == NL_CLOCK || l.kind == NL_SFEN)) {
        if (!settings_held)
            settings_held = g_ptr_array_new_with_free_func(g_free);
        if (settings_held->len < 8)
            g_ptr_array_add(settings_held, g_strdup(line));
        return;
    }
    char *before;
    switch (l.kind) {
    case NL_VERSION:
        app.peer_version = l.n[0];
        if (welcome_timer) {
            g_source_remove(welcome_timer);
            welcome_cb(NULL);
        }
        lobby_update();
        return;
    case NL_KICKED:
    case NL_BANNED:
        if (join) {
            app.net_reject = l.kind == NL_BANNED ? 2 : 1;
            g_idle_add(lobby_reject_idle, NULL);
        }
        return;
    case NL_TAKEBACK:
        if (app.game.count == app.takeback_refused_theirs)
            net_send(app.net, "TAKEBACK 0"); /* declined already for this move */
        else if (!app.takeback_theirs && app.game.result == RESULT_NONE) {
            app.takeback_theirs = TRUE;
            play_sound("notify");
            next_turn();
        }
        return;
    case NL_TAKEBACK_ANSWER:
        if (!app.takeback_mine)
            return;
        app.takeback_mine = FALSE;
        if (l.n[0] && app.game.result == RESULT_NONE) { /* not once the game ended here (a flag) */
            takeback_apply(app.net_color);
        } else {
            app.takeback_refused_mine = app.game.count;
            set_move_text(tr("Takeback declined."));
            update_undo();
        }
        return;
    case NL_GAME:
        if (!join)
            return;
        before = settings_key();
        app.handicap = (Handicap) l.n[0];
        app.time_index = l.n[1];
        app.net_color = l.n[2] ? SECOND : FIRST;
        app.net_rule = l.n[3];
        setting_taken(before);
        return;
    case NL_VARIANT:
        if (!join)
            return;
        before = settings_key();
        app.variant = l.n[0];
        rules_set_variant(app.variant);
        setting_taken(before);
        return;
    case NL_CLOCK:
        if (!join)
            return;
        before = settings_key();
        app.time_index = TIME_CUSTOM;
        app.custom_tc = (TimeControl) {NULL, l.n[0], l.n[1], l.n[2]};
        app.custom_min = l.n[0] / 60;
        app.custom_byoyomi = l.n[1];
        app.custom_increment = l.n[2];
        setting_taken(before);
        return;
    case NL_SFEN:
        if (!join)
            return;
        before = settings_key();
        g_free(app.net_start);
        app.net_start = g_strdup(l.text);
        setting_taken(before);
        return;
    case NL_READY:
        if (!host)
            return;
        app.remote_ready = l.n[0];
        /* The custom clock again, for one that missed it. */
        if (app.remote_ready && app.time_index == TIME_CUSTOM) {
            const TimeControl *custom = tc();
            char clock[64];
            snprintf(clock, sizeof clock, "CLOCK %d %d %d", custom->main_s, custom->byoyomi_s, custom->increment_s);
            net_send(app.net, clock);
        }
        lobby_update();
        if (app.remote_ready)
            play_sound("notify");
        return;
    case NL_DENIED:
        if (join)
            g_idle_add(denied_idle, NULL);
        return;
    case NL_BYE:
        if (!join)
            return;
        /* The host took a newer connection to this game (the same player on another device). */
        if (app.game.result == RESULT_NONE)
            game_end(&app.game, RESULT_ABORTED, REASON_ABORT);
        g_idle_add(replaced_idle, NULL); /* not inside this connection's callback */
        return;
    case NL_REMATCH:
        app.rematch_theirs = TRUE;
        if (!app.rematch_mine && app.over_label)
            play_sound("notify");
        rematch_check();
        return;
    case NL_CHAT:
        chat_add(app.remote_name ? app.remote_name : tr("Opponent"), l.text);
        play_sound("notify");
        return;
    case NL_MOVES:
        if (!join)
            return;
        /* The settings held back count for a game without moves: the host began another one. */
        if (settings_held && l.n[0] <= 3) {
            settings_apply = TRUE;
            for (guint i = 0; i < settings_held->len; i++)
                net_line_cb(settings_held->pdata[i], NULL);
            settings_apply = FALSE;
        }
        if (settings_held)
            g_ptr_array_set_size(settings_held, 0);
        g_free(app.net_moves);
        app.net_moves = g_strdup(l.text);
        app.rematch_mine = app.rematch_theirs = FALSE;
        if (app.over_dialog)
            gtk_dialog_response(GTK_DIALOG(app.over_dialog), GTK_RESPONSE_CLOSE);
        if (app.wait_dialog)
            gtk_dialog_response(GTK_DIALOG(app.wait_dialog), GTK_RESPONSE_OK);
        else {
            apply_settings();
            net_load_game();
        }
        return;
    case NL_NAME:
        g_free(app.remote_name);
        app.remote_name = g_strdup(l.text);
        for (int side = 0; side < 2; side++)
            if (app.players[side] == PLAYER_REMOTE)
                snprintf(app.game.names[side], sizeof app.game.names[side], "%s", app.remote_name);
        view_refresh_clocks(app.view);
        lobby_update();
        return;
    case NL_PING:
        return;
    case NL_RESIGN:
    case NL_ILLEGAL:
    case NL_MOVE:
    case NL_TIME_UP:
        break;
    }
    if (app.game.result != RESULT_NONE)
        return;
    /* The other player may resign on either turn (also to leave for another game). */
    int remote = app.players[FIRST] == PLAYER_REMOTE ? FIRST : SECOND;
    if (l.kind == NL_RESIGN || l.kind == NL_TIME_UP) { /* its own clock, which only its side decides on */
        if (app.players[remote] == PLAYER_REMOTE) {
            game_end(&app.game, remote == FIRST ? RESULT_SECOND_WIN : RESULT_FIRST_WIN, l.kind == NL_RESIGN ? REASON_RESIGN : REASON_TIME);
            next_turn();
        }
        return;
    }
    int side = app.game.pos.side;
    if (app.net_suspended || app.players[side] != PLAYER_REMOTE)
        return;
    Move m;
    if (l.kind == NL_ILLEGAL) {
        if (l.text && move_from_net_any(&app.game.pos, l.text, &m)) {
            play_illegal_move(&m);
        } else {
            game_end(&app.game, side == FIRST ? RESULT_SECOND_WIN : RESULT_FIRST_WIN, REASON_ILLEGAL);
            next_turn();
        }
        return;
    }
    if (move_from_net(&app.game.pos, line, &m))
        play_move(&m);
}

static gboolean net_reconnect_cb(gpointer data)
{
    (void) data;
    app.net_retry = 0;
    if (!app.net_suspended)
        return FALSE;
    if (app.mode == MODE_HOST && app.net) { /* still listening: the joiner comes back through the door */
        net_drop(app.net);
        return FALSE;
    }
    net_free(app.net);
    app.net = app.mode == MODE_HOST ? net_host(net_port(), app.room_password, &NET_CB, NULL, NULL)
                                    : net_join(app.net_address, NET_PORT, &NET_CB, NULL);
    if (!app.net)
        app.net_retry = g_timeout_add_seconds(3, net_reconnect_cb, NULL);
    return FALSE;
}

static void lost_response_cb(GtkDialog *d, int response, gpointer data)
{
    (void) d, (void) response, (void) data;
    app.lost_dialog = NULL;
    gtk_widget_destroy(GTK_WIDGET(d));
    if (app.game.result == RESULT_NONE)
        game_end(&app.game, RESULT_ABORTED, REASON_ABORT);
    net_stop();
    next_turn();
}

static gboolean lobby_relisten_cb(gpointer data)
{
    (void) data;
    app.net_retry = 0;
    if (app.net)
        net_drop(app.net); /* the port stays open, and whoever waits at it */
    else if (!(app.net = net_host(net_port(), app.room_password, &NET_CB, NULL, NULL)))
        app.net_retry = g_timeout_add_seconds(3, lobby_relisten_cb, NULL);
    lobby_update();
    return FALSE;
}

static void net_suspend(const char *why);
static void upnp_done_cb(const char *ip, void *data);

/* The connection is gone; `why` is shown when it says something to the player, NULL otherwise. */
static void connection_lost(const char *why)
{
    if (app.wait_dialog && app.mode == MODE_HOST && app.lobby_players) {
        /* The joiner left the lobby: wait for someone else. */
        app.remote_ready = FALSE;
        g_free(app.remote_name);
        app.remote_name = NULL;
        app.net_retry = g_idle_add(lobby_relisten_cb, NULL);
        return;
    }
    if (app.wait_dialog) {
        gtk_dialog_response(GTK_DIALOG(app.wait_dialog), GTK_RESPONSE_REJECT);
        return;
    }
    if (app.net_suspended) {
        /* A reconnection attempt failed: try again shortly. */
        if (!app.net_retry)
            app.net_retry = g_timeout_add_seconds(3, net_reconnect_cb, NULL);
        return;
    }
    if (app.game.result != RESULT_NONE) {
        /* After the game: the rematch offer is no longer possible. */
        app.peer_left = TRUE;
        if (app.over_label)
            gtk_label_set_text(GTK_LABEL(app.over_label), tr("The other player left."));
        if (app.over_rematch)
            gtk_widget_set_sensitive(app.over_rematch, FALSE);
        app.rematch_mine = app.rematch_theirs = FALSE;
        update_move_label();
        play_sound("notify");
        return;
    }
    if (app.game.result != RESULT_NONE ||
        (app.players[FIRST] != PLAYER_REMOTE && app.players[SECOND] != PLAYER_REMOTE))
        return;
    net_suspend(why);
}

static gboolean too_old_idle(gpointer data)
{
    (void) data;
    message(tr("The other player has an older version of the game, without encryption."));
    return FALSE;
}

/* The system's words ("connection closed") are for the log, not the notice. */
static void net_closed_cb(const char *why, void *data)
{
    (void) data;
    g_printerr("network: %s\n", why);
    if (why == NET_TOO_OLD && app.wait_dialog)
        g_idle_add(too_old_idle, NULL);
    connection_lost(NULL);
}

/* The game waits, clocks stopped, for the connection: the host listens again, the joiner retries. */
static void net_suspend(const char *why)
{
    app.net_suspended = TRUE;
    /* A takeback asked before the connection dropped is dropped too: its answer may be lost. */
    app.takeback_mine = app.takeback_theirs = FALSE;
    update_undo();
    app.pause_start = g_get_monotonic_time();
    if (app.mode == MODE_HOST && !resume_announce)
        resume_announce = announce_room();
    /* Not from inside the connection's own callback. */
    app.net_retry = g_idle_add(net_reconnect_cb, NULL);
    GtkWidget *d = app.lost_dialog = dialog_new(tr("Network game"));
    drawer_set_sticky(d);
    char *text = g_strdup_printf("%s%s%s\n\n%s", tr("The connection to the other player was lost."), why ? "\n" : "", why ? why : "",
                                 app.mode == MODE_HOST ? tr("The clocks are stopped. Waiting for the other player to join again…")
                                                       : tr("The clocks are stopped. Reconnecting…"));
    GtkWidget *label = row_para(text);
    g_free(text);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), label, TRUE, TRUE, 12);
    gtk_dialog_add_button(GTK_DIALOG(d), tr("End the game"), GTK_RESPONSE_CANCEL);
    g_signal_connect(d, "response", G_CALLBACK(lost_response_cb), NULL);
    gtk_widget_show_all(d);
}

static gboolean host_rejoin_idle(gpointer data)
{
    (void) data;
    if (app.net_suspended && !app.lost_dialog)
        net_suspend(tr("The game was closed and opened again."));
    return FALSE;
}

/* A host whose app closed during a network game: resume() brought the game back from the autosave;
 * listen again for the joiner, which keeps retrying, as after a lost connection. */
void host_rejoin(void)
{
    GKeyFile *k = app.settings;
    if (g_key_file_get_integer(k, "Game", "Mode", NULL) != MODE_HOST || app.game.result != RESULT_NONE ||
        !net_available())
        return;
    app.mode = MODE_HOST;
    app.net_color = CLAMP(g_key_file_get_integer(k, "Game", "NetColor", NULL), 0, 1);
    app.net_rule = CLAMP(g_key_file_get_integer(k, "Game", "NetRule", NULL), 0, 5);
    app.handicap = (Handicap) CLAMP(g_key_file_get_integer(k, "Game", "Handicap", NULL), 0, HANDICAP_COUNT - 1);
    g_free(app.net_start);
    app.net_start = g_key_file_get_string(k, "Game", "NetStart", NULL);
    if (app.net_start && !*app.net_start) {
        g_free(app.net_start);
        app.net_start = NULL;
    }
    g_free(app.room_password);
    app.room_password = g_key_file_get_string(k, "Game", "RoomPassword", NULL);
    app.players[app.net_color] = PLAYER_HUMAN;
    app.players[!app.net_color] = PLAYER_REMOTE;
    kindle_firewall(TRUE);
    if (setting_int("UPnP", 1))
        upnp_open(net_port(), "Denboku Shōgi", upnp_done_cb, NULL);
    app.net_suspended = TRUE; /* the clocks wait from now */
    app.pause_start = g_get_monotonic_time();
    next_turn();
    /* Once the game window is up, or the dialog opens behind it. */
    g_idle_add(host_rejoin_idle, NULL);
}

/* A host is listed once: heard again from another of its addresses (its cable and its Wi-Fi), the
 * same name and port are the same room. */
static void host_found_cb(const char *address, const char *name, void *data)
{
    GtkListStore *store = data;
    GtkTreeIter it;
    const char *port = strchr(address, ':');
    char *key = g_strdup_printf("%s\t%s", name, port ? port : "");
    gboolean more = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &it), same = FALSE;
    while (more && !same) {
        char *have, *have_key;
        gtk_tree_model_get(GTK_TREE_MODEL(store), &it, 0, &have, 2, &have_key, -1);
        same = !strcmp(have, address) || (*name && !strcmp(have_key, key));
        g_free(have), g_free(have_key);
        more = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &it);
    }
    if (!same) {
        char *room = room_label(name), *label = g_strdup_printf("%s  (%s)", room, address);
        g_free(room);
        gtk_list_store_append(store, &it);
        gtk_list_store_set(store, &it, 0, address, 1, label, 2, key, -1);
        g_free(label);
    }
    g_free(key);
}

static void host_chosen_cb(GtkTreeSelection *sel, gpointer entry)
{
    GtkTreeModel *model;
    GtkTreeIter it;
    if (!gtk_tree_selection_get_selected(sel, &model, &it))
        return;
    char *address;
    gtk_tree_model_get(model, &it, 0, &address, -1);
    gtk_entry_set_text(GTK_ENTRY(entry), address);
    g_free(address);
}

static void copy_link_cb(GtkWidget *b, gpointer entry)
{
    (void) b;
    const char *t = gtk_entry_get_text(GTK_ENTRY(entry));
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), t, -1);
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_PRIMARY), t, -1);
}

/* Shows `text` until the connection is ready (OK) or the user cancels; `link` adds a field for
 * the lishogi challenge link, with a Copy button. */
gboolean wait_for_peer_link(const char *text, gboolean link)
{
    app.wait_dialog = dialog_new(tr("Network game"));
    drawer_set_sticky(app.wait_dialog);
    GtkWidget *label = app.wait_label = row_para(text);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(app.wait_dialog)->vbox), label, TRUE, TRUE, 12);
    if (link) {
        GtkWidget *row = gtk_hbox_new(FALSE, 6);
        app.wait_link = gtk_entry_new();
        gtk_editable_set_editable(GTK_EDITABLE(app.wait_link), FALSE);
        gtk_entry_set_width_chars(GTK_ENTRY(app.wait_link), 32);
        gtk_entry_set_text(GTK_ENTRY(app.wait_link), "…");
        GtkWidget *copy = gtk_button_new_with_label(tr("Copy"));
        g_signal_connect(copy, "clicked", G_CALLBACK(copy_link_cb), app.wait_link);
        gtk_box_pack_start(GTK_BOX(row), app.wait_link, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(row), copy, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(GTK_DIALOG(app.wait_dialog)->vbox), row, FALSE, FALSE, 6);
    }
    gtk_dialog_add_button(GTK_DIALOG(app.wait_dialog), tr("Cancel"), GTK_RESPONSE_CANCEL);
    gtk_widget_show_all(app.wait_dialog);
    int r = gtk_dialog_run(GTK_DIALOG(app.wait_dialog));
    gtk_widget_destroy(app.wait_dialog);
    app.wait_dialog = NULL;
    app.wait_link = app.wait_label = NULL;
    return r == GTK_RESPONSE_OK;
}

void wait_set_text(const char *text)
{
    if (app.wait_label)
        gtk_label_set_text(GTK_LABEL(app.wait_label), text);
}

void wait_set_link(const char *link)
{
    if (app.wait_link)
        gtk_entry_set_text(GTK_ENTRY(app.wait_link), link);
}

/* ---- lobby and rematch ---- */

/* The host removes the current player from the room; a ban also refuses that address until the
 * host closes the room (the list lives in memory only). */
static void lobby_kick_cb(GtkWidget *w, gpointer data)
{
    (void) w;
    gboolean ban = GPOINTER_TO_INT(data);
    if (!app.net || !app.remote_name)
        return;
    if (ban) {
        char *question = g_strdup_printf("%s\n\n%s", app.remote_name,
                                         tr("Ban this player from the room?"));
        gboolean yes = confirm_with(question, tr("Ban"));
        g_free(question);
        if (!yes)
            return;
        if (!app.banned)
            app.banned = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        char *address = net_peer_address(app.net);
        if (address)
            g_hash_table_insert(app.banned, address, GINT_TO_POINTER(BANNED));
    }
    net_send(app.net, ban ? "BANNED" : "KICKED");
    net_drop(app.net);
    g_free(app.remote_name);
    app.remote_name = NULL;
    app.remote_ready = FALSE;
    lobby_update();
}

/* Behind the lobby the board shows the game to come (its variant and handicap as the host has
 * them now), not the game left; `on` FALSE gives the view its game back. */
static void lobby_board(gboolean on)
{
    static Game *preview;
    static gboolean left_flipped;
    if (!on) {
        if (preview && view_game(app.view) == preview && view_flipped(app.view) != left_flipped)
            view_set_flipped(app.view, left_flipped);
        view_set_game(app.view, &app.game);
        return;
    }
    if (!preview || view_game(app.view) != preview)
        left_flipped = view_flipped(app.view);
    if (view_flipped(app.view) != (app.net_color == SECOND)) /* as the game will be; a redraw of it all */
        view_set_flipped(app.view, app.net_color == SECOND);
    Handicap h = app.net_start ? HANDICAP_NONE : app.handicap;
    static int shown_variant, shown_handicap;
    if (preview && view_game(app.view) == preview && shown_variant == app.variant && shown_handicap == (int) h) {
        view_refresh_clocks(app.view); /* a name came */
        return;
    }
    if (!preview)
        preview = g_new0(Game, 1);
    shown_variant = app.variant, shown_handicap = h;
    rules_set_variant(app.variant);
    apply_style(); /* the variant's pieces */
    game_new(preview, h);
    view_set_game(app.view, preview);
}

static void lobby_update(void)
{
    if (!app.lobby_players)
        return;
    lobby_board(TRUE);
    gboolean host = app.mode == MODE_HOST, joined = app.net && app.remote_name;
    /* Each seat a row: the name, and on the right whether the player is ready (a pill). */
    GList *old = gtk_container_get_children(GTK_CONTAINER(app.lobby_players));
    for (GList *o = old; o; o = o->next)
        gtk_widget_destroy(o->data);
    g_list_free(old);
    for (int c = 0; c < 2; c++) {
        gboolean me = c == app.net_color, seated = me || joined;
        char *name = me ? g_strdup_printf("%s (%s)", local_name(), tr("you"))
                     : joined ? g_strdup(app.remote_name) : g_strdup(host ? tr("waiting for a player…") : tr("the host"));
        gboolean ready = !seated ? FALSE
                         : me ? (host || (app.lobby_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app.lobby_button))))
                         : (!host || app.remote_ready);
        char *text = g_strdup_printf("%s %s", SIDE_MARKS[c], name);
        GtkWidget *row = gtk_hbox_new(FALSE, 8), *label = row_text(text, !seated, FALSE, FALSE);
        gtk_container_set_border_width(GTK_CONTAINER(row), 8);
        gtk_box_pack_start(GTK_BOX(row), label, TRUE, TRUE, 0);
        if (seated) {
            GtkWidget *pill = gtk_toggle_button_new_with_label(ready ? tr("✓ Ready") : tr("Waiting…"));
            gtk_widget_set_name(pill, "denboku-chip");
            gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(pill), FALSE);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pill), ready);
            gtk_widget_set_can_focus(pill, FALSE);
            g_signal_connect(pill, "button-press-event", G_CALLBACK(gtk_true), NULL); /* a state, not a control */
            gtk_box_pack_end(GTK_BOX(row), pill, FALSE, FALSE, 0);
        }
        gtk_box_pack_start(GTK_BOX(app.lobby_players), row, FALSE, FALSE, 0);
        g_free(name);
        g_free(text);
    }
    gtk_widget_show_all(app.lobby_players);
    const char *hc = app.net_start ? tr("set-up position")
                     : handicap_label(app.handicap);
    /* The host has chips for the variant, the clock and the side; what they do not show is here. */
    GString *set = g_string_new(NULL);
    if (!host)
        g_string_append_printf(set, "%s: %s\n%s: %s\n", tr("Variant"), variant_label(app.variant), tr("Clock"), tc_label());
    g_string_append_printf(set, "%s: %s\n%s: %s\n%s: %s", tr("Handicap"), hc, tr("Legal moves"),
                           tr(app.net_rule & 1 ? "Hidden" : "Shown"), tr("Illegal moves"), tr(ILLEGAL_LABELS[app.net_rule >> 1]));
    gtk_label_set_text(GTK_LABEL(app.lobby_settings), set->str);
    g_string_free(set, TRUE);
    gtk_widget_set_sensitive(app.lobby_button, host ? joined && app.remote_ready : joined);
    if (app.lobby_kick) /* only with someone to remove */
        gtk_widget_set_visible(app.lobby_kick, host && joined);
    if (app.lobby_ban)
        gtk_widget_set_visible(app.lobby_ban, host && joined);
}

/* Host: the room's settings changed from the lobby, without hosting it again; the joiner gets
 * them as on joining and presses Ready again. */
static void room_settings_cb(GtkButton *b, gpointer data)
{
    (void) b, (void) data;
    char *before = settings_key();
    int side = setting_int("Side", 0);
    room_settings_dialog();
    net_set_password(app.net, app.room_password);
    int now_side = CLAMP(setting_int("Side", 0), 0, 2);
    if (now_side != side)
        app.net_color = pick_color(now_side);
    char *now = settings_key();
    if (strcmp(before, now) && app.wait_dialog) {
        app.remote_ready = FALSE;
        if (app.remote_name)
            net_send_settings();
        lobby_update();
    }
    g_free(before);
    g_free(now);
}

/* The host's chips in the lobby: the setting counts at once, the joiner is told and presses Ready again. */
static const int LOBBY_CLOCKS[] = {0, 1, 2, 7, 8, 10}; /* the TIMES the chips offer */

static void lobby_changed(void)
{
    save_settings();
    app.remote_ready = FALSE;
    if (app.remote_name)
        net_send_settings();
    lobby_update();
}

static void lobby_variant_cb(int v, gpointer data)
{
    (void) data;
    if (v == app.variant)
        return;
    app.variant = v;
    if (v != V_STANDARD)
        app.handicap = HANDICAP_NONE;
    g_free(app.net_start); /* a set-up position is of its variant */
    app.net_start = NULL;
    g_key_file_set_integer(app.settings, "Settings", "Variant", v);
    g_key_file_set_integer(app.settings, "Settings", "Handicap", app.handicap);
    lobby_changed();
}

static void lobby_clock_cb(int i, gpointer data)
{
    (void) data;
    app.time_index = LOBBY_CLOCKS[i];
    g_key_file_set_integer(app.settings, "Settings", "Time", app.time_index);
    lobby_changed();
}

static void lobby_side_cb(int side, gpointer data)
{
    (void) data;
    app.net_color = pick_color(side);
    g_key_file_set_integer(app.settings, "Settings", "Side", side);
    lobby_changed();
}

static void ready_cb(GtkToggleButton *b, gpointer data)
{
    (void) data;
    net_send(app.net, gtk_toggle_button_get_active(b) ? "READY 1" : "READY 0");
    lobby_update();
}

static void copy_address_cb(GtkButton *b, gpointer address)
{
    (void) b;
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), address, -1);
    toast(tr("Address copied"));
}

/* The lobby, before a network game: both players' names and the host's settings, and the host's
 * `address` large, with Copy, for the other player to type. The joiner presses Ready, then the host
 * Start. True when the game starts. */
static void lobby_chat_cb(GtkWidget *w, gpointer data)
{
    (void) w, (void) data;
    chat_toggle();
}

static gboolean lobby_run(const char *text, const char *address, gboolean host)
{
    GtkWidget *d = app.wait_dialog = dialog_new(tr("Network game"));
    drawer_set_sticky(d);
    drawer_set_beside(d); /* the board shows the game to come */
    if (net_vpn_on()) {
        GtkWidget *vpn = row_text(tr("A VPN is on: the other device may not find this one. Turn it off to play on "
                                     "the same network."), FALSE, TRUE, FALSE);
        gtk_label_set_line_wrap(GTK_LABEL(vpn), TRUE);
        gtk_widget_set_size_request(vpn, rows_width() - 20, -1);
        gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), vpn, FALSE, FALSE, 8);
    }
    GtkWidget *label = row_para(text);
    app.lobby_players = gtk_vbox_new(FALSE, 0);
    app.lobby_settings = gtk_label_new("");
    gtk_misc_set_alignment(GTK_MISC(app.lobby_settings), 0, 0.5);
    gtk_misc_set_padding(GTK_MISC(app.lobby_settings), 10, 8);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), label, FALSE, FALSE, 8);
    if (address && *address) {
        GtkWidget *row = gtk_hbox_new(FALSE, 8), *a = gtk_label_new(NULL);
        char *m = g_markup_printf_escaped("<span size=\"x-large\" weight=\"bold\">%s</span>", address);
        gtk_label_set_markup(GTK_LABEL(a), m);
        g_free(m);
        gtk_label_set_selectable(GTK_LABEL(a), TRUE);
        gtk_label_set_line_wrap(GTK_LABEL(a), TRUE);
        gtk_misc_set_alignment(GTK_MISC(a), 0, 0.5);
        gtk_box_pack_start(GTK_BOX(row), a, TRUE, TRUE, 0);
        if (!app.kindle) { /* the Kindle has no clipboard to share */
            GtkWidget *copy = gtk_button_new_with_label(tr("Copy"));
            gtk_widget_set_name(copy, "denboku-chip");
            g_signal_connect(copy, "clicked", G_CALLBACK(copy_address_cb), (gpointer) address);
            gtk_box_pack_end(GTK_BOX(row), copy, FALSE, FALSE, 0);
        }
        gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), row, FALSE, FALSE, 4);
    }
    if (host) {
        app.lobby_internet = row_para(setting_int("UPnP", 1) ? tr("From the internet: asking the router…") : "");
        gtk_label_set_selectable(GTK_LABEL(app.lobby_internet), TRUE);
        gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), app.lobby_internet, FALSE, FALSE, 4);
        if (app.public_address)
            gtk_label_set_text(GTK_LABEL(app.lobby_internet), app.public_address);
    }
    rows_group(GTK_DIALOG(d)->vbox, tr("Players"));
    GtkWidget *players = rows_card(GTK_DIALOG(d)->vbox);
    row_add(players, app.lobby_players);
    /* The bar, and its chat button, is under the lobby in portrait (beside it, chat_event() takes the taps). */
    GtkWidget *chat = gtk_button_new();
    gtk_widget_set_name(chat, "denboku-flatrow");
    gtk_container_add(GTK_CONTAINER(chat), row_text(tr("Chat"), FALSE, FALSE, FALSE));
    g_signal_connect(chat, "clicked", G_CALLBACK(lobby_chat_cb), NULL);
    row_add(players, chat);
    gtk_widget_show_all(chat);
    chat_opener(chat);
    GtkWidget *settings = app.lobby_settings;
    rows_group(GTK_DIALOG(d)->vbox, tr("Game"));
    GtkWidget *game = rows_card(GTK_DIALOG(d)->vbox);
    if (host) { /* chips that count at once, and the rest a tap away */
        const char *variants[V_COUNT], *clocks[G_N_ELEMENTS(LOBBY_CLOCKS)], *sides[3];
        for (int v = 0; v < V_COUNT; v++)
            variants[v] = variant_label(v);
        int clock = -1;
        for (size_t k = 0; k < G_N_ELEMENTS(LOBBY_CLOCKS); k++) {
            clocks[k] = tr(TIMES[LOBBY_CLOCKS[k]].label);
            if (LOBBY_CLOCKS[k] == app.time_index)
                clock = (int) k;
        }
        for (int k = 0; k < 3; k++)
            sides[k] = tr(SIDE_LABELS[k]);
        int side = CLAMP(setting_int("Side", 0), 0, 2);
        row_add(game, row_chips(tr("Variant"), variants, V_COUNT, app.variant, rows_width(), lobby_variant_cb, NULL));
        row_add(game, row_chips(tr("Clock"), clocks, (int) G_N_ELEMENTS(LOBBY_CLOCKS), clock, rows_width(), lobby_clock_cb, NULL));
        row_add(game, row_chips(tr("You play"), sides, 3, side, rows_width(), lobby_side_cb, NULL));
    }
    if (host) { /* a tap changes them */
        settings = gtk_button_new();
        gtk_widget_set_name(settings, "denboku-flatrow");
        GtkWidget *lines = gtk_vbox_new(FALSE, 2);
        gtk_box_pack_start(GTK_BOX(lines), app.lobby_settings, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(lines), row_text(tr("More settings"), TRUE, FALSE, TRUE), FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(settings), lines);
        g_signal_connect(settings, "clicked", G_CALLBACK(room_settings_cb), NULL);
    }
    row_add(game, settings);
    gtk_dialog_add_button(GTK_DIALOG(d), tr(host ? "Cancel" : "Leave"), GTK_RESPONSE_CANCEL);
    if (host) {
        app.lobby_button = gtk_dialog_add_button(GTK_DIALOG(d), tr("Start"), GTK_RESPONSE_OK);
        app.lobby_kick = gtk_button_new_with_label(tr("Kick"));
        app.lobby_ban = gtk_button_new_with_label(tr("Ban"));
        g_signal_connect(app.lobby_kick, "clicked", G_CALLBACK(lobby_kick_cb), GINT_TO_POINTER(FALSE));
        g_signal_connect(app.lobby_ban, "clicked", G_CALLBACK(lobby_kick_cb), GINT_TO_POINTER(TRUE));
        gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->action_area), app.lobby_ban, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->action_area), app.lobby_kick, FALSE, FALSE, 0);
    } else {
        app.lobby_button = gtk_toggle_button_new_with_label(tr("Ready"));
        g_signal_connect(app.lobby_button, "toggled", G_CALLBACK(ready_cb), NULL);
        gtk_box_pack_end(GTK_BOX(GTK_DIALOG(d)->action_area), app.lobby_button, FALSE, FALSE, 0);
    }
    app.remote_ready = FALSE;
    int left_variant = rules_variant;
    lobby_update();
    gtk_widget_show_all(d);
    lobby_update(); /* Kick and Ban shown only with someone to remove */
    lobby_hidden = FALSE;
    int r;
    while ((r = gtk_dialog_run(GTK_DIALOG(d))) == GTK_RESPONSE_NONE && lobby_hidden)
        lobby_hidden = FALSE;
    app.lobby_players = app.lobby_settings = app.lobby_button = app.lobby_internet = NULL;
    app.lobby_kick = app.lobby_ban = NULL;
    gtk_widget_destroy(d);
    app.wait_dialog = NULL;
    lobby_board(FALSE);
    if (r != GTK_RESPONSE_OK)
        rules_set_variant(left_variant), apply_style(); /* the game left is on the board again */
    back_to_game();
    return r == GTK_RESPONSE_OK;
}

static gboolean side_chosen; /* the host picked a side in the room's settings: the rematch keeps it */

/* Host, after a game: the room's settings changed without a new room; the joiner is told, and the rematch
 * (both asking, as always) starts with them. */
void room_settings_after_game(void)
{
    if (!app.net || app.mode != MODE_HOST)
        return;
    char *before = settings_key();
    int side = setting_int("Side", 0);
    room_settings_dialog();
    net_set_password(app.net, app.room_password);
    int now_side = CLAMP(setting_int("Side", 0), 0, 2);
    if (now_side != side) {
        app.net_color = pick_color(now_side);
        side_chosen = TRUE;
    }
    char *now = settings_key();
    if (strcmp(before, now)) {
        if (app.remote_name)
            net_send_settings();
        toast(tr("Room settings changed. The rematch starts with them."));
    }
    g_free(before);
    g_free(now);
}

/* Both asked for a rematch: the host starts it, with the colors swapped. */
void rematch_check(void)
{
    if (app.over_label && app.rematch_theirs && !app.rematch_mine)
        gtk_label_set_text(GTK_LABEL(app.over_label), tr("The other player wants a rematch."));
    if (!app.rematch_mine || !app.rematch_theirs || app.mode != MODE_HOST)
        return;
    app.rematch_mine = app.rematch_theirs = FALSE;
    if (!side_chosen)
        app.net_color = !app.net_color;
    side_chosen = FALSE;
    net_send_settings();
    net_send_game(TRUE);
    if (app.over_dialog)
        gtk_dialog_response(GTK_DIALOG(app.over_dialog), GTK_RESPONSE_CLOSE);
    start_game(app.handicap, MODE_HOST);
}

void rematch(void)
{
    app.rematch_mine = TRUE;
    net_send(app.net, "REMATCH");
    if (app.over_label)
        gtk_label_set_text(GTK_LABEL(app.over_label), tr("Waiting for the other player…"));
    rematch_check();
}

/* Joiner: the host's address, typed or picked from the games announced on the network. */
static char *ask_host_address(void)
{
    kindle_firewall(TRUE);
    GtkWidget *d = dialog_new(tr("Network game"));
    GtkWidget *entry = text_entry();
    gtk_entry_set_width_chars(GTK_ENTRY(entry), 28);
    char *last = setting_string("LastHost", "192.168.");
    gtk_entry_set_text(GTK_ENTRY(entry), last);
    g_free(last);
    /* Games announced on the local network; tapping one fills in its address. */
    GtkListStore *found = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget *list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(found));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(list), FALSE);
    GtkCellRenderer *text = gtk_cell_renderer_text_new(); /* a room's settings wrap within the page */
    g_object_set(text, "wrap-width", rows_width() - 20, "wrap-mode", PANGO_WRAP_WORD_CHAR, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(list), gtk_tree_view_column_new_with_attributes("", text, "text", 1, NULL));
    gtk_widget_set_size_request(list, -1, MIN(160, gdk_screen_height() / 3));
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(list)), "changed", G_CALLBACK(host_chosen_cb), entry);
    NetBrowser *browser = net_browse(host_found_cb, found);
    GtkWidget *where = row_para(tr("Address of the other device (add :port if the game is not on port 4081):"));
    gtk_label_set_line_wrap_mode(GTK_LABEL(where), PANGO_WRAP_WORD_CHAR);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), row_para(tr("Games on this network:")), FALSE, FALSE, 6);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), list, TRUE, TRUE, 6);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), where, FALSE, FALSE, 6);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), entry, FALSE, FALSE, 6);
    gtk_dialog_add_button(GTK_DIALOG(d), tr("Cancel"), GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(d), tr("Join"), GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
    gtk_window_set_resizable(GTK_WINDOW(d), FALSE);
    gtk_widget_show_all(d);
    /* No automatic keyboard here: the address field opens it when tapped. */
    gboolean go = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK;
    char *host = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
    net_browser_free(browser);
    gtk_widget_destroy(d);
    g_object_unref(found);
    if (!go) {
        g_free(host);
        kindle_firewall(FALSE);
        return NULL;
    }
    g_key_file_set_string(app.settings, "Settings", "LastHost", host);
    return host;
}

/* `address` skips the address dialog when joining (debugging: DENBOKU_NET=join:IP). */

/* The router answered: show the address to join from the internet. */
static void upnp_done_cb(const char *ip, void *data)
{
    (void) data;
    g_free(app.public_address);
    app.public_address = ip && *ip ? g_strdup_printf("%s %s:%d", tr("From the internet:"), ip, net_port())
                                   : g_strdup(tr("From the internet: the router does not open ports by itself "
                                                 "(UPnP); forward the port on it to play over the internet."));
    if (app.lobby_internet)
        gtk_label_set_text(GTK_LABEL(app.lobby_internet), app.public_address);
}

void network_game(Handicap h, Mode mode, const char *address)
{
    if (!net_available()) {
        message(tr("No network connection. Turn off airplane mode or connect to Wi-Fi."));
        return;
    }
    Mode was_mode = app.mode;
    int was_variant = app.variant;
    Handicap was_handicap = app.handicap;
    net_stop();
    app.net_reject = 0;
    online_stop();
    server_stop();
    g_free(app.net_start);
    app.net_start = NULL;
    if (mode == MODE_HOST) {
        app.net_start = app.start_sfen; /* a position from the editor, for the whole room */
        app.start_sfen = NULL;
    }
    app.mode = mode;
    app.handicap = h;
    if (mode == MODE_HOST) {
        GError *err = NULL;
        int side = CLAMP(setting_int("Side", 0), 0, 2);
        app.net_color = pick_color(side);
        kindle_firewall(TRUE);
        app.net = net_host(net_port(), app.room_password, &NET_CB, NULL, &err);
        if (!app.net) {
            message(err ? err->message : tr("Could not open the network port."));
            if (err)
                g_error_free(err);
            goto give_back;
        }
        char *ips = net_local_addresses();
        if (net_port() != NET_PORT) {
            char **list = g_strsplit(ips, " ", -1);
            GString *with = g_string_new(NULL);
            for (int i = 0; list[i]; i++)
                g_string_append_printf(with, "%s%s:%d", i ? " " : "", list[i], net_port());
            g_strfreev(list);
            g_free(ips);
            ips = g_string_free(with, FALSE);
        }
        const char *text = tr("On the other device, join this address:");
        if (setting_int("UPnP", 1))
            upnp_open(net_port(), "Denboku Shōgi", upnp_done_cb, NULL);
        NetAnnounce *announce = announce_room();
        gboolean ok = lobby_run(text, *ips ? ips : "?", TRUE);
        net_announce_free(announce);
        g_free(ips);
        if (!ok) {
            net_stop();
            goto give_back;
        }
        net_send_settings();
        net_send_game(TRUE);
    } else {
        char *host = address ? g_strdup(address) : ask_host_address();
        if (!address) { /* a room picked now: its password is asked; a rejoin's was kept (rejoin()) */
            g_free(app.net_password);
            app.net_password = NULL;
        }
        if (!host)
            goto give_back;
        /* The host's VARIANT line overrides it. Only now: the game shown behind the address page is
         * still in its own variant (a Tori game read as 9×9 was a jumble of pieces). */
        app.variant = V_STANDARD;
        rules_set_variant(V_STANDARD);
        g_free(app.net_address);
        app.net_address = host;
        g_free(app.net_moves);
        app.net_moves = NULL;
        app.net = net_join(host, NET_PORT, &NET_CB, NULL);
        if (!lobby_run(tr("Press Ready when you are ready; the host starts the game."), NULL, FALSE)) {
            net_stop();
            if (app.net_reject) {
                message(app.net_reject == 2 ? tr("You are banned from this room.")
                                            : tr("You were removed from the room."));
                app.net_reject = 0;
            }
            goto give_back;
        }
    }
    apply_settings();
    if (mode == MODE_JOIN)
        net_load_game();
    else
        start_game(app.handicap, mode);
    return;
give_back: /* no room after all: the game left goes on as it was (a lesson, a game against the engine) */
    app.mode = was_mode, app.variant = was_variant, app.handicap = was_handicap;
    if (mode == MODE_HOST && app.net_start) { /* the editor's position goes back to the game it came from */
        g_free(app.start_sfen);
        app.start_sfen = app.net_start;
        app.net_start = NULL;
    }
    rules_set_variant(app.variant);
    view_refresh(app.view);
}
