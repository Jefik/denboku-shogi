#ifndef VIEW_H
#define VIEW_H

#include <gtk/gtk.h>

#include "game.h"
#include "pieces.h"

typedef struct {
    double board[3], hand[3], last[3], dot[3];
    /* Mark the last move with corner brackets instead of a fill: e-ink partial
     * updates draw only black and white, so grays turn white where pieces move. */
    gboolean last_corners;
    double fg[3], bg[3];  /* text and frames, background */
    double lines[3];      /* grid and marks drawn on the board and the hands */
    double accent[3];     /* the side to move's pill, the current move, chosen rows; `dot` is the board's */
    double hint[3];       /* the hint's ring and arrows */
} BoardColors;

extern const BoardColors COLORS_MONO, COLORS_DARK;

typedef struct View View;

typedef struct {
    /* A complete legal move chosen on the board. */
    void (*move)(View *v, const Move *m, void *data);
    /* Whether the human may move pieces of this color now. */
    gboolean (*can_move)(View *v, int color, void *data);
    /* Text above each clock (mark, name or "thinking") and the clock itself. */
    void (*player_text)(View *v, int color, char *label, char *clock, gboolean *active, void *data);
    /* The shown move changed from inside the view (tap on the move list). */
    void (*ply_changed)(View *v, void *data);
    /* Setting up a position: a tap on an empty part of the hand of `hand_color`. */
    void (*edit_tap)(View *v, int sq, int hand_color, void *data);
    /* The editor changed the position: EDIT_BEFORE comes first (for undo), then EDIT_CHANGED;
     * EDIT_SELECTED when only the selection changed. */
    void (*edit_event)(View *v, int what, void *data);
    /* Whether the human may premove with this color during the opponent's turn. */
    gboolean (*can_premove)(View *v, int color, void *data);
    /* Playing from the keyboard or a controller: draws the key or button that goes to an area
     * (VIEW_AREA_*) with its right edge at `right` and its top at `top`, `h` tall, in `fg` on `bg`;
     * `active` for the area in use, the others faded. */
    void (*area_badge)(View *v, cairo_t *cr, int area, double right, double top, double h, gboolean active,
                       const double *fg, const double *bg, void *data);
    /* The bar of actions (VIEW_BAR_*): a button pressed; and each button's label, the icon drawn when
     * the label does not fit (ViewBarIcon, set already to the button's usual one) and state (ViewBarState). */
    void (*bar)(View *v, int item, void *data);
    int (*bar_state)(View *v, int item, char *label, int size, int *icon, void *data);
    /* A tap on square `sq` (the pointer, or Select on the keys' cursor) before the view acts on it:
     * TRUE takes it (a lesson asking which square). */
    gboolean (*square_tap)(View *v, int sq, void *data);
} ViewCallbacks;

/* Chat takes Hint's place in network games (Hint is left out there), so the others keep theirs. */
/* ASK, YES and NO: a question to the player (its text, not a button) and its two answers, shown
 * in the others' place while it lasts. */
enum {
    VIEW_BAR_MENU, VIEW_BAR_UNDO, VIEW_BAR_HINT, VIEW_BAR_CHAT, VIEW_BAR_PREV, VIEW_BAR_NEXT, VIEW_BAR_INFO,
    VIEW_BAR_ASK, VIEW_BAR_YES, VIEW_BAR_NO, VIEW_BAR_COUNT
};
/* Not in the bar: ⌄ at the end of a single row of moves (portrait), to the bar callback. */
enum { VIEW_MOVES = 100 };
/* A bar button left out, shown but not usable now, usable, or usable with a dot (news). */
typedef enum { BAR_HIDDEN = -1, BAR_OFF, BAR_ON, BAR_ALERT } ViewBarState;
/* The icon of a button whose word does not fit: the word's first letter, an arrow turning back, a
 * bulb, ‖ or ▶. */
typedef enum { BAR_ICON_LETTER, BAR_ICON_UNDO, BAR_ICON_HINT, BAR_ICON_PAUSE, BAR_ICON_RESUME } ViewBarIcon;

