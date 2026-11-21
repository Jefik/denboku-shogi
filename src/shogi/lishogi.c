/* Lishogi through its Board API. */

#include "app.h"

static void lishogi_post_done(int status, void *data);
static void lishogi_set_moves(const char *moves);
static void lishogi_open_game(const char *id);
static void take_back_here(void);

/* The event stream, the game stream, a pending seek or challenge, the game id, our name (to tell
 * our own chat lines), an invitation the API cannot accept (opened in the browser instead), and
 * the moves the server last sent. */
static Http *li_events, *li_game, *li_request;
static char li_id[16], li_challenge[16], li_error[160], li_name[48];
static char li_shown[16]; /* the game the board was set up for */
static gboolean li_vs_ai; /* the game is against Lishogi's AI: no greetings */
/* Both streams send an empty line every 6 s: one quiet for 20 s is a connection that died without
 * closing (a network that came back), and is opened again (lishogi_watch_cb). */
static guint li_watch;
/* Lishogi keeps one game stream and one event stream per account, and a new one ends the one
 * before: a stream it ended (not one cut off) while the game goes on was opened by another device
 * with the account. Opening it again took it back from that device, which did the same, and the
 * moves came late on both; the game stays there until a move or line here takes it back. */
static gboolean li_elsewhere;
/* Our lines sent and not back yet: shown at once, and the copy the game stream brings back is
 * left out (waiting for it hid them while the stream was slow). */
static GQueue said = G_QUEUE_INIT;
static char *li_accept_link, *li_moves;

/* ---- Lishogi ---- */

/* Play on lishogi.org through its Board API with a personal token: an event stream reports games
 * that start (from a seek, a challenge or the AI), and a stream per game sends every move. */

char *online_token(void) { return g_strstrip(setting_string("LishogiToken", "")); }

void online_stop(void)
{
    http_cancel(li_events);
    http_cancel(li_game);
    http_cancel(li_request);
    li_events = li_game = li_request = NULL;
    if (li_id[0])
        chat_clear();
    li_id[0] = li_challenge[0] = li_shown[0] = 0;
    li_vs_ai = li_elsewhere = FALSE;
    if (li_watch)
        g_source_remove(li_watch);
    li_watch = 0;
    g_free(li_moves);
    li_moves = NULL;
    while (!g_queue_is_empty(&said))
        g_free(g_queue_pop_head(&said));
}

/* Fire and forget: a move, a resignation, a cancelled challenge; counted, so that quitting
 * waits for them (a resignation cut short left the game going on lishogi). */
static int posts_pending;

void online_post(const char *path, gboolean resync)
{
    char *token = online_token(), *url = g_strconcat(ONLINE_URL, path, NULL);
    if (http_start("POST", url, token, NULL, NULL, lishogi_post_done, GINT_TO_POINTER(resync)))
        posts_pending++;
    g_free(url);
    g_free(token);
}

/* A move lishogi refused: go back to the moves it knows. */
static void lishogi_post_done(int status, void *data)
{
    posts_pending--;
    if (GPOINTER_TO_INT(data) && (status < 200 || status > 299) && app.mode == MODE_ONLINE && li_moves)
        lishogi_set_moves(li_moves);
}

void online_flush(void)
{
    gint64 until = g_get_monotonic_time() + 5 * G_USEC_PER_SEC;
    while (posts_pending > 0 && g_get_monotonic_time() < until)
        if (!g_main_context_iteration(NULL, FALSE))
            g_usleep(10000);
}

bool online_vs_ai(void) { return app.mode == MODE_ONLINE && li_vs_ai; }

bool online_is_me(const char *who) { return app.mode == MODE_ONLINE && *li_name && !strcmp(who, li_name); }

/* Lishogi starts a game with both first moves; before them it can only be aborted. */
void online_resign(void)
{
    if (app.mode != MODE_ONLINE || !li_id[0])
        return;
    char path[64];
    snprintf(path, sizeof path, "/api/board/game/%s/%s", li_id, app.game.count < 2 ? "abort" : "resign");
    online_post(path, FALSE);
}

static void chat_sent(int status, void *data)
{
    char *text = data;
    if (status < 200 || status > 299) {
        GList *l = g_queue_find_custom(&said, text, (GCompareFunc) strcmp);
        if (l) {
            g_free(l->data);
            g_queue_delete_link(&said, l);
        }
        chat_add("", tr("Lishogi did not take that line."));
    }
    g_free(text);
}

