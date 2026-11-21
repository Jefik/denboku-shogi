#ifndef APP_H
#define APP_H

/* Shared by the parts of the game program (one file per area, see each file). */

#include <errno.h>
#include <glib/gstdio.h>
#ifdef G_OS_WIN32
#include <windows.h>
#endif
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "engine.h"
#include "game.h"
#include "histindex.h"
#include "http.h"
#include "i18n.h"
#include "json.h"
#include "net.h"
#include "netproto.h"
#include "pieces.h"
#include "upnp.h"
#include "view.h"
#include "online.h"

#define KINDLE_TITLE "L:A_N:application_ID:denboku_PC:N_O:URL"
#define KINDLE_DIALOG "L:A_D:application_ID:denbokuDialog"

typedef enum { PLAYER_HUMAN, PLAYER_ENGINE, PLAYER_REMOTE } PlayerType;
typedef enum { MODE_FIRST, MODE_SECOND, MODE_RANDOM, MODE_TWO_PLAYERS, MODE_ENGINES, MODE_HOST, MODE_JOIN, MODE_ONLINE, MODE_SERVER, MODE_COUNT } Mode;
typedef enum { COLOR_AUTO, COLOR_MONO, COLOR_COLOR } ColorMode;

#define TIME_CUSTOM NTIMES /* the last choice: the custom values below */
#define NTIME_CHOICES (NTIMES + 1)
#define LEVELS 10
#define RESPONSE_DELETE 1
#define RESPONSE_CLEAR 3
/* LAN greeting: bumped when a new line or variant needs both sides to understand it. Unknown
 * lines are ignored, so a newer side can still talk to an older one, down to 4 (the first sealed). */
#define NET_VERSION 5

/* "Illegal moves": refused; allowed and they lose (二歩, 王手放置…); or pieces go to any square,
 * as on a real board, and illegal moves lose. */
static const char *const ILLEGAL_LABELS[] = {"Not allowed", "Allowed, but they lose", "Any square, but they lose"};

/* The New Game dialog asks who plays, then which side: */
typedef enum { PLAY_ENGINE, PLAY_TWO, PLAY_ENGINES, PLAY_HOST, PLAY_JOIN, PLAY_ONLINE, PLAY_SERVER, PLAY_COUNT } Play;
/* New Game's chips: hosting and joining are one entry, "Network", with a second choice for which. */
#define PLAY_CHOICES (PLAY_COUNT - 1)
static const char *const PLAY_LABELS[PLAY_CHOICES] = {"Computer", "Two players", "Engines", "Network", ONLINE_LABEL,
                                                   SERVER_LABEL};

#define BANNED 5 /* wrong room passwords from one address, or the host's Ban */