View *view_new(Game *g, const ViewCallbacks *cb, void *data);
/* Another game to show (the one to come, behind a lobby), or the usual one again. */
void view_set_game(View *v, Game *g);
Game *view_game(View *v);
GtkWidget *view_widget(View *v);
void blit_frame(GtkWidget *w, cairo_surface_t *frame, GdkRegion *region, GdkImage **gray, gboolean mono);
void in_bands(const GdkRegion *region, GdkWindow *win, void (*send)(GdkRegion *band, void *data), void *data);
void paint_in_bands(GdkWindow *win);
GdkRegion *take_pending_area(GdkWindow *win);
int eink_busy_ms(void);
gboolean painting_in_bands(void);
gboolean in_one_band(const GdkRegion *region, GdkWindow *win);
void view_set_style(View *v, PieceStyle *s, const PieceTheme *theme, const BoardColors *colors);
/* A board of another variant in the game's place (Preferences, a variant's page): `g` in `variant`
 * with `s`, set only while the view draws, so the rest of the program goes on with the game; taps
 * on the board do nothing meanwhile. NULL `g` shows the game again. */
void view_preview(View *v, Game *g, int variant, PieceStyle *s);
void view_set_flipped(View *v, gboolean flipped);
/* Taps edit the position (game->pos) instead of moving pieces. */
void view_set_editing(View *v, gboolean editing);
enum { EDIT_BEFORE, EDIT_CHANGED, EDIT_SELECTED };
/* Editor tools: move and select pieces (a drag on empty squares selects a rectangle), erase,
 * or place `piece` (a tap on the same piece removes it; a drag paints). */
typedef enum { TOOL_MOVE, TOOL_ERASE, TOOL_PLACE } EditTool;
void view_set_edit_tool(View *v, EditTool tool, int piece);
/* Operations on the selected squares; they return FALSE when nothing is selected. */
typedef enum { EDIT_DELETE, EDIT_PROMOTE, EDIT_FLIP, EDIT_SELECT_ALL, EDIT_DESELECT } EditOp;
gboolean view_edit_op(View *v, EditOp op);
int view_edit_selected(View *v);
/* The premove chosen during the opponent's turn, if any (taking it clears it). */
gboolean view_take_premove(View *v, Move *m);
void view_clear_premove(View *v);
gboolean view_flipped(View *v);
void view_set_japanese(View *v, gboolean ja);
/* Move texts in Japanese (▲７六歩) or Western (☗P-7f) notation. */
void view_set_notation_ja(View *v, gboolean ja);
void view_set_coords(View *v, gboolean show);
/* -1 shows the live position; otherwise the position after `ply` moves (read only). */
void view_set_ply(View *v, int ply);
int view_ply(View *v);
void view_set_hint(View *v, const Move *m);
/* A lesson's squares to reach, drawn as stars on the live position (n = 0: none). */
void view_set_goals(View *v, const int *squares, int n);
/* An analysis' best moves (up to 5, the best first) for the position after `ply` moves, drawn as
 * arrows while that position is shown; n = 0 takes them away. */
void view_set_best(View *v, const Move *moves, int n, int ply);
/* The mark of the move shown (?!, ?, ??) also on its square. */
void view_set_board_marks(View *v, gboolean show);
/* Marks after the moves in the list: marks[i] for move i, 0 none, 1 ?!, 2 ?, 3 ??. */
void view_set_marks(View *v, const char *marks, int n);
void view_set_show_moves(View *v, gboolean show);
/* Promote without asking: 0 never, 1 pieces that keep their moves (pawn, bishop, rook), 2 all. */
void view_set_auto_promote(View *v, int mode);
/* A picked-up promoted piece shows its unpromoted side in the square's corner. */
void view_set_other_side(View *v, gboolean show);
/* Chess mode: sente's pieces white and gote's black, all facing the same way. */
void view_set_chess_colors(View *v, gboolean on);
void view_refresh(View *v);
/* The board size or pieces changed outside a move (another variant): rebuild the layout and
 * redraw everything. */
void view_reload(View *v);
/* Moves offered: 0 legal only, 1 every move a piece can make, 2 any square (the app then
 * decides: illegal moves lose). */
void view_set_physical(View *v, int physical);
/* A dragged piece drawn under the pointer (not on e-ink, where a drop still plays, and where a
 * move is redrawn in two small updates instead of one). */