void online_chat(const char *text)
{
    char *token = online_token(), *url = g_strdup_printf(ONLINE_URL "/api/board/game/%s/chat", li_id);
    char *t = http_escape(text), *form = g_strconcat("room=player&text=", t, NULL);
    take_back_here();
    chat_add(*li_name ? li_name : local_name(), text);
    g_queue_push_tail(&said, g_strdup(text));
    if (!http_start("POST", url, token, form, NULL, chat_sent, g_strdup(text)))
        chat_sent(0, g_strdup(text));
    g_free(form);
    g_free(t);
    g_free(url);
    g_free(token);
}

/* A move or chat line here after another device took the game: it is played here again. */
static void take_back_here(void)
{
    if (!li_elsewhere)
        return;
    li_elsewhere = FALSE;
    lishogi_open_game(li_id);
    online_events_cb(NULL);
}

void online_send_move(const Move *m)
{
    take_back_here();
    char usi[8], path[64];
    move_to_usi(m, usi);
    snprintf(path, sizeof path, "/api/board/game/%s/move/%s", li_id, usi);
    online_post(path, TRUE);
}

/* Brings the board to lishogi's move list: plays the new moves, or replays from the start when
 * ours differ (a refused move, a takeback). A single new move (the opponent's) is heard. */
static void lishogi_set_moves(const char *moves)
{
    int before = app.game.count;
    char **usi = g_strsplit(moves, " ", -1);
    int n = (int) g_strv_length(usi);
    gboolean same = n >= app.game.count;
    for (int i = 0; same && i < app.game.count; i++) {
        char mine[8];
        move_to_usi(&app.game.moves[i], mine);
        same = !strcmp(mine, usi[i]);
    }
    if (!same) {
        Position start = app.game.start;
        char names[2][64];
        memcpy(names, app.game.names, sizeof names);
        game_new(&app.game, HANDICAP_NONE);
        app.game.start = app.game.pos = start;
        app.game.hashes[0] = start.hash;
        memcpy(app.game.names, names, sizeof names);
    }
    for (int i = app.game.count; i < n && app.game.result == RESULT_NONE; i++) {
        Move m;
        if (!move_from_usi(&app.game.pos, usi[i], &m)) /* legal, as it checks */
            break;
        game_play(&app.game, &m, 0);
    }
    g_strfreev(usi);
    if (same && app.game.count == before + 1)
        move_sound(FALSE);
}

static void lishogi_state(const char *state)
{
    char *moves = g_malloc(strlen(state) + 1), status[32] = "", winner[8] = "";
    if (json_get(state, "moves", moves, strlen(state) + 1)) {
        g_free(li_moves);
        li_moves = g_strdup(moves);
        lishogi_set_moves(moves);
    }
    g_free(moves);
    app.remaining[FIRST] = json_long(state, "btime", app.remaining[FIRST]);
    app.remaining[SECOND] = json_long(state, "wtime", app.remaining[SECOND]);
    json_get(state, "status", status, sizeof status);
    json_get(state, "winner", winner, sizeof winner);
    if (*status && strcmp(status, "started") && strcmp(status, "created")) {
        static const struct { const char *status; Reason reason; } REASONS[] = {
            {"mate", REASON_CHECKMATE}, {"resign", REASON_RESIGN}, {"outoftime", REASON_TIME},
            {"timeout", REASON_TIME}, {"repetition", REASON_REPETITION}, {"perpetualCheck", REASON_PERPETUAL_CHECK},
            {"impasse27", REASON_IMPASSE}, {"illegalMove", REASON_ILLEGAL}, {"aborted", REASON_ABORT},
        };
        Reason why = REASON_ABORT;
        for (size_t i = 0; i < G_N_ELEMENTS(REASONS); i++)
            if (!strcmp(status, REASONS[i].status))
                why = REASONS[i].reason;
        Result r = !strcmp(winner, "sente") ? RESULT_FIRST_WIN : !strcmp(winner, "gote") ? RESULT_SECOND_WIN
                   : why == REASON_ABORT ? RESULT_ABORTED : RESULT_DRAW;
        app.game.result = RESULT_NONE;
        game_end(&app.game, r, why);
    }
    next_turn();
}

/* A note of the site's, in the chat: a few are sent as keys of its translations ("key:gameAborted:"). */
static const char *lishogi_note(const char *text)
{
    static const char *const KEYS[][2] = {
        {"gameAborted", "Game aborted"}, {"whiteResigned", "Gote resigned"}, {"blackResigned", "Sente resigned"},
        {"whiteLeftTheGame", "Gote left the game"}, {"blackLeftTheGame", "Sente left the game"},
        {"draw", "Draw"}, {"timeOut", "Time out"}, {"checkmate", "Checkmate"},
        {"takebackPropositionSent", "Takeback sent"}, {"takebackPropositionDeclined", "Takeback declined"},
        {"takebackPropositionAccepted", "Takeback accepted"}, {"takebackPropositionCanceled", "Takeback cancelled"},
        {"drawOfferSent", "Draw offer sent"}, {"drawOfferDeclined", "Draw offer declined"},
        {"drawOfferAccepted", "Draw offer accepted"}, {"drawOfferCanceled", "Draw offer cancelled"},
    };
    if (strncmp(text, "key:", 4))
        return text;
    for (size_t i = 0; i < G_N_ELEMENTS(KEYS); i++) {
        size_t n = strlen(KEYS[i][0]);
        if (!strncmp(text + 4, KEYS[i][0], n) && (text[4 + n] == ':' || !text[4 + n]))
            return tr(KEYS[i][1]);
    }
    return text + 4;
}