typedef struct {
    gboolean kindle;
    char *base, *home, *saves, *history, *engine_path, *fonts, *styles;
    char *custom, *custom_pieces, *custom_sounds; /* the user's own sets: custom/pieces/ and custom/sounds/ */
    GKeyFile *settings;
    char *settings_path;

    GtkWidget *window;
    /* What the bar's Undo and Hint show, but in a replay, a lesson or an engines game. */
    const char *undo_label; /* English: "Undo", or "Takeback" in a network game */
    gboolean can_undo, can_hint, hint_visible;
    View *view;
    PieceStyle *style;
    GPtrArray *style_files;

    Game game;
    Mode mode;
    gboolean loaded_record; /* the game came from a file, the clipboard or lishogi: its names stay */
    PlayerType players[2];
    int level, time_index, variant;
    int level_first;   /* two engines: Sente's level (`level` is Gote's) */
    gboolean paused;   /* two engines: neither moves, the clock stopped */
    long paused_ms;    /* what the side to move had used of its turn */
    /* "Custom…" in New Game: minutes of main time, seconds of byoyomi and increment. */
    int custom_min, custom_byoyomi, custom_increment;
    TimeControl custom_tc;
    Handicap handicap;
    long remaining[2];   /* milliseconds of main time left */
    gint64 turn_start;   /* monotonic µs when the side to move started */
    guint tick;
    Engine *engine;
    gboolean engine_thinking, hint_pending;
    /* A mate problem being played (problem.c): its plies (0: none), its place in the list, whether
     * the defender's search is awaited to tell that the last move was right, and the mate score that
     * search reported (USI's, in plies; 0 for none). */
    int problem_plies, problem_index, problem_mate;
    gboolean problem_check;
    /* LAN game: the connection, our color, and the dialog shown while waiting. */
    Net *net;
    int net_color;
    int net_rule;  /* the host's move rule for both players (see move_rule) */
    gboolean net_suspended;  /* connection lost: clocks stopped, reconnecting */
    int peer_version;        /* the other side's DENBOKU version, 0 while unknown */
    gint64 net_heard, pause_start;
    guint ping, net_retry;
    char *net_address, *net_moves;  /* joiner: the host, and the game it sent */
    char *net_password;              /* joiner: the room password given, sent again on reconnecting */
    char *room_password;             /* host: this room's password; on disk only in [Game] while a hosted game lasts */
    GHashTable *banned;              /* host: address → wrong passwords, BANNED or more is banned (memory only) */
    int net_reject;                  /* joiner: 1 kicked, 2 banned, 0 nothing */
    gboolean takeback_mine, takeback_theirs;
    /* The move count a takeback was declined at, ours and theirs: not asked again before another move
     * (0 is no move, where none can be asked anyway). */
    int takeback_refused_mine, takeback_refused_theirs;
    GtkWidget *lost_dialog;
    /* Lobby: the players and settings, the host's Start or the joiner's Ready; rematch requests. */
    GtkWidget *lobby_players, *lobby_settings, *lobby_button, *lobby_internet;
    GtkWidget *lobby_kick, *lobby_ban;
    gboolean remote_ready, rematch_mine, rematch_theirs;
    gboolean peer_left; /* the other player left after the game ended */
    char *public_address; /* "From the internet: 203.0.113.5:4081", when the router forwards the port */
    GtkWidget *over_label, *over_dialog, *over_rematch;
    /* Setting up a position: the bar of editing buttons, the game before, the position made. */
    GtkWidget *edit_bar, *edit_side;
    GtkWidget *edit_variant;
    int edit_saved_variant;
    Game *edit_saved;
    char *start_sfen;
    char *net_start; /* the start position of this network room, when set up in the editor */
    TimeControl online_tc; /* the clock a server set */
    GtkWidget *wait_dialog, *wait_label, *wait_link; /* wait_for_peer_link() */
    char *remote_name;
} App;

extern App app;

/* The server keeps the clock and the record (lishogi, a CSA server). */
static inline gboolean server_game(void) { return app.mode == MODE_ONLINE || app.mode == MODE_SERVER; }
/* A mode that plays over a connection (LAN, lishogi, a CSA server). */
static inline gboolean connected_mode(Mode m) { return m == MODE_HOST || m == MODE_JOIN || m == MODE_ONLINE || m == MODE_SERVER; }

typedef struct {
    const char *name;
    BoardColors c;
} Palette;
#define NPALETTES 12
extern const Palette PALETTES[NPALETTES];
#define NTEXT_SIZES 8
extern const int TEXT_SIZES[NTEXT_SIZES];
void apply_text_size(void);
gboolean piece_shape(int variant); /* -1: the setting for all variants */
int shape_setting(int variant);     /* the variant's own: -1 same as all variants */
gboolean colored_pieces(int variant); /* -1: the setting for all variants */
gboolean chess_mode(int variant); /* -1: the setting for all variants */
int colored_setting(int variant);   /* the variant's own: -1 same as all variants */

/* settings.c */
char *setting_string(const char *key, const char *fallback);
void apply_language(void);
int setting_int(const char *key, int fallback);
void save_settings(void);
void find_paths(void);
void custom_folder_set(const char *dir);
/* updater.c: the daily look for a new version, the first question about it, About's line and the choice
 * in Preferences (none of it where something else updates the game). */
