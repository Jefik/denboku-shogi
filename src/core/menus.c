/* New Game, Preferences, saving and loading, How to Play, About; the actions and the menu. */

#include "app.h"
#include <math.h>
#include "release.h"
#include "sheet.h"
#include "texts.h"

static gboolean give_up_online_game(void);

/* ---- menus and dialogs ---- */


/* A variant's name as shown (variant_name() is the one in records and settings). */
const char *variant_label(int variant) { return tr(VARIANT_LABELS[CLAMP(variant, 0, V_COUNT - 1)]); }

/* New Game's rows; the online ones (ROW_LI_*) are lishogi's choices. */
typedef enum {
    ROW_PLAYERS, ROW_ROLE, ROW_OPPONENT, ROW_USER, ROW_SIDE, ROW_LEVEL_FIRST, ROW_ENGINE, ROW_CLOCK, ROW_MINUTES,
    ROW_BYOYOMI, ROW_INCREMENT, ROW_LI_MODE, ROW_LI_LIMIT, ROW_LI_BYOYOMI, ROW_LI_INCREMENT, ROW_LI_PERIODS,
    ROW_LI_DAYS, ROW_LI_BELOW, ROW_LI_ABOVE, ROW_RATED, ROW_SERVER, ROW_LOGIN, ROW_PASSWORD, ROW_ROOM_PASSWORD,
    ROW_VARIANT, ROW_HANDICAP, ROW_LEGAL, ROW_ILLEGAL, ROW_COUNT
} NewGameRow;

typedef struct {
    GtkWidget *w[ROW_COUNT][2]; /* label and widget of each row */
    GtkWidget *illegal, *dialog, *time, *handicap, *handicap_note, *variant, *more, *side, *level;
    /* An online game: the server's own choices (online_setup.h). */
    GtkWidget *li_mode, *li_limit, *li_byoyomi, *li_increment, *li_periods, *li_days, *li_below, *li_above, *rated;
    gboolean more_shown; /* "More options": the handicap and a host's move rules */
    gboolean room;       /* the settings of a room already hosted, from its lobby */
} NewGameRows;

/* The players chosen: the first list, and for "Network" the second (host or join). */
static Play chosen_play(NewGameRows *r)
{
    int i = gtk_combo_box_get_active(GTK_COMBO_BOX(r->w[ROW_PLAYERS][1]));
    if (i < PLAY_HOST)
        return (Play) i;
    if (i == PLAY_HOST)
        return gtk_combo_box_get_active(GTK_COMBO_BOX(r->w[ROW_ROLE][1])) == 1 ? PLAY_JOIN : PLAY_HOST;
    return (Play) (i + 1);
}

static int active(GtkWidget *combo) { return gtk_combo_box_get_active(GTK_COMBO_BOX(combo)); }

/* The online game the rows describe. */
static void online_setup_of(NewGameRows *r, OnlineSetup *s)
{
    *s = (OnlineSetup) {
        .who = (OnlineOpponent) active(r->w[ROW_OPPONENT][1]),
        .variant = MAX(active(r->variant), V_STANDARD),
        .level = active(r->level) + 1,
        .side = active(r->side),
        .handicap = (Handicap) MAX(active(r->handicap), 0),
        .rated = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(r->rated)),
        .time_mode = (OnlineTimeMode) MAX(active(r->li_mode), 0),
        .limit_s = ONLINE_LIMITS[MAX(active(r->li_limit), 0)],
        .byoyomi_s = ONLINE_SECONDS[MAX(active(r->li_byoyomi), 0)],
        .increment_s = ONLINE_SECONDS[MAX(active(r->li_increment), 0)],
        .periods = MAX(active(r->li_periods), 0) + 1,
        .days = ONLINE_DAYS[MAX(active(r->li_days), 0)],
        .range_below = MAX(active(r->li_below), 0) * ONLINE_RANGE_STEP,
        .range_above = MAX(active(r->li_above), 0) * ONLINE_RANGE_STEP,
    };
    if (s->variant != V_STANDARD)
        s->handicap = HANDICAP_NONE;
}

/* A list of numbers; the one nearest `current` chosen. */
static GtkWidget *value_combo(const int *values, int n, int current, void (*label)(int value, char *buf, int size))
{
    GtkWidget *c = gtk_combo_box_new_text();
    close_popup_on_leave(c);
    int best = 0;
    for (int i = 0; i < n; i++) {
        char buf[32];
        label(values[i], buf, sizeof buf);
        gtk_combo_box_append_text(GTK_COMBO_BOX(c), buf);
        if (abs(values[i] - current) < abs(values[best] - current))
            best = i;
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(c), best);
    return c;
}

static void number_label(int v, char *buf, int size) { snprintf(buf, size, "%d", v); }

/* "45 s", "1 min 30 s", "10 min". */
static void duration_label(int s, char *buf, int size)
{
    if (s && s < 60)
        snprintf(buf, size, tr("%d s"), s);
    else if (s % 60)
        snprintf(buf, size, tr("%d min %d s"), s / 60, s % 60);
    else
        snprintf(buf, size, tr("%d min"), s / 60);
}

static int variant_rules_page(int variant);

static void variant_rules_cb(GtkWidget *w, gpointer data)
{
    (void) w;
    NewGameRows *r = data;
    int v = gtk_combo_box_get_active(GTK_COMBO_BOX(r->variant));
    variant_rules_page(v >= 0 ? v : V_STANDARD);
}

/* What decides which of New Game's rows show. */
typedef struct {
    Play play;
    OnlineOpponent who;
    int variant;
    gboolean custom_time, handicap_set, more_shown, room;
    gboolean start_sfen, net_start; /* a set-up position; a room's game started from one */
    OnlineTimeMode time_mode;
    int byoyomi_s;
} NewGameState;

/* Only the rows that matter for the chosen players: a joiner gets everything from the host, and on
 * lishogi the opponent comes first, as it decides the rest. Returns whether "More options" shows. */
static gboolean rows_visible(const NewGameState *s, gboolean visible[ROW_COUNT])
{
    Play m = s->play;
    OnlineOpponent who = s->who;
    gboolean li = m == PLAY_ONLINE;
    gboolean csa = m == PLAY_SERVER; /* the server sets the clock, the side and the start */
    /* Waiting for a challenge or accepting one: the challenger chose the side and the clock. */
    gboolean set_by_other = m == PLAY_JOIN || csa || (li && (who == ONLINE_WAIT || who == ONLINE_ACCEPT || who == ONLINE_RESUME));
    gboolean asks = li && (who == ONLINE_AI || who == ONLINE_USER || who == ONLINE_LINK);
    /* Online: the server's clock (a seek is in real time only), its rating range for a seek. */
    gboolean real_time = li && !set_by_other && (who == ONLINE_SEEK || s->time_mode == ONLINE_REAL_TIME);
    visible[ROW_PLAYERS] = !s->room; /* the players are the room's */
    visible[ROW_ROLE] = (m == PLAY_HOST || m == PLAY_JOIN) && !s->room;
    visible[ROW_OPPONENT] = li;
    visible[ROW_USER] = li && (who == ONLINE_USER || who == ONLINE_ACCEPT || who == ONLINE_RESUME);
    visible[ROW_SIDE] = (m == PLAY_ENGINE || m == PLAY_HOST || li) && !set_by_other;
    visible[ROW_LEVEL_FIRST] = m == PLAY_ENGINES; /* two engines: a level each */
    visible[ROW_ENGINE] = m == PLAY_ENGINE || m == PLAY_ENGINES || (li && who == ONLINE_AI);
    visible[ROW_CLOCK] = !set_by_other && !li;
    visible[ROW_MINUTES] = visible[ROW_BYOYOMI] = visible[ROW_INCREMENT] = visible[ROW_CLOCK] && s->custom_time;
    visible[ROW_LI_MODE] = asks;
    visible[ROW_LI_LIMIT] = visible[ROW_LI_BYOYOMI] = visible[ROW_LI_INCREMENT] = real_time;
    visible[ROW_LI_PERIODS] = real_time && s->byoyomi_s;
    visible[ROW_LI_DAYS] = asks && s->time_mode == ONLINE_CORRESPONDENCE;
    visible[ROW_LI_BELOW] = visible[ROW_LI_ABOVE] = li && who == ONLINE_SEEK;
    visible[ROW_RATED] = li && who != ONLINE_AI && who != ONLINE_WAIT && who != ONLINE_ACCEPT && who != ONLINE_RESUME;
    visible[ROW_SERVER] = visible[ROW_LOGIN] = visible[ROW_PASSWORD] = csa;
    visible[ROW_ROOM_PASSWORD] = visible[ROW_LEGAL] = visible[ROW_ILLEGAL] = m == PLAY_HOST;
    visible[ROW_VARIANT] = m != PLAY_JOIN && (!li || !set_by_other) && !csa && !s->start_sfen;
    visible[ROW_HANDICAP] = m != PLAY_JOIN && (!li || asks) && !csa && !s->start_sfen;
    if (s->room) /* a set-up position has no variant or handicap */
        visible[ROW_HANDICAP] = visible[ROW_VARIANT] = visible[ROW_VARIANT] && !s->net_start;
    /* The handicap (standard shogi only) and a host's move rules wait under "More options" when
     * there are two or more of them (a handicap chosen stays in sight); one alone is shown. */
    visible[ROW_HANDICAP] = visible[ROW_HANDICAP] && s->variant == V_STANDARD;
    gboolean more = visible[ROW_HANDICAP] + visible[ROW_LEGAL] + visible[ROW_ILLEGAL] >= 2;
    visible[ROW_HANDICAP] = visible[ROW_HANDICAP] && (!more || s->more_shown || s->handicap_set);
    visible[ROW_LEGAL] = visible[ROW_LEGAL] && (!more || s->more_shown);
    visible[ROW_ILLEGAL] = visible[ROW_ILLEGAL] && (!more || s->more_shown);
    return more;
}