/* "Name (1500)", the AI's level, or "Anonymous" for a player not logged in. */
static void lishogi_player(const char *json, const char *side, char *out, size_t size)
{
    char key[32], name[48] = "";
    snprintf(key, sizeof key, "%s.name", side);
    json_get(json, key, name, sizeof name);
    snprintf(key, sizeof key, "%s.aiLevel", side);
    long ai = json_long(json, key, 0);
    snprintf(key, sizeof key, "%s.rating", side);
    long rating = json_long(json, key, 0);
    if (ai)
        snprintf(out, size, "%s %ld", tr("Lishogi AI, level"), ai);
    else if (rating)
        snprintf(out, size, "%s (%ld)", *name ? name : "?", rating);
    else
        snprintf(out, size, "%s", *name ? name : tr("Anonymous"));
}

/* The game's chat so far, as the board is set up (the game started again, the app opened again):
 * the lines live only on Lishogi. */
typedef struct {
    GString *text;
    char id[16];
    gboolean anonymous;
} History;

static void history_line(const char *line, void *data) { g_string_append(((History *) data)->text, line); }

static void history_done(int status, void *data)
{
    History *h = data;
    if (app.mode == MODE_ONLINE && !strcmp(h->id, li_id)) {
        char key[16], who[48], text[200];
        for (int i = 0; status == 200; i++) {
            snprintf(key, sizeof key, "%d.text", i);
            if (!json_get(h->text->str, key, text, sizeof text))
                break;
            snprintf(key, sizeof key, "%d.user", i);
            if (!json_get(h->text->str, key, who, sizeof who))
                who[0] = 0;
            if (!strcmp(who, "lishogi")) /* the site's own notes */
                chat_add("", lishogi_note(text));
            else
                chat_add(who, text);
        }
        if (h->anonymous)
            chat_add("", tr("Lishogi does not pass on to apps what an anonymous player writes: this player's lines will not show here."));
    }
    g_string_free(h->text, TRUE);
    g_free(h);
}

static void chat_history(gboolean anonymous)
{
    History *h = g_new0(History, 1);
    h->text = g_string_new(NULL);
    g_strlcpy(h->id, li_id, sizeof h->id);
    h->anonymous = anonymous;
    char *token = online_token(), *url = g_strdup_printf(ONLINE_URL "/api/board/game/%s/chat", li_id);
    if (!http_start("GET", url, token, NULL, history_line, history_done, h))
        history_done(0, h);
    g_free(url);
    g_free(token);
}

