/* CSA servers (Floodgate, shogi-server): CSA protocol 1.2. Log in, read the game summary, agree,
 * then moves as CSA lines; the server keeps the clock and announces the end. */

#include "app.h"

static void csa_connected_cb(void *data);
static void csa_line_cb(const char *line, void *data);
static void csa_closed_cb(const char *why, void *data);
static const NetCallbacks CSA_CB = {csa_connected_cb, csa_line_cb, csa_closed_cb, NULL, NULL, NULL};

static char *login_name, *login_password;
static guint keepalive;
static Game summary; /* the start and the moves already played, from the game summary */
static char names[2][64];
static Net *csa_net;
static GString *csa_summary; /* the game summary being read */
static char csa_reason[24], csa_error[160]; /* why the game ended; why the login failed */

void server_stop(void)
{
    if (keepalive)
        g_source_remove(keepalive);
    keepalive = 0;
    if (csa_net) {
        net_send(csa_net, "LOGOUT");
        net_free(csa_net);
    }
    csa_net = NULL;
    if (csa_summary)
        g_string_free(csa_summary, TRUE);
    csa_summary = NULL;
}

/* Servers drop quiet connections: an empty line now and then keeps it (CSA 1.2). */
static gboolean keepalive_cb(gpointer data)
{
    (void) data;
    net_send(csa_net, "");
    return csa_net != NULL;
}

static void csa_connected_cb(void *data)
{
    (void) data;
    char *line = g_strdup_printf("LOGIN %s %s", login_name, login_password);
    net_send(csa_net, line);
    g_free(line);
    if (!keepalive)
        keepalive = g_timeout_add_seconds(30, keepalive_cb, NULL);
}

/* "Key:value" inside the summary. */
static long summary_long(const char *key, long fallback)
{
    const char *at = strstr(csa_summary->str, key);
    return at ? atol(at + strlen(key)) : fallback;
}

static void summary_text(const char *key, char *out, size_t size)
{
    const char *at = strstr(csa_summary->str, key);
    if (!at) {
        *out = 0;
        return;
    }
    at += strlen(key);
    size_t n = strcspn(at, "\n");
    snprintf(out, size, "%.*s", (int) MIN(n, size - 1), at);
}

/* The whole summary is in: the side, the names, the clock, the position. */
static void summary_done(void)
{
    char turn[4];
    summary_text("Your_Turn:", turn, sizeof turn);
    app.net_color = turn[0] == '-' ? SECOND : FIRST;
    summary_text("Name+:", names[FIRST], sizeof names[FIRST]);
    summary_text("Name-:", names[SECOND], sizeof names[SECOND]);
    char unit[16];
    summary_text("Time_Unit:", unit, sizeof unit);
    long scale = strstr(unit, "min") ? 60 : strstr(unit, "msec") ? 0 : 1;
    long total = summary_long("Total_Time:", 0), byoyomi = summary_long("Byoyomi:", 0),
         increment = summary_long("Increment:", 0);
    if (!scale) /* milliseconds */
        total /= 1000, byoyomi /= 1000, increment /= 1000, scale = 1;
    app.online_tc = (TimeControl) {"CSA", (int) (total * scale), (int) (byoyomi * scale), (int) (increment * scale)};
    const char *pos = strstr(csa_summary->str, "BEGIN Position");
    if (!pos || !game_read_csa_text(&summary, pos))
        game_new(&summary, HANDICAP_NONE);
    net_send(csa_net, "AGREE");
    wait_set_text(tr("Starting the game…"));
}

static gboolean logout_idle(gpointer data)
{
    (void) data;
    server_stop();
    return FALSE;
}