void updater_start(void);
void updater_ask(void);
void updater_restart(void);
gboolean updater_offered(void);
void updater_about(GtkWidget *dialog, GtkWidget *box);
GtkWidget *updater_choice(void);
const char *updater_choice_label(void);
/* "Version 0.9.0 · Build 20261201.1530", or only the build between releases (g_free it). */
char *updater_version_text(void);
/* What About's update button answers its dialog with, once it has done its part. */
#define RESPONSE_UPDATER (-31)
void add_ui_fonts(void);

/* dialogs.c */
GtkWidget *dialog_new(const char *title);
int run_dialog(GtkWidget *d);
void message(const char *text);
void toast(const char *text);
gboolean confirm(const char *text);
/* Same, with `ok` on the confirming button ("Resign", "Ban", ...). */
gboolean confirm_with(const char *text, const char *ok);
/* Same, under `title` (the page it is about) instead of the game's name. */
gboolean confirm_titled(const char *title, const char *text, const char *ok);
GtkWidget *combo(const char *const *items, int n, int active, gboolean translate);
void close_popup_on_leave(GtkWidget *combo);
void keyboard_enter_cb(GtkEntry *e, gpointer data);
void keyboard_hide(void);
void keyboard_hide_leftover(void);
GtkWidget *text_entry(void);
/* The entry with a Paste button beside it (alone on the Kindle, which has no clipboard). */
GtkWidget *with_paste(GtkWidget *entry);
GtkWidget *show_password_check(GtkWidget *entry);
void black_frame(GtkWidget *w);
/* Focus a text field once its dialog is up (opens the Kindle keyboard). */
void dialog_focus_entry(GtkWidget *entry);
void center_dialogs(void);
GtkWidget *table_row(GtkWidget *table, int row, const char *label, GtkWidget *w);
/* Opens a web page in the browser; the Kindle has none, so it shows the address. */
void open_url(const char *url);
GtkWidget *link_button(const char *text, const char *url);

/* play.c */
void prepare_engine(void);
int own_move_rule(void);
gboolean show_moves(void);
void move_sound(gboolean illegal);
int illegal_rule(void);
const TimeControl *tc(void);
const char *tc_label(void);
/* The label of clock choice `index`, `custom` holding the values of TIME_CUSTOM. */
const char *tc_label_of(int index, const TimeControl *custom);
const char *player_name(int color);
void update_move_label(void);
void set_move_text(const char *text);
void autosave(void);
const char *engine_variant(void);
const char *engine_path(void);
gboolean ensure_engine(void);
void apply_level(void);
int engine_level(int side);
gboolean engines_game(void);
void pause_toggle(void);
void update_undo(void);
void engine_search(gboolean hint);
const char *game_result_text(const Game *g, const PlayerType *players);
const char *result_text(void);
void next_turn(void);
void turn_clock_new(void); /* another game: its turn clock starts with the next next_turn() */
void skip_game_over(void);
void play_move(const Move *m);
void play_illegal_move(const Move *m);
void start_game(Handicap h, Mode mode);
void view_move_cb(View *v, const Move *m, void *data);
gboolean view_can_premove_cb(View *v, int color, void *data);
gboolean view_square_tap_cb(View *v, int sq, void *data);
gboolean view_can_move_cb(View *v, int color, void *data);
void view_text_cb(View *v, int color, char *label, char *clock, gboolean *active, void *data);
void view_ply_cb(View *v, void *data);

/* analysis.c */
gboolean analysis_possible(void);
void analysis_dialog(void);
const char *analysis_menu_label(void);
/* Adds the analysis of the position after `ply` moves to the top bar's text. */
void analysis_text(int ply, char *buf, int size);