static void lishogi_game_line(const char *line, void *data)
{
    (void) data;
    char type[16] = "";
    json_get(line, "type", type, sizeof type);
    if (!strcmp(type, "gameFull") && app.mode == MODE_ONLINE && !strcmp(li_shown, li_id)) {
        /* a reconnection: the game on the board brought up to date, not started again (with its sound) */
        char *state = g_malloc(strlen(line) + 1);
        if (json_get(line, "state", state, strlen(line) + 1))
            lishogi_state(state);
        g_free(state);
    } else if (!strcmp(type, "gameFull")) {
        char sfen[128] = "";
        json_get(line, "initialSfen", sfen, sizeof sfen);
        app.online_tc = (TimeControl) {"Lishogi", (int) (json_long(line, "clock.initial", 0) / 1000),
                                   (int) (json_long(line, "clock.byoyomi", 0) / 1000),
                                   (int) (json_long(line, "clock.increment", 0) / 1000)};
        char vkey[24] = "";
        json_get(line, "variant.key", vkey, sizeof vkey);
        int variant = online_variant_from_key(vkey);
        if (variant < 0) { /* Chu or Annan shogi */
            message(tr("This variant cannot be played here."));
            variant = V_STANDARD;
        }
        app.variant = variant;
        rules_set_variant(variant);
        start_game(HANDICAP_NONE, MODE_ONLINE);
        g_strlcpy(li_shown, li_id, sizeof li_shown);
        Position p;
        if (*sfen && strcmp(sfen, "startpos") && pos_from_text(&p, sfen)) {
            app.game.start = app.game.pos = p;
            app.game.hashes[0] = p.hash;
        }
        lishogi_player(line, "sente", app.game.names[FIRST], sizeof app.game.names[FIRST]);
        lishogi_player(line, "gote", app.game.names[SECOND], sizeof app.game.names[SECOND]);
        g_free(app.remote_name);
        app.remote_name = g_strdup(app.game.names[!app.net_color]);
        char key[16];
        snprintf(key, sizeof key, "%s.aiLevel", app.net_color == FIRST ? "gote" : "sente");
        li_vs_ai = json_long(line, key, 0) > 0;
        /* Lishogi's game stream passes on only the lines of players with an account (UserLine in its
         * GameStateStream.scala): an anonymous player's never reach an app. */
        char other[48] = "";
        snprintf(key, sizeof key, "%s.name", app.net_color == FIRST ? "gote" : "sente");
        gboolean anonymous = !li_vs_ai && !json_get(line, key, other, sizeof other);
        snprintf(key, sizeof key, "%s.name", app.net_color == FIRST ? "sente" : "gote");
        if (!json_get(line, key, li_name, sizeof li_name))
            li_name[0] = 0;
        chat_history(anonymous);
        char *state = g_malloc(strlen(line) + 1);
        if (json_get(line, "state", state, strlen(line) + 1))
            lishogi_state(state);
        g_free(state);
    } else if (!strcmp(type, "gameState")) {
        lishogi_state(line);
    } else if (!strcmp(type, "chatLine")) {
        char who[48] = "", text[200] = "";
        json_get(line, "username", who, sizeof who);
        json_get(line, "text", text, sizeof text);
        if (!strcmp(who, li_name)) { /* ours, back: shown when sent */
            GList *l = g_queue_find_custom(&said, text, (GCompareFunc) strcmp);
            if (l) {
                g_free(l->data);
                g_queue_delete_link(&said, l);
                return;
            }
            chat_add(who, text);
            return;
        }
        if (!strcmp(who, "lishogi")) { /* the site's own notes: "key:gameAborted:" stands for its text */
            const char *shown = lishogi_note(text);
            chat_add("", shown);
            return;
        }
        chat_add(who, text);
        play_sound("notify");
    }
}

static gboolean lishogi_reopen_cb(gpointer data)
{
    (void) data;
    if (app.mode == MODE_ONLINE && li_id[0] && !li_game && !li_elsewhere && app.game.result == RESULT_NONE)
        lishogi_open_game(li_id);
    return FALSE;
}

/* The game stream ended while the game goes on: cut off, it is opened again; ended by Lishogi,
 * another device has the game. */
static void lishogi_game_done(int status, void *data)
{
    (void) data;
    li_game = NULL;
    if (app.mode != MODE_ONLINE || app.game.result != RESULT_NONE || li_elsewhere)
        return;
    if (status != 200) {
        g_timeout_add_seconds(3, lishogi_reopen_cb, NULL);
        return;
    }
    li_elsewhere = TRUE;
    http_cancel(li_events);
    li_events = NULL;
    message(tr("This game was opened on another device with your Lishogi account and goes on there. "
               "A move or a line here brings it back."));
}

static gboolean lishogi_watch_cb(gpointer data)
{
    (void) data;
    if (app.mode != MODE_ONLINE) {
        li_watch = 0;
        return FALSE;
    }
    const gint64 quiet = 20 * G_USEC_PER_SEC;
    if (li_game && http_quiet_us(li_game) > quiet && app.game.result == RESULT_NONE) {
        if (g_getenv("DENBOKU_DEBUG"))
            g_printerr("lishogi: the game stream went quiet, opening it again\n");
        http_cancel(li_game);
        li_game = NULL;
        lishogi_open_game(li_id);
    }
    if (li_events && http_quiet_us(li_events) > quiet) {
        http_cancel(li_events);
        li_events = NULL;
        online_events_cb(NULL);
    }
    return TRUE;
}

static void lishogi_open_game(const char *id)
{
    if (!li_watch)
        li_watch = g_timeout_add_seconds(10, lishogi_watch_cb, NULL);
    char *token = online_token(), *url = g_strdup_printf(ONLINE_URL "/api/board/game/stream/%s", id);
    if (id != li_id) /* most callers pass li_id itself: a copy onto itself left it empty */
        g_strlcpy(li_id, id, sizeof li_id);
    http_cancel(li_game);
    li_game = http_start("GET", url, token, NULL, lishogi_game_line, lishogi_game_done, NULL);
    g_free(url);
    g_free(token);
}