void view_set_live_drag(View *v, gboolean on);
/* Marks what the mouse is over (not on touch screens). */
void view_set_hover(View *v, gboolean on);
void view_refresh_clocks(View *v);
/* The bar's buttons changed (a label, enabled or not): the view keeps them from one call to the next. */
void view_queue_bar(View *v);
/* The bar's button at (x, y) in the view, -1 for none; pressing one. */
int view_bar_item_at(View *v, double x, double y);
void view_bar_activate(View *v, int item);
/* Where the bar is, in the view. */
void view_bar_area(View *v, GdkRectangle *r);
/* A short text shown with the moves (a check, the result); "" for none. */
void view_set_notice(View *v, const char *text);
/* The bar's button (VIEW_BAR_*) a tap on the notice presses, -1 for none: set after the notice. */
void view_set_notice_item(View *v, int item);
/* Playing from the keyboard or a controller: a cursor on the board. Arrows show it, then move
 * it; Select taps its square (or answers the promotion question); Cancel drops the selection,
 * then hides it; Hand picks the next piece in hand. FALSE when there was nothing to do. */
typedef enum { CURSOR_UP, CURSOR_DOWN, CURSOR_LEFT, CURSOR_RIGHT, CURSOR_SELECT, CURSOR_CANCEL, CURSOR_HAND } ViewCursor;
gboolean view_cursor(View *v, ViewCursor a);
/* The areas the keyboard or controller is in: the board's cursor, a piece in hand, the moves. */
enum { VIEW_AREA_BOARD, VIEW_AREA_HAND, VIEW_AREA_MOVES, VIEW_AREA_BAR }; /* VIEW_AREA_BAR + a VIEW_BAR_*: that button */
/* Shows each area's key or button (area_badge), the one in use brighter; `browsing` puts the
 * moves in use. */
void view_set_area_hints(View *v, gboolean show, gboolean browsing);
/* ⏮ ◀ ▶ ⏭ through the moves. */
typedef enum { STEP_FIRST, STEP_PREV, STEP_NEXT, STEP_LAST } ViewStep;
void view_step(View *v, ViewStep step);
/* Something drawn over the whole view (the menu as a sheet): it draws last and takes the pointer
 * while set. */
typedef struct {
    void (*draw)(View *v, cairo_t *cr, double w, double h, void *data);
    void (*press)(View *v, double x, double y, void *data);
    void (*motion)(View *v, double x, double y, void *data);
    void (*release)(View *v, double x, double y, void *data);
    /* Optional: a rectangle it covers with paint, where the game under it is not drawn at all (on the
     * Kindle the menu's tiles took 170 ms to draw over a board drawn for nothing). */
    gboolean (*opaque)(View *v, GdkRectangle *r, void *data);
} ViewOverlay;
/* NULL takes it away. */
void view_set_overlay(View *v, const ViewOverlay *o, void *data, const GdkRectangle *area);
/* Landscape: the column at the right (panels, moves, bar), the window's height; portrait: the view. */
void view_column_area(View *v, GdkRectangle *r);
gboolean view_landscape(View *v);
/* Landscape: the board at the view's left edge instead of centered with the column (the chat
 * sits beside it); and the width the board and the column take at height `h`, at most. */
void view_set_align_left(View *v, gboolean on);
int view_natural_width(View *v, int h);
/* The game's view as it is now, drawn as vectors into an SVG file (the drawers are GTK's, not in it). */
gboolean view_save_svg(View *v, const char *path);
int view_natural_height(View *v, int w, int h);
/* Only the board, as large as it fits (a drawer under it in portrait). */
void view_set_board_only(View *v, gboolean on);
void view_set_dimmed(View *v, gboolean on);
/* The part of the view under a drawer, which it does not draw (NULL: none). */
void view_set_covered(View *v, const GdkRectangle *r);
/* Room kept at the right (a drawer beside the game): the game is laid out, centered, in the rest. */
void view_set_inset(View *v, int inset);
/* Where the game ends at the right, for a view of W×H: the column's edge in landscape (the board
 * and the column are centered on wide screens), else W less the inset. */
int view_game_right(View *v, int W, int H);
/* A mouse, which can hover (see view_set_hover). */
gboolean view_hover(View *v);
const BoardColors *view_colors(View *v);
/* Drawing in the view's style: the fill of panels (level 0) and of buttons on them (1), a rounded
 * box, a label centered in a box (shrunk to fit), a text's width. */
void view_panel_color(View *v, cairo_t *cr, int level);
void view_rounded(cairo_t *cr, double x, double y, double w, double h, double r);
void view_label(cairo_t *cr, const char *text, double x, double y, double w, double h, gboolean bold);
double view_text_width(cairo_t *cr, const char *text, double size);
/* A passing notice at the top of the view, gone after a few seconds (one or two lines). */
void view_toast(View *v, const char *text);
/* Paints the board black for a moment so the e-ink does a full refresh. */
void view_flash(View *v);

#endif