static void new_game_rows_cb(GtkComboBox *changed, gpointer data)
{
    (void) changed;
    NewGameRows *r = data;
    OnlineSetup setup;
    online_setup_of(r, &setup);
    NewGameState state = {
        .play = chosen_play(r),
        .who = (OnlineOpponent) active(r->w[ROW_OPPONENT][1]),
        .variant = MAX(active(r->variant), V_STANDARD),
        .custom_time = active(r->time) == TIME_CUSTOM,
        .handicap_set = active(r->handicap) != HANDICAP_NONE,
        .more_shown = r->more_shown,
        .room = r->room,
        .start_sfen = app.start_sfen != NULL,
        .net_start = app.net_start != NULL,
        .time_mode = setup.time_mode,
        .byoyomi_s = setup.byoyomi_s,
    };
    gboolean visible[ROW_COUNT];
    gboolean more = rows_visible(&state, visible);
    gtk_widget_set_sensitive(r->rated, online_can_rate(&setup));
    gtk_label_set_text(GTK_LABEL(r->w[ROW_ENGINE][0]), tr(state.play == PLAY_ENGINES ? SIDE_LABELS[1] : "Engine"));
    gtk_widget_set_visible(r->more, more);
    gtk_button_set_label(GTK_BUTTON(r->more), tr(r->more_shown ? "More options ▴" : "More options ▾"));
    for (int i = 0; i < ROW_COUNT; i++) /* an empty label (Host or Join, Rated) left a blank line above its row */
        for (int j = 0; j < 2; j++)
            gtk_widget_set_visible(r->w[i][j], visible[i] && (j || *gtk_label_get_text(GTK_LABEL(r->w[i][0]))));
    int h = state.variant == V_STANDARD ? gtk_combo_box_get_active(GTK_COMBO_BOX(r->handicap)) : HANDICAP_NONE;
    int you = gtk_combo_box_get_active(GTK_COMBO_BOX(r->w[ROW_SIDE][1]));
    if (state.variant != V_STANDARD)
        gtk_combo_box_set_active(GTK_COMBO_BOX(r->handicap), HANDICAP_NONE);
    const char *note = h == HANDICAP_NONE ? NULL : HANDICAP_NOTES[CLAMP(you, 0, 2)];
    if (note)
        gtk_label_set_text(GTK_LABEL(r->handicap_note), tr(note));
    gtk_widget_set_visible(r->handicap_note, note != NULL);
    gtk_label_set_text(GTK_LABEL(r->w[ROW_USER][0]), tr(state.who == ONLINE_ACCEPT ? "Link or code" : state.who == ONLINE_RESUME ? "Game link (optional)" : "Username"));
    gtk_window_resize(GTK_WINDOW(r->dialog), 1, 1);
    /* Hidden rows (the custom-time scales) can leave a stale horizontal line when the dialog
     * shrinks; repaint the whole dialog, not only the resized children. */
    gtk_widget_queue_draw(GTK_WIDGET(r->dialog));
}

static void more_options_cb(GtkButton *b, gpointer data)
{
    (void) b;
    NewGameRows *r = data;
    r->more_shown = !r->more_shown;
    new_game_rows_cb(NULL, r);
}