/* Someone challenges us while we wait: ask, then accept or decline on lishogi. */
static void lishogi_offer(const char *line, const char *id)
{
    play_sound("notify");
    char who[48] = "?", tc[48] = "", rated[8] = "";
    json_get(line, "challenge.challenger.name", who, sizeof who);
    json_get(line, "challenge.timeControl.show", tc, sizeof tc);
    json_get(line, "challenge.rated", rated, sizeof rated);
    char *text = g_strdup_printf("%s %s\n%s%s", who, tr("challenges you."), tc,
                                 !strcmp(rated, "true") ? tr(", rated") : "");
    GtkWidget *d = dialog_new(tr("Lishogi"));
    drawer_set_sticky(d);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), gtk_label_new(text), TRUE, TRUE, 12);
    g_free(text);
    gtk_dialog_add_button(GTK_DIALOG(d), tr("✗ Decline"), GTK_RESPONSE_REJECT);
    gtk_dialog_add_button(GTK_DIALOG(d), tr("✓ Accept"), GTK_RESPONSE_ACCEPT);
    gtk_widget_show_all(d);
    gboolean yes = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT;
    gtk_widget_destroy(d);
    char path[64];
    snprintf(path, sizeof path, "/api/challenge/%s/%s", id, yes ? "accept" : "decline");
    online_post(path, FALSE); /* a game that starts comes as gameStart */
}

/* Continuing a game in progress: lishogi's event stream starts with one gameStart for each. */
static gboolean resuming;
static char resume_id[16]; /* empty: any game */
static guint resume_timer;

static gboolean resume_timeout_cb(gpointer data)
{
    (void) data;
    resume_timer = 0;
    if (resuming && app.wait_dialog) {
        snprintf(li_error, sizeof li_error, "%s",
                 tr(*resume_id ? "That game is not one of yours in progress on Lishogi."
                               : "You have no game in progress on Lishogi."));
        gtk_dialog_response(GTK_DIALOG(app.wait_dialog), GTK_RESPONSE_REJECT);
    }
    return FALSE;
}

static void lishogi_event_line(const char *line, void *data)
{
    (void) data;
    char type[32] = "", id[16] = "", color[8] = "";
    json_get(line, "type", type, sizeof type);
    if (!strcmp(type, "gameStart")) {
        json_get(line, "game.id", id, sizeof id);
        json_get(line, "game.color", color, sizeof color);
        /* One game at a time: a game already on the board keeps it (its stream opened again if it
         * dropped: the event stream says so again when it comes back). */
        if (*id && !strcmp(id, li_id)) {
            if (!li_game && app.mode == MODE_ONLINE && app.game.result == RESULT_NONE)
                lishogi_open_game(id);
            return;
        }
        if (!*id || (li_id[0] && app.game.result == RESULT_NONE && !app.wait_dialog))
            return;
        if (resuming && *resume_id && strcmp(id, resume_id))
            return; /* another of our games in progress */
        char board[8] = "";
        if (resuming && json_get(line, "game.compat.board", board, sizeof board) && !strcmp(board, "false")) {
            snprintf(li_error, sizeof li_error, "%s",
                     tr("Lishogi lets an app play only games of at least 5 minutes (15 when rated) by its estimate: "
                        "this one goes on on its site."));
            gtk_dialog_response(GTK_DIALOG(app.wait_dialog), GTK_RESPONSE_REJECT);
            return;
        }
        app.net_color = strcmp(color, "gote") ? FIRST : SECOND;
        snprintf(li_id, sizeof li_id, "%s", id);
        if (app.wait_dialog)
            gtk_dialog_response(GTK_DIALOG(app.wait_dialog), GTK_RESPONSE_OK);
        else
            lishogi_open_game(id);
    } else if (!strcmp(type, "challenge") && app.wait_dialog) {
        json_get(line, "challenge.id", id, sizeof id);
        if (*id && strcmp(id, li_challenge))
            lishogi_offer(line, id);
    } else if (!strcmp(type, "challengeDeclined") || !strcmp(type, "challengeCanceled")) {
        json_get(line, "challenge.id", id, sizeof id);
        if (app.wait_dialog && *id && !strcmp(id, li_challenge)) {
            snprintf(li_error, sizeof li_error, "%s", tr("The challenge was declined."));
            gtk_dialog_response(GTK_DIALOG(app.wait_dialog), GTK_RESPONSE_REJECT);
        }
    }
}

static void lishogi_events_done(int status, void *data)
{
    (void) data;
    li_events = NULL;
    if (status == 401)
        snprintf(li_error, sizeof li_error, "%s", tr("Lishogi did not accept the token."));
    else if (!status)
        snprintf(li_error, sizeof li_error, "%s", tr("Could not reach lishogi.org."));
    if (app.wait_dialog)
        gtk_dialog_response(GTK_DIALOG(app.wait_dialog), GTK_RESPONSE_REJECT);
    else if (app.mode == MODE_ONLINE && status != 401 && status != 200 && !li_elsewhere)
        g_timeout_add_seconds(5, online_events_cb, NULL); /* ended by Lishogi: another device has it */
}

