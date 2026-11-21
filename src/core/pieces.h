#ifndef PIECES_H
#define PIECES_H

#include <cairo.h>
#include <stdbool.h>

typedef struct {
    double fill[3], fill2[3], border[3], ink[3], red[3];
    bool gradient;
    /* Chess mode: 1 the black side, 2 the white, whose symbols are drawn around in `outline`; the
     * symbol in ink (red when promoted); 3 and 4 the animals' light and dark tiles. */
    int chess;
    double outline[3];
} PieceTheme;

extern const PieceTheme THEME_MONO, THEME_MONO_DARK, THEME_COLOR, THEME_COLOR_ON_DARK;

typedef struct PieceStyle PieceStyle;

/* Loads a style file (see data/styles/), or a folder or zip of piece pictures (style_is_pictures()). Font
 * names are looked up next to the file, then in font_dir. */
PieceStyle *style_load(const char *path, const char *font_dir);
/* Whether `path` (a folder or a zip) is a set of pictures: 0FU.png, FU.png and so on, with an optional style.ini. */
bool style_is_pictures(const char *path);
void style_free(PieceStyle *s);
/* The theme for pieces drawn over `bg`: text-only pieces (no koma) need ink that contrasts
 * with what is behind them; other styles keep `th`. */
bool style_no_shape(const PieceStyle *s);
const PieceTheme *style_theme_on(const PieceStyle *s, const PieceTheme *th, const double *bg);
/* Chess mode: the theme of the first player's pieces (black) or the second's (white), for `th`'s
 * screen (`bg`, what is behind the piece, is not used yet). */
const PieceTheme *style_chess_theme(const PieceStyle *s, const PieceTheme *th, bool first, const double *bg);
/* Square tiles (the animals): in chess mode their tiles take the side's color. */
bool style_square(const PieceStyle *s);
const char *style_name(const PieceStyle *s, int lang); /* lang: a Language (i18n.h) */
/* The variant a style is made for (Variant= in the file, as variant_name()), or NULL for all. */
const char *style_variant(const PieceStyle *s);
/* Offered for `variant` (-1: the row for all variants): Variant= and NotFor= in its [Style]. */
bool style_serves(const PieceStyle *s, int variant);

/* Draws a piece (a kind: type from rules.h, or SECOND_KING) inside w×h at (x, y); upside_down for the
 * player at the top. Uses a cache keyed by size, so repeated draws are cheap. */
void style_draw(PieceStyle *s, const PieceTheme *theme, cairo_t *cr, int type, bool upside_down,
                double x, double y, double w, double h);
/* Only the symbol of a piece, without the koma: the first kanji in a plain font, or the picture
 * of a pictorial style; ink chosen to contrast with `bg`. */
void style_draw_symbol(PieceStyle *s, cairo_t *cr, int type, bool upside_down, const double *bg, double x, double y,
                       double w, double h);
/* Drawn straight onto the target instead of through the cache of pictures, for a vector one (an SVG):
 * all but the pieces that are pictures or made from pixels (chess colors), which stay pictures. */
void style_set_vector(bool on);
/* Fills the cache for one piece without painting (an idle prewarm before a first draw). */
void style_cache(PieceStyle *s, const PieceTheme *theme, int type, double w, double h, bool upside_down);
/* Fills the cache for every type in both orientations at one size (a style change behind a
 * dialog, so the next board redraw has no glyph rasterization). */
/* Small marks on the piece edge showing where it moves (dots: one step, bars: any distance). */
void style_set_move_marks(PieceStyle *s, bool marks);
void style_set_shape(PieceStyle *s, bool shape);
/* The background of each piece in its own color, on color themes (Preferences → Colored pieces). */
void style_set_colored(PieceStyle *s, bool colored);

#endif
