#ifndef LAYOUT_H
#define LAYOUT_H

#include <stdbool.h>

/* Where everything on the game screen goes, for a window's size: arithmetic only, no GTK
 * (tests/test_layout.c tries it over a grid of sizes). */

typedef struct { double x, y, w, h; } Rect;

typedef struct {
    /* What is asked. */
    int board_w, board_h;
    double square_ratio; /* a square's height over its width */
    int usual_w;         /* the usual board's width, whose squares the text and the hands are measured by */
    int hand_kinds;  /* the kinds of piece a hand can hold: it keeps room for one of each, so
                      * captures never change the pieces' size */
    int bottom;      /* the side drawn at the bottom */
    bool coords;     /* a band for the coordinates around the board */
    bool board_only; /* only the board, as large as it fits */
    bool align_left; /* landscape: the board at the left edge */
    /* The answer. */
    bool landscape;
    double sw, sh, bx, by, band; /* a square's size, the board's corner, the coordinates' band */
    Rect side[2], hand[2], label[2], clock[2], kifu, bar;
    double hand_cell[2]; /* each hand's piece size and pieces in a row */
    double hand_min; /* the smallest a piece in hand may be: half a square, or less on a small screen */
    int hand_cols[2];
    int kifu_rows;       /* portrait: rows of moves, each kifu_row_h high */
    double kifu_row_h;
} Layout;

void layout_compute(Layout *l, double W, double H);
/* The bar's height in a view of W×H with all of the game in it (also while only the board shows). */
double layout_bar_height(double W, double H);
/* Landscape: the narrowest window `H` high that holds the board and the column at their largest.
 * Portrait: the least height of a window W×H that keeps the board as wide as `W` allows (the sizes
 * besides the board are those of a window `H` high). Both take the board and the coordinates from `l`. */
double layout_natural_width(const Layout *l, double H);
double layout_natural_height(const Layout *l, double W, double H);
/* The place of the `index`th kind of piece in a hand. */
void layout_hand_rect(const Layout *l, int color, int index, Rect *r);

#endif