/* New Game, or with `room` the settings of the room hosted (from its lobby, over it). */
static void game_dialog(gboolean room)
{
    if (!room && !give_up_online_game())
        return;
    GtkWidget *d = dialog_new(tr(room ? "Room settings" : "New Game"));
    GtkWidget *table = gtk_table_new(24, 2, FALSE);
    /* Start from the choices of the last New Game, not from a game that a host set up. */
    Mode last = room ? MODE_HOST : (Mode) CLAMP(setting_int("Mode", lesson_playing() ? MODE_FIRST : app.mode), 0, MODE_COUNT - 1);
    static const Play PLAY_OF[MODE_COUNT] = {PLAY_ENGINE, PLAY_ENGINE, PLAY_ENGINE, PLAY_TWO,
                                             PLAY_ENGINES, PLAY_HOST, PLAY_JOIN, PLAY_ONLINE, PLAY_SERVER};
    Play last_play = PLAY_OF[last];
    GtkWidget *mode = combo(PLAY_LABELS, PLAY_CHOICES, last_play <= PLAY_HOST ? last_play : last_play - 1, TRUE);
    static const char *roles[] = {"Host a game", "Join a game"};
    GtkWidget *role = combo(roles, 2, last_play == PLAY_JOIN, TRUE);
    int last_side = last == MODE_SECOND ? 1 : last == MODE_RANDOM ? 2 : last == MODE_FIRST ? 0 : setting_int("Side", 0);
    GtkWidget *side = combo(SIDE_LABELS, 3, CLAMP(last_side, 0, 2), TRUE);
    const char *levels[LEVELS];
    char level_text[LEVELS][32];
    for (int i = 0; i < LEVELS; i++) {
        snprintf(level_text[i], 32, "%s %d", tr("Level"), i + 1);
        levels[i] = level_text[i];
    }
    GtkWidget *level = combo(levels, LEVELS, CLAMP(setting_int("Level", app.level), 1, LEVELS) - 1, FALSE);
    GtkWidget *level_first = combo(levels, LEVELS, CLAMP(setting_int("LevelFirst", app.level_first), 1, LEVELS) - 1, FALSE);
    const char *times[NTIME_CHOICES];
    for (int i = 0; i < NTIMES; i++)
        times[i] = tr(TIMES[i].label);
    times[NTIMES] = tr("Custom…");
    GtkWidget *time_combo = combo(times, NTIME_CHOICES, CLAMP(setting_int("Time", app.time_index), 0, NTIMES), FALSE);
    GtkWidget *custom_min = gtk_hscale_new_with_range(0, 180, 1);
    GtkWidget *custom_byoyomi = gtk_hscale_new_with_range(0, 300, 1);
    GtkWidget *custom_increment = gtk_hscale_new_with_range(0, 60, 1);
    GtkWidget *customs[] = {custom_min, custom_byoyomi, custom_increment};
    int custom_values[] = {app.custom_min, app.custom_byoyomi, app.custom_increment};
    for (int i = 0; i < 3; i++) {
        gtk_scale_set_value_pos(GTK_SCALE(customs[i]), GTK_POS_RIGHT);
        gtk_range_set_increments(GTK_RANGE(customs[i]), 1, 5);
        gtk_range_set_value(GTK_RANGE(customs[i]), custom_values[i]);
        gtk_widget_set_size_request(customs[i], 220, -1);
    }
    const char *handicaps[HANDICAP_COUNT];
    for (int i = 0; i < HANDICAP_COUNT; i++)
        handicaps[i] = handicap_label(i);
    GtkWidget *handicap = combo(handicaps, HANDICAP_COUNT,
                                CLAMP(room ? (int) app.handicap : setting_int("Handicap", app.handicap), 0, HANDICAP_COUNT - 1), FALSE);
    GtkWidget *handicap_note = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(handicap_note), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(handicap_note), PANGO_WRAP_WORD_CHAR);
    gtk_misc_set_alignment(GTK_MISC(handicap_note), 0, 0.5);
    gtk_widget_set_size_request(handicap_note, 300, -1);
    GtkWidget *handicap_box = gtk_vbox_new(FALSE, 2);
    gtk_box_pack_start(GTK_BOX(handicap_box), handicap, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(handicap_box), handicap_note, FALSE, FALSE, 0);
    GtkWidget *variant = combo(VARIANT_LABELS, V_COUNT,
                               /* a lesson's variant and mode are not the player's choices */
                               !lesson_playing() && app.variant >= 0 && app.variant < V_COUNT ? app.variant
                                                                         : CLAMP(setting_int("Variant", V_STANDARD), 0, V_COUNT - 1),
                               TRUE);
    /* The host's rule for both players: the last one chosen, or the host's own preferences. */
    static const char *shown[] = {"Shown", "Hidden"};
    int last_rule = CLAMP(room ? app.net_rule : setting_int("HostRule", own_move_rule()), 0, 5);
    GtkWidget *legal = combo(shown, 2, last_rule & 1, TRUE);
    GtkWidget *illegal = combo(ILLEGAL_LABELS, 3, last_rule >> 1, TRUE);
    NewGameRows rows = {.illegal = illegal, .dialog = d, .time = time_combo, .handicap = handicap,
                        .handicap_note = handicap_note, .variant = variant, .room = room, .side = side, .level = level};
    rows.w[ROW_PLAYERS][0] = table_row(table, 0, tr("Players"), mode), rows.w[ROW_PLAYERS][1] = mode;
    rows.w[ROW_ROLE][0] = table_row(table, 1, "", role), rows.w[ROW_ROLE][1] = role; /* shares the row with lishogi's Opponent */
    rows.w[ROW_SIDE][0] = table_row(table, 3, tr("You play"), side), rows.w[ROW_SIDE][1] = side;
    /* Shares the row with "You play", which a game between engines does not ask. */
    rows.w[ROW_LEVEL_FIRST][0] = table_row(table, 3, tr(SIDE_LABELS[0]), level_first), rows.w[ROW_LEVEL_FIRST][1] = level_first;
    rows.w[ROW_ENGINE][0] = table_row(table, 4, tr("Engine"), level), rows.w[ROW_ENGINE][1] = level;
    rows.w[ROW_CLOCK][0] = table_row(table, 5, tr("Clock"), time_combo), rows.w[ROW_CLOCK][1] = time_combo;
    rows.more = gtk_button_new_with_label("");
    gtk_widget_set_name(rows.more, "denboku-flatrow");
    g_signal_connect(rows.more, "clicked", G_CALLBACK(more_options_cb), &rows);
    gtk_table_attach(GTK_TABLE(table), rows.more, 0, 2, 18, 19, GTK_FILL, GTK_FILL, 8, 6);
    rows.w[ROW_HANDICAP][0] = table_row(table, 19, tr("Handicap"), handicap_box), rows.w[ROW_HANDICAP][1] = handicap_box;
    rows.w[ROW_LEGAL][0] = table_row(table, 20, tr("Legal moves"), legal), rows.w[ROW_LEGAL][1] = legal;
    rows.w[ROW_ILLEGAL][0] = table_row(table, 21, tr("Illegal moves"), illegal), rows.w[ROW_ILLEGAL][1] = illegal;
    GtkWidget *who = combo(ONLINE_OPPONENTS, ONLINE_COUNT, CLAMP(setting_int("OnlineOpponent", 0), 0, ONLINE_COUNT - 1), TRUE);
    GtkWidget *user = text_entry();
    char *last_user = setting_string("LishogiUser", "");
    gtk_entry_set_text(GTK_ENTRY(user), last_user);
    g_free(last_user);
    GtkWidget *rated = gtk_toggle_button_new_with_label(tr("Rated")); /* a chip, filled when on */
    gtk_widget_set_name(rated, "denboku-chip");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rated), setting_int("LishogiRated", 0));
    rows.w[ROW_OPPONENT][0] = table_row(table, 1, tr("Opponent"), who), rows.w[ROW_OPPONENT][1] = who;
    GtkWidget *user_box = with_paste(user);
    rows.w[ROW_USER][0] = table_row(table, 2, tr("Username"), user_box), rows.w[ROW_USER][1] = user_box;
    rows.w[ROW_RATED][0] = table_row(table, 12, "", rated), rows.w[ROW_RATED][1] = rated;
    rows.rated = rated;
    /* Online, the server's choices: they share the rows of the clock and its custom values. */
    rows.li_mode = combo(ONLINE_TIME_MODES, ONLINE_TIME_MODE_COUNT,
                         CLAMP(setting_int("OnlineTimeMode", ONLINE_REAL_TIME), 0, ONLINE_TIME_MODE_COUNT - 1), TRUE);
    rows.li_limit = value_combo(ONLINE_LIMITS, ONLINE_NLIMITS, setting_int("OnlineLimit", 600), duration_label);
    rows.li_byoyomi = value_combo(ONLINE_SECONDS, ONLINE_NSECONDS, setting_int("OnlineByoyomi", 10), number_label);
    rows.li_increment = value_combo(ONLINE_SECONDS, ONLINE_NSECONDS, setting_int("OnlineIncrement", 0), number_label);
    static const int PERIODS[ONLINE_PERIODS] = {1, 2, 3, 4, 5};
    rows.li_periods = value_combo(PERIODS, ONLINE_PERIODS, setting_int("OnlinePeriods", 1), number_label);
    rows.li_days = value_combo(ONLINE_DAYS, ONLINE_NDAYS, setting_int("OnlineDays", 2), number_label);
    int ranges[ONLINE_RANGE_MAX / ONLINE_RANGE_STEP + 1];
    for (int i = 0; i <= ONLINE_RANGE_MAX / ONLINE_RANGE_STEP; i++)
        ranges[i] = i * ONLINE_RANGE_STEP;
    rows.li_below = value_combo(ranges, G_N_ELEMENTS(ranges), setting_int("OnlineBelow", 500), number_label);
    rows.li_above = value_combo(ranges, G_N_ELEMENTS(ranges), setting_int("OnlineAbove", 500), number_label);
    rows.w[ROW_LI_MODE][0] = table_row(table, 5, tr("Time control"), rows.li_mode), rows.w[ROW_LI_MODE][1] = rows.li_mode;
    rows.w[ROW_LI_LIMIT][0] = table_row(table, 6, tr("Time per player"), rows.li_limit), rows.w[ROW_LI_LIMIT][1] = rows.li_limit;
    rows.w[ROW_LI_BYOYOMI][0] = table_row(table, 7, tr("Byoyomi (seconds)"), rows.li_byoyomi), rows.w[ROW_LI_BYOYOMI][1] = rows.li_byoyomi;
    rows.w[ROW_LI_INCREMENT][0] = table_row(table, 8, tr("Increment (seconds)"), rows.li_increment), rows.w[ROW_LI_INCREMENT][1] = rows.li_increment;
    rows.w[ROW_LI_PERIODS][0] = table_row(table, 9, tr("Byoyomi periods"), rows.li_periods), rows.w[ROW_LI_PERIODS][1] = rows.li_periods;
    rows.w[ROW_LI_DAYS][0] = table_row(table, 9, tr("Days per move"), rows.li_days), rows.w[ROW_LI_DAYS][1] = rows.li_days;
    rows.w[ROW_LI_BELOW][0] = table_row(table, 10, tr("Rating below yours, up to"), rows.li_below), rows.w[ROW_LI_BELOW][1] = rows.li_below;
    rows.w[ROW_LI_ABOVE][0] = table_row(table, 11, tr("Rating above yours, up to"), rows.li_above), rows.w[ROW_LI_ABOVE][1] = rows.li_above;
    GtkWidget *server = text_entry(), *login = text_entry(), *password = text_entry();
    char *v = setting_string("CsaServer", "wdoor.c.u-tokyo.ac.jp:4081");
    gtk_entry_set_text(GTK_ENTRY(server), v);
    g_free(v);
    v = setting_string("CsaLogin", "");
    gtk_entry_set_text(GTK_ENTRY(login), v);
    g_free(v);
    v = setting_string("CsaPassword", "");
    gtk_entry_set_text(GTK_ENTRY(password), v);
    g_free(v);
    gtk_entry_set_visibility(GTK_ENTRY(password), FALSE);
    rows.w[ROW_SERVER][0] = table_row(table, 13, tr("Server"), server), rows.w[ROW_SERVER][1] = server;
    rows.w[ROW_LOGIN][0] = table_row(table, 14, tr("Login"), login), rows.w[ROW_LOGIN][1] = login;
    rows.w[ROW_PASSWORD][0] = table_row(table, 15, tr("Password"), password), rows.w[ROW_PASSWORD][1] = password;
    GtkWidget *room_pw = text_entry(); /* not remembered: only this room's password, in memory */
    if (room && app.room_password)
        gtk_entry_set_text(GTK_ENTRY(room_pw), app.room_password);
    GtkWidget *room_box = gtk_hbox_new(FALSE, 8);
    gtk_box_pack_start(GTK_BOX(room_box), room_pw, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(room_box), show_password_check(room_pw), FALSE, FALSE, 0);
    rows.w[ROW_ROOM_PASSWORD][0] = table_row(table, 16, tr("Room password"), room_box), rows.w[ROW_ROOM_PASSWORD][1] = room_box;
    rows.w[ROW_MINUTES][0] = table_row(table, 6, tr("Minutes per player"), custom_min), rows.w[ROW_MINUTES][1] = custom_min;
    rows.w[ROW_BYOYOMI][0] = table_row(table, 7, tr("Byoyomi (seconds)"), custom_byoyomi), rows.w[ROW_BYOYOMI][1] = custom_byoyomi;
    rows.w[ROW_INCREMENT][0] = table_row(table, 8, tr("Increment (seconds)"), custom_increment), rows.w[ROW_INCREMENT][1] = custom_increment;
    GtkWidget *rules = gtk_button_new_with_label(tr("Rules"));
    g_signal_connect(rules, "clicked", G_CALLBACK(variant_rules_cb), &rows);
    GtkWidget *variant_box = gtk_hbox_new(FALSE, 4);
    gtk_box_pack_start(GTK_BOX(variant_box), variant, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(variant_box), rules, FALSE, FALSE, 0);
    rows.w[ROW_VARIANT][0] = table_row(table, 17, tr("Variant"), variant_box), rows.w[ROW_VARIANT][1] = variant_box;
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), table, TRUE, TRUE, 6);
    gtk_dialog_add_button(GTK_DIALOG(d), tr("Cancel"), GTK_RESPONSE_CANCEL);
    GtkWidget *start = gtk_dialog_add_button(GTK_DIALOG(d), tr(room ? "Apply" : "Start"), GTK_RESPONSE_OK);
    /* The rows are chosen before the dialog maps, so it opens at its final size: shown whole
     * first, it was taller than the Kindle screen, and the window manager kept it cut off. */
    gtk_widget_show_all(GTK_DIALOG(d)->vbox);
    /* No entry gets the initial focus: on the Kindle that would open the keyboard before the
     * user taps a field. A tap on an entry focuses it and opens the keyboard then. */
    gtk_window_set_focus(GTK_WINDOW(d), start);
    g_signal_connect(mode, "changed", G_CALLBACK(new_game_rows_cb), &rows);
    g_signal_connect(role, "changed", G_CALLBACK(new_game_rows_cb), &rows);
    g_signal_connect(who, "changed", G_CALLBACK(new_game_rows_cb), &rows);
    g_signal_connect(side, "changed", G_CALLBACK(new_game_rows_cb), &rows);
    g_signal_connect(handicap, "changed", G_CALLBACK(new_game_rows_cb), &rows);
    g_signal_connect(variant, "changed", G_CALLBACK(new_game_rows_cb), &rows);
    g_signal_connect(time_combo, "changed", G_CALLBACK(new_game_rows_cb), &rows);
    GtkWidget *online_rows[] = {rows.li_mode, rows.li_byoyomi, rows.li_increment, rows.li_periods};
    for (size_t i = 0; i < G_N_ELEMENTS(online_rows); i++)
        g_signal_connect(online_rows[i], "changed", G_CALLBACK(new_game_rows_cb), &rows);
    new_game_rows_cb(NULL, &rows);
    if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
        app.net_rule = gtk_combo_box_get_active(GTK_COMBO_BOX(legal)) | gtk_combo_box_get_active(GTK_COMBO_BOX(illegal)) << 1;
        app.level = gtk_combo_box_get_active(GTK_COMBO_BOX(level)) + 1;
        app.level_first = gtk_combo_box_get_active(GTK_COMBO_BOX(level_first)) + 1;
        app.time_index = gtk_combo_box_get_active(GTK_COMBO_BOX(time_combo));
        if (app.time_index == TIME_CUSTOM) {
            app.custom_min = (int) gtk_range_get_value(GTK_RANGE(custom_min));
            app.custom_byoyomi = (int) gtk_range_get_value(GTK_RANGE(custom_byoyomi));
            app.custom_increment = (int) gtk_range_get_value(GTK_RANGE(custom_increment));
            app.custom_tc = (TimeControl) {NULL, app.custom_min * 60, app.custom_byoyomi, app.custom_increment};
            g_key_file_set_integer(app.settings, "Settings", "CustomMin", app.custom_min);
            g_key_file_set_integer(app.settings, "Settings", "CustomByoyomi", app.custom_byoyomi);
            g_key_file_set_integer(app.settings, "Settings", "CustomIncrement", app.custom_increment);
        }
        Play play = chosen_play(&rows);
        int you = gtk_combo_box_get_active(GTK_COMBO_BOX(side));
        static const Mode MODE_OF[PLAY_COUNT] = {MODE_FIRST, MODE_TWO_PLAYERS, MODE_ENGINES,
                                                 MODE_HOST, MODE_JOIN, MODE_ONLINE, MODE_SERVER};
        Mode m = play == PLAY_ENGINE ? (Mode) (MODE_FIRST + you) : MODE_OF[play];
        app.variant = MAX(gtk_combo_box_get_active(GTK_COMBO_BOX(variant)), V_STANDARD);
        if (m == MODE_SERVER)
            app.variant = V_STANDARD;
        g_key_file_set_integer(app.settings, "Settings", "Variant", app.variant);
        g_key_file_set_integer(app.settings, "Settings", "Side", you);
        Handicap h = (Handicap) gtk_combo_box_get_active(GTK_COMBO_BOX(handicap));
        if (app.variant != V_STANDARD)
            h = HANDICAP_NONE;
        g_key_file_set_integer(app.settings, "Settings", "Mode", m);
        g_key_file_set_integer(app.settings, "Settings", "Level", app.level);
        g_key_file_set_integer(app.settings, "Settings", "LevelFirst", app.level_first);
        g_key_file_set_integer(app.settings, "Settings", "Time", app.time_index);
        g_key_file_set_integer(app.settings, "Settings", "Handicap", h);
        g_key_file_set_integer(app.settings, "Settings", "HostRule", app.net_rule);
        int opponent = gtk_combo_box_get_active(GTK_COMBO_BOX(who));
        char *name = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(user))));
        gboolean is_rated = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(rated));
        g_key_file_set_integer(app.settings, "Settings", "OnlineOpponent", opponent);
        if (opponent == ONLINE_USER)
            g_key_file_set_string(app.settings, "Settings", "LishogiUser", name);
        g_key_file_set_integer(app.settings, "Settings", "LishogiRated", is_rated);
        OnlineSetup setup;
        online_setup_of(&rows, &setup);
        if (m == MODE_ONLINE) {
            g_key_file_set_integer(app.settings, "Settings", "OnlineTimeMode", setup.time_mode);
            g_key_file_set_integer(app.settings, "Settings", "OnlineLimit", setup.limit_s);
            g_key_file_set_integer(app.settings, "Settings", "OnlineByoyomi", setup.byoyomi_s);
            g_key_file_set_integer(app.settings, "Settings", "OnlineIncrement", setup.increment_s);
            g_key_file_set_integer(app.settings, "Settings", "OnlinePeriods", setup.periods);
            g_key_file_set_integer(app.settings, "Settings", "OnlineDays", setup.days);
            g_key_file_set_integer(app.settings, "Settings", "OnlineBelow", setup.range_below);
            g_key_file_set_integer(app.settings, "Settings", "OnlineAbove", setup.range_above);
        }
        /* The CSA login, read before the dialog goes. */
        char *srv = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(server))));
        char *login_name = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(login))));
        char *pw = g_strdup(gtk_entry_get_text(GTK_ENTRY(password)));
        if (m == MODE_HOST) {
            g_free(app.room_password);
            app.room_password = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(room_pw))));
        }
        if (m == MODE_SERVER) {
            g_key_file_set_string(app.settings, "Settings", "CsaServer", srv);
            g_key_file_set_string(app.settings, "Settings", "CsaLogin", login_name);
            g_key_file_set_string(app.settings, "Settings", "CsaPassword", pw);
        }
        save_settings();
        gtk_widget_destroy(d);
        if (room) {
            app.handicap = h; /* the lobby sends what changed */
        } else if (m == MODE_SERVER) {
            server_connect(srv, login_name, pw);
        } else if (m == MODE_ONLINE) {
            online_game(&setup, name);
        } else if (m == MODE_HOST || m == MODE_JOIN) {
            network_game(h, m, NULL);
        } else {
            net_stop();
            online_stop();
            server_stop();
            start_game(h, m);
        }
        g_free(srv), g_free(login_name), g_free(pw);
        g_free(name);
        return;
    }
    gtk_widget_destroy(d);
}

