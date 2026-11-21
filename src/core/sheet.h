#ifndef SHEET_H
#define SHEET_H

#include "view.h"

/* A sheet of tiles drawn over the view (the menu): from the bottom in portrait, over the column at
 * the right in landscape, wider (over the board) when its labels need the room. It closes with
 * its × button, a drag towards the edge it came from, a tap outside it, or Cancel. */
typedef struct {
    const char *label;
    int id;
    gboolean enabled;
} SheetTile;

typedef struct {
    /* A tile chosen; the sheet stays open (the callee closes it or shows another page). */
    void (*activate)(int id);
    /* A tile's icon, in the current source, in the square (x, y, s). */
    void (*icon)(cairo_t *cr, int id, double x, double y, double s);
    /* The back arrow of a page under another; NULL for a top page. */
    void (*back)(void);
} SheetCallbacks;

/* Shows the tiles (copied) under `title`, replacing what the sheet showed; the keyboard's mark on
 * tile `focus`, -1 for none. */
void sheet_show(View *v, const char *title, const SheetTile *tiles, int n, const SheetCallbacks *cb, int focus);
void sheet_close(void);
/* The top page's tiles: every page is as large as it, and sheet_size() gives that size. */
void sheet_set_reference(const SheetTile *tiles, int n);
gboolean sheet_size(View *v, int *w, int *h);
/* The sheet's unit (1 at the bar's least height) and its round × or ‹ button, `d` wide, for the
 * drawers' heads. */
double sheet_unit(View *v);
void sheet_round_button(View *v, cairo_t *cr, double x, double y, double d, gboolean close, gboolean lit);
gboolean sheet_is_open(void);
/* The keyboard or controller (arrows, Select, Cancel as in ViewCursor): FALSE when closed. */
gboolean sheet_cursor(ViewCursor a);
/* Whether a drag moves the sheet along with the finger (not on e-ink, where it closes on release). */
void sheet_set_live(gboolean live);
/* The tiles scrolled down (dir 1) or up (-1), when they do not fit: the controller's right stick. */
void sheet_scroll(int dir);

#endif