/* `data` non-NULL: listen even outside a lishogi game, to resume one that is going on. */
gboolean online_events_cb(gpointer data)
{
    if (li_events || (app.mode != MODE_ONLINE && !app.wait_dialog && !data))
        return FALSE;
    char *token = online_token();
    li_events = http_start("GET", ONLINE_URL "/api/stream/event", token, NULL, lishogi_event_line,
                               lishogi_events_done, NULL);
    g_free(token);
    return FALSE;
}

/* The answer to a seek, challenge or AI request: errors come as {"error": ...}. */
static void lishogi_request_line(const char *line, void *data)
{
    (void) data;
    char v[160];
    if (json_get(line, "error", v, sizeof v) || json_get(line, "global", v, sizeof v))
        snprintf(li_error, sizeof li_error, "%s", v);
    if (json_get(line, "challenge.id", v, sizeof v) || json_get(line, "id", v, sizeof v))
        snprintf(li_challenge, sizeof li_challenge, "%.15s", v);
    if (json_get(line, "challenge.url", v, sizeof v) || json_get(line, "url", v, sizeof v)) {
        wait_set_link(v);
    }
}

static void lishogi_request_done(int status, void *data)
{
    (void) data;
    li_request = NULL;
    if ((status >= 200 && status <= 299) || !app.wait_dialog)
        return;
    if (li_accept_link && status == 404) {
        /* Lishogi accepts by API only challenges addressed to us; an open invitation is
         * accepted on its page, and the game then comes here through the event stream. */
        open_url(li_accept_link);
        wait_set_text(
                           tr(app.kindle ? "Lishogi accepts this invitation only on its page: open the link on another "
                                           "device logged in to your account, and the game comes here."
                                         : "Lishogi accepts this invitation only on its page, which opened in your "
                                           "browser: accept it there, and the game comes here."));
        return;
    }
    if (status == 401)
        snprintf(li_error, sizeof li_error, "%s", tr("Lishogi did not accept the token."));
    if (!li_error[0])
        snprintf(li_error, sizeof li_error, "HTTP %d", status);
    if (!status)
        snprintf(li_error, sizeof li_error, "%s", tr("Could not reach lishogi.org."));
    gtk_dialog_response(GTK_DIALOG(app.wait_dialog), GTK_RESPONSE_REJECT);
}

/* The challenge id in an invitation link ("https://lishogi.org/9EPMMIId", maybe with "?color=…"),
 * in the bare code "9EPMMIId", or in a 12-letter game id; NULL if there is none. */
static char *lishogi_link_id(const char *link)
{
    const char *s = strstr(link, "lishogi.org/");
    s = s ? s + strlen("lishogi.org/") : link;
    while (*s == ' ' || *s == '/')
        s++;
    int n = 0;
    while (g_ascii_isalnum(s[n]))
        n++;
    return n >= 8 ? g_strndup(s, 8) : NULL;
}

/* The token shows while it is typed or pasted, and hides when the field is left. */
static gboolean token_focus_cb(GtkWidget *w, GdkEventFocus *e, gpointer data)
{
    (void) data;
    gtk_entry_set_visibility(GTK_ENTRY(w), e->in);
    return FALSE;
}

static void token_test_line(const char *line, void *data)
{
    char name[64];
    if (json_get(line, "username", name, sizeof name))
        snprintf(data, 96, "%s %s", tr("Connected to Lishogi as"), name);
}

static void token_test_done(int status, void *data)
{
    char *result = data;
    if (!*result)
        snprintf(result, 96, "%s", status == 401 ? tr("Lishogi did not accept the token.")
                                   : status ? tr("Lishogi answered with an error.")
                                            : tr("Could not reach lishogi.org."));
    message(result);
    g_free(result);
}

static void token_test_cb(GtkButton *b, gpointer entry)
{
    (void) b;
    char *token = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(entry))));
    if (!http_start("GET", ONLINE_URL "/api/account", token, NULL, token_test_line, token_test_done, g_malloc0(96)))
        message(tr("Could not reach lishogi.org."));
    g_free(token);
}