void new_game_dialog(void) { game_dialog(FALSE); }

void room_settings_dialog(void) { game_dialog(TRUE); }

/* A saved game with the same text: the same game in the same position. */
static char *same_save(const char *path)
{
    char *text, *found = NULL;
    gsize len;
    if (!g_file_get_contents(path, &text, &len, NULL))
        return NULL;
    const char *ext = strrchr(path, '.'), *name;
    GDir *dir = g_dir_open(app.saves, 0, NULL);
    while (dir && !found && (name = g_dir_read_name(dir))) {
        char *other = g_build_filename(app.saves, name, NULL), *t;
        gsize n;
        GStatBuf st; /* read only a file of the same size */
        if (strcmp(other, path) && g_str_has_suffix(name, ext) && g_stat(other, &st) == 0 && (gsize) st.st_size == len &&
            g_file_get_contents(other, &t, &n, NULL)) {
            if (n == len && !memcmp(t, text, n))
                found = g_strdup(other);
            g_free(t);
        }
        g_free(other);
    }
    if (dir)
        g_dir_close(dir);
    g_free(text);
    return found;
}

static void save_as(gboolean csa)
{
    char name[64];
    time_t t = time(NULL);
    strftime(name, sizeof name, csa ? "%Y%m%d-%H%M%S." RECORD_ALT_EXT : "%Y%m%d-%H%M%S." RECORD_EXT, localtime(&t));
    char *path = g_build_filename(app.saves, name, NULL);
    gboolean ok = record_save(path, csa);
    char *same = ok ? same_save(path) : NULL;
    if (same)
        g_unlink(path);
    char *shown = NULL;
#ifdef __ANDROID__
    if (ok && !same) { /* the app's folder is private: the game is also copied where other apps see it */
        android_export(path);
        shown = g_strdup_printf("Documents/Denboku/%s", name);
    }
#endif
    char *text = g_strdup_printf("%s\n%s", same ? tr("This game is already saved:") : ok ? tr("Game saved:") : tr("Could not save the game:"),
                                 same ? same : shown ? shown : path);
    g_free(shown);
    if (ok) /* news: a passing notice; a failure asks to be read */
        toast(text);
    else
        message(text);
    g_free(text);
    g_free(same);
    g_free(path);
}

void save_game(void) { save_as(FALSE); }

void save_csa(void) { save_as(TRUE); }

gint compare_names_asc(gconstpointer a, gconstpointer b)
{
    return strcmp(*(char *const *) a, *(char *const *) b);
}

static gint compare_names_desc(gconstpointer a, gconstpointer b)
{
    return -strcmp(*(char *const *) a, *(char *const *) b);
}
/* The history list's lines for a game: when, the variant, the players, the result and length. */
static char *history_summary(const HistEntry *e, Game *g)
{
    char date[32];
    time_t t = (time_t) e->started;
    strftime(date, sizeof date, "%Y-%m-%d %H:%M", localtime(&t));
    char moves[32];
    snprintf(moves, sizeof moves, tr("%d moves"), e->count);
    g->result = e->result;
    g->reason = e->reason;
    return g_strdup_printf("%s · %s\n%s %s – %s %s\n%s · %s", date, variant_label(e->variant), SIDE_SYMBOLS[FIRST], e->names[FIRST], SIDE_SYMBOLS[SECOND],
                           e->names[SECOND], g->result == RESULT_NONE ? tr("Unfinished") : game_result_text(g, NULL),
                           moves);
}

/* A row of the list held down: asks to delete its game. */
static gboolean row_press_cb(GtkWidget *w, GdkEventButton *e, gpointer data)
{
    (void) data;
    g_object_set_data(G_OBJECT(w), "pressed", GUINT_TO_POINTER(e->time));
    return FALSE;
}

static void row_clicked_cb(GtkButton *b, gpointer d)
{
    guint32 pressed = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(b), "pressed"));
    GdkEvent *e = gtk_get_current_event();
    /* Held: a press of the pointer kept down, not Enter or a controller later on the same row. */
    gboolean held = pressed && e && e->type == GDK_BUTTON_RELEASE && gtk_get_current_event_time() - pressed > 600;
    if (e)
        gdk_event_free(e);
    g_object_set_data(G_OBJECT(b), "pressed", NULL);
    g_object_set_data(G_OBJECT(d), "row", b);
    gtk_dialog_response(GTK_DIALOG(d), held ? RESPONSE_DELETE : GTK_RESPONSE_OK);
}

/* Saved games, or the history of games played (`summaries`: each game's date, players and result
 * instead of its file name), newest first, a row each: a tap opens it, holding it deletes it. */
static void game_list_dialog(const char *folder, const char *title, const char *none, const char *question,
                             gboolean summaries)
{
    if (!give_up_online_game())
        return;
    GPtrArray *files = summaries ? history_games() : record_files(folder);
    if (!summaries)
        g_ptr_array_sort(files, compare_names_desc);
    Game *result = summaries ? g_new0(Game, 1) : NULL; /* what game_result_text() reads of a game */
    GtkWidget *d = dialog_new(tr(title));
    GtkWidget *hint = row_text(tr("Tap to open; hold to delete"), TRUE, FALSE, TRUE);
    gtk_misc_set_padding(GTK_MISC(hint), 6, 2);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), hint, FALSE, FALSE, 0);
    GtkWidget *card = rows_card(GTK_DIALOG(d)->vbox);
    int shown = 0;
    for (guint i = 0; i < files->len; i++) {
        const HistEntry *e = summaries ? g_ptr_array_index(files, i) : NULL;
        const char *file = e ? e->file : g_ptr_array_index(files, i);
        char *text = e ? history_summary(e, result) : g_strdup(file);
        GtkWidget *lines = gtk_vbox_new(FALSE, 2);
        char **parts = g_strsplit(text, "\n", 3);
        for (int k = 0; parts[k]; k++) { /* long names end in "…": the drawer as wide as the others */
            GtkWidget *l = row_text(parts[k], k > 0, k == 0, k > 0);
            gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
            gtk_box_pack_start(GTK_BOX(lines), l, FALSE, FALSE, 0);
        }
        g_strfreev(parts);
        GtkWidget *b = gtk_button_new();
        gtk_widget_set_name(b, "denboku-flatrow");
        gtk_container_add(GTK_CONTAINER(b), lines);
        g_object_set_data_full(G_OBJECT(b), "file", g_strdup(file), g_free);
        g_object_set_data_full(G_OBJECT(b), "text", text, g_free);
        g_signal_connect(b, "button-press-event", G_CALLBACK(row_press_cb), NULL);
        g_signal_connect(b, "clicked", G_CALLBACK(row_clicked_cb), d);
        row_add(card, b);
        shown++;
    }
    g_ptr_array_free(files, TRUE);
    g_free(result);
    if (!shown) {
        gtk_widget_destroy(d);
        toast(tr(none));
        return;
    }
    const char *clear = summaries ? "Clear History" : "Delete All";
    gtk_dialog_add_button(GTK_DIALOG(d), tr(clear), RESPONSE_CLEAR);
    gtk_widget_show_all(d);
    int response;
    while ((response = gtk_dialog_run(GTK_DIALOG(d))) == RESPONSE_DELETE || response == RESPONSE_CLEAR) {
        GList *rows = gtk_container_get_children(GTK_CONTAINER(card));
        if (response == RESPONSE_CLEAR) {
            if (confirm_with(tr(summaries ? "Delete every game in the history? This cannot be undone."
                                          : "Delete every saved game? This cannot be undone."),
                             tr(clear)))
                for (GList *l = rows; l; l = l->next) {
                    char *path = g_build_filename(folder, g_object_get_data(l->data, "file"), NULL);
                    if (g_remove(path) == 0)
                        gtk_widget_destroy(l->data);
                    g_free(path);
                }
        } else {
            GtkWidget *row = g_object_get_data(G_OBJECT(d), "row");
            char *ask = g_strdup_printf("%s\n%s", tr(question), (char *) g_object_get_data(G_OBJECT(row), "text"));
            if (confirm(ask)) {
                char *path = g_build_filename(folder, g_object_get_data(G_OBJECT(row), "file"), NULL);
                if (g_remove(path) == 0)
                    gtk_widget_destroy(row);
                g_free(path);
            }
            g_free(ask);
        }
        g_list_free(rows);
        rows = gtk_container_get_children(GTK_CONTAINER(card));
        gboolean empty = rows == NULL;
        g_list_free(rows);
        if (empty)
            break;
    }
    if (response == GTK_RESPONSE_OK) {
        GtkWidget *row = g_object_get_data(G_OBJECT(d), "row");
        const char *file = g_object_get_data(G_OBJECT(row), "file");
        char *path = g_build_filename(folder, file, NULL);
        FILE *f = g_fopen(path, "r");
        Game g;
        gboolean csa = record_kind(file) == 2;
        if (f && (csa ? record_read_alt(&g, f) : record_read(&g, f))) {
            gtk_widget_destroy(d);
            d = NULL;
            use_loaded_game(&g);
        } else {
            rules_set_variant(app.variant); /* a failed read may have changed it */
            message(tr("Could not read this game."));
        }
        if (f)
            fclose(f);
        g_free(path);
    }
    if (d)
        gtk_widget_destroy(d);
}