static void csa_line_cb(const char *line, void *data)
{
    (void) data;
    if (csa_summary) {
        if (!strcmp(line, "END Game_Summary"))
            summary_done();
        else
            g_string_append_printf(csa_summary, "%s\n", line);
        if (!strcmp(line, "END Game_Summary")) {
            g_string_free(csa_summary, TRUE);
            csa_summary = NULL;
        }
        return;
    }
    if (g_str_has_prefix(line, "LOGIN:") && g_str_has_suffix(line, " OK")) {
        wait_set_text(tr("Logged in. Waiting for an opponent…"));
    } else if (g_str_has_prefix(line, "LOGIN:incorrect")) {
        snprintf(csa_error, sizeof csa_error, "%s", tr("The server did not accept the login."));
        if (app.wait_dialog)
            gtk_dialog_response(GTK_DIALOG(app.wait_dialog), GTK_RESPONSE_REJECT);
    } else if (!strcmp(line, "BEGIN Game_Summary")) {
        csa_summary = g_string_new(NULL);
    } else if (g_str_has_prefix(line, "START:")) {
        if (app.wait_dialog)
            gtk_dialog_response(GTK_DIALOG(app.wait_dialog), GTK_RESPONSE_OK);
    } else if (g_str_has_prefix(line, "REJECT:")) {
        wait_set_text(tr("The opponent did not agree. Waiting for another game…"));
    } else if ((line[0] == '+' || line[0] == '-') && strlen(line) >= 7) {
        /* Every move comes back to both players, ours too ("+7776FU,T5"). */
        int mover = line[0] == '+' ? FIRST : SECOND;
        if (mover == app.net_color || app.game.result != RESULT_NONE)
            return;
        char move[8];
        snprintf(move, sizeof move, "%.7s", line);
        Move m;
        if (csa_parse_move(&app.game.pos, move, &m))
            play_move(&m);
    } else if (line[0] == '#') {
        static const struct { const char *word; Reason reason; } REASONS[] = {
            {"#RESIGN", REASON_RESIGN}, {"#TIME_UP", REASON_TIME}, {"#ILLEGAL_MOVE", REASON_ILLEGAL},
            {"#SENNICHITE", REASON_REPETITION}, {"#OUTE_SENNICHITE", REASON_PERPETUAL_CHECK},
            {"#JISHOGI", REASON_IMPASSE}, {"#MAX_MOVES", REASON_ABORT}, {"#CHUDAN", REASON_ABORT},
        };
        for (size_t i = 0; i < G_N_ELEMENTS(REASONS); i++)
            if (!strcmp(line, REASONS[i].word))
                snprintf(csa_reason, sizeof csa_reason, "%s", line);
        gboolean win = !strcmp(line, "#WIN"), lose = !strcmp(line, "#LOSE");
        if ((win || lose || !strcmp(line, "#DRAW") || !strcmp(line, "#CENSORED")) && app.game.result == RESULT_NONE) {
            Reason why = REASON_ABORT;
            for (size_t i = 0; i < G_N_ELEMENTS(REASONS); i++)
                if (!strcmp(csa_reason, REASONS[i].word))
                    why = REASONS[i].reason;
            int us = app.net_color;
            Result r = win ? (us == FIRST ? RESULT_FIRST_WIN : RESULT_SECOND_WIN)
                     : lose ? (us == FIRST ? RESULT_SECOND_WIN : RESULT_FIRST_WIN)
                            : !strcmp(line, "#DRAW") ? RESULT_DRAW : RESULT_ABORTED;
            game_end(&app.game, r, why);
            next_turn();
            /* One game per login: log out (from the main loop, not inside this connection's callback). */
            g_idle_add(logout_idle, NULL);
        }
    }
}

static void csa_closed_cb(const char *why, void *data)
{
    (void) data;
    if (app.wait_dialog) {
        if (!csa_error[0])
            snprintf(csa_error, sizeof csa_error, "%s %s", tr("Could not reach the server:"), why);
        gtk_dialog_response(GTK_DIALOG(app.wait_dialog), GTK_RESPONSE_REJECT);
        return;
    }
    if (app.mode == MODE_SERVER && app.game.result == RESULT_NONE) {
        game_end(&app.game, RESULT_ABORTED, REASON_ABORT);
        char *text = g_strdup_printf("%s\n%s", tr("The connection to the server was lost."), why);
        message(text);
        g_free(text);
        next_turn();
    }
}

void server_send_move(const Move *m)
{
    char text[16];
    csa_move(&app.game.pos, m, text);
    net_send(csa_net, text);
}

void server_resign(void) { net_send(csa_net, "%TORYO"); }

/* Logs in to `server` ("host" or "host:port") and plays the game it offers. */
void server_connect(const char *server, const char *name, const char *password)
{
    if (!net_available()) {
        message(tr("No network connection. Turn off airplane mode or connect to Wi-Fi."));
        return;
    }
    app.variant = V_STANDARD;
    rules_set_variant(V_STANDARD);
    net_stop();
    online_stop();
    server_stop();
    g_free(login_name);
    g_free(login_password);
    login_name = g_strdup(name), login_password = g_strdup(password);
    csa_error[0] = csa_reason[0] = 0;
    app.mode = MODE_SERVER;
    csa_net = net_join_plain(server, 4081, &CSA_CB, NULL);
    if (!wait_for_peer_link(tr("Connecting to the server…"), FALSE)) {
        server_stop();
        app.mode = MODE_FIRST;
        if (csa_error[0])
            message(csa_error);
        start_game(app.handicap, MODE_FIRST);
        return;
    }
    start_game(HANDICAP_NONE, MODE_SERVER);
    app.game.start = app.game.pos = summary.start;
    app.game.hashes[0] = summary.start.hash;
    for (int i = 0; i < summary.count; i++) {
        Move m = summary.moves[i];
        if (pos_is_legal(&app.game.pos, &m))
            game_play(&app.game, &m, 0);
    }
    g_free(app.remote_name);
    app.remote_name = g_strdup(names[!app.net_color][0] ? names[!app.net_color] : tr("Opponent"));
    for (int c = 0; c < 2; c++)
        if (names[c][0])
            snprintf(app.game.names[c], sizeof app.game.names[c], "%s", names[c]);
    next_turn();
}