/* A field for the token with a Test button beside it; `entry` is the field. */
GtkWidget *token_field(GtkWidget **entry)
{
    GtkWidget *box = gtk_hbox_new(FALSE, 6);
    *entry = text_entry();
    gtk_entry_set_visibility(GTK_ENTRY(*entry), FALSE);
    g_signal_connect(*entry, "focus-in-event", G_CALLBACK(token_focus_cb), NULL);
    g_signal_connect(*entry, "focus-out-event", G_CALLBACK(token_focus_cb), NULL);
    GtkWidget *test = gtk_button_new_with_label(tr("Test"));
    g_signal_connect(test, "clicked", G_CALLBACK(token_test_cb), *entry);
    gtk_box_pack_start(GTK_BOX(box), with_paste(*entry), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), test, FALSE, FALSE, 0);
    return box;
}

/* No token yet: explain where to get one and take it here. True when one was saved. */
static gboolean lishogi_token_dialog(void)
{
    GtkWidget *d = dialog_new(tr("Lishogi token"));
    GtkWidget *label = gtk_label_new(tr("To play on Lishogi, create a personal token with \"Play games with "
                                        "the board API\" and paste it here:"));
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    GtkWidget *link = link_button(ONLINE_URL "/account/oauth/token", ONLINE_TOKEN_URL);
    GtkWidget *entry, *field = token_field(&entry);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), label, FALSE, FALSE, 8);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), link, FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), field, FALSE, FALSE, 8);
    gtk_dialog_add_button(GTK_DIALOG(d), tr("Cancel"), GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(d), tr("OK"), GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
    gtk_widget_show_all(d);
    gtk_window_set_focus(GTK_WINDOW(d), entry);
    gtk_widget_grab_focus(entry);
    gboolean ok = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK;
    char *token = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(entry))));
    ok = ok && *token;
    if (ok) {
        g_key_file_set_string(app.settings, "Settings", "LishogiToken", token);
        save_settings();
    }
    g_free(token);
    gtk_widget_destroy(d);
    return ok;
}

/* A seek's rating range is around our rating in its perf, asked first. */
static OnlineSetup seek_setup;
static int seek_rating;

static void seek_post(void)
{
    char *token = online_token(), *form = online_setup_form(&seek_setup, seek_rating);
    li_request = http_start("POST", ONLINE_URL "/api/board/seek", token, form, lishogi_request_line,
                                lishogi_request_done, NULL);
    free(form);
    g_free(token);
}

static void seek_account_line(const char *line, void *data)
{
    (void) data;
    char key[64];
    snprintf(key, sizeof key, "perfs.%s.rating", online_perf_key(&seek_setup));
    seek_rating = (int) json_long(line, key, seek_rating);
}

static void seek_account_done(int status, void *data)
{
    (void) status, (void) data;
    li_request = NULL;
    if (app.wait_dialog)
        seek_post(); /* without our rating (a new account), no range */
}