/* gamepad.c: game controllers, as these inputs */
typedef enum {
    PAD_A, PAD_B, PAD_X, PAD_Y, PAD_LB, PAD_RB, PAD_LT, PAD_RT, PAD_BACK, PAD_START, PAD_LS, PAD_RS,
    PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT, PAD_STICK_UP, PAD_STICK_DOWN, PAD_STICK_LEFT, PAD_STICK_RIGHT,
    PAD_RSTICK_UP, PAD_RSTICK_DOWN, PAD_COUNT
} PadInput;
void gamepad_start(void);
/* The controller's motors at `strong` and `weak` (0-1) for `ms`, where it has them. */
void gamepad_rumble(double strong, double weak, int ms);
/* The controller last connected, NULL for none. */
const char *gamepad_name(void);
/* Whose button names and colors to show: Preferences → Controls, or the controller's own. */
typedef enum { LAYOUT_AUTO, LAYOUT_XBOX, LAYOUT_NINTENDO, LAYOUT_PLAYSTATION } PadLayout;
PadLayout gamepad_layout(void);

/* controls.c */
extern const char *const PAD_IDS[PAD_COUNT];
void controls_load(void);
gboolean controls_key(const GdkEventKey *e);
/* The last input was a click or a tap (focus frames hidden); Tab and the arrows show them. */
gboolean controls_pointer(void);
/* The last input was the controller. */
gboolean controls_pad_used(void);
void controls_nav_key(guint keyval);
void controls_pad(PadInput input, gboolean down);
GtkWidget *controls_page(void);
/* Keeps what the Controls page set. */
void controls_save(void);
/* Watches for the pointer and the focus, to show keys or controller buttons only when in use. */
void controls_init(void);
/* ViewCallbacks.area_badge. */
void controls_area_badge(View *v, cairo_t *cr, int area, double right, double top, double h, gboolean active,
                         const double *fg, const double *bg, void *data);

/* sound.c */
void play_sound(const char *name);
void sound_preview(const char *set);
void feedback_tick(void);
GtkWidget *sound_chooser(void);
GtkWidget *volume_row(GtkWidget *sounds);
int sound_volume(void);
GPtrArray *sound_sets(void);
/* The file of a piece set: in the user's styles/ folder when it is there, else the built-in one. */
char *style_path(const char *file);
/* Mate problems: the dialog (menus.c), and starting one (play.c: `position` as the game writes it). */
void problems_dialog(void);
void start_problem(int variant, int plies, int index, const char *position);
gboolean problem_playing(void);
char *problem_label(int plies);
const char *problem_title(void);
/* After a problem is solved (marked in the settings) and the next one to play. */
void problem_solved_now(void);
void problem_next(void);
/* The engine's line for the problem on the board, shown as moves when it is found. */
void problem_solution(void);
/* The tutorial (tutorial.c): the list of lessons, and a lesson being played, whose moves, Hint and
 * Undo (the step again) go to it; any other game ends it (lesson_end, from start_game). */
void tutorial_dialog(void);
/* On the first start (no game saved), asks once whether to start the tutorial. */
void tutorial_offer(void);
gboolean lesson_playing(void);
int lesson_player(void);
void tutorial_move(const Move *m);
/* A tap on a square: TRUE when a lesson's step asks for one (lessons.c GOAL_QUIZ) and took it. */
gboolean tutorial_tap(int sq);
void tutorial_hint(void);
void tutorial_again(void);
void lesson_end(void);
/* The data folder the player chose (NULL: the default one) and choosing one (NULL: back to the
 * default); it is used from the next start, bringing the default folder's files along. */
void data_folder_choose(const char *path);
const char *sound_set_name(const char *set);
char *variant_sound_set(int variant); /* NULL: the same as all variants */
/* The sound set chosen as Preferences names it ("Silent" when off). */
char *sound_label(void);