void load_game_dialog(void)
{
    game_list_dialog(app.saves, "Load Game", "No saved games.", "Delete this saved game?", FALSE);
}

void history_dialog(void)
{
    game_list_dialog(app.history, "Game History", "No games played yet.", "Delete this game from the history?",
                     TRUE);
}

static void go_to_move_cb(GtkButton *b, gpointer ply)
{
    gtk_dialog_response(GTK_DIALOG(g_object_get_data(G_OBJECT(b), "dialog")), GPOINTER_TO_INT(ply));
}

/* The moves, a row each (a tap goes to that position), and the result. */
static void add_move_rows(GtkWidget *d)
{
    GtkWidget *card = rows_card(GTK_DIALOG(d)->vbox);
    Position p = app.game.start;
    for (int i = 0; i < app.game.count; i++) {
        char mv[64], row[96];
        Move m = app.game.moves[i];
        if (m.piece && COLOR(m.piece) != p.side) /* a lesson's step where the other side passed */
            p.side = COLOR(m.piece);
        move_notation(&p, &m, i ? app.game.moves[i - 1].to : -1, notation_ja(), mv, sizeof mv);
        snprintf(row, sizeof row, "%3d  %s", i + 1, mv);
        game_replay(&p, &m);
        GtkWidget *b = row_add(card, gtk_button_new());
        gtk_widget_set_name(b, "denboku-flatrow");
        gtk_container_add(GTK_CONTAINER(b), row_text(row, FALSE, FALSE, FALSE));
        g_object_set_data(G_OBJECT(b), "dialog", d);
        g_signal_connect(b, "clicked", G_CALLBACK(go_to_move_cb), GINT_TO_POINTER(i + 1));
    }
    if (app.game.result != RESULT_NONE) {
        GtkWidget *r = row_add(card, row_text(result_text(), TRUE, TRUE, FALSE));
        gtk_misc_set_padding(GTK_MISC(r), 10, 8);
    }
}

static void run_moves(GtkWidget *d)
{
    gtk_widget_show_all(d);
    int r = gtk_dialog_run(GTK_DIALOG(d));
    if (r > 0) {
        view_set_ply(app.view, MIN(r, app.game.count));
        update_move_label();
    }
    gtk_widget_destroy(d);
}

static void info_dialog(void)
{
    GtkWidget *d = dialog_new(tr("Game"));
    char *head = g_strdup_printf("%s %s\n%s %s\n%s: %s", SIDE_SYMBOLS[FIRST], app.game.names[FIRST], SIDE_SYMBOLS[SECOND], app.game.names[SECOND], tr("Handicap"),
                                 handicap_label(app.game.handicap));
    GtkWidget *label = gtk_label_new(head);
    g_free(head);
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), label, FALSE, FALSE, 6);
    int ply = view_ply(app.view);
    char style[256];
    game_style(&app.game, ply < 0 ? app.game.count : ply, style, sizeof style);
    if (*style) {
        GtkWidget *s = gtk_label_new(style);
        gtk_misc_set_alignment(GTK_MISC(s), 0, 0.5);
        gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), s, FALSE, FALSE, 6);
    }
    /* The shown position as SFEN, in a field that can be selected and copied. */
    Position shown_pos;
    game_position_at(&app.game, ply < 0 ? app.game.count : ply, &shown_pos);
    char sfen[128];
    pos_to_text(&shown_pos, sfen, sizeof sfen);
    GtkWidget *entry = text_entry();
    gtk_entry_set_text(GTK_ENTRY(entry), sfen);
    gtk_editable_set_editable(GTK_EDITABLE(entry), FALSE);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), entry, FALSE, FALSE, 6);
    add_move_rows(d);
    run_moves(d);
}

/* The moves alone, from ⌄ beside a single row of them. */
void moves_dialog(void)
{
    GtkWidget *d = dialog_new(tr("Moves"));
    add_move_rows(d);
    run_moves(d);
}

/* A page of text over the one that opened it (‹ goes back), with `extra` (or NULL) below it. */
static int text_page(const char *title, const char *markup, GtkWidget *extra)
{
    GtkWidget *d = dialog_new(title);
    drawer_set_back(d);
    GtkWidget *label = row_para(NULL);
    gtk_label_set_markup(GTK_LABEL(label), markup);
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0);
    gtk_misc_set_padding(GTK_MISC(label), 6, 6);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), label, FALSE, FALSE, 0);
    if (extra)
        gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), extra, FALSE, FALSE, 0);
    gtk_widget_show_all(d);
    int r = gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
    return r;
}

static void section_row_cb(GtkButton *b, gpointer i)
{
    gtk_dialog_response(GTK_DIALOG(g_object_get_data(G_OBJECT(b), "dialog")), 100 + GPOINTER_TO_INT(i));
}

/* A variant's rules, its name in bold ("Minishogi: …"). */
static char *rules_markup(int v)
{
    char *text = g_markup_escape_text(tr(VARIANT_RULES[v]), -1), *colon = strstr(text, "：");
    if (!colon)
        colon = strchr(text, ':');
    int name = colon ? (int) (colon - text) : 0;
    char *markup = g_strdup_printf("<b>%.*s</b>%s", name, text, text + name);
    g_free(text);
    return markup;
}

static void rules_chip_cb(int v, gpointer data)
{
    GtkWidget *d = data;
    char *markup = rules_markup(v);
    gtk_label_set_markup(GTK_LABEL(g_object_get_data(G_OBJECT(d), "rules")), markup);
    g_free(markup);
    drawer_set_title(d, tr(VARIANT_LABELS[v]));
}

/* The variants' rules, from New Game's Rules and How to Play: a chip per variant, as in New Game,
 * the one shown named in the title. */
static int variant_rules_page(int variant)
{
    GtkWidget *d = dialog_new(tr(VARIANT_LABELS[variant]));
    drawer_set_back(d);
    const char *names[V_COUNT];
    for (int v = 0; v < V_COUNT; v++)
        names[v] = tr(VARIANT_LABELS[v]);
    GtkWidget *label = row_para(NULL);
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0);
    gtk_misc_set_padding(GTK_MISC(label), 6, 6);
    g_object_set_data(G_OBJECT(d), "rules", label);
    GtkWidget *chips = row_chips(NULL, names, V_COUNT, variant, rows_width(), rules_chip_cb, d);
    g_object_set_data(G_OBJECT(d), "step-chips", chips);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), chips, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), label, FALSE, FALSE, 0);
    rules_chip_cb(variant, d);
    gtk_widget_show_all(d);
    int r = gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
    return r;
}

static void open_custom_cb(GtkButton *b, gpointer data)
{
    (void) b, (void) data;
    open_url(app.custom);
}

/* Your own pieces and sounds: where the custom folder is on this device, then how a set is made. */
static int custom_page(void)
{
    const char *android = strstr(app.custom, "/Android/data/");
    char *where = app.kindle && g_str_has_prefix(app.custom, "/mnt/us/")
                      ? g_strdup_printf(tr("On the Kindle's drive (connect it to a computer by USB): %s"), app.custom + strlen("/mnt/us/"))
                  : android ? g_strdup_printf(tr("In the phone's storage (connect it to a computer by USB): %s"), android + 1)
                            : g_strdup_printf(tr("In this folder: %s"), app.custom);
    char *esc = g_markup_escape_text(where, -1), *markup = g_strconcat(esc, "\n\n", tr(HOW_TO_CUSTOM_EN), NULL);
    GtkWidget *open = NULL;
    if (!app.kindle && !android) {
        GtkWidget *b = gtk_button_new_with_label(tr("Open Folder"));
        g_signal_connect(b, "clicked", G_CALLBACK(open_custom_cb), NULL);
        open = gtk_hbox_new(FALSE, 0);
        gtk_container_set_border_width(GTK_CONTAINER(open), 6);
        gtk_box_pack_start(GTK_BOX(open), b, FALSE, FALSE, 0);
    }
    int r = text_page(tr("Your pieces and sounds"), markup, open);
    g_free(where), g_free(esc), g_free(markup);
    return r;
}

/* How to Play: the rules, the variants, the app and your own sets, each a page of its own. */
void how_to_play(void)
{
    static const char *const TITLES[] = {"Rules", "Variants", "Using the app", "Your pieces and sounds"};
    static const char *const ICONS[] = {"王", "◇", "☰", "＋"};
    for (int left = -1;;) { /* the page just left: the cursor goes back to its row */
        GtkWidget *d = dialog_new(tr("How to Play")), *back_to = NULL;
        GtkWidget *card = rows_card(GTK_DIALOG(d)->vbox);
        for (int i = 0; i < (int) G_N_ELEMENTS(TITLES); i++) {
            GtkWidget *b = row_add(card, row_section(ICONS[i], tr(TITLES[i]), ""));
            g_object_set_data(G_OBJECT(b), "dialog", d);
            g_signal_connect(b, "clicked", G_CALLBACK(section_row_cb), GINT_TO_POINTER(i));
            if (i == left)
                back_to = b;
        }
        gtk_widget_show_all(d);
        if (back_to)
            gtk_widget_grab_focus(back_to);
        int r = gtk_dialog_run(GTK_DIALOG(d));
        gtk_widget_destroy(d);
        if (r < 100)
            break;
        left = r - 100;
        r = r == 101   ? variant_rules_page(app.variant != V_STANDARD ? app.variant : V_STANDARD + 1)
            : r == 103 ? custom_page()
                       : text_page(tr(TITLES[r - 100]), tr(r == 100 ? HOW_TO_RULES_EN : HOW_TO_APP_EN), NULL);
        if (r == GTK_RESPONSE_DELETE_EVENT)
            break;
    }
}