/* Starts a lishogi game: a seek, the AI, a challenge to `user`, or waits for or accepts one. */
void online_game(const OnlineSetup *s, const char *user)
{
    if (!net_available()) {
        message(tr("No network connection. Turn off airplane mode or connect to Wi-Fi."));
        return;
    }
    const char *problem = online_setup_problem(s);
    if (problem) {
        message(tr(problem));
        return;
    }
    app.variant = s->who == ONLINE_WAIT || s->who == ONLINE_ACCEPT || s->who == ONLINE_RESUME ? V_STANDARD : s->variant;
    rules_set_variant(app.variant);
    char *token = online_token();
    if (!*token) {
        g_free(token);
        if (!lishogi_token_dialog())
            return;
        token = online_token();
    }
    net_stop();
    online_stop();
    server_stop();
    li_error[0] = 0;
    OnlineOpponent who = s->who;
    char *form = online_setup_form(s, 0), *url;
    if (who == ONLINE_AI) {
        url = g_strdup(ONLINE_URL "/api/challenge/ai");
    } else {
        /* "-" can be no one's name: lishogi then makes a challenge anyone with the link can
         * accept, as "Play with a friend" on the site does. */
        char *name = http_escape(who == ONLINE_LINK ? "-" : user);
        url = g_strdup_printf(ONLINE_URL "/api/challenge/%s", name);
        g_free(name);
    }
    GString *body = g_string_new(form);
    free(form);
    app.mode = MODE_ONLINE;
    online_events_cb(NULL);
    if (who == ONLINE_ACCEPT) {
        char *id = lishogi_link_id(user);
        if (!id) {
            g_free(url);
            g_string_free(body, TRUE);
            g_free(token);
            app.mode = MODE_FIRST;
            message(tr("That does not look like a Lishogi invitation link."));
            return;
        }
        g_free(url);
        url = g_strdup_printf(ONLINE_URL "/api/challenge/%s/accept", id);
        g_free(li_accept_link);
        li_accept_link = g_strdup_printf(ONLINE_URL "/%s", id);
        g_string_truncate(body, 0);
        g_free(id);
    }
    if (who == ONLINE_SEEK) {
        seek_setup = *s;
        seek_rating = 0;
        li_request = http_start("GET", ONLINE_URL "/api/account", token, NULL, seek_account_line,
                                    seek_account_done, NULL);
    } else if (who == ONLINE_RESUME) {
        char *id = *user ? lishogi_link_id(user) : NULL;
        if (*user && !id) {
            g_free(url);
            g_string_free(body, TRUE);
            g_free(token);
            app.mode = MODE_FIRST;
            message(tr("That is not a Lishogi game link."));
            return;
        }
        snprintf(resume_id, sizeof resume_id, "%s", id ? id : "");
        g_free(id);
        resuming = TRUE;
        resume_timer = g_timeout_add_seconds(10, resume_timeout_cb, NULL);
    } else if (who != ONLINE_WAIT)
        li_request = http_start("POST", url, token, body->len ? body->str : NULL, lishogi_request_line,
                                    lishogi_request_done, NULL);
    g_free(url);
    g_string_free(body, TRUE);
    g_free(token);
    const char *text = who == ONLINE_SEEK ? tr("Looking for an opponent on Lishogi…")
                       : who == ONLINE_AI ? tr("Starting a game against the Lishogi AI…")
                       : who == ONLINE_LINK ? tr("Send this link to the other player; the first one to open it plays you:")
                       : who == ONLINE_WAIT ? tr("Waiting for someone to challenge you on Lishogi…")
                       : who == ONLINE_ACCEPT ? tr("Accepting the invitation…")
                       : who == ONLINE_RESUME ? tr("Looking for your games in progress on Lishogi…")
                                             : tr("Waiting for the other player to accept the challenge…");
    if (who != ONLINE_ACCEPT) {
        g_free(li_accept_link);
        li_accept_link = NULL;
    }
    gboolean started = wait_for_peer_link(text, who == ONLINE_LINK);
    resuming = FALSE;
    if (resume_timer)
        g_source_remove(resume_timer);
    resume_timer = 0;
    if (started) {
        http_cancel(li_request); /* a seek stays open until a game starts */
        li_request = NULL;
        lishogi_open_game(li_id);
        return;
    }
    if (li_challenge[0] && (who == ONLINE_USER || who == ONLINE_LINK) && !li_error[0]) {
        char path[64];
        snprintf(path, sizeof path, "/api/challenge/%s/cancel", li_challenge);
        online_post(path, FALSE);
    }
    online_stop();
    server_stop();
    app.mode = MODE_FIRST;
    if (li_error[0])
        message(li_error);
    start_game(app.handicap, MODE_FIRST);
}

/* ---- opening a lishogi game ---- */

static GString *lishogi_record;

static void record_line(const char *line, void *data)
{
    (void) data;
    g_string_append(lishogi_record, line);
    g_string_append_c(lishogi_record, '\n');
}

static void record_done(int status, void *data)
{
    (void) data;
    gboolean ok = status == 200 && load_record_text(lishogi_record->str);
    g_string_free(lishogi_record, TRUE);
    lishogi_record = NULL;
    if (!ok)
        message(status ? tr("Lishogi has no game with this link.") : tr("Could not reach lishogi.org."));
}

/* The game id in a link (lishogi.org/abcdEFGH, a player's link has 4 more characters) or alone. */
static gboolean lishogi_game_id(const char *text, char id[9])
{
    char *found = lishogi_link_id(text);
    if (found)
        memcpy(id, found, 9);
    g_free(found);
    return found != NULL;
}

void online_open_game(void)
{
    if (lishogi_record)
        return; /* one already on its way */
    GtkWidget *d = dialog_new(tr("Open from Lishogi"));
    GtkWidget *label = gtk_label_new(tr("Link or ID of a Lishogi game:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    GtkWidget *entry = text_entry();
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), label, FALSE, FALSE, 6);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), with_paste(entry), FALSE, FALSE, 6);
    gtk_dialog_add_button(GTK_DIALOG(d), tr("Cancel"), GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(d), tr("Open"), GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
    gtk_widget_show_all(d);
    char id[9];
    gboolean go = FALSE;
    while (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
        if ((go = lishogi_game_id(gtk_entry_get_text(GTK_ENTRY(entry)), id)))
            break;
        message(tr("That is not a Lishogi game link."));
    }
    gtk_widget_destroy(d);
    if (!go)
        return;
    char *url = g_strdup_printf(ONLINE_URL "/game/export/%s", id);
    lishogi_record = g_string_new("");
    set_move_text(tr("Opening the game…"));
    http_start("GET", url, NULL, NULL, record_line, record_done, NULL);
    g_free(url);
}