/* lan.c */
int net_port(void);
const char *local_name(void);
void net_stop(void);
gboolean wait_for_peer_link(const char *text, gboolean link);
/* What the waiting drawer says, and the link it shows; nothing when it is not open. */
void wait_set_text(const char *text);
void wait_set_link(const char *link);
void rematch_check(void);
gboolean takeback_asked(void);
void takeback_answer(gboolean yes);
void host_rejoin(void);
void takeback_decline_by_moving(void);
void rematch(void);
/* style.c: flat rounded widgets; `framed` for black and white screens. */
void style_install(void);
void style_follow_text(void);
void style_set_framed(gboolean framed);
/* The color of the button that confirms and of the chosen chip, and of their text. */
void style_set_accent(const double *accent, const double *on_accent);
/* drawer.c: dialogs as drawers in the window (see there). */
void drawer_init(GtkWidget *column);
gboolean drawer_ready(void);
void drawer_wrap(GtkWidget *dialog, const char *title);
/* A new title for a page (in its header). */
void drawer_set_title(GtkWidget *dialog, const char *title);
void drawer_layout(void);
gboolean drawer_open(void);
/* The top drawer's dialog and its box (for the keyboard and the controller). */
GtkWidget *drawer_dialog(void);
/* The top page scrolled a quarter of its height down (dir 1) or up (-1): the controller's right stick. */
void drawer_scroll(int dir);
GtkWidget *drawer_box(void);
gboolean drawer_key(const GdkEventKey *e);
/* A page's answer for "the page after" and "the page before" (LB/RB, Ctrl+Page Up/Down), on
 * dialogs marked "page-step". */
#define RESPONSE_PAGE_NEXT (-20)
#define RESPONSE_PAGE_PREV (-21)
gboolean controls_dialog_key(GtkWidget *win, GtkWidget *dialog, const GdkEventKey *e);
/* A page opened from another: ‹ (Escape) goes back to it with GTK_RESPONSE_CLOSE; × closes every page.
 * Other pages close with ‹ too, back to the ☰ menu when opened from it (drawer_from_menu()). */
void drawer_set_back(GtkWidget *dialog);
void drawer_from_menu(gboolean on);
/* The drawers' head (‹ unless `back` is NULL, the title, ×), as the ☰ menu's; drawer_head_fit() sizes it after the
 * menu's, with its margins or not. */
GtkWidget *drawer_head_new(const char *title, GCallback back, GCallback close, gpointer data);
void drawer_head_fit(GtkWidget *head, gboolean margins);
/* A page that waits on something: a tap outside does not close it. */
void drawer_set_sticky(GtkWidget *dialog);
void drawer_set_outside_ok(GtkWidget *dialog);
void drawer_close_all(void); /* every page, as × */ /* a tap outside the drawer confirms this page instead of cancelling it */
void drawer_set_passive(GtkWidget *dialog);
void show_result(void);
void drawer_set_beside(GtkWidget *dialog);
int drawer_width(void);
GtkWidget *drawer_stage(GtkWidget *main);
/* rows.c: the rows of the drawer's pages (see there). */
int rows_width(void);
GtkWidget *row_text(const char *text, gboolean dim, gboolean bold, gboolean small);
GtkWidget *row_para(const char *text);
GtkWidget *rows_card(GtkWidget *page);
void rows_group(GtkWidget *page, const char *title);
GtkWidget *row_add(GtkWidget *card, GtkWidget *row);
GtkWidget *row_switch(const char *label, gboolean on);
GtkWidget *row_choice(const char *label, const char *value, GtkWidget **value_label);
void row_set_value(GtkWidget *value_label, const char *value);
GtkWidget *row_section(const char *icon, const char *title, const char *summary);
void chip_pack(GtkWidget *box, GtkWidget *chip, int width);
GtkWidget *row_chips(const char *label, const char *const *items, int n, int active, int width,
                     void (*cb)(int, gpointer), gpointer data);
/* The chip `step` after (or before) the chosen one in a row_chips(), chosen; a dialog whose "step-chips"
 * is that row goes through them with LB/RB and Ctrl+Page Up/Down. */
void chips_step(GtkWidget *chips, int step);
GtkWidget *row_stepper(const char *label, GtkWidget **value, GtkWidget **minus, GtkWidget **plus);
/* A list in a drawer page over the current one, each choice shown at once: Confirm keeps it, Cancel
 * (or back) gives `picked` `active` again. */