void about_dialog(void)
{
    GtkWidget *d = dialog_new(tr("About"));
    /* The image and the text share this width, so the dialog fits the Kindle screen: its font is
     * large and unwrapped lines would make the window wider than the display. */
    int maxw = rows_width() - 20;
    char *png = g_build_filename(app.base, "art", "banner.png", NULL);
    GdkPixbuf *pb = gdk_pixbuf_new_from_file(png, NULL);
    g_free(png);
    if (pb) {
        int w = gdk_pixbuf_get_width(pb), h = gdk_pixbuf_get_height(pb);
        if (w > maxw) {
            h = MAX(1, h * maxw / w);
            w = maxw;
        }
        GdkPixbuf *scaled = gdk_pixbuf_scale_simple(pb, w, h, GDK_INTERP_BILINEAR);
        gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), gtk_image_new_from_pixbuf(scaled), FALSE, FALSE, 6);
        g_object_unref(scaled);
        g_object_unref(pb);
    }
    GtkWidget *label = gtk_label_new(tr(ABOUT_CREDITS_EN));
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
    gtk_misc_set_alignment(GTK_MISC(label), 0.5, 0.5);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), label, FALSE, FALSE, 6);
    /* As wide as its longest line once wrapped: GTK 2 places a wrapped label by its widest line
     * but centers the lines in the width asked for, so a wider one left them off to one side. */
    gtk_widget_ensure_style(label);
    PangoLayout *pl = gtk_widget_create_pango_layout(label, tr(ABOUT_CREDITS_EN));
    pango_layout_set_width(pl, maxw * PANGO_SCALE);
    pango_layout_set_wrap(pl, PANGO_WRAP_WORD_CHAR);
    int textw;
    pango_layout_get_pixel_size(pl, &textw, NULL);
    g_object_unref(pl);
    gtk_widget_set_size_request(label, textw, -1);
    char *build = updater_version_text();
    GtkWidget *number = row_text(build, TRUE, FALSE, TRUE);
    gtk_misc_set_alignment(GTK_MISC(number), 0.5, 0.5);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), number, FALSE, FALSE, 6);
    g_free(build);
    GtkWidget *repo = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(repo), link_button(REPO_HOST, REPO_URL), TRUE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), repo, FALSE, FALSE, 0);
    updater_about(d, GTK_DIALOG(d)->vbox);
    run_dialog(d);
}

/* Back to how the game was installed: the settings, the game in progress and the history go;
 * saved games are the player's files and stay. TRUE when it was done. */
gboolean reset_everything(void)
{
    if (!confirm_with(tr("Reset the settings and the game history? The game in progress and saved games are kept."), tr("Reset")))
        return FALSE;
    /* The game in progress stays as it is: its group, and the choices it was started with. */
    static const char *const GAME_KEYS[] = {"Variant", "Level", "LevelFirst", "Time", "CustomMin", "CustomByoyomi", "CustomIncrement"};
    GKeyFile *kept = g_key_file_new();
    char **keys = g_key_file_get_keys(app.settings, "Game", NULL, NULL);
    for (int i = 0; keys && keys[i]; i++) {
        char *v = g_key_file_get_value(app.settings, "Game", keys[i], NULL);
        g_key_file_set_value(kept, "Game", keys[i], v);
        g_free(v);
    }
    g_strfreev(keys);
    for (guint i = 0; i < G_N_ELEMENTS(GAME_KEYS); i++) {
        char *v = g_key_file_get_value(app.settings, "Settings", GAME_KEYS[i], NULL);
        if (v)
            g_key_file_set_value(kept, "Settings", GAME_KEYS[i], v);
        g_free(v);
    }
    g_key_file_free(app.settings);
    app.settings = kept;
    save_settings();
    GDir *dir = g_dir_open(app.history, 0, NULL);
    for (const char *name; dir && (name = g_dir_read_name(dir));) {
        char *path = g_build_filename(app.history, name, NULL);
        g_remove(path);
        g_free(path);
    }
    if (dir)
        g_dir_close(dir);
    apply_language();
    apply_orientation(FALSE);
    apply_settings();
    relabel();
    return TRUE;
}

/* ---- actions and the menu ---- */

/* The undone moves, the last one on top, while the position is still the one they were undone to. */
static Move redo_moves[MAX_PLY];
static int redo_count;
static uint64_t redo_hash;

void forget_redo(void) { redo_count = 0; }

void undo_cb(GtkWidget *w, gpointer data)
{
    (void) w, (void) data;
    if (lesson_playing()) {
        tutorial_again();
        return;
    }
    if (replaying()) {
        replay_next_speed();
        return;
    }
    if (server_game() || edit_undo())
        return;
    if (app.net || app.net_suspended) {
        /* Online: an undo is a takeback the other player has to accept; with theirs asked, it accepts it. */
        if (app.takeback_theirs && app.game.result == RESULT_NONE && !app.net_suspended) {
            takeback_answer(TRUE);
            return;
        }
        if (app.net_suspended || app.takeback_mine || app.game.result != RESULT_NONE || app.game.count == 0 ||
            app.game.count == app.takeback_refused_mine)
            return;
        app.takeback_mine = TRUE;
        net_send(app.net, "TAKEBACK");
        app.can_undo = FALSE;
        view_queue_bar(app.view);
        set_move_text(tr("Takeback requested…"));
        return;
    }
    if (!app.game.count)
        return;
    view_clear_premove(app.view);
    if (app.engine)
        engine_cancel(app.engine);
    app.engine_thinking = app.hint_pending = FALSE;
    view_set_ply(app.view, -1);
    gboolean vs_engine = app.players[FIRST] != app.players[SECOND];
    if (app.game.pos.hash != redo_hash)
        redo_count = 0;
    do {
        game_undo(&app.game);
        redo_moves[redo_count++] = app.game.moves[app.game.count];
    } while (vs_engine && app.game.count > 0 && app.players[app.game.pos.side] == PLAYER_ENGINE);
    redo_hash = app.game.pos.hash;
    view_set_hint(app.view, NULL);
    next_turn();
}

/* Redo: the sandbox and other games between two players here. */
void redo(void)
{
    if (!redo_count || app.game.pos.hash != redo_hash || app.net || server_game() ||
        app.players[FIRST] != PLAYER_HUMAN || app.players[SECOND] != PLAYER_HUMAN)
        return;
    view_set_ply(app.view, -1);
    Move m = redo_moves[--redo_count];
    play_move(&m);
    redo_hash = app.game.pos.hash;
}

void hint_cb(GtkWidget *w, gpointer data)
{
    (void) w, (void) data;
    if (lesson_playing()) {
        tutorial_hint();
        return;
    }
    if (replaying()) {
        replay_pause_toggle();
        return;
    }
    if (engines_game()) {
        pause_toggle();
        return;
    }
    if (app.game.result != RESULT_NONE || app.engine_thinking)
        return;
    engine_search(TRUE);
}

void history_step(int step)
{
    if (replaying() && !replay_paused()) /* stepping pauses it, to go on from there */
        replay_pause_toggle();
    int ply = view_ply(app.view);
    if (ply < 0)
        ply = app.game.count;
    ply = CLAMP(ply + step, 0, app.game.count);
    view_set_ply(app.view, ply);
    update_move_label();
}

static void resign_now(void)
{
    int side = app.game.pos.side;
    if (app.players[side] != PLAYER_HUMAN)
        side = !side;
    if (app.net)
        net_send(app.net, "%TORYO");
    if (app.mode == MODE_SERVER)
        server_resign();
    online_resign();
    game_end(&app.game, side == FIRST ? RESULT_SECOND_WIN : RESULT_FIRST_WIN, REASON_RESIGN);
    next_turn();
}

void resign(void)
{
    if (app.game.result == RESULT_NONE && confirm_with(tr("Resign the game?"), tr("Resign")))
        resign_now();
}

/* Another game in the middle of an online one only after resigning it (lishogi sends a player
 * back to the game in progress; GNOME Chess asks before leaving an unfinished game). */
static gboolean in_online_game(void)
{
    return app.game.result == RESULT_NONE && (app.net || app.net_suspended || server_game());
}

static gboolean give_up_online_game(void)
{
    if (!in_online_game())
        return TRUE;
    if (!confirm_with(tr("You are in an online game. To start another one, resign this one first."), tr("Resign")))
        return FALSE;
    resign_now();
    skip_game_over();
    return TRUE;
}

void declare(void)
{
    if (app.players[app.game.pos.side] != PLAYER_HUMAN || app.net || app.net_suspended || server_game())
        return;
    if (game_declare(&app.game))
        next_turn();
    else
        toast(tr(DECLARE_RULE_TEXT));
}

static void flip(void)
{
    view_set_flipped(app.view, !view_flipped(app.view));
}

/* Quitting asks first. In an online game resigning is up to the player (the app may only need a
 * restart, and the game can be joined again); any other game is kept for next time. */
void quit(void)
{
    static gboolean asking;
    if (asking) /* the window's close button again: no need to answer */
        goto out;
    if (!in_online_game()) {
        gboolean playing = app.game.result == RESULT_NONE && app.game.count > 0;
        GtkWidget *d = dialog_new(tr("Quit"));
        gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox),
                           row_para(tr(playing ? "Quit Denboku Shōgi? The game in progress is kept for next time." : "Quit Denboku Shōgi?")),
                           TRUE, TRUE, 12);
        gtk_dialog_add_button(GTK_DIALOG(d), tr("Cancel"), GTK_RESPONSE_CANCEL);
        gtk_dialog_add_button(GTK_DIALOG(d), tr("Quit"), GTK_RESPONSE_OK);
        gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
        asking = TRUE;
        int r = run_dialog(d);
        asking = FALSE;
        if (r != GTK_RESPONSE_OK)
            return;
    } else {
        GtkWidget *d = dialog_new(tr("Quit"));
        gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), row_para(tr("You are in an online game. Quit anyway?")), TRUE, TRUE, 12);
        gtk_dialog_add_button(GTK_DIALOG(d), tr("Cancel"), GTK_RESPONSE_CANCEL);
        gtk_dialog_add_button(GTK_DIALOG(d), tr("Resign and Quit"), GTK_RESPONSE_REJECT);
        gtk_dialog_add_button(GTK_DIALOG(d), tr("Quit"), GTK_RESPONSE_OK);
        int r = run_dialog(d);
        if (r != GTK_RESPONSE_OK && r != GTK_RESPONSE_REJECT)
            return;
        if (r == GTK_RESPONSE_REJECT) {
            resign_now();
            skip_game_over();
            online_flush();
        }
    }