int rows_pick(const char *title, const char *const *items, GdkPixbuf *const *pictures, int n, int active,
              void (*picked)(int, gpointer), gpointer data);
/* From the event handler: a drag in a scrolled area scrolls it; TRUE when the event was used. */
gboolean touch_scroll_event(GdkEvent *ev);
/* The top of the Kindle keyboard when it is up over the lower part of the screen, else 0. */
int keyboard_covering(void);
/* The look of a drawer for other panels (the chat): the background with a rounded line and a
 * handle on the edge that faces the board. */
typedef enum { EDGE_NONE, EDGE_TOP, EDGE_LEFT, EDGE_BOTTOM } DrawerEdge;
GtkWidget *drawer_frame_new(void);
void drawer_frame_edge(GtkWidget *frame, DrawerEdge edge);
/* chat.c */
void chat_add(const char *who, const char *text);
/* The quick lines shown or not, as the game goes (after each move and at its end). */
void chat_presets_update(void);
/* Leaving a network or lishogi game: its lines go. */
void chat_clear(void);
/* The chat panel, at the start of `side` (landscape) or after the first child of `top` (portrait). */
void chat_panel_new(GtkWidget *side, GtkWidget *top);
/* The window turned, or a network game began or ended. */
void chat_layout(void);
/* An event on the panel while a drawer holds the grab, given to it; FALSE if not one. */
gboolean chat_event(GdkEvent *ev);
/* A button that opens the chat, shown only in portrait (chat_layout()). */
void chat_opener(GtkWidget *w);
/* Portrait: the bar's chat button opens or closes the panel. */
void chat_toggle(void);
/* The bar's chat button (ViewBarState): left out unless a network game in portrait. */
int chat_bar_state(void);
void network_game(Handicap h, Mode mode, const char *address);

/* theme.c */
ColorMode color_mode(void);
gboolean color_screen(void);
gboolean auto_color_screen(void);
int palette_index(void);
int wood_palette(void);
int theme_setting(void);
void load_style_list(void);
char *variant_key(const char *key, int variant);
#define DEFAULT_PIECES "printout-kanji1.ini"
char *pieces_setting(int variant);
char *pieces_file(char *file);
int move_marks_setting(int variant);
void apply_style(void);
void apply_variant_style(void);
/* Preferences, a variant's page: the board shows that variant's start with its pieces (-1: the game again). */
void board_preview(int variant);

/* menus.c */
void new_game_dialog(void);
void room_settings_dialog(void);
void room_settings_after_game(void);
const char *variant_label(int variant);
GdkPixbuf *pieces_pixbuf(PieceStyle *s, const int *types, int n, gboolean upside, int cell);
gboolean notation_ja(void);
void apply_settings(void);
void preferences_dialog(void);
/* A page of the Preferences section being shown: LB/RB (Ctrl+Page Up/Down) go to the sections beside it. */
gboolean prefs_section_page(const char *title);
void save_game(void);
void save_csa(void);
gint compare_names_asc(gconstpointer a, gconstpointer b);
void load_game_dialog(void);
void history_dialog(void);
void moves_dialog(void);
void how_to_play(void);
void about_dialog(void);
void undo_cb(GtkWidget *w, gpointer data);
void redo(void);
void forget_redo(void);
void hint_cb(GtkWidget *w, gpointer data);
/* Steps through the moves shown, `step` plies from the one shown now. */
void history_step(int step);
void resign(void);
void declare(void);
void quit(void);
/* Opens the menu (a sheet of tiles over the view), or closes it. */
void menu_open(void);
/* The menu again, on the page a drawer was opened from (its ‹). */
void menu_back(void);
/* The menu's size, which the drawers over the game take too. */
gboolean menu_size(int *w, int *h);
void info_cb(GtkWidget *w, gpointer data);
gboolean delete_cb(GtkWidget *w, GdkEvent *ev, gpointer data);