out:
    net_stop();
    apply_orientation(TRUE);
    autosave();
    /* Not gtk_main_quit(): a page or a dialog open runs a loop of its own (gtk_dialog_run()), and
     * the program stayed open until it was closed. */
    engine_free(app.engine);
    exit(0);
}

static void replay_fast(void) { replay_start(REPLAY_FAST); }
static void replay_slow(void) { replay_start(REPLAY_SLOW); }
static void replay_realtime(void) { replay_start(REPLAY_REALTIME); }

/* The menu, a sheet of tiles (sheet.c): `pc` entries only on computers (the Kindle has no
 * clipboard to share), `optional` ones only while they can be used; `sub` opens another page. */
typedef struct MenuEntry {
    const char *label;
    void (*fn)(void);
    gboolean pc, optional;
    const struct MenuEntry *sub;
} MenuEntry;

static const MenuEntry GAMES_MENU[] = {
    {"Save Game", save_game, FALSE, FALSE, NULL}, {RECORD_SAVE_ALT_LABEL, save_csa, FALSE, FALSE, NULL},
    {"Load Game…", load_game_dialog, FALSE, FALSE, NULL}, {"Game History…", history_dialog, FALSE, FALSE, NULL},
    {RECORD_COPY_LABEL, copy_game, TRUE, FALSE, NULL}, {"Paste Game", paste_game, TRUE, FALSE, NULL},
    {ONLINE_OPEN_LABEL, online_open_game, FALSE, FALSE, NULL}, {NULL, NULL, FALSE, FALSE, NULL},
};

static const MenuEntry REPLAY_MENU[] = {
    {"Fast", replay_fast, FALSE, FALSE, NULL}, {"Slow", replay_slow, FALSE, FALSE, NULL},
    {"Real time", replay_realtime, FALSE, FALSE, NULL}, {"Stop", replay_stop, FALSE, FALSE, NULL},
    {NULL, NULL, FALSE, FALSE, NULL},
};

static void open_tutorial(void)
{
    if (give_up_online_game())
        tutorial_dialog();
}

static const MenuEntry HELP_MENU[] = {
    {"Tutorial", open_tutorial, FALSE, FALSE, NULL}, {"How to Play", how_to_play, FALSE, FALSE, NULL}, {"About", about_dialog, FALSE, FALSE, NULL},
    {NULL, NULL, FALSE, FALSE, NULL},
};

static void open_problems(void)
{
    if (give_up_online_game())
        problems_dialog();
}

static const MenuEntry MENU[] = {
    {"New Game…", new_game_dialog, FALSE, FALSE, NULL}, {"Sandbox", set_up_position, FALSE, FALSE, NULL},
    {"Mate problems…", open_problems, FALSE, FALSE, NULL}, {"Solution", problem_solution, FALSE, TRUE, NULL},
    {"Games", NULL, FALSE, FALSE, GAMES_MENU}, {"Analyze Game", analysis_dialog, FALSE, FALSE, NULL},
    {"Replay", NULL, FALSE, FALSE, REPLAY_MENU}, {"Flip Board", flip, FALSE, FALSE, NULL},
    {"Result", show_result, FALSE, TRUE, NULL}, {"Resign", resign, FALSE, FALSE, NULL}, {"Declare Impasse", declare, FALSE, TRUE, NULL},
    {"Preferences…", preferences_dialog, FALSE, FALSE, NULL}, {"Help", NULL, FALSE, FALSE, HELP_MENU},
    {"Quit", quit, FALSE, FALSE, NULL}, {NULL, NULL, FALSE, FALSE, NULL},
};

/* Replay needs moves; real time, the times they took; Stop, a replay going on. */
static gboolean entry_enabled(const MenuEntry *e)
{
    gboolean over = app.game.result != RESULT_NONE;
    return !((e->sub == REPLAY_MENU && !app.game.count) || (e->fn == replay_realtime && !replay_has_times()) ||
             (e->fn == replay_stop && !replaying()) || (e->fn == resign && over) || (e->fn == show_result && (!over || app.over_dialog)) ||
             (e->fn == analysis_dialog && !analysis_possible()) || (e->fn == problem_solution && !problem_playing()) ||
             /* Not over the network: the other side (a LAN player, lishogi, a CSA server) would not hear of it;
              * nor in a mate problem or a lesson, which have a goal of their own. */
             (e->fn == declare && (over || app.players[app.game.pos.side] != PLAYER_HUMAN || !game_has_declare() ||
                                   app.net || app.net_suspended || server_game() || problem_playing() || lesson_playing())));
}