/* records.c */
typedef enum { REPLAY_FAST, REPLAY_SLOW, REPLAY_REALTIME } ReplaySpeed;
void use_loaded_game(Game *g);
gboolean record_save(const char *path, gboolean alt);
void paste_game(void);
void copy_game(void);
gboolean load_record_text(const char *text);
void history_add(void);
int record_kind(const char *name);
GPtrArray *record_files(const char *folder);
GPtrArray *history_games(void);
/* A replay playing or paused; while one is on, the bar's Undo is its speed and Hint its pause. */
gboolean replaying(void);
gboolean replay_paused(void);
gboolean replay_has_times(void);
void replay_start(ReplaySpeed speed);
void replay_stop(void);
void replay_pause_toggle(void);
void replay_next_speed(void);
const char *replay_speed_label(void);

/* editor.c */
void set_up_position(void);
void view_edit_cb(View *v, int sq, int hand_color, void *data);
void view_edit_event_cb(View *v, int what, void *data);
gboolean edit_undo(void);
gboolean edit_redo(void);
void edit_tool_step(int step);
void edit_other_color(void);
gboolean controls_in_bar(void);
gboolean edit_key(guint key, guint state);
GtkWidget *edit_bar_new(void);
void edit_relabel(void);
void edit_bar_layout(void);

/* android.c */
#ifdef __ANDROID__
void android_window(GtkWidget *window);
void android_start(void);
void android_play(const char *path, int volume);
void android_vibrate(int ms, int strength);
/* The game's view also in the space beside the camera (a notch or a punch hole). */
void android_notch(gboolean on);
void android_rumble(int ms, int strong, int weak);
/* Asks for Android's clipboard, which goes into `entry` when it comes. */
void android_paste(GtkWidget *entry);
/* gamepad.c: the controllers the app reads (android.c passes them on). */
void gamepad_android_name(const char *driver, const char *name);
void gamepad_android_bits(guint32 bits);
void android_open_url(const char *url);
/* A saved game (a file of the app's own folder) copied to Documents/Denboku, where other apps see it. */
void android_export(const char *path);
void android_install(const char *apk); /* an update, to the system's installer */
void android_allow_install(void);
void android_input_reset(void); /* the focused field emptied by the game: the keyboard's copy too */       /* updates turned on: the system's question to allow installs, now */
/* The on-screen keyboard is up (the window is what it leaves). */
gboolean android_keyboard(void);
/* Android's time of the finger at x, y (root coordinates) when it got there, else `time`. */
guint32 android_touch_time(int x, int y, guint32 time);
#define OTHER_ENGINE FALSE /* Android runs no program from the user's files */
#define TOUCH_ONLY TRUE
#else
#define OTHER_ENGINE (!app.kindle) /* another USI program as the engine, on computers */
#define TOUCH_ONLY app.kindle /* no pointer: nothing is lit for being under it (where the last tap was) */
#endif

/* kindle.c */
void watch_screensaver(void);
void kindle_bands_soon(void);
void kindle_draw_in_bands(void);
gboolean drawer_flash_wanted(void);
gboolean drawer_settling(void);
GdkRegion *drawer_flash_now(void);
/* The Kindle's front light, 0 to KINDLE_LIGHT_MAX (-1 unknown), and setting it. */
#define KINDLE_LIGHT_MAX 24
int kindle_light(void);
void kindle_set_light(int level);

/* app.c */
GtkWidget *button(const char *label, GCallback cb, gpointer data);
/* Toolbar icons drawn in the button's text color: the same on every device, whatever its fonts
 * have (Windows lacked ⓘ, the Kindle ⏮). */
typedef enum { ICON_CLOSE, ICON_BACK } Icon;
GtkWidget *icon_button(Icon icon, GCallback cb, gpointer data);
double ui_scale(void);
/* The text's line height in pixels (19 on a computer), measured again only when the font or the
 * screen's density changes. */
int line_height(void);
int ui_limit(int px);
void relabel(void);
/* `w` (a button or a label) shows tr(english), again when the language changes; `english` must last. */
void follow_language(GtkWidget *w, const char *english);
gboolean reset_everything(void);
void apply_orientation(gboolean leaving);
gboolean resume(void);

#endif