/* Each entry's icon, drawn: the Kindle's and Windows' fonts lack most symbols. */
static void draw_icon(cairo_t *cr, const MenuEntry *e, double x, double y, double s)
{
    double cx = x + s / 2, cy = y + s / 2, r = s * 0.36;
    cairo_set_line_width(cr, MAX(1.5, s * 0.08));
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    void (*f)(void) = e->fn;
    if (f == new_game_dialog) {
        cairo_move_to(cr, cx - r, cy), cairo_line_to(cr, cx + r, cy);
        cairo_move_to(cr, cx, cy - r), cairo_line_to(cr, cx, cy + r);
    } else if (f == open_problems) { /* a koma with a check mark: the mate */
        cairo_move_to(cr, cx, cy - r * 1.15), cairo_line_to(cr, cx + r * 0.8, cy - r * 0.55), cairo_line_to(cr, cx + r, cy + r * 1.1);
        cairo_line_to(cr, cx - r, cy + r * 1.1), cairo_line_to(cr, cx - r * 0.8, cy - r * 0.55), cairo_close_path(cr);
        cairo_move_to(cr, cx - r * 0.4, cy + r * 0.2), cairo_line_to(cr, cx - r * 0.08, cy + r * 0.55), cairo_line_to(cr, cx + r * 0.45, cy - r * 0.3);
    } else if (f == problem_solution) { /* a line of moves: a path with an arrowhead */
        cairo_move_to(cr, cx - r, cy + r * 0.6), cairo_line_to(cr, cx - r * 0.2, cy - r * 0.4), cairo_line_to(cr, cx + r * 0.2, cy + r * 0.2),
            cairo_line_to(cr, cx + r, cy - r * 0.7);
        cairo_move_to(cr, cx + r * 0.45, cy - r * 0.75), cairo_line_to(cr, cx + r, cy - r * 0.7), cairo_line_to(cr, cx + r * 0.9, cy - r * 0.15);
    } else if (f == set_up_position) { /* a pencil */
        cairo_save(cr);
        cairo_translate(cr, cx, cy);
        cairo_rotate(cr, -G_PI / 4);
        cairo_rectangle(cr, -r * 0.7, -r * 0.22, r * 1.3, r * 0.44);
        cairo_move_to(cr, -r * 0.7, -r * 0.22), cairo_line_to(cr, -r * 1.05, 0), cairo_line_to(cr, -r * 0.7, r * 0.22);
        cairo_restore(cr);
    } else if (f == save_game || f == save_csa || f == load_game_dialog) { /* an arrow into or out of a tray */
        gboolean up = f == load_game_dialog;
        cairo_move_to(cr, cx - r, cy + r * 0.3), cairo_line_to(cr, cx - r, cy + r), cairo_line_to(cr, cx + r, cy + r),
            cairo_line_to(cr, cx + r, cy + r * 0.3);
        double top = cy - r, tip = up ? top : cy + r * 0.45, base = up ? cy + r * 0.45 : top;
        cairo_move_to(cr, cx, base), cairo_line_to(cr, cx, tip);
        double d = up ? r * 0.4 : -r * 0.4;
        cairo_move_to(cr, cx - r * 0.4, tip + d), cairo_line_to(cr, cx, tip), cairo_line_to(cr, cx + r * 0.4, tip + d);
        if (f == save_csa) /* two arrows: the other format */
            cairo_move_to(cr, cx + r * 0.6, cy - r * 0.2), cairo_line_to(cr, cx + r * 0.6, cy + r * 0.45);
    } else if (f == copy_game || f == paste_game) {
        if (f == copy_game) {
            cairo_rectangle(cr, cx - r, cy - r, r * 1.3, r * 1.3);
            cairo_rectangle(cr, cx - r * 0.3, cy - r * 0.3, r * 1.3, r * 1.3);
        } else { /* a clipboard */
            cairo_rectangle(cr, cx - r * 0.8, cy - r * 0.8, r * 1.6, r * 1.8);
            cairo_rectangle(cr, cx - r * 0.35, cy - r * 1.0, r * 0.7, r * 0.4);
        }
    } else if (f == history_dialog) {
        for (int i = 0; i < 3; i++) {
            double ly = cy - r * 0.7 + i * r * 0.7;
            cairo_move_to(cr, cx - r, ly), cairo_line_to(cr, cx - r * 0.85, ly);
            cairo_move_to(cr, cx - r * 0.45, ly), cairo_line_to(cr, cx + r, ly);
        }
    } else if (f == online_open_game) { /* a globe */
        cairo_new_sub_path(cr), cairo_arc(cr, cx, cy, r, 0, 2 * G_PI);
        cairo_save(cr), cairo_translate(cr, cx, cy), cairo_scale(cr, 0.45, 1);
        cairo_new_sub_path(cr), cairo_arc(cr, 0, 0, r, 0, 2 * G_PI), cairo_restore(cr);
        cairo_move_to(cr, cx - r, cy), cairo_line_to(cr, cx + r, cy);
    } else if (f == analysis_dialog) { /* bars */
        for (int i = 0; i < 3; i++) {
            double bx = cx - r + i * r * 0.75, h = r * (0.8 + 0.6 * ((i + 1) % 3));
            cairo_rectangle(cr, bx, cy + r - h, r * 0.45, h);
        }
        cairo_fill(cr);
    } else if (e->sub == REPLAY_MENU || f == replay_slow || f == replay_fast) { /* one or two triangles */
        int n = f == replay_fast ? 2 : 1;
        for (int k = 0; k < n; k++) {
            double ox = n == 2 ? (k - 0.5) * r * 0.9 : 0, w = n == 2 ? 0.55 : 1;
            cairo_move_to(cr, cx + ox - r * 0.75 * w, cy - r), cairo_line_to(cr, cx + ox + r * 0.75 * w, cy),
                cairo_line_to(cr, cx + ox - r * 0.75 * w, cy + r);
            cairo_close_path(cr);
        }
        cairo_fill(cr);
    } else if (f == replay_realtime) { /* a clock */
        cairo_new_sub_path(cr), cairo_arc(cr, cx, cy, r, 0, 2 * G_PI);
        cairo_move_to(cr, cx, cy - r * 0.6), cairo_line_to(cr, cx, cy), cairo_line_to(cr, cx + r * 0.45, cy + r * 0.3);
    } else if (f == replay_stop) {
        cairo_rectangle(cr, cx - r * 0.7, cy - r * 0.7, r * 1.4, r * 1.4);
        cairo_fill(cr);
    } else if (e->sub == GAMES_MENU) { /* a folder */
        cairo_move_to(cr, cx - r, cy + r * 0.8), cairo_line_to(cr, cx - r, cy - r * 0.75), cairo_line_to(cr, cx - r * 0.3, cy - r * 0.75),
            cairo_line_to(cr, cx - r * 0.1, cy - r * 0.45), cairo_line_to(cr, cx + r, cy - r * 0.45), cairo_line_to(cr, cx + r, cy + r * 0.8);
        cairo_close_path(cr);
    } else if (e->sub == HELP_MENU) {
        cairo_new_sub_path(cr), cairo_arc(cr, cx, cy, r, 0, 2 * G_PI);
        cairo_stroke(cr);
        cairo_new_sub_path(cr), cairo_arc(cr, cx, cy - r * 0.2, r * 0.32, G_PI, 2.3 * G_PI);
        cairo_line_to(cr, cx, cy + r * 0.25);
        cairo_stroke(cr);
        cairo_new_sub_path(cr), cairo_arc(cr, cx, cy + r * 0.58, r * 0.08, 0, 2 * G_PI);
        cairo_fill(cr);
    } else if (f == flip) { /* up and down arrows */
        for (int k = -1; k <= 1; k += 2) {
            double ax = cx + k * r * 0.45, tip = cy - k * r, base = cy + k * r;
            cairo_move_to(cr, ax, base), cairo_line_to(cr, ax, tip);
            cairo_move_to(cr, ax - r * 0.35, tip + k * r * 0.35), cairo_line_to(cr, ax, tip),
                cairo_line_to(cr, ax + r * 0.35, tip + k * r * 0.35);
        }
    } else if (f == show_result) { /* a trophy's cup */
        cairo_move_to(cr, cx - r, cy - r), cairo_line_to(cr, cx + r, cy - r), cairo_line_to(cr, cx + r * 0.5, cy + r * 0.2),
            cairo_line_to(cr, cx - r * 0.5, cy + r * 0.2);
        cairo_close_path(cr);
        cairo_move_to(cr, cx, cy + r * 0.2), cairo_line_to(cr, cx, cy + r);
        cairo_move_to(cr, cx - r * 0.6, cy + r), cairo_line_to(cr, cx + r * 0.6, cy + r);
    } else if (f == resign) { /* a flag */
        cairo_move_to(cr, cx - r * 0.7, cy + r), cairo_line_to(cr, cx - r * 0.7, cy - r);
        cairo_move_to(cr, cx - r * 0.7, cy - r), cairo_line_to(cr, cx + r, cy - r * 0.55),
            cairo_line_to(cr, cx - r * 0.7, cy - r * 0.1);
        cairo_close_path(cr);
        cairo_stroke_preserve(cr);
        cairo_fill(cr);
    } else if (f == declare) { /* a crown */
        cairo_move_to(cr, cx - r, cy + r * 0.6), cairo_line_to(cr, cx - r, cy - r * 0.5), cairo_line_to(cr, cx - r * 0.5, cy),
            cairo_line_to(cr, cx, cy - r * 0.8), cairo_line_to(cr, cx + r * 0.5, cy), cairo_line_to(cr, cx + r, cy - r * 0.5),
            cairo_line_to(cr, cx + r, cy + r * 0.6);
        cairo_close_path(cr);
    } else if (f == preferences_dialog) { /* a gear */
        for (int i = 0; i < 8; i++) {
            double t = i * G_PI / 4;
            cairo_move_to(cr, cx + cos(t) * r * 0.75, cy + sin(t) * r * 0.75);
            cairo_line_to(cr, cx + cos(t) * r * 1.05, cy + sin(t) * r * 1.05);
        }
        cairo_stroke(cr);
        cairo_new_sub_path(cr), cairo_arc(cr, cx, cy, r * 0.65, 0, 2 * G_PI);
        cairo_stroke(cr);
        cairo_new_sub_path(cr), cairo_arc(cr, cx, cy, r * 0.22, 0, 2 * G_PI);
    } else if (f == open_tutorial) { /* a star, as the lessons' targets */
        for (int k = 0; k < 10; k++) {
            double a = -G_PI / 2 + k * G_PI / 5, rad = k % 2 ? r * 0.42 : r;
            cairo_line_to(cr, cx + rad * cos(a), cy + rad * sin(a));
        }
        cairo_close_path(cr);
    } else if (f == how_to_play) { /* an open book */
        cairo_move_to(cr, cx, cy - r * 0.6), cairo_curve_to(cr, cx - r * 0.4, cy - r * 0.85, cx - r * 0.8, cy - r * 0.85, cx - r, cy - r * 0.7);
        cairo_line_to(cr, cx - r, cy + r * 0.75);
        cairo_curve_to(cr, cx - r * 0.8, cy + r * 0.6, cx - r * 0.4, cy + r * 0.6, cx, cy + r * 0.85);
        cairo_curve_to(cr, cx + r * 0.4, cy + r * 0.6, cx + r * 0.8, cy + r * 0.6, cx + r, cy + r * 0.75);
        cairo_line_to(cr, cx + r, cy - r * 0.7);
        cairo_curve_to(cr, cx + r * 0.8, cy - r * 0.85, cx + r * 0.4, cy - r * 0.85, cx, cy - r * 0.6);
        cairo_line_to(cr, cx, cy + r * 0.85);
    } else if (f == about_dialog) { /* i in a circle */
        cairo_new_sub_path(cr), cairo_arc(cr, cx, cy, r, 0, 2 * G_PI);
        cairo_stroke(cr);
        cairo_new_sub_path(cr), cairo_arc(cr, cx, cy - r * 0.45, r * 0.1, 0, 2 * G_PI);
        cairo_fill(cr);
        cairo_move_to(cr, cx, cy - r * 0.1), cairo_line_to(cr, cx, cy + r * 0.55);
    } else if (f == quit) { /* power */
        cairo_new_sub_path(cr), cairo_arc(cr, cx, cy, r, -G_PI / 2 + 0.6, 3 * G_PI / 2 - 0.6);
        cairo_move_to(cr, cx, cy - r * 1.1), cairo_line_to(cr, cx, cy - r * 0.1);
    }
    cairo_stroke(cr);
}

/* The page shown. The keyboard and the controller have a mark on a tile (the pointer none). */
static const MenuEntry *page = MENU;
/* Cancel in a dialog opened from the menu goes back to the menu, on the same page. */
static gboolean cancelled;

static gboolean response_hook(GSignalInvocationHint *hint, guint n, const GValue *params, gpointer data)
{
    (void) hint, (void) n, (void) data;
    cancelled = g_value_get_int(&params[1]) == GTK_RESPONSE_CANCEL;
    return TRUE;
}

static void show_page(const MenuEntry *entries, const MenuEntry *from);

/* Where ‹ on a page opened from the menu goes back to. */
static const MenuEntry *back_page, *back_entry;

void menu_back(void)
{
    if (!back_page || sheet_is_open())
        return;
    sheet_set_live(!app.kindle);
    show_page(back_page, back_entry);
}

static const MenuEntry *entry_of(int id) { return &page[id]; }

static void tile_activate(int id)
{
    const MenuEntry *e = entry_of(id);
    if (e->sub) {
        show_page(e->sub, NULL);
        return;
    }
    const MenuEntry *from = page;
    sheet_close();
    cancelled = FALSE;
    back_page = from, back_entry = e;
    drawer_from_menu(TRUE);
    e->fn();
    drawer_from_menu(FALSE);
    if (cancelled)
        show_page(from, e);
}

static void tile_icon(cairo_t *cr, int id, double x, double y, double s) { draw_icon(cr, entry_of(id), x, y, s); }

/* Back on the top page, the mark on the tile of the page left. */
static void back(void)
{
    const MenuEntry *from = MENU;
    while (from->label && from->sub != page)
        from++;
    show_page(MENU, from->label ? from : NULL);
}

static const char *page_title(const MenuEntry *entries)
{
    for (const MenuEntry *e = MENU; e->label; e++)
        if (e->sub == entries)
            return tr(e->label);
    return tr("Menu");
}

static int page_tiles(const MenuEntry *entries, const MenuEntry *from, SheetTile *tiles, int *focus)
{
    int n = 0;
    for (const MenuEntry *e = entries; e->label && n < 24; e++) {
        gboolean on = entry_enabled(e);
        if ((e->pc && app.kindle) || (e->optional && !on))
            continue;
        if (e == from && !controls_pointer())
            *focus = n;
        const char *label = e->fn == analysis_dialog ? analysis_menu_label() : e->label;
        tiles[n++] = (SheetTile) {tr(label), (int) (e - entries), on};
    }
    return n;
}

/* Every page of the menu, and the drawers over the game, as large as its top page. */
static void set_reference(void)
{
    SheetTile tiles[24];
    int focus;
    sheet_set_reference(tiles, page_tiles(MENU, NULL, tiles, &focus));
}

/* `from`: the entry to put the keyboard's mark on (when the keyboard opened the menu). */
static void show_page(const MenuEntry *entries, const MenuEntry *from)
{
    static gulong hook;
    if (!hook) {
        g_type_class_ref(GTK_TYPE_DIALOG); /* its signals exist once the class does */
        hook = g_signal_add_emission_hook(g_signal_lookup("response", GTK_TYPE_DIALOG), 0, response_hook, NULL, NULL);
    }
    SheetTile tiles[24];
    int focus = controls_pointer() ? -1 : 0, n = page_tiles(entries, from, tiles, &focus);
    page = entries;
    set_reference();
    static const SheetCallbacks CB = {tile_activate, tile_icon, back}, TOP = {tile_activate, tile_icon, NULL};
    sheet_show(app.view, page_title(entries), tiles, n, entries == MENU ? &TOP : &CB, focus);
}

gboolean menu_size(int *w, int *h)
{
    set_reference();
    return sheet_size(app.view, w, h);
}

/* The bar's Menu, the menu key or the controller's: opens the menu, or closes it. */
void menu_open(void)
{
    if (sheet_is_open()) {
        sheet_close();
        return;
    }
    sheet_set_live(!app.kindle);
    show_page(MENU, NULL);
}

void info_cb(GtkWidget *w, gpointer data)
{
    (void) w, (void) data;
    info_dialog();
}

gboolean delete_cb(GtkWidget *w, GdkEvent *ev, gpointer data)
{
    (void) w, (void) ev, (void) data;
    quit();
    return TRUE;
}
