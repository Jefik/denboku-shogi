#include "view.h"

#ifndef DENBOKU_KINDLE /* the Kindle's cairo has no SVG */
#include <cairo-svg.h>
#endif
#include <math.h>
#include <string.h>

#include "app.h"
#include "i18n.h"
#include "layout.h"
#include "rules.h"

const BoardColors COLORS_MONO = {{1, 1, 1}, {1, 1, 1}, {0, 0, 0}, {0, 0, 0}, TRUE, {0, 0, 0}, {1, 1, 1}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
const BoardColors COLORS_DARK = {{0, 0, 0}, {0, 0, 0}, {1, 1, 1}, {1, 1, 1}, TRUE, {1, 1, 1}, {0, 0, 0}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}};

/* A button of the bar: ViewBarState, ViewBarIcon, its word and its place. */
typedef struct {
    int state, icon;
    char label[64];
    Rect rect;
} BarItem;

struct View {
    GtkWidget *area;
    Game *game;
    ViewCallbacks cb;
    void *data;
    PieceStyle *style;
    const PieceTheme *theme;
    const BoardColors *colors;
    gboolean flipped, japanese, notation_ja, show_coords, show_moves;
    int ply;

    /* Selection: a board square or a piece type in hand (for a drop). */
    int sel_sq, sel_drop;
    gboolean other_side; /* show the unpromoted side of a picked-up promoted piece */
    gboolean chess; /* chess mode: each side's pieces in its color, all facing the same way */
    int peek_sq; /* a promoted piece tapped to see its other side, as when picked up from a real board */
    int hover_sq; /* square under the mouse while a piece is selected */
    int hover_kifu; /* the move or history button (kifu_at()) under the mouse, -1 for none */
    gboolean hover_expand; /* the mouse is over ⌄ (kifu_expand) */
    gboolean hover;  /* a mouse: touch screens leave the pointer where the last tap was */
    gboolean editing;
    /* The editor: tool, selected squares, a piece picked from a hand, and the drag going on
     * (painting with the tool, moving the pieces pressed, or a selection rectangle). */
    EditTool tool;
    int tool_piece;
    gboolean ed_sel[81];
    int ed_hand_color, ed_hand_type;
    enum { DRAG_NONE, DRAG_PAINT, DRAG_MOVE, DRAG_RECT } ed_drag;
    int ed_from, ed_cur;
    uint8_t ed_paint;
    gboolean ed_was_selected;
    /* Plan arrows and circles (from == to), as on lichess: right-drag; on touch a drag, or a
     * press held on a square for a circle.
     * They last until the next move (arrows_ply). */
    int arrow_from[32], arrow_to[32], narrows, arrows_ply, drag_sq, hold_sq;
    guint32 press_time; /* X event time: the e-ink redraw after a tap must not make it a hold */
    gboolean pressed;   /* button 1 went down here: a release after a press elsewhere (on the menu that it closed) is ignored */
    int press_count;    /* moves made when pressed: a tap that moved is not a hold */
    /* A premove: chosen during the opponent's turn on a copy of the position with our side to
     * move (pre_pos), played when our turn comes if it is legal then. */
    gboolean pre, has_premove;
    Position pre_pos;
    Move premove;
    Move targets[MAX_MOVES];
    gboolean legal_to[81]; /* targets that are legal: the only ones marked on the board */
    int ntargets;
    /* Promotion question for this move (from/to set), or none. */
    gboolean asking, flashing;
    Move ask;
    Move hint;
    gboolean has_hint;
    int goals[81], ngoals; /* a lesson's squares to reach (view_set_goals) */
    /* An analysis' best moves for the position after best_ply moves (arrows, the first thickest),
     * and whether a marked move shows its mark on the board too. */
    Move best[5];
    int nbest, best_ply;
    gboolean board_marks, any_marks;
    char bar_shown[640]; /* the bar's buttons as last queued (view_queue_bar) */
    /* The bar's buttons as last asked (bar_state) and where they go in the layout, until
     * view_queue_bar or a new layout. */
    BarItem bar_items[VIEW_BAR_COUNT];
    gboolean bar_valid;
    gboolean area_hints, browsing; /* see view_set_area_hints */
    char marks[MAX_PLY];
    /* The keyboard or controller cursor: a square, -1 hidden; on the promotion question the
     * choice it is on (0 promote, 1 keep). `synthetic` while it taps. */
    int cursor_sq, cursor_choice;
    gboolean synthetic;

    cairo_surface_t *frame; /* what is on screen, drawn here first (see expose_cb) */
    Game *preview; /* view_preview()'s game, variant and pieces */
    int preview_variant;
    PieceStyle *preview_style;
    GdkImage *gray;         /* the same in gray, shared with the X server, on gray screens */
    /* What view_refresh last showed, to redraw only what a move changed. */
    gboolean refreshed;
    Position refreshed_pos;
    int refreshed_last[2];
    char shown_text[2][200]; /* each side's clock and name as last drawn */
    guint later;            /* the rest of a move's redraw, shortly after its squares */
    gboolean later_hand[2];
    int later_sq[2];
    int physical; /* 0 legal moves, 1 every move a piece can make, 2 any square */
    int auto_promote; /* 0 ask, 1 promote pieces that keep their moves, 2 every piece */
    Layout lay;
    int hover_bar;     /* the bar button under the mouse, -1 for none */
    const ViewOverlay *overlay; /* drawn over everything, taking the pointer (the menu) */
    gboolean portrait;          /* only the board, as large as it fits (see view_set_board_only) */
    gboolean dimmed;            /* faded behind a drawer over it */
    GdkRectangle covered;       /* under a drawer over it: not drawn */
    int inset;                  /* room kept at the right for a drawer beside the game */
    gboolean align_left;        /* landscape: the board at the left edge (the chat beside it) */
    char toast[256];            /* a passing notice at the top (view_toast), "" for none */
    guint toast_id;
    Rect toast_rect;
    /* A piece dragged: pressed on (PDRAG_PENDING), then moved (PDRAG_ON); where the pointer is;
     * whether a release without moving deselects it (it was picked already); and whether it is
     * drawn under the pointer (not on e-ink, where a drop still plays). */
    enum { PDRAG_NONE, PDRAG_PENDING, PDRAG_ON } pdrag;
    double pdrag_x0, pdrag_y0, pdrag_x, pdrag_y;
    gboolean pdrag_again, live_drag;
    void *overlay_data;
    char notice[256];  /* a check, the result: shown with the moves */
    double notice_h;   /* landscape: the notice's row at the top of the move list */
    Rect notice_box;   /* portrait, rows of moves: the notice over the end of the first row */
    Rect notice_hit;   /* where the notice was drawn */
    int notice_item;   /* the bar's button a tap on the notice presses (a chat line: Chat), -1 none */
    /* Move list: cached texts and the visible range. */
    GPtrArray *notes;
    GArray *note_hashes;  /* position hash after each noted move */
    gboolean notes_ja;
    int kifu_first, kifu_shown, kifu_visible;
    double kifu_step, kifu_inner_x, kifu_cell;
    Rect kifu_expand; /* portrait, one row of moves: the button that opens them all */
    int kifu_cols; /* portrait: the grid of moves' columns */
    /* Scrolling: -1 follows the current move; otherwise the first move shown. A drag (touch)
     * scrolls the list, and a tap without moving goes to a move. */
    int kifu_scroll, kifu_drag_first;
    gboolean kifu_drag, kifu_moved;
    double kifu_drag_x, kifu_drag_y;
    /* Idle prewarm of the piece cache for the current layout sizes. */
    guint warm_id;
    double warm_sw, warm_sh, warm_hand;
};

static Position *shown(View *v, Position *tmp)
{
    if (v->ply < 0)
        return &v->game->pos;
    game_position_at(v->game, v->ply, tmp);
    return tmp;
}

/* Color drawn at the bottom of the screen. */
static int bottom_color(View *v)
{
    return v->flipped ? SECOND : FIRST;
}

static void square_rect(View *v, int sq, Rect *r)
{
    int row = ROW(sq), col = COL(sq);
    if (v->flipped)
        row = board_h - 1 - row, col = board_w - 1 - col;
    *r = (Rect) {v->lay.bx + col * v->lay.sw, v->lay.by + row * v->lay.sh, v->lay.sw, v->lay.sh};
}

static void queue_rect(View *v, Rect r, double grow)
{
    int x = (int) floor(r.x - grow), y = (int) floor(r.y - grow);
    gtk_widget_queue_draw_area(v->area, x, y, (int) ceil(r.x + r.w + grow) - x + 1, (int) ceil(r.y + r.h + grow) - y + 1);
}

/* A square and what its marks reach past it: the frames' strokes are centered a few pixels in,
 * and are 8% of a square wide at most (a large square on a phone). */
static void queue_square(View *v, int sq, double grow)
{
    if (sq < 0 || sq >= SQUARES)
        return;
    Rect r;
    square_rect(v, sq, &r);
    queue_rect(v, r, MAX(grow, ceil(v->lay.sw * 0.05) + 3));
}

static void queue_hover(View *v, int sq) { queue_square(v, sq, 0); }

static int square_at(View *v, double x, double y)
{
    int col = (int) floor((x - v->lay.bx) / v->lay.sw), row = (int) floor((y - v->lay.by) / v->lay.sh);
    if (col < 0 || col >= board_w || row < 0 || row >= board_h)
        return -1;
    if (v->flipped)
        row = board_h - 1 - row, col = board_w - 1 - col;
    return SQ(row, col);
}

/* The kinds of piece a hand can hold: it keeps room for one of each, so captures never change
 * the pieces' size. */
static int hand_kinds(void)
{
    int count;
    hand_order(&count);
    return count;
}

/* What layout.c is asked for this view's board and options. */
static void layout_ask(View *v, Layout *l)
{
    l->board_w = board_w, l->board_h = board_h, l->square_ratio = SQUARE_RATIO, l->usual_w = BOARD_USUAL;
    l->hand_kinds = hand_kinds();
    l->bottom = bottom_color(v);
    l->coords = v->show_coords, l->board_only = v->portrait, l->align_left = v->align_left;
}

/* Where everything goes. */
static void layout(View *v, double W, double H)
{
    layout_ask(v, &v->lay);
    layout_compute(&v->lay, W, H);
    v->bar_valid = FALSE;
}

static void hand_rect(View *v, int color, int index, Rect *r) { layout_hand_rect(&v->lay, color, index, r); }

/* Where the editor's cursor frames a hand: the hand, or its side panel when the hand has no room. */
static Rect cursor_hand_rect(View *v, int color)
{
    return v->lay.hand[color].w > 0 ? v->lay.hand[color] : v->lay.side[color];
}

static void rounded(cairo_t *cr, Rect r, double rad)
{
    cairo_new_sub_path(cr);
    cairo_arc(cr, r.x + r.w - rad, r.y + rad, rad, -G_PI / 2, 0);
    cairo_arc(cr, r.x + r.w - rad, r.y + r.h - rad, rad, 0, G_PI / 2);
    cairo_arc(cr, r.x + rad, r.y + r.h - rad, rad, G_PI / 2, G_PI);
    cairo_arc(cr, r.x + rad, r.y + rad, rad, G_PI, 3 * G_PI / 2);
    cairo_close_path(cr);
}

static void set_rgb(cairo_t *cr, const double *c)
{
    cairo_set_source_rgb(cr, c[0], c[1], c[2]);
}

static void fg(View *v, cairo_t *cr) { set_rgb(cr, v->colors->fg); }
static void bg(View *v, cairo_t *cr) { set_rgb(cr, v->colors->bg); }
static void ink(View *v, cairo_t *cr) { set_rgb(cr, v->colors->lines); }

/* An arrow from square to square in the current source; `scale` 1 for the usual width. */
static void draw_arrow(View *v, cairo_t *cr, int from, int to, double scale)
{
    Rect a, b;
    square_rect(v, from, &a);
    square_rect(v, to, &b);
    double ax = a.x + a.w / 2, ay = a.y + a.h / 2, bx = b.x + b.w / 2, by = b.y + b.h / 2;
    double len = hypot(bx - ax, by - ay), ux = (bx - ax) / len, uy = (by - ay) / len;
    double head = v->lay.sw * 0.38 * (0.5 + scale / 2), width = v->lay.sw * 0.14 * scale;
    double ex = bx - ux * head, ey = by - uy * head;
    cairo_set_line_width(cr, width);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
    cairo_new_path(cr);
    cairo_move_to(cr, ax + ux * v->lay.sw * 0.2, ay + uy * v->lay.sw * 0.2);
    cairo_line_to(cr, ex, ey);
    cairo_stroke(cr);
    cairo_move_to(cr, bx, by);
    cairo_line_to(cr, ex - uy * head * 0.6, ey + ux * head * 0.6);
    cairo_line_to(cr, ex + uy * head * 0.6, ey - ux * head * 0.6);
    cairo_close_path(cr);
    cairo_fill(cr);
}

/* A move of the engine's (a hint, the analysis' best ones): an arrow, or a ring on a drop's square;
 * `weight` 1 for the first, thinner for the others. */
static void draw_engine_move(View *v, cairo_t *cr, const Move *m, double weight)
{
    if (!IS_DROP(*m)) {
        draw_arrow(v, cr, m->from, m->to, weight);
        return;
    }
    Rect r;
    square_rect(v, m->to, &r);
    cairo_set_line_width(cr, v->lay.sw * (weight < 1 ? 0.05 : 0.09));
    cairo_new_path(cr);
    cairo_arc(cr, r.x + r.w / 2, r.y + r.h / 2, v->lay.sw * 0.4, 0, 2 * G_PI);
    cairo_stroke(cr);
}

/* The analysis marks of a move: none, ?!, ?, ??. */
static const char *const MARK_GLYPHS[] = {"", "?!", "?", "??"};
/* The fill of the clock and move list boxes: the hands' color on color screens, which have no frames. */
static void panel(View *v, cairo_t *cr) { set_rgb(cr, v->colors->last_corners ? v->colors->bg : v->colors->hand); }

/* The frame of a box (hands, clocks, move list): black and white draws it bold, as the boxes are
 * told apart by it; on color screens the boxes stand out by their fill alone. */
static void stroke_frame(View *v, cairo_t *cr, double width)
{
    if (!v->colors->last_corners) {
        cairo_new_path(cr);
        return;
    }
    fg(v, cr);
    cairo_set_line_width(cr, width);
    cairo_stroke(cr);
}

/* Panels (level 0) and the buttons on them (1): on color screens each level a step from the
 * hands' color towards the text's; on black and white screens the background, framed. */
void view_panel_color(View *v, cairo_t *cr, int level)
{
    const BoardColors *c = v->colors;
    if (c->last_corners) {
        set_rgb(cr, c->bg);
        return;
    }
    double t = level * 0.07;
    cairo_set_source_rgb(cr, c->hand[0] + (c->fg[0] - c->hand[0]) * t, c->hand[1] + (c->fg[1] - c->hand[1]) * t,
                         c->hand[2] + (c->fg[2] - c->hand[2]) * t);
}

void view_rounded(cairo_t *cr, double x, double y, double w, double h, double r)
{
    rounded(cr, (Rect) {x, y, w, h}, MIN(r, MIN(w, h) / 2));
}

const BoardColors *view_colors(View *v) { return v->colors; }
gboolean view_landscape(View *v) { return v->lay.landscape; }
gboolean view_hover(View *v) { return v->hover; }

/* The board's text font (registered by add_ui_fonts()), or the system's for what it lacks. */
#define UI_FONT "Denboku Sans, Sans"

/* One layout and two font descriptions for all the board's text: making them for every label
 * cost tens of milliseconds a redraw on the Kindle. */
static PangoLayout *text_layout(cairo_t *cr, const char *text, double size, gboolean bold)
{
    static PangoLayout *pl;
    static PangoFontDescription *fd[2];
    if (!pl)
        pl = pango_cairo_create_layout(cr);
    else
        pango_cairo_update_layout(cr, pl);
    if (!fd[bold]) {
        fd[bold] = pango_font_description_from_string(UI_FONT);
        if (bold)
            pango_font_description_set_weight(fd[bold], PANGO_WEIGHT_BOLD);
    }
    pango_font_description_set_absolute_size(fd[bold], size * PANGO_SCALE);
    pango_layout_set_font_description(pl, fd[bold]);
    pango_layout_set_text(pl, text, -1);
    return pl;
}

/* The part being redrawn (several rectangles after a move), while an expose runs. */
static GdkRegion *redrawn;

/* Whether a box reaches the part being redrawn: what does not is skipped. */
static gboolean in_clip(cairo_t *cr, double x, double y, double w, double h)
{
    if (redrawn) {
        GdkRectangle r = {(int) floor(x), (int) floor(y), (int) ceil(w) + 1, (int) ceil(h) + 1};
        return gdk_region_rect_in(redrawn, &r) != GDK_OVERLAP_RECTANGLE_OUT;
    }
    double x0, y0, x1, y1;
    cairo_clip_extents(cr, &x0, &y0, &x1, &y1);
    return x < x1 && x + w > x0 && y < y1 && y + h > y0;
}

static void text_centered(cairo_t *cr, const char *text, double cx, double cy, double size, gboolean bold)
{
    cairo_select_font_face(cr, "Denboku Sans", CAIRO_FONT_SLANT_NORMAL,
                           bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, size);
    cairo_text_extents_t e;
    cairo_text_extents(cr, text, &e);
    cairo_move_to(cr, cx - e.x_bearing - e.width / 2, cy - e.y_bearing - e.height / 2);
    cairo_show_text(cr, text);
}

/* Text centered on (cx, cy), with Pango (the font's fallbacks for kanji). */
static void draw_label(cairo_t *cr, const char *text, double cx, double cy, double size, gboolean bold)
{
    if (!in_clip(cr, cx - size * 4, cy - size, size * 8, size * 2))
        return;
    PangoLayout *pl = text_layout(cr, text, size, bold);
    int w, h;
    pango_layout_get_pixel_size(pl, &w, &h);
    cairo_move_to(cr, cx - w / 2.0, cy - h / 2.0);
    pango_cairo_show_layout(cr, pl);
}

static double text_width(cairo_t *cr, const char *text, double size)
{
    int w, h;
    pango_layout_get_pixel_size(text_layout(cr, text, size, FALSE), &w, &h);
    return w;
}

static void draw_label_fit(cairo_t *cr, const char *text, Rect r, gboolean bold)
{
    if (!in_clip(cr, r.x, r.y, r.w, r.h))
        return;
    double size = r.h * 0.72;
    PangoLayout *pl;
    int w, h;
    for (;;) {
        pl = text_layout(cr, text, size, bold);
        pango_layout_get_pixel_size(pl, &w, &h);
        if (w <= r.w || size < 8)
            break;
        size *= 0.9;
    }
    /* The clock (bold) centered by its ink: the font's line height has room for accents and
     * descenders that "To move" and the digits lack, which left them looking high. A time is
     * centered by the ink of all the digits, so it stays put as round and flat digits come and go. */
    double y = r.y + (r.h - h) / 2.0;
    if (bold) {
        PangoRectangle ink;
        pango_layout_get_pixel_extents(strpbrk(text, "0123456789") ? text_layout(cr, "0123456789", size, TRUE) : pl,
                                       &ink, NULL);
        pl = text_layout(cr, text, size, TRUE);
        y = r.y + (r.h - ink.height) / 2.0 - ink.y;
    }
    cairo_move_to(cr, r.x + (r.w - w) / 2.0, y);
    pango_cairo_show_layout(cr, pl);
}

/* A notice (check, the result, a chat line): shrunk at most to three quarters, then cut with
 * "…" (a chat line's tap opens the chat), not shrunk until a long line fits and nobody can read it. */
static void draw_notice(cairo_t *cr, const char *text, Rect r)
{
    if (!in_clip(cr, r.x, r.y, r.w, r.h))
        return;
    double size = r.h * 0.72, least = MAX(8, size * 0.75);
    PangoLayout *pl;
    int w, h;
    for (;;) {
        pl = text_layout(cr, text, size, TRUE);
        pango_layout_get_pixel_size(pl, &w, &h);
        if (w <= r.w || size * 0.9 < least)
            break;
        size *= 0.9;
    }
    if (w > r.w) {
        pango_layout_set_width(pl, (int) (r.w * PANGO_SCALE));
        pango_layout_set_ellipsize(pl, PANGO_ELLIPSIZE_END);
        pango_layout_get_pixel_size(pl, &w, &h);
    }
    cairo_move_to(cr, r.x + (r.w - w) / 2.0, r.y + (r.h - h) / 2.0);
    pango_cairo_show_layout(cr, pl);
    pango_layout_set_width(pl, -1); /* the layout is shared */
    pango_layout_set_ellipsize(pl, PANGO_ELLIPSIZE_NONE);
}

void view_label(cairo_t *cr, const char *text, double x, double y, double w, double h, gboolean bold)
{
    draw_label_fit(cr, text, (Rect) {x, y, w, h}, bold);
}

double view_text_width(cairo_t *cr, const char *text, double size) { return text_width(cr, text, size); }

/* "number move" with the number right-aligned in a column as wide as three digits, so 1, 10
 * and 100 line up; centered in `r` for the portrait row, left-aligned for the landscape column. */
static void draw_move_text(cairo_t *cr, const char *num, const char *note, Rect r, double size,
                           gboolean centered, gboolean bold)
{
    double num_w, note_w;
    for (;;) {
        num_w = text_width(cr, "999", size);
        note_w = text_width(cr, note, size);
        if (num_w + size * 0.5 + note_w <= r.w || size < 8)
            break;
        size *= 0.9;
    }
    double gap = size * 0.5, total = num_w + gap + note_w;
    double x = centered ? r.x + (r.w - total) / 2 : r.x;
    PangoLayout *pl = text_layout(cr, num, size, bold);
    int w, h;
    pango_layout_get_pixel_size(pl, &w, &h);
    cairo_move_to(cr, x + num_w - w, r.y + (r.h - h) / 2);
    pango_cairo_show_layout(cr, pl);
    pl = text_layout(cr, note, size, bold);
    pango_layout_get_pixel_size(pl, &w, &h);
    cairo_move_to(cr, x + num_w + gap, r.y + (r.h - h) / 2);
    pango_cairo_show_layout(cr, pl);
}

static gboolean edit_marked(View *v, int sq);

/* Chess mode in use: not for the animals without their tiles, which keep their own colors and turns. */
static gboolean chess_on(View *v) { return v->chess && !(style_square(v->style) && style_no_shape(v->style)); }

/* A piece's theme and whether it is drawn upside down: by side, or in chess mode by its color,
 * all facing the same way. */
static const PieceTheme *piece_theme(View *v, int color, const double *under)
{
    if (chess_on(v))
        return style_chess_theme(v->style, v->theme, color == FIRST, under);
    return style_theme_on(v->style, v->theme, under);
}

static gboolean piece_upside(View *v, int color) { return !chess_on(v) && color != bottom_color(v); }

/* A promoted piece picked up shows its unpromoted side: a small piece in the square's corner. */
static void draw_other_side(View *v, cairo_t *cr, const Position *p, int sq)
{
    if (sq < 0 || sq >= SQUARES || !p->board[sq])
        return;
    int t = TYPE(p->board[sq]), base = unpromote_type(t);
    if (base == t)
        return;
    Rect r, b;
    square_rect(v, sq, &r);
    double s = r.w * 0.46;
    b = (Rect) {r.x + r.w - s - 2, r.y + r.h - s * 1.08 - 2, s, s * 1.08};
    rounded(cr, b, 3);
    set_rgb(cr, v->colors->bg);
    cairo_fill_preserve(cr);
    ink(v, cr);
    cairo_set_line_width(cr, 2);
    cairo_stroke(cr);
    style_draw_symbol(v->style, cr, piece_kind(PIECE(base, COLOR(p->board[sq]))), COLOR(p->board[sq]) != bottom_color(v),
                      v->colors->bg, b.x + 2, b.y + 2, b.w - 4,
               b.h - 4);
}
static void edit_motion(View *v, double x, double y);

/* The editor's selection (solid frames), and where a drag would put the pieces or which
 * rectangle it selects (dashed). */
static void draw_edit_marks(View *v, cairo_t *cr)
{
    ink(v, cr);
    cairo_set_line_width(cr, MAX(4, v->lay.sw * 0.06));
    for (int sq = 0; sq < SQUARES; sq++)
        if (v->ed_sel[sq]) {
            Rect r;
            square_rect(v, sq, &r);
            cairo_rectangle(cr, r.x + 3, r.y + 3, r.w - 6, r.h - 6);
        }
    cairo_stroke(cr);
    if (v->ed_drag != DRAG_MOVE && v->ed_drag != DRAG_RECT)
        return;
    double dash[] = {v->lay.sw * 0.1, v->lay.sw * 0.07};
    cairo_set_dash(cr, dash, 2, 0);
    cairo_set_line_width(cr, MAX(3, v->lay.sw * 0.045));
    for (int sq = 0; sq < SQUARES; sq++)
        if (edit_marked(v, sq)) {
            Rect r;
            square_rect(v, sq, &r);
            cairo_rectangle(cr, r.x + 7, r.y + 7, r.w - 14, r.h - 14);
        }
    cairo_stroke(cr);
    cairo_set_dash(cr, NULL, 0, 0);
}

static void draw_strip(View *v, cairo_t *cr, const Position *p, int color)
{
    Rect h = v->lay.side[color];
    rounded(cr, h, 2);
    set_rgb(cr, v->colors->hand);
    cairo_fill_preserve(cr);
    stroke_frame(v, cr, 2);
    int index = 0;
    gboolean upside = piece_upside(v, color);
    int count;
    const int *order = hand_order(&count);
    for (int i = 0; i < count; i++) {
        int t = order[i], n = p->hand[color][hand_slot(t)];
        if (!n)
            continue;
        Rect r;
        hand_rect(v, color, index++, &r);
        style_draw(v->style, piece_theme(v, color, v->colors->hand), cr, piece_kind(PIECE(t, color)), upside, r.x,
                   r.y, r.w, r.h);
        if ((v->ply < 0 && v->sel_drop == t && p->side == color) ||
            (v->editing && v->ed_hand_type == t && v->ed_hand_color == color)) {
            cairo_rectangle(cr, r.x - 2, r.y - 2, r.w + 4, r.h + 4);
            fg(v, cr);
            cairo_set_line_width(cr, 6);
            cairo_stroke(cr);
        }
        if (n > 1) {
            char num[16];
            snprintf(num, sizeof num, "%d", n);
            double rad = r.w * 0.17;
            cairo_new_sub_path(cr);
            cairo_arc(cr, r.x + r.w - rad * 0.6, r.y + r.h - rad * 0.6, rad, 0, 2 * G_PI);
            fg(v, cr);
            cairo_fill(cr);
            bg(v, cr);
            text_centered(cr, num, r.x + r.w - rad * 0.6, r.y + r.h - rad * 0.6, rad * 1.3, TRUE);
        }
    }

    char label[128] = "", clock[64] = "";
    gboolean active = FALSE;
    if (v->cb.player_text)
        v->cb.player_text(v, color, label, clock, &active, v->data);
    snprintf(v->shown_text[color], sizeof v->shown_text[color], "%d%s\n%s", active, clock, label);
    /* The name at the left, as large as the clock's text (shrunk to fit). */
    Rect l = v->lay.label[color];
    /* The clock's pill, as wide as its text at the right of its box; beside it, on the same row, the
     * name takes the room the pill leaves (all of it beside an empty clock). */
    Rect box = v->lay.clock[color];
    double size = MIN(box.h * 0.5, v->lay.label[color].h * 0.75); /* in a low panel, as large as the name's row */
    double ls = MIN(l.h * 0.62, MAX(l.h * 0.5, box.h * 0.5)), least = MAX(9, ls * 0.7);
    gboolean row = l.y == box.y && l.x < box.x;
    if (row && *clock) { /* a name and a pill too wide for the row together: both a little smaller */
        double need = text_width(cr, label, ls) + ls * 0.6 + text_width(cr, clock, size) + size * 2.0;
        double f = MAX(0.6, MIN(1, (box.x + box.w - l.x) / need));
        ls *= f, size *= f;
    }
    while (*clock && size > 8 && text_width(cr, clock, size) + size * 1.6 > box.w)
        size *= 0.92;
    double pw = *clock ? MIN(box.w, text_width(cr, clock, size) + size * 1.6) : 0, ph = MIN(box.h, size * 1.9);
    if (row)
        l.w = MAX(l.w, box.x + box.w - pw - l.x - (pw ? size * 0.4 : 0));
    fg(v, cr);
    if (in_clip(cr, l.x, l.y, l.w, l.h)) {
        while (ls > least && text_width(cr, label, ls) > l.w - ls * 0.6)
            ls *= 0.92;
        /* A name too long even so ends in …, instead of getting too small to read; one with a
         * part in parentheses (the engine's level, a rating) keeps that part whole. */
        char *open = strrchr(label, '(');
        if (open && open > label && label[strlen(label) - 1] == ')' && text_width(cr, label, ls) > l.w - ls * 0.6) {
            char *tail = g_strdup(open), *head = g_strndup(label, open - label);
            g_strchomp(head);
            char *fit = NULL;
            while (*head) {
                g_free(fit);
                fit = g_strconcat(head, "… ", tail, NULL);
                if (text_width(cr, fit, ls) <= l.w - ls * 0.6)
                    break;
                *g_utf8_prev_char(head + strlen(head)) = 0;
            }
            if (fit)
                g_strlcpy(label, fit, sizeof label);
            g_free(fit), g_free(head), g_free(tail);
        }
        PangoLayout *pl = text_layout(cr, label, ls, FALSE);
        pango_layout_set_width(pl, (int) ((l.w - ls * 0.6) * PANGO_SCALE));
        pango_layout_set_ellipsize(pl, PANGO_ELLIPSIZE_END);
        int tw, th;
        pango_layout_get_pixel_size(pl, &tw, &th);
        /* Above the clock (a low panel), at the right as the clock is. */
        gboolean stacked = l.x == v->lay.clock[color].x;
        cairo_move_to(cr, stacked ? l.x + l.w - tw - ls * 0.3 : l.x + ls * 0.3, l.y + (l.h - th) / 2.0);
        pango_cairo_show_layout(cr, pl);
        pango_layout_set_width(pl, -1); /* the layout is everyone's (text_layout) */
        pango_layout_set_ellipsize(pl, PANGO_ELLIPSIZE_NONE);
    }
    /* As in the mockups: the side to move gets a pill in the accent color (solid ink in black and
     * white, which e-ink shows crisply) with "To move" and its clock; the other side its clock
     * alone, in bold. */
    const BoardColors *c = v->colors;
    if (!*clock)
        return;
    Rect pill = {box.x + box.w - pw, box.y + (box.h - ph) / 2, pw, ph};
    if (active) {
        rounded(cr, pill, ph / 2);
        set_rgb(cr, c->last_corners ? c->fg : c->accent);
        cairo_fill(cr);
        bg(v, cr);
    } else {
        fg(v, cr);
    }
    draw_label_fit(cr, clock, (Rect) {pill.x + size * 0.8, pill.y + (ph - size / 0.72) / 2, pw - size * 1.6, size / 0.72}, TRUE);
}

/* The square of the king in check, or -1; none while legal moves are hidden. */
static int checked_king(View *v, const Position *p)
{
    if (!v->show_moves || !variant_shows_check() || !pos_in_check(p))
        return -1;
    for (int sq = 0; sq < SQUARES; sq++)
        if (p->board[sq] && TYPE(p->board[sq]) == KING && COLOR(p->board[sq]) == p->side)
            return sq;
    return -1;
}

/* The king in check: a red glow as on lishogi, or on black and white screens the square in
 * the line color, which e-ink shows crisply. */
static void draw_check(View *v, cairo_t *cr, int sq)
{
    if (sq >= 0) {
        Rect r;
        square_rect(v, sq, &r);
        if (v->colors->last_corners) {
            ink(v, cr);
            cairo_rectangle(cr, r.x, r.y, r.w, r.h);
            cairo_fill(cr);
        } else {
            double cx = r.x + r.w / 2, cy = r.y + r.h / 2;
            cairo_pattern_t *g = cairo_pattern_create_radial(cx, cy, 0, cx, cy, r.w * 0.8);
            cairo_pattern_add_color_stop_rgba(g, 0, 1, 0, 0, 1);
            cairo_pattern_add_color_stop_rgba(g, 0.6, 0.9, 0, 0, 0.9);
            cairo_pattern_add_color_stop_rgba(g, 1, 0.6, 0, 0, 0.3);
            cairo_set_source(cr, g);
            cairo_rectangle(cr, r.x, r.y, r.w, r.h);
            cairo_fill(cr);
            cairo_pattern_destroy(g);
        }
    }
}

/* ◀ / ▶ as shapes: the Kindle fonts lack those characters. */
static void draw_step(cairo_t *cr, Rect r, gboolean forward)
{
    double t = MIN(r.w, r.h) * 0.38, cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    double tip = forward ? cx + t / 2 : cx - t / 2, base = forward ? cx - t / 2 : cx + t / 2;
    cairo_move_to(cr, base, cy - t / 2);
    cairo_line_to(cr, tip, cy);
    cairo_line_to(cr, base, cy + t / 2);
    cairo_close_path(cr);
    cairo_fill(cr);
}

static void draw_board(View *v, cairo_t *cr, const Position *p, int check_sq)
{
    double bw = board_w * v->lay.sw, bh = board_h * v->lay.sh;
    cairo_rectangle(cr, v->lay.bx, v->lay.by, bw, bh);
    set_rgb(cr, v->colors->board);
    cairo_fill(cr);

    int ply = v->ply < 0 ? v->game->count : v->ply;
    if (ply > 0) {
        const Move *last = &v->game->moves[ply - 1];
        Rect r;
        set_rgb(cr, v->colors->last);
        int squares[2] = {IS_DROP(*last) ? -1 : last->from, last->to};
        for (int i = 0; i < 2; i++) {
            if (squares[i] < 0)
                continue;
            square_rect(v, squares[i], &r);
            if (!v->colors->last_corners) {
                cairo_rectangle(cr, r.x, r.y, r.w, r.h);
                cairo_fill(cr);
                continue;
            }
            /* Corner brackets, thicker on the destination. */
            double k = r.w * 0.22, in = 3;
            cairo_set_line_width(cr, i ? MAX(4, r.w * 0.06) : MAX(2, r.w * 0.03));
            for (int c = 0; c < 4; c++) {
                double x = c % 2 ? r.x + r.w - in : r.x + in, y = c / 2 ? r.y + r.h - in : r.y + in;
                double dx = c % 2 ? -k : k, dy = c / 2 ? -k : k;
                cairo_move_to(cr, x + dx, y);
                cairo_line_to(cr, x, y);
                cairo_line_to(cr, x, y + dy);
            }
            cairo_stroke(cr);
        }
    }

    ink(v, cr);
    cairo_set_line_width(cr, 1.5);
    for (int i = 1; i < board_w; i++) {
        cairo_move_to(cr, v->lay.bx + i * v->lay.sw, v->lay.by);
        cairo_line_to(cr, v->lay.bx + i * v->lay.sw, v->lay.by + bh);
    }
    for (int i = 1; i < board_h; i++) {
        cairo_move_to(cr, v->lay.bx, v->lay.by + i * v->lay.sh);
        cairo_line_to(cr, v->lay.bx + bw, v->lay.by + i * v->lay.sh);
    }
    cairo_stroke(cr);
    cairo_set_line_width(cr, v->colors->last_corners ? 4 : 2); /* e-ink wants the bold edge */
    cairo_rectangle(cr, v->lay.bx, v->lay.by, bw, bh);
    cairo_stroke(cr);
    int stars[8][2], nstars = board_stars(stars);
    for (int i = 0; i < nstars; i++) {
        cairo_new_sub_path(cr);
        cairo_arc(cr, v->lay.bx + stars[i][0] * v->lay.sw, v->lay.by + stars[i][1] * v->lay.sh, MAX(3, v->lay.sw * 0.045),
                  0, 2 * G_PI);
        cairo_fill(cr);
    }

    /* Coordinates: files on top, ranks on the right; none on squares narrower than them (a board made
     * tiny, as by the chat over the lobby in portrait). */
    fg(v, cr);
    double fs = v->lay.band * 0.75;
    gboolean coords = v->show_coords && v->lay.sw >= fs * 1.2;
    for (int i = 0; i < board_w && coords; i++) {
        char f[16];
        file_label(v->flipped ? board_w - 1 - i : i, v->japanese, f, sizeof f);
        draw_label(cr, f, v->lay.bx + (i + 0.5) * v->lay.sw, v->lay.by - v->lay.band / 2, fs, FALSE);
    }
    for (int i = 0; i < board_h && coords; i++) {
        char r[16];
        rank_label(v->flipped ? board_h - 1 - i : i, v->japanese, r, sizeof r);
        draw_label(cr, r, v->lay.bx + bw + v->lay.band / 2, v->lay.by + (i + 0.5) * v->lay.sh, fs, FALSE);
    }

    draw_check(v, cr, check_sq);
    double pad = MAX(2, v->lay.sw * 0.03);
    /* Color screens: a soft fill for the selected piece and the legal square under the mouse.
     * E-ink keeps the outline-only marks (partial updates draw only black and white). */
    if (v->ply < 0 && !v->colors->last_corners && (v->sel_sq >= 0 || v->sel_drop)) {
        Rect r;
        if (v->sel_sq >= 0) {
            square_rect(v, v->sel_sq, &r);
            cairo_set_source_rgba(cr, 0.95, 0.82, 0.25, 0.24);
            cairo_rectangle(cr, r.x, r.y, r.w, r.h);
            cairo_fill(cr);
        }
        /* With legal moves hidden, every square lights up: the mark must not give them away. */
        if (v->hover_sq >= 0 && v->hover_sq < SQUARES && v->hover_sq != v->sel_sq &&
            (v->legal_to[v->hover_sq] || !v->show_moves)) {
            square_rect(v, v->hover_sq, &r);
            cairo_set_source_rgba(cr, 0.80, 0.72, 0.12, 0.38);
            cairo_rectangle(cr, r.x, r.y, r.w, r.h);
            cairo_fill(cr);
        }
    }
    for (int sq = 0; sq < SQUARES; sq++) {
        uint8_t pc = p->board[sq];
        if (!pc)
            continue;
        Rect r;
        square_rect(v, sq, &r);
        if (!in_clip(cr, r.x, r.y, r.w, r.h))
            continue;
        int t = piece_kind(pc);
        /* Black and white: the king in check stands on a square filled with the lines' color. */
        const double *under = sq == check_sq && v->colors->last_corners ? v->colors->lines : v->colors->board;
        style_draw(v->style, piece_theme(v, COLOR(pc), under), cr, t, piece_upside(v, COLOR(pc)), r.x + pad, r.y + pad,
                   r.w - 2 * pad, r.h - 2 * pad);
    }
    if (v->ply < 0 && !v->editing && v->other_side) {
        /* With the keys' cursor, only while it is on the piece (as a finger shows it while it holds it). */
        if (v->cursor_sq < 0 || v->cursor_sq == v->sel_sq)
            draw_other_side(v, cr, p, v->sel_sq);
        draw_other_side(v, cr, p, v->peek_sq);
    }

    for (int i = 0; i < v->ngoals && v->ply < 0; i++) { /* stars in the board's ink, under the move marks */
        Rect r;
        square_rect(v, v->goals[i], &r);
        double cx = r.x + r.w / 2, cy = r.y + r.h / 2, out = MIN(r.w, r.h) * 0.36, in = out * 0.42;
        cairo_new_path(cr);
        for (int k = 0; k < 10; k++) {
            double a = -M_PI / 2 + k * M_PI / 5, rad = k % 2 ? in : out;
            cairo_line_to(cr, cx + rad * cos(a), cy + rad * sin(a));
        }
        cairo_close_path(cr);
        ink(v, cr);
        /* where the piece picked up may go: hollow, the move's dot showing inside */
        if (v->show_moves && v->ntargets && v->legal_to[v->goals[i]]) {
            cairo_set_line_width(cr, MAX(2, v->lay.sw * 0.03));
            cairo_stroke(cr);
        } else {
            cairo_fill(cr);
        }
    }
    if (v->ply < 0 && v->sel_sq >= 0) {
        Rect r;
        square_rect(v, v->sel_sq, &r);
        ink(v, cr);
        cairo_set_line_width(cr, MAX(4, v->lay.sw * 0.06));
        cairo_rectangle(cr, r.x + 3, r.y + 3, r.w - 6, r.h - 6);
        cairo_stroke(cr);
    }
    if (v->editing)
        draw_edit_marks(v, cr);
    if (v->ply < 0 && v->show_moves) {
        for (int i = 0; i < v->ntargets; i++) {
            if (!v->legal_to[v->targets[i].to])
                continue;
            Rect r;
            square_rect(v, v->targets[i].to, &r);
            if (p->board[v->targets[i].to]) {
                double dash[] = {v->lay.sw * 0.12, v->lay.sw * 0.07};
                cairo_set_dash(cr, dash, 2, 0);
                ink(v, cr);
                cairo_set_line_width(cr, MAX(3, v->lay.sw * 0.04));
                cairo_rectangle(cr, r.x + 5, r.y + 5, r.w - 10, r.h - 10);
                cairo_stroke(cr);
                cairo_set_dash(cr, NULL, 0, 0);
            } else {
                set_rgb(cr, v->colors->dot);
                cairo_new_sub_path(cr);
                cairo_arc(cr, r.x + r.w / 2, r.y + r.h / 2, v->lay.sw * 0.11, 0, 2 * G_PI);
                cairo_fill(cr);
            }
        }
    }
    if (v->arrows_ply != v->game->count)
        v->narrows = 0;
    for (int i = 0; i < v->narrows; i++) {
        Rect a;
        square_rect(v, v->arrow_from[i], &a);
        double ax = a.x + a.w / 2, ay = a.y + a.h / 2;
        if (v->colors->last_corners)
            ink(v, cr); /* e-ink: solid, no half-tones */
        else
            cairo_set_source_rgba(cr, v->colors->hint[0], v->colors->hint[1], v->colors->hint[2], 0.75);
        if (v->arrow_from[i] == v->arrow_to[i]) {
            cairo_set_line_width(cr, MAX(3, v->lay.sw * 0.08));
            cairo_new_path(cr); /* else the arc starts with a line from the last text drawn */
            cairo_arc(cr, ax, ay, v->lay.sw * 0.43, 0, 2 * G_PI);
            cairo_stroke(cr);
            continue;
        }
        draw_arrow(v, cr, v->arrow_from[i], v->arrow_to[i], 1);
    }
    int shown_ply = v->ply < 0 ? v->game->count : v->ply;
    if (v->nbest && v->best_ply == shown_ply) {
        /* Engine arrows in blue, as on lichess; the best move thickest, then the other lines. */
        for (int i = v->nbest - 1; i >= 0; i--) {
            if (v->colors->last_corners)
                ink(v, cr);
            else
                cairo_set_source_rgba(cr, 0.0, 0.19, 0.53, i ? 0.4 : 0.65);
            draw_engine_move(v, cr, &v->best[i], i ? 0.6 : 1);
        }
    }
    if (v->board_marks && shown_ply > 0 && v->marks[shown_ply - 1]) {
        /* The mark of the move shown, at the corner of its square: ?! blue, ? orange, ?? red. */
        static const double COLORS[][3] = {{0, 0, 0}, {0.34, 0.71, 0.91}, {0.9, 0.62, 0}, {0.87, 0.33, 0.33}};
        int mark = v->marks[shown_ply - 1] & 3;
        Rect r;
        square_rect(v, v->game->moves[shown_ply - 1].to, &r);
        double rad = v->lay.sw * 0.2, cx = r.x + r.w - rad * 0.8, cy = r.y + rad * 0.8;
        if (v->colors->last_corners)
            ink(v, cr);
        else
            cairo_set_source_rgb(cr, COLORS[mark][0], COLORS[mark][1], COLORS[mark][2]);
        cairo_new_path(cr);
        cairo_arc(cr, cx, cy, rad, 0, 2 * G_PI);
        cairo_fill(cr);
        set_rgb(cr, v->colors->last_corners ? v->colors->board : (const double[]){1, 1, 1});
        draw_label(cr, MARK_GLYPHS[mark], cx, cy, rad * 1.1, TRUE);
    }
    if (v->has_premove && v->ply < 0) {
        double dash[] = {v->lay.sw * 0.08, v->lay.sw * 0.06};
        cairo_set_dash(cr, dash, 2, 0);
        ink(v, cr);
        cairo_set_line_width(cr, MAX(3, v->lay.sw * 0.05));
        int squares[2] = {IS_DROP(v->premove) ? -1 : v->premove.from, v->premove.to};
        for (int i = 0; i < 2; i++) {
            if (squares[i] < 0)
                continue;
            Rect r;
            square_rect(v, squares[i], &r);
            cairo_rectangle(cr, r.x + 6, r.y + 6, r.w - 12, r.h - 12);
        }
        cairo_stroke(cr);
        cairo_set_dash(cr, NULL, 0, 0);
    }
    if (v->has_hint && v->ply < 0) {
        /* An arrow, as the engine's in analysis: two dashed squares looked like a glitch. */
        if (v->colors->last_corners)
            ink(v, cr);
        else
            cairo_set_source_rgba(cr, 0.0, 0.19, 0.53, 0.65);
        draw_engine_move(v, cr, &v->hint, 1);
    }
}

/* ---- move list ---- */

static void update_notes(View *v)
{
    Game *g = v->game;
    if (!v->notes) {
        v->notes = g_ptr_array_new_with_free_func(g_free);
        v->note_hashes = g_array_new(FALSE, FALSE, sizeof(uint64_t));
    }
    if (v->notes_ja != v->notation_ja) {
        g_ptr_array_set_size(v->notes, 0);
        g_array_set_size(v->note_hashes, 0);
        v->notes_ja = v->notation_ja;
    }
    /* Reuse the texts of the moves that are still the same (usually all but the newest). */
    int keep = 0;
    while (keep < (int) v->notes->len && keep < g->count &&
           g_array_index(v->note_hashes, uint64_t, keep) == g->hashes[keep + 1])
        keep++;
    if (keep == g->count && keep == (int) v->notes->len)
        return;
    g_ptr_array_set_size(v->notes, keep);
    g_array_set_size(v->note_hashes, keep);
    Position p;
    game_position_at(g, keep, &p);
    for (int i = keep; i < g->count; i++) {
        char text[64];
        Move m = g->moves[i];
        if (m.piece && COLOR(m.piece) != p.side) /* the notation of a lesson's step where the other side passed */
            p.side = COLOR(m.piece);
        move_notation(&p, &m, i ? g->moves[i - 1].to : -1, v->notation_ja, text, sizeof text);
        g_ptr_array_add(v->notes, g_strdup(text));
        g_array_append_val(v->note_hashes, g->hashes[i + 1]);
        game_replay(&p, &m);
    }
}

/* Move `mi` of the list with its analysis mark. */
static const char *note_text(View *v, int mi, char *buf, int size)
{
    snprintf(buf, size, "%s%s", (char *) g_ptr_array_index(v->notes, mi), MARK_GLYPHS[v->marks[mi] & 3]);
    return buf;
}

/* A faint fill under the mouse. */
static void hover_fill(View *v, cairo_t *cr, Rect r)
{
    const double *c = v->colors->fg;
    cairo_set_source_rgba(cr, c[0], c[1], c[2], 0.15);
    cairo_rectangle(cr, r.x, r.y, r.w, r.h);
    cairo_fill(cr);
}

/* The current move in the list, leaving the source set for its text: solid ink in black and
 * white; on color screens a tint of the palette's accent with a bar at its left. */
static void current_fill(View *v, cairo_t *cr, Rect r)
{
    const BoardColors *c = v->colors;
    cairo_rectangle(cr, r.x, r.y, r.w, r.h);
    if (c->last_corners) {
        fg(v, cr);
        cairo_fill(cr);
        bg(v, cr);
        return;
    }
    cairo_set_source_rgb(cr, c->hand[0] + (c->accent[0] - c->hand[0]) * 0.28, c->hand[1] + (c->accent[1] - c->hand[1]) * 0.28,
                         c->hand[2] + (c->accent[2] - c->hand[2]) * 0.28);
    cairo_fill(cr);
    set_rgb(cr, c->accent);
    cairo_rectangle(cr, r.x, r.y, MAX(3, r.h * 0.1), r.h);
    cairo_fill(cr);
    fg(v, cr);
}

static void draw_kifu(View *v, cairo_t *cr)
{
    Rect k = v->lay.kifu;
    rounded(cr, k, 2);
    panel(v, cr);
    cairo_fill_preserve(cr);
    stroke_frame(v, cr, 2);
    /* Landscape: ⏮ ◀ ▶ ⏭ in a row below the moves, as tall as one. */
    double size = CLAMP(v->lay.bar.h * 0.3, 14, 36), step = size * 1.6;
    /* One row of moves in portrait: ⌄ at its end opens them all (VIEW_MOVES). */
    v->kifu_expand = (Rect) {0, 0, 0, 0};
    if (!v->lay.landscape && v->lay.kifu_rows <= 1 && v->cb.bar) {
        double e = k.h;
        v->kifu_expand = (Rect) {k.x + k.w - e, k.y, e, k.h};
        if (v->hover_expand)
            hover_fill(v, cr, (Rect) {k.x + k.w - e + 3, k.y + 3, e - 6, k.h - 6});
        fg(v, cr);
        cairo_set_line_width(cr, MAX(2, e * 0.07));
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        double cx = k.x + k.w - e / 2, cy = k.y + k.h / 2, a = e * 0.17;
        cairo_move_to(cr, cx - a, cy - a / 2);
        cairo_line_to(cr, cx, cy + a / 2);
        cairo_line_to(cr, cx + a, cy - a / 2);
        cairo_stroke(cr);
        k.w -= e;
    }
    /* A notice (check, the result), where it never moves the moves about: in landscape a row kept
     * for it above the list; in portrait at the end of the row, and with rows of moves at the end
     * of the last one shown, or on an empty row below it, where it covers no move if it can (drawn
     * after them, below). */
    gboolean grid = !v->lay.landscape && v->lay.kifu_rows > 1;
    v->notice_h = 0;
    v->notice_box = v->notice_hit = (Rect) {0, 0, 0, 0};
    if (v->lay.landscape) {
        Rect n = {k.x, k.y, k.w, step * 1.3};
        fg(v, cr);
        if (*v->notice) {
            draw_notice(cr, v->notice, (Rect) {n.x + 6, n.y + n.h * 0.15, n.w - 12, n.h * 0.7});
            v->notice_hit = n;
        }
        v->notice_h = n.h, k.y += n.h, k.h -= n.h;
    } else if (*v->notice) {
        double ns = v->lay.kifu_row_h * 0.42, nw = MIN(k.w * 0.6, text_width(cr, v->notice, ns) + ns * 2);
        Rect n = {k.x + k.w - nw, k.y, nw, grid ? v->lay.kifu_row_h : k.h};
        if (grid) {
            v->notice_box = n;
        } else {
            fg(v, cr);
            draw_notice(cr, v->notice, (Rect) {n.x + 6, n.y + n.h * 0.15, n.w - 12, n.h * 0.7});
            v->notice_hit = n;
            k.w -= nw;
        }
    }
    if (v->lay.landscape || v->lay.kifu_rows > 1) {
        /* A big, faint 墨 behind the moves, centered on the panel with the notice's row, without
         * the buttons (not behind a single row, where it would be small). */
        Rect m = k;
        m.y -= v->notice_h, m.h += v->notice_h;
        static PangoLayout *sumi;
        static PangoFontDescription *fd;
        static double sumi_size;
        if (!sumi) {
            sumi = pango_cairo_create_layout(cr);
            pango_layout_set_text(sumi, "墨", -1);
            fd = pango_font_description_from_string("Yuji Syuku, " UI_FONT);
        } else {
            pango_cairo_update_layout(cr, sumi);
        }
        if (MIN(m.w, m.h) * 0.8 != sumi_size) {
            sumi_size = MIN(m.w, m.h) * 0.8;
            pango_font_description_set_absolute_size(fd, sumi_size * PANGO_SCALE);
            pango_layout_set_font_description(sumi, fd);
        }
        PangoRectangle ink; /* centered by its strokes: the line's box has room above and below */
        pango_layout_get_pixel_extents(sumi, &ink, NULL);
        cairo_save(cr);
        cairo_rectangle(cr, m.x, m.y, m.w, m.h);
        cairo_clip(cr);
        const double *c = v->colors->fg;
        cairo_set_source_rgba(cr, c[0], c[1], c[2], 0.07);
        cairo_move_to(cr, m.x + (m.w - ink.width) / 2.0 - ink.x, m.y + (m.h - ink.height) / 2.0 - ink.y);
        pango_cairo_show_layout(cr, sumi);
        cairo_restore(cr);
    }
    update_notes(v);
    int n = (int) v->notes->len, cur = (v->ply < 0 ? v->game->count : v->ply) - 1;
    double pad = 8;
    if (!v->lay.landscape) {
        /* Rows of moves; fixed-width cells so taps map simply; a drag scrolls them. */
        /* Each cell as wide as a long move at the row's text size, plus a gap, so moves neither
         * shrink nor touch. */
        double rh = v->lay.kifu_row_h;
        size = rh * 0.5;
        int rows = MAX(1, v->lay.kifu_rows);
        double cell = MAX(size * 6.5, text_width(cr, NOTATION_WIDEST[v->notation_ja ? 0 : 1], (rh - 6) * 0.72) +
                                          size * 1.2);
        v->kifu_inner_x = k.x;
        int cols = MAX(1, (int) ((k.w - 2 * pad) / cell)), first;
        if (rows == 1) {
            int max_first = MAX(0, n - cols);
            first = v->kifu_scroll >= 0 ? CLAMP(v->kifu_scroll, 0, max_first)
                                        : MIN(MAX(0, MAX(cur, 0) - cols + 2), max_first);
        } else {
            /* Whole rows: the current move's row is the last one shown. */
            int max_row = MAX(0, (n + cols - 1) / cols - rows);
            int row = v->kifu_scroll >= 0 ? v->kifu_scroll / cols : MAX(cur, 0) / cols - rows + 1;
            first = CLAMP(row, 0, max_row) * cols;
        }
        v->kifu_first = first, v->kifu_shown = MIN(cols * rows, n - first);
        v->kifu_step = rows > 1 ? rh : cell;
        v->kifu_visible = cols * rows, v->kifu_cols = cols, v->kifu_cell = cell;
        for (int i = 0; i < v->kifu_shown; i++) {
            int mi = first + i;
            Rect c = {k.x + pad + i % cols * cell, k.y + i / cols * rh + 3, cell - 4, rh - 6};
            char num[16];
            snprintf(num, sizeof num, "%d", mi + 1);
            if (mi == cur) {
                current_fill(v, cr, c);
            } else {
                if (mi == v->hover_kifu)
                    hover_fill(v, cr, c);
                fg(v, cr);
            }
            char note[80];
            draw_move_text(cr, num, note_text(v, mi, note, sizeof note), c, c.h * 0.72, TRUE, FALSE);
        }
        if (v->notice_box.w > 0) {
            Rect b = v->notice_box;
            int last = MAX(0, v->kifu_shown - 1) / cols, in_last = v->kifu_shown - last * cols;
            if (k.w - pad - in_last * cell >= b.w)
                b.y = k.y + last * rh;
            else if (last + 1 < rows)
                b.y = k.y + (last + 1) * rh;
            v->notice_box = b;
            cairo_rectangle(cr, b.x, b.y + 2, b.w - 2, b.h - 4);
            panel(v, cr);
            cairo_fill(cr);
            fg(v, cr);
            draw_notice(cr, v->notice, (Rect) {b.x + 6, b.y + b.h * 0.15, b.w - 12, b.h * 0.7});
            v->notice_hit = b;
        }
        return;
    }
    {
        int rows = MAX(1, (int) ((k.h - 2 * pad) / step));
        int max_first = MAX(0, n - rows);
        int first = v->kifu_scroll >= 0 ? CLAMP(v->kifu_scroll, 0, max_first)
                                        : MIN(MAX(0, MAX(cur, 0) - rows + 2), max_first);
        v->kifu_first = first, v->kifu_shown = MIN(rows, n - first), v->kifu_step = step;
        v->kifu_visible = rows;
        for (int i = 0; i < v->kifu_shown; i++) {
            int mi = first + i;
            double y = k.y + pad + i * step;
            char num[16];
            snprintf(num, sizeof num, "%d", mi + 1);
            if (mi == cur) {
                current_fill(v, cr, (Rect) {k.x + 3, y, k.w - 6, step});
            } else {
                if (mi == v->hover_kifu)
                    hover_fill(v, cr, (Rect) {k.x + 3, y, k.w - 6, step});
                fg(v, cr);
            }
            if (!in_clip(cr, k.x, y, k.w, step))
                continue;
            char note[80];
            draw_move_text(cr, num, note_text(v, mi, note, sizeof note),
                           (Rect) {k.x + pad, y, k.w - 2 * pad, step}, size, FALSE, FALSE);
        }
    }
}

/* Index of the move under (x, y) in the move list, or -1. */
static int kifu_at(View *v, double x, double y)
{
    Rect k = v->lay.kifu;
    if (k.w <= 0 || x < k.x || x >= k.x + k.w || y < k.y || y >= k.y + k.h)
        return -1;
    int i;
    if (!v->lay.landscape) {
        int cols = MAX(1, v->kifu_cols);
        int col = (int) floor((x - v->kifu_inner_x - 8) / v->kifu_cell), row = (int) ((y - k.y) / v->lay.kifu_row_h);
        Rect b = v->notice_box; /* the moves under a notice are not there to tap */
        if (col < 0 || col >= cols || (b.w > 0 && x >= b.x && y >= b.y && y < b.y + b.h))
            return -1;
        i = row * cols + col;
    } else {
        i = (int) ((y - k.y - v->notice_h - 8) / v->kifu_step);
    }
    return i >= 0 && i < v->kifu_shown ? v->kifu_first + i : -1;
}

/* The promotion popup covers the target square and the one towards the center. */
static void promo_rect(View *v, int choice, Rect *r)
{
    Rect t;
    square_rect(v, v->ask.to, &t);
    double dir = (t.y - v->lay.by) < board_h / 2.0 * v->lay.sh ? 1 : -1;
    double y = t.y + (choice ? dir * v->lay.sh : 0);
    *r = (Rect) {t.x - v->lay.sw * 0.1, y, v->lay.sw * 1.2, v->lay.sh};
}

static void draw_promo(View *v, cairo_t *cr)
{
    Rect a, b;
    promo_rect(v, 0, &a);
    promo_rect(v, 1, &b);
    Rect box = {a.x, MIN(a.y, b.y) - 4, a.w, 2 * v->lay.sh + 8};
    rounded(cr, (Rect) {box.x + 5, box.y + 5, box.w, box.h}, 3);
    cairo_set_source_rgb(cr, 0.45, 0.45, 0.45);
    cairo_fill(cr);
    rounded(cr, box, 3);
    bg(v, cr);
    cairo_fill_preserve(cr);
    fg(v, cr);
    cairo_set_line_width(cr, 4);
    cairo_stroke(cr);
    /* A drop asks which face to put down (Kyotoshogi): the two pieces, no words. */
    gboolean drop = IS_DROP(v->ask);
    int type = drop ? DROP_TYPE(v->ask) : TYPE(v->game->pos.board[v->ask.from]);
    gboolean upside = piece_upside(v, v->game->pos.side);
    for (int i = 0; i < 2; i++) {
        Rect r;
        promo_rect(v, i, &r);
        int t = piece_kind(PIECE(i == 0 ? promote_type(type) : type, v->game->pos.side));
        double s = 0.78;
        style_draw(v->style, piece_theme(v, v->game->pos.side, v->colors->bg), cr, t, upside, r.x + r.w * (1 - s) / 2, r.y + 2,
                   r.w * s, r.h * s);
        fg(v, cr);
        if (!drop)
            draw_label(cr, v->japanese ? PROMOTE_LABELS[1][i] : tr(PROMOTE_LABELS[0][i]),
                       r.x + r.w / 2, r.y + r.h * 0.9, r.h * 0.14, FALSE);
    }
}

/* Fill the piece cache one piece per idle turn: a style change, a rotation or the first hand
 * pieces are then drawn without rasterizing glyphs on the redraw. */
/* The pieces drawn next, cached ahead with the theme, side and size the drawing will ask for (any
 * other is a miss that only pushes live ones out): those on the board now, then every piece in hand
 * the variant has, at the hand's size. */
static void warm(View *v, gboolean board, gboolean hands)
{
    if (!v->style || !v->game || v->lay.sw <= 0)
        return;
    const Position *p = &v->game->pos;
    double pad = MAX(2, v->lay.sw * 0.03);
    gboolean done[2][PIECE_KINDS] = {{FALSE}};
    for (int sq = 0; board && sq < SQUARES; sq++) {
        uint8_t pc = p->board[sq];
        int k = pc ? piece_kind(pc) : 0;
        if (!pc || k <= 0 || k >= PIECE_KINDS || done[COLOR(pc)][k])
            continue;
        done[COLOR(pc)][k] = TRUE;
        style_cache(v->style, piece_theme(v, COLOR(pc), v->colors->board), k, v->lay.sw - 2 * pad, v->lay.sh - 2 * pad,
                    piece_upside(v, COLOR(pc)));
    }
    int count;
    const int *order = hand_order(&count);
    for (int c = 0; hands && c < 2; c++) {
        Rect r;
        hand_rect(v, c, 0, &r);
        if (r.w <= 0)
            continue;
        for (int i = 0; i < count; i++)
            style_cache(v->style, piece_theme(v, c, v->colors->hand), piece_kind(PIECE(order[i], c)), r.w, r.h,
                        piece_upside(v, c));
    }
}

static gboolean warm_step(gpointer data)
{
    View *v = data;
    v->warm_id = 0;
    warm(v, TRUE, TRUE);
    return FALSE;
}

static void schedule_warm(View *v)
{
    if (!v->style || v->lay.sw <= 0)
        return;
    if (v->warm_id)
        g_source_remove(v->warm_id);
    v->warm_id = g_idle_add(warm_step, v);
}

/* The bar of actions: buttons side by side, the menu, undo and hint wider than the arrows; the
 * ones left out (BAR_HIDDEN) take no room. */
static const double BAR_WEIGHT[VIEW_BAR_COUNT] = {1.3, 1.3, 1.3, 1.3, 1, 1, 0.8, 1.6, 1.6, 1.6};

static gboolean in_rect(Rect r, double x, double y);

static int bar_state(View *v, int item, char *label, int size, int *icon)
{
    *label = 0;
    *icon = item == VIEW_BAR_UNDO ? BAR_ICON_UNDO : item == VIEW_BAR_HINT ? BAR_ICON_HINT : BAR_ICON_LETTER;
    return v->cb.bar_state ? v->cb.bar_state(v, item, label, size, icon, v->data) : BAR_ON;
}

/* Asks every button's state and lays them out in the bar. */
static void bar_update(View *v)
{
    Rect b = v->lay.bar;
    double total = 0, x = b.x;
    int n = 0;
    for (int i = 0; i < VIEW_BAR_COUNT; i++) {
        BarItem *it = &v->bar_items[i];
        it->state = bar_state(v, i, it->label, sizeof it->label, &it->icon);
        total += it->state != BAR_HIDDEN ? BAR_WEIGHT[i] : 0;
        n += it->state != BAR_HIDDEN;
    }
    double gap = MAX(4, b.h * 0.1), unit = (b.w - gap * (n - 1)) / total;
    for (int i = 0; i < VIEW_BAR_COUNT; i++) {
        if (v->bar_items[i].state == BAR_HIDDEN) {
            v->bar_items[i].rect = (Rect) {0, 0, 0, 0};
            continue;
        }
        v->bar_items[i].rect = (Rect) {x, b.y, BAR_WEIGHT[i] * unit, b.h};
        x += BAR_WEIGHT[i] * unit + gap;
    }
    v->bar_valid = TRUE;
}

static int bar_at(View *v, double x, double y)
{
    if (!v->bar_valid)
        bar_update(v);
    for (int i = 0; v->lay.bar.w > 0 && i < VIEW_BAR_COUNT; i++)
        if (in_rect(v->bar_items[i].rect, x, y))
            return i;
    return -1;
}

/* Whether a button may be pressed now (asked again: it is about to act). */
static gboolean bar_usable(View *v, int item)
{
    char label[64];
    int icon;
    return item >= 0 && bar_state(v, item, label, sizeof label, &icon) >= BAR_ON;
}

/* A speech bubble, as wide as `s`. */
static void draw_bubble(cairo_t *cr, double cx, double cy, double s)
{
    Rect b = {cx - s * 0.5, cy - s * 0.38, s, s * 0.62};
    rounded(cr, b, s * 0.18);
    cairo_move_to(cr, b.x + s * 0.22, b.y + b.h - 1);
    cairo_line_to(cr, b.x + s * 0.18, b.y + b.h + s * 0.2);
    cairo_line_to(cr, b.x + s * 0.44, b.y + b.h - 1);
    cairo_close_path(cr);
    cairo_fill(cr);
}

static gboolean word_fits(cairo_t *cr, const char *text, double w, double size)
{
    int tw, th;
    pango_layout_get_pixel_size(text_layout(cr, text, size, TRUE), &tw, &th);
    return tw <= w;
}

/* A bar button's icon (ViewBarIcon) in an `s` square at (cx, cy), for when its word does not fit:
 * the letter one for a replay's speed or an answer's own ✓ or ✗. */
static void draw_bar_icon(cairo_t *cr, int icon, const char *label, double cx, double cy, double s)
{
    cairo_set_line_width(cr, MAX(1.5, s * 0.12));
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    if (icon == BAR_ICON_UNDO) {
        double r = s * 0.36;
        cairo_new_sub_path(cr);
        cairo_arc(cr, cx, cy + s * 0.05, r, -G_PI * 0.95, G_PI * 0.75);
        cairo_stroke(cr);
        double ax = cx - r, ay = cy + s * 0.05;
        cairo_move_to(cr, ax - s * 0.17, ay - s * 0.2);
        cairo_line_to(cr, ax, ay + s * 0.02);
        cairo_line_to(cr, ax + s * 0.2, ay - s * 0.14);
        cairo_stroke(cr);
    } else if (icon == BAR_ICON_PAUSE) {
        cairo_rectangle(cr, cx - s * 0.28, cy - s * 0.35, s * 0.18, s * 0.7);
        cairo_rectangle(cr, cx + s * 0.1, cy - s * 0.35, s * 0.18, s * 0.7);
        cairo_fill(cr);
    } else if (icon == BAR_ICON_RESUME) {
        cairo_move_to(cr, cx - s * 0.25, cy - s * 0.35);
        cairo_line_to(cr, cx + s * 0.35, cy);
        cairo_line_to(cr, cx - s * 0.25, cy + s * 0.35);
        cairo_close_path(cr);
        cairo_fill(cr);
    } else if (icon == BAR_ICON_HINT) {
        cairo_new_sub_path(cr);
        cairo_arc(cr, cx, cy - s * 0.1, s * 0.27, G_PI * 0.75, G_PI * 2.25);
        cairo_line_to(cr, cx + s * 0.12, cy + s * 0.25);
        cairo_line_to(cr, cx - s * 0.12, cy + s * 0.25);
        cairo_close_path(cr);
        cairo_stroke(cr);
        cairo_move_to(cr, cx - s * 0.1, cy + s * 0.4);
        cairo_line_to(cr, cx + s * 0.1, cy + s * 0.4);
        cairo_stroke(cr);
    } else {
        char first[8];
        g_utf8_strncpy(first, label, 1);
        draw_label_fit(cr, first, (Rect) {cx - s / 2, cy - s / 2, s, s}, TRUE);
    }
}

static void draw_bar(View *v, cairo_t *cr)
{
    const double *c = v->colors->fg;
    if (!v->bar_valid)
        bar_update(v);
    for (int i = 0; i < VIEW_BAR_COUNT; i++) {
        Rect r = v->bar_items[i].rect;
        const char *label = v->bar_items[i].label;
        int state = v->bar_items[i].state;
        if (state == BAR_HIDDEN)
            continue;
        if (i == VIEW_BAR_ASK) {
            fg(v, cr);
            draw_label_fit(cr, label, (Rect) {r.x, r.y + r.h * 0.25, r.w, r.h * 0.5}, TRUE);
            continue;
        }
        gboolean on = state >= BAR_ON;
        rounded(cr, r, MIN(r.h * 0.22, 12));
        view_panel_color(v, cr, 1);
        cairo_fill_preserve(cr);
        stroke_frame(v, cr, 2);
        if (i == v->hover_bar && on) {
            rounded(cr, r, MIN(r.h * 0.22, 12));
            cairo_set_source_rgba(cr, c[0], c[1], c[2], 0.12);
            cairo_fill(cr);
        }
        cairo_set_source_rgba(cr, c[0], c[1], c[2], on ? 1 : 0.35);
        double s = MIN(r.w, r.h), cx = r.x + r.w / 2, cy = r.y + r.h / 2;
        gboolean labelled = *label && r.h >= 52 && r.w >= r.h * 1.1;
        double icy = labelled ? r.y + r.h * 0.36 : cy, is = labelled ? s * 0.42 : s * 0.55;
        if (i == VIEW_BAR_MENU) {
            for (int k = 0; k < 3; k++)
                cairo_rectangle(cr, cx - is * 0.4, icy - is * 0.32 + k * is * 0.27, is * 0.8, is * 0.12);
            cairo_fill(cr);
        } else if (i == VIEW_BAR_PREV || i == VIEW_BAR_NEXT) {
            draw_step(cr, (Rect) {cx - s / 2, cy - s / 2, s, s}, i == VIEW_BAR_NEXT);
        } else if (i == VIEW_BAR_CHAT) {
            draw_bubble(cr, cx, icy, is * 0.8);
            if (state == BAR_ALERT) {
                set_rgb(cr, v->colors->accent);
                cairo_arc(cr, cx + is * 0.42, icy - is * 0.32, MAX(4, is * 0.14), 0, 2 * G_PI);
                cairo_fill(cr);
            }
        } else if (i == VIEW_BAR_INFO) {
            cairo_set_line_width(cr, MAX(1.5, s * 0.05));
            cairo_new_sub_path(cr);
            cairo_arc(cr, cx, cy, s * 0.24, 0, 2 * G_PI);
            cairo_stroke(cr);
            cairo_arc(cr, cx, cy - s * 0.11, s * 0.035, 0, 2 * G_PI);
            cairo_fill(cr);
            cairo_rectangle(cr, cx - s * 0.03, cy - s * 0.04, s * 0.06, s * 0.16);
            cairo_fill(cr);
            continue;
        } else {
            /* Undo, Hint and a question's answers: their word, large (it changes: Takeback, Stop),
             * or an icon where it would come out too small to read. */
            if (word_fits(cr, label, r.w - 12, MAX(11, r.h * 0.18)))
                draw_label_fit(cr, label, (Rect) {r.x + 6, r.y + r.h * 0.2, r.w - 12, r.h * 0.6}, TRUE);
            else
                draw_bar_icon(cr, v->bar_items[i].icon, label, cx, cy, s * 0.5);
            continue;
        }
        if (labelled)
            draw_label_fit(cr, label, (Rect) {r.x + 4, r.y + r.h * 0.62, r.w - 8, r.h * 0.3}, FALSE);
    }
    /* Each button's key or controller button at its corner, over what was drawn. */
    for (int i = 0; v->area_hints && v->cb.area_badge && i < VIEW_BAR_COUNT; i++) {
        Rect r = v->bar_items[i].rect;
        int state = v->bar_items[i].state;
        if (state != BAR_HIDDEN && r.w > 0)
            v->cb.area_badge(v, cr, VIEW_AREA_BAR + i, r.x + r.w - 3, r.y + 3, CLAMP(r.h * 0.3, 12, 20), state >= BAR_ON,
                             v->colors->fg, v->colors->bg, v->data);
    }
}

static void draw_pdrag(View *v, cairo_t *cr);
static void draw_toast(View *v, cairo_t *cr, double W);
static void queue_pdrag(View *v);

/* One rectangle of an expose: `p` is the position shown and `check_sq` its king in check. */
static void draw_all(View *v, cairo_t *cr, double W, double H, const Position *p, int check_sq)
{
    cairo_rectangle(cr, 0, 0, W, H);
    bg(v, cr);
    cairo_fill(cr);
    if (v->flashing) {
        fg(v, cr);
        cairo_paint(cr);
        return;
    }
    /* Parts away from what is being redrawn are skipped: walking them costs on the Kindle. */
    for (int c = 0; c < 2; c++) {
        Rect a = v->lay.side[c], k = v->lay.clock[c], l = v->lay.label[c];
        double x0 = MIN(a.x, MIN(k.x, l.x)), y0 = MIN(a.y, MIN(k.y, l.y));
        double x1 = MAX(a.x + a.w, MAX(k.x + k.w, l.x + l.w)), y1 = MAX(a.y + a.h, MAX(k.y + k.h, l.y + l.h));
        if (in_clip(cr, x0 - 4, y0 - 4, x1 - x0 + 8, y1 - y0 + 8))
            draw_strip(v, cr, p, c);
    }
    if (in_clip(cr, v->lay.bx - v->lay.band, v->lay.by - v->lay.band, board_w * v->lay.sw + 3 * v->lay.band,
                board_h * v->lay.sh + 3 * v->lay.band))
        draw_board(v, cr, p, check_sq);
    if (v->lay.kifu.w > 0 && in_clip(cr, v->lay.kifu.x - 4, v->lay.kifu.y - 4, v->lay.kifu.w + 8, v->lay.kifu.h + 8))
        draw_kifu(v, cr);
    if (v->lay.bar.w > 0 && in_clip(cr, v->lay.bar.x - 4, v->lay.bar.y - 4, v->lay.bar.w + 8, v->lay.bar.h + 8))
        draw_bar(v, cr);
    if (v->asking)
        draw_promo(v, cr);
    if (v->cursor_sq >= 0 && v->cursor_sq < SQUARES) {
        Rect r;
        double lw = MAX(3, v->lay.sw * 0.07);
        ink(v, cr);
        cairo_set_line_width(cr, lw);
        if (v->asking) {
            /* Bars at the sides of the choice: a frame would cross its label. */
            promo_rect(v, v->cursor_choice, &r);
            fg(v, cr);
            cairo_move_to(cr, r.x + lw, r.y + 4);
            cairo_line_to(cr, r.x + lw, r.y + r.h - 4);
            cairo_move_to(cr, r.x + r.w - lw, r.y + 4);
            cairo_line_to(cr, r.x + r.w - lw, r.y + r.h - 4);
        } else {
            square_rect(v, v->cursor_sq, &r);
            cairo_rectangle(cr, r.x + 3, r.y + 3, r.w - 6, r.h - 6);
        }
        cairo_stroke(cr);
    } else if (v->cursor_sq >= SQUARES) {
        /* The editor's cursor on a hand. */
        Rect r = cursor_hand_rect(v, v->cursor_sq - SQUARES);
        ink(v, cr);
        cairo_set_line_width(cr, MAX(3, v->lay.sw * 0.07));
        cairo_rectangle(cr, r.x + 2, r.y + 2, r.w - 4, r.h - 4);
        cairo_stroke(cr);
    }
    if (v->area_hints && v->cb.area_badge && !v->editing) {
        /* Small, in corners nothing else uses: above the rank letters, at the top of the hand
         * of the side to move and of the move list. */
        int focus = v->browsing || v->ply >= 0 ? VIEW_AREA_MOVES : v->sel_drop ? VIEW_AREA_HAND : VIEW_AREA_BOARD;
        double h = CLAMP(v->lay.band * 0.85, 12, 20);
        Rect a = v->lay.side[v->game->pos.side];
        v->cb.area_badge(v, cr, VIEW_AREA_BOARD, v->lay.bx + board_w * v->lay.sw + v->lay.band / 2 + h / 2,
                         v->lay.by - v->lay.band / 2 - h / 2, h, focus == VIEW_AREA_BOARD, v->colors->fg, v->colors->bg, v->data);
        if (a.w > 0)
            v->cb.area_badge(v, cr, VIEW_AREA_HAND, a.x + a.w - 4, a.y + 4, h, focus == VIEW_AREA_HAND,
                             v->colors->fg, v->colors->bg, v->data);
        if (v->lay.kifu.w > 0)
            v->cb.area_badge(v, cr, VIEW_AREA_MOVES, v->lay.kifu.x + v->lay.kifu.w - 4, v->lay.kifu.y + 4, h,
                             focus == VIEW_AREA_MOVES, v->colors->fg, v->colors->bg, v->data);
    }
    if (v->pdrag == PDRAG_ON && v->live_drag)
        draw_pdrag(v, cr);
    if (v->dimmed) { /* as behind the ☰ menu */
        gboolean dark = v->colors->bg[0] + v->colors->bg[1] + v->colors->bg[2] < 1.5;
        cairo_rectangle(cr, 0, 0, W, H);
        cairo_set_source_rgba(cr, dark ? 0 : 1, dark ? 0 : 1, dark ? 0 : 1, dark ? 0.5 : 0.55);
        cairo_fill(cr);
    }
    if (v->overlay)
        v->overlay->draw(v, cr, W, H, v->overlay_data);
    if (*v->toast)
        draw_toast(v, cr, W);
}

/* The Kindle's window manager (Lab126's e-ink library) joins what arrives together, and what
 * overlaps an update still on its way, into one update the size of the box around it; and an
 * update over ~420 px both ways starts only ~4 s after the last such one (a move and the engine's
 * reply showed together seconds later). So what is large goes in bands under that, a few ms
 * apart, on rows fixed on the screen: what the e-ink joins stays within a row. Within a band all
 * at once, so the dots along a bishop's line show together. Calls send(band, data) for each band
 * of `region`, in the coordinates of `win` (NULL: the screen's). */
enum { BAND = 360, BAND_GAP_US = 15000, EINK_UPDATE_MS = 400 };
static gint64 last_band_us;

/* How long until what was last sent has been shown (an e-ink update takes ~350 ms). */
int eink_busy_ms(void)
{
    return MAX(0, EINK_UPDATE_MS - (int) ((g_get_monotonic_time() - last_band_us) / 1000));
}

/* Whether `region` (in `win`, NULL: the screen) lies within one band. */
gboolean in_one_band(const GdkRegion *region, GdkWindow *win)
{
    GdkRectangle all;
    int ox = 0, oy = 0;
    if (win)
        gdk_window_get_origin(win, &ox, &oy);
    gdk_region_get_clipbox((GdkRegion *) region, &all);
    return all.height <= 0 || (all.y + oy) / BAND == (all.y + oy + all.height - 1) / BAND;
}

void in_bands(const GdkRegion *region, GdkWindow *win, void (*send)(GdkRegion *band, void *data), void *data)
{
    GdkRectangle all;
    int ox = 0, oy = 0;
    if (win)
        gdk_window_get_origin(win, &ox, &oy);
    gdk_region_get_clipbox((GdkRegion *) region, &all);
    for (int y = all.y, sent = 0; y < all.y + all.height;) {
        int end = MIN(((y + oy) / BAND + 1) * BAND - oy, all.y + all.height);
        GdkRegion *band = gdk_region_rectangle(&(GdkRectangle) {all.x, y, all.width, end - y});
        gdk_region_intersect(band, (GdkRegion *) region);
        if (!gdk_region_empty(band)) {
            if (sent++) { /* once the X server has taken the band before: a busy one joined them */
                gdk_display_sync(gdk_display_get_default());
                g_usleep(BAND_GAP_US);
            }
            send(band, data);
            last_band_us = g_get_monotonic_time();
        }
        gdk_region_destroy(band);
        y = end;
    }
}

typedef struct {
    GdkWindow *win;
    GdkGC *gc;
    GdkImage *image;
    int width, height;
} GrayBlit;

static void send_gray(GdkRegion *band, void *data)
{
    GrayBlit *b = data;
    GdkRectangle box;
    gdk_region_get_clipbox(band, &box);
    gdk_rectangle_intersect(&box, &(GdkRectangle) {0, 0, b->width, b->height}, &box);
    if (box.width > 0 && box.height > 0) /* the gray image is current outside the rectangles too */
        gdk_draw_image(b->win, b->gc, b->image, box.x, box.y, box.x, box.y, box.width, box.height);
}

static gboolean band_painting;

/* Whether GTK draws now for paint_in_bands(); any other drawing waits for it (kindle.c). */
gboolean painting_in_bands(void) { return band_painting; }

static void paint_band(GdkRegion *band, void *data)
{
    gdk_window_invalidate_region(data, band, TRUE);
    band_painting = TRUE;
    gdk_window_process_updates(data, TRUE);
    band_painting = FALSE;
}

/* Takes what waits to be drawn in `w` and the windows in it (a window's own leaves out its
 * children's), at (x, y) in the top window. */
static void take_pending(GdkWindow *w, int x, int y, GdkRegion *into)
{
    GdkRegion *r = gdk_window_get_update_area(w);
    if (r) {
        gdk_region_offset(r, x, y);
        gdk_region_union(into, r);
        gdk_region_destroy(r);
    }
    for (GList *l = gdk_window_peek_children(w); l; l = l->next) {
        int cx, cy;
        if (!gdk_window_is_viewable(l->data))
            continue;
        gdk_window_get_position(l->data, &cx, &cy);
        take_pending(l->data, x + cx, y + cy, into);
    }
}

/* What GTK is about to draw in the top window `win`, taken from it (GTK no longer draws it). */
GdkRegion *take_pending_area(GdkWindow *win)
{
    GdkRegion *pending = gdk_region_new();
    take_pending(win, 0, 0, pending);
    return pending;
}

/* What GTK is about to draw in the top window `win` (a drawer opening or closing), drawn now in bands. */
void paint_in_bands(GdkWindow *win)
{
    GdkRegion *pending = take_pending_area(win);
    if (!gdk_region_empty(pending))
        in_bands(pending, win, paint_band, win);
    gdk_region_destroy(pending);
}

/* Puts `frame` (an image the size of `w`) on `w`'s window, only within `region`. On gray screens
 * (the Kindle) the pixels are turned to gray here, into an image the X server shares with us
 * (MIT-SHM, as Gambatte-K2 does): handed color the server converts it slowly, and a plain image
 * goes through its socket. `gray` keeps that image between calls. */
void blit_frame(GtkWidget *w, cairo_surface_t *frame, GdkRegion *region, GdkImage **gray, gboolean mono)
{
    GdkWindow *win = gtk_widget_get_window(w);
    GdkVisual *vis = gdk_drawable_get_visual(win);
    int width = cairo_image_surface_get_width(frame), height = cairo_image_surface_get_height(frame);
    if (gdk_drawable_get_depth(win) == 8 && vis &&
        (vis->type == GDK_VISUAL_STATIC_GRAY || vis->type == GDK_VISUAL_GRAYSCALE)) {
        if (!*gray || (*gray)->width != width || (*gray)->height != height) {
            if (*gray)
                g_object_unref(*gray);
            /* Shared memory is the fast path (MIT-SHM, as Gambatte-K2); fall back when absent. */
            *gray = gdk_image_new(GDK_IMAGE_SHARED, vis, width, height);
            if (!*gray)
                *gray = gdk_image_new(GDK_IMAGE_FASTEST, vis, width, height);
        }
        if (!*gray)
            return;
        GdkRectangle *rects;
        int n;
        gdk_region_get_rectangles(region, &rects, &n);
        cairo_surface_flush(frame);
        const guint8 *px = cairo_image_surface_get_data(frame);
        int stride = cairo_image_surface_get_stride(frame);
        GdkGC *gc = gtk_widget_get_style(w)->black_gc;
        guint8 *mem = (*gray)->mem;
        int bpl = (*gray)->bpl;
        for (int i = 0; i < n; i++) {
            GdkRectangle r = rects[i];
            gdk_rectangle_intersect(&r, &(GdkRectangle) {0, 0, width, height}, &r);
            for (int y = r.y; y < r.y + r.height; y++) {
                const guint32 *src = (const guint32 *) (px + (gsize) y * stride);
                guint8 *dst = mem + (gsize) y * bpl;
                if (mono) {
                    /* The mono theme draws r == g == b; one channel is the gray value. */
                    for (int x = r.x; x < r.x + r.width; x++)
                        dst[x] = (guint8) (src[x] & 255);
                } else {
                    for (int x = r.x; x < r.x + r.width; x++) {
                        guint32 c = src[x];
                        dst[x] = (guint8) ((((c >> 16) & 255) * 77 + ((c >> 8) & 255) * 150 + (c & 255) * 29) >> 8);
                    }
                }
            }
        }
        g_free(rects);
        in_bands(region, win, send_gray, &(GrayBlit) {win, gc, *gray, width, height});
        return;
    }
    cairo_t *cr = gdk_cairo_create(win);
    gdk_cairo_region(cr, region);
    cairo_clip(cr);
    cairo_set_source_surface(cr, frame, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
}

gboolean view_save_svg(View *v, const char *path)
{
#ifdef DENBOKU_KINDLE
    (void) v, (void) path;
    return FALSE;
#else
    GtkAllocation a;
    gtk_widget_get_allocation(v->area, &a);
    cairo_surface_t *svg = cairo_svg_surface_create(path, a.width, a.height);
    if (cairo_surface_status(svg) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(svg);
        return FALSE;
    }
    cairo_svg_surface_restrict_to_version(svg, CAIRO_SVG_VERSION_1_1);
    cairo_t *cr = cairo_create(svg);
    layout(v, a.width - v->inset, a.height);
    Position tmp, *p = shown(v, &tmp);
    style_set_vector(TRUE);
    draw_all(v, cr, a.width, a.height, p, checked_king(v, p));
    style_set_vector(FALSE);
    cairo_destroy(cr);
    cairo_surface_finish(svg);
    gboolean ok = cairo_surface_status(svg) == CAIRO_STATUS_SUCCESS;
    cairo_surface_destroy(svg);
    return ok;
#endif
}

static gboolean expose_cb(GtkWidget *w, GdkEventExpose *ev, gpointer user)
{
    View *v = user;
    GtkAllocation a;
    gtk_widget_get_allocation(w, &a);
    gint64 t0 = g_get_monotonic_time();
    /* Drawn into an image in this process, then sent to the window in one piece: drawn on the
     * window, every piece and text went to the X server on its own, which on the Kindle composes
     * them in software (about a second for the whole board). */
    if (!v->frame || cairo_image_surface_get_width(v->frame) != a.width ||
        cairo_image_surface_get_height(v->frame) != a.height) {
        if (v->frame)
            cairo_surface_destroy(v->frame);
        v->frame = cairo_image_surface_create(CAIRO_FORMAT_RGB24, a.width, a.height);
    }
    /* One rectangle at a time: cairo clips to a plain rectangle quickly, to several through a mask. */
    cairo_t *cr = cairo_create(v->frame);
    GdkRectangle *rects;
    int n;
    GdkRegion *region = gdk_region_copy(ev->region);
    if (v->covered.width > 0) {
        GdkRegion *under = gdk_region_rectangle(&v->covered);
        gdk_region_subtract(region, under);
        gdk_region_destroy(under);
    }
    gdk_region_get_rectangles(region, &rects, &n);
    /* What every rectangle draws from, worked out once. */
    Game *game = v->game;
    PieceStyle *style = v->style;
    int variant = rules_variant;
    if (v->preview) {
        v->game = v->preview, v->style = v->preview_style;
        rules_set_variant(v->preview_variant);
    }
    layout(v, a.width - v->inset, a.height);
    if (v->lay.sw != v->warm_sw || v->lay.sh != v->warm_sh || v->lay.hand_cell[0] != v->warm_hand) {
        v->warm_sw = v->lay.sw, v->warm_sh = v->lay.sh, v->warm_hand = v->lay.hand_cell[0];
        schedule_warm(v);
    }
    Position tmp, *p = shown(v, &tmp);
    int check_sq = checked_king(v, p);
    GdkRectangle cover;
    gboolean covering = v->overlay && v->overlay->opaque && !v->flashing && v->overlay->opaque(v, &cover, v->overlay_data);
    for (int i = 0; i < n; i++) {
        /* Under an overlay's paint only the overlay is drawn; the game around it as always. */
        GdkRectangle under;
        gboolean hidden = covering && gdk_rectangle_intersect(&rects[i], &cover, &under);
        GdkRegion *around = gdk_region_rectangle(&rects[i]);
        if (hidden) {
            GdkRegion *c = gdk_region_rectangle(&cover);
            gdk_region_subtract(around, c);
            gdk_region_destroy(c);
        }
        GdkRectangle *parts;
        int nparts;
        gdk_region_get_rectangles(around, &parts, &nparts);
        gdk_region_destroy(around);
        for (int j = 0; j < nparts; j++) {
            cairo_save(cr);
            cairo_rectangle(cr, parts[j].x, parts[j].y, parts[j].width, parts[j].height);
            cairo_clip(cr);
            redrawn = gdk_region_rectangle(&parts[j]);
            draw_all(v, cr, a.width, a.height, p, check_sq);
            gdk_region_destroy(redrawn);
            redrawn = NULL;
            cairo_restore(cr);
        }
        g_free(parts);
        if (hidden) {
            cairo_save(cr);
            cairo_rectangle(cr, under.x, under.y, under.width, under.height);
            cairo_clip(cr);
            redrawn = gdk_region_rectangle(&under);
            v->overlay->draw(v, cr, a.width, a.height, v->overlay_data);
            if (*v->toast)
                draw_toast(v, cr, a.width);
            gdk_region_destroy(redrawn);
            redrawn = NULL;
            cairo_restore(cr);
        }
    }
    if (v->preview) {
        rules_set_variant(variant);
        v->game = game, v->style = style;
    }
    g_free(rects);
    cairo_destroy(cr);
    gint64 t1 = g_get_monotonic_time();
    if (n)
        blit_frame(w, v->frame, region, &v->gray, v->colors->last_corners);
    gdk_region_destroy(region);
    if (g_getenv("DENBOKU_DEBUG"))
        g_printerr("%.3f draw clip %dx%d+%d+%d in %d: %.0f ms drawing, %.0f ms sending (covered %dx%d+%d+%d%s)\n",
                   g_get_monotonic_time() / 1e6, ev->area.width, ev->area.height, ev->area.x, ev->area.y, n, (t1 - t0) / 1e3,
                   (g_get_monotonic_time() - t1) / 1e3, v->covered.width, v->covered.height, v->covered.x, v->covered.y,
                   ev->send_event ? ", sent" : "");

    return TRUE;
}

/* Redraws only what a selection marks (the square or the hand, and the targets): on e-ink a
 * small update shows much sooner than the whole screen. */
static void queue_marks(View *v)
{
    queue_square(v, v->sel_sq, 0);
    if (v->sel_drop)
        for (int c = 0; c < 2; c++)
            queue_rect(v, v->lay.side[c], 4);
    for (int i = 0; i < v->ntargets; i++)
        queue_square(v, v->targets[i].to, 0);
}

static void clear_selection(View *v)
{
    if (v->area && v->hover_sq >= 0)
        queue_hover(v, v->hover_sq); /* its mark goes with the selection */
    v->sel_sq = -1;
    v->sel_drop = 0;
    v->hover_sq = -1;
    v->ntargets = 0;
    v->asking = FALSE;
    v->peek_sq = -1;
}

static Position *acting(View *v) { return v->pre ? &v->pre_pos : &v->game->pos; }

/* Moves offered on the board for the piece on `from` (or in hand): legal ones, or every
 * physical move when illegal moves lose. A premove, checked when it is played, goes wherever
 * the piece reaches on an empty board, as on lishogi (`premove()` in its ui/shogi/src/pre-move.ts):
 * the piece in the way may move or be taken first, and a piece of ours taken can be taken back. */
static int board_moves(View *v, int from, Move *all)
{
    if (v->physical == 2)
        return pos_any_moves(acting(v), all);
    if (v->pre && from >= 0 && from < SQUARES) {
        Position alone = v->pre_pos;
        memset(alone.board, 0, sizeof alone.board);
        alone.board[from] = v->pre_pos.board[from];
        return pos_physical_moves(&alone, all);
    }
    return v->physical || v->pre ? pos_physical_moves(acting(v), all) : pos_legal_moves(acting(v), all);
}

static void select_moves(View *v, int from)
{
    Move all[MAX_MOVES];
    int n = board_moves(v, from, all);
    v->ntargets = 0;
    memset(v->legal_to, 0, sizeof v->legal_to);
    Move legal[MAX_MOVES];
    int nl = v->physical && !v->pre ? pos_legal_moves(&v->game->pos, legal) : 0;
    for (int i = 0; i < nl; i++)
        if (legal[i].from == from)
            v->legal_to[legal[i].to] = TRUE;
    for (int i = 0; i < n; i++) {
        if (all[i].from != from)
            continue;
        if (!v->physical || v->pre)
            v->legal_to[all[i].to] = TRUE;
        gboolean dup = FALSE;
        for (int j = 0; j < v->ntargets; j++)
            dup |= v->targets[j].to == all[i].to;
        if (!dup)
            v->targets[v->ntargets++] = all[i];
    }
}

/* The hint's arrow: the box around its two squares. */
static void queue_hint(View *v)
{
    if (!v->has_hint)
        return;
    Rect a, b;
    square_rect(v, IS_DROP(v->hint) ? v->hint.to : v->hint.from, &a);
    square_rect(v, v->hint.to, &b);
    double x0 = MIN(a.x, b.x), y0 = MIN(a.y, b.y);
    queue_rect(v, (Rect) {x0, y0, MAX(a.x, b.x) + a.w - x0, MAX(a.y, b.y) + a.h - y0}, 4);
}

/* The move is made: the game's refresh redraws what it changed (and the marks of the selection
 * and the hint, queued here before they go); a premove still redraws all. */
static void choose(View *v, Move m)
{
    gboolean all = v->pre || v->asking;
    queue_marks(v);
    queue_hint(v);
    clear_selection(v);
    v->has_hint = FALSE;
    if (v->pre) {
        v->premove = m;
        v->has_premove = TRUE;
        v->pre = FALSE;
    } else if (v->cb.move) {
        v->cb.move(v, &m, v->data);
    }
    if (all)
        gtk_widget_queue_draw(v->area);
}

gboolean view_take_premove(View *v, Move *m)
{
    if (!v->has_premove)
        return FALSE;
    v->has_premove = FALSE;
    *m = v->premove;
    gtk_widget_queue_draw(v->area);
    return TRUE;
}

void view_clear_premove(View *v)
{
    v->has_premove = v->pre = FALSE;
    clear_selection(v);
    gtk_widget_queue_draw(v->area);
}

static void tap_target(View *v, int to)
{
    int from = v->sel_drop ? DROP + v->sel_drop : v->sel_sq;
    Move all[MAX_MOVES];
    int n = board_moves(v, from, all);
    Move with = {0}, without = {0};
    gboolean has_with = FALSE, has_without = FALSE;
    for (int i = 0; i < n; i++) {
        if (all[i].from != from || all[i].to != to)
            continue;
        if (all[i].promote)
            with = all[i], has_with = TRUE;
        else
            without = all[i], has_without = TRUE;
    }
    int type = v->sel_drop ? 0 : TYPE(acting(v)->board[from]);
    if (has_with && has_without && !v->sel_drop && (v->auto_promote == 2 || (v->auto_promote == 1 && promotion_keeps_moves(type))))
        has_without = FALSE;
    if (has_with && has_without) {
        v->asking = TRUE;
        v->ask = with;
        v->ntargets = 0;
    } else if (has_with || has_without) {
        choose(v, has_with ? with : without);
    } else {
        queue_marks(v);
        clear_selection(v);
    }
}

static gboolean in_rect(Rect r, double x, double y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void queue_kifu(View *v)
{
    if (v->lay.kifu.w > 0)
        gtk_widget_queue_draw_area(v->area, (int) v->lay.kifu.x - 2, (int) v->lay.kifu.y - 2, (int) v->lay.kifu.w + 4,
                                   (int) v->lay.kifu.h + 4);
}

/* Wheel: one notch scrolls three moves. A drag on touch scrolls by the distance moved. */
static gboolean scroll_cb(GtkWidget *w, GdkEventScroll *ev, gpointer user)
{
    (void) w;
    View *v = user;
    if (v->overlay)
        return TRUE;
    if (v->preview || !in_rect(v->lay.kifu, ev->x, ev->y) || !v->notes || !v->kifu_visible)
        return FALSE;
    int n = (int) v->notes->len, max_first = MAX(0, n - v->kifu_visible);
    if (max_first <= 0)
        return TRUE;
    int first = v->kifu_scroll >= 0 ? CLAMP(v->kifu_scroll, 0, max_first) : v->kifu_first;
    int delta = 0;
    int notch = !v->lay.landscape && v->lay.kifu_rows > 1 ? v->kifu_cols : 3;
    if (ev->direction == GDK_SCROLL_UP || ev->direction == GDK_SCROLL_LEFT)
        delta = -notch;
    else if (ev->direction == GDK_SCROLL_DOWN || ev->direction == GDK_SCROLL_RIGHT)
        delta = notch;
    if (!delta)
        return TRUE;
    v->kifu_scroll = CLAMP(first + delta, 0, max_first);
    queue_kifu(v);
    return TRUE;
}

static gboolean motion_cb(GtkWidget *w, GdkEventMotion *ev, gpointer user)
{
    (void) w;
    View *v = user;
    if (v->overlay) {
        v->overlay->motion(v, ev->x, ev->y, v->overlay_data);
        return TRUE;
    }
    if (v->preview)
        return TRUE;
    if (v->pdrag != PDRAG_NONE && (ev->state & GDK_BUTTON1_MASK)) {
        if (v->pdrag == PDRAG_PENDING && v->hover && hypot(ev->x - v->pdrag_x0, ev->y - v->pdrag_y0) > MAX(10, v->lay.sw * 0.3))
            v->pdrag = PDRAG_ON;
        if (v->pdrag == PDRAG_ON && v->live_drag) {
            queue_pdrag(v);
            v->pdrag_x = ev->x, v->pdrag_y = ev->y;
            queue_pdrag(v);
        }
        v->pdrag_x = ev->x, v->pdrag_y = ev->y;
        return TRUE;
    }
    if (v->editing && v->ed_drag != DRAG_NONE) {
        edit_motion(v, ev->x, ev->y);
        return TRUE;
    }
    if (!v->kifu_drag) {
        int item = v->hover ? bar_at(v, ev->x, ev->y) : -1;
        if (item != v->hover_bar) {
            v->hover_bar = item;
            view_queue_bar(v);
        }
        int mi = v->hover ? kifu_at(v, ev->x, ev->y) : -1;
        gboolean expand = v->hover && in_rect(v->kifu_expand, ev->x, ev->y);
        if (mi != v->hover_kifu || expand != v->hover_expand) {
            v->hover_kifu = mi, v->hover_expand = expand;
            queue_kifu(v);
        }
        /* Touch has no hover: the pointer jumps to the finger before the tap, and the mark would be
         * one more e-ink update ahead of the dots or the move. */
        int sq = v->hover ? square_at(v, ev->x, ev->y) : -1;
        if (sq != v->hover_sq) {
            if (v->hover_sq >= 0)
                queue_hover(v, v->hover_sq);
            v->hover_sq = (v->sel_sq >= 0 || v->sel_drop) ? sq : -1;
            if (v->hover_sq >= 0)
                queue_hover(v, v->hover_sq);
        }
        return TRUE;
    }
    gboolean grid = !v->lay.landscape && v->lay.kifu_rows > 1;
    double d = v->lay.landscape || grid ? ev->y - v->kifu_drag_y : ev->x - v->kifu_drag_x;
    if (fabs(d) > 8)
        v->kifu_moved = TRUE;
    if (!v->kifu_moved || !v->notes || !v->kifu_visible)
        return TRUE;
    int n = (int) v->notes->len, max_first = MAX(0, n - v->kifu_visible);
    int delta = (int) (d / v->kifu_step) * (grid ? v->kifu_cols : 1);
    v->kifu_scroll = CLAMP(v->kifu_drag_first - delta, 0, max_first);
    queue_kifu(v);
    return TRUE;
}

static gboolean leave_cb(GtkWidget *w, GdkEventCrossing *ev, gpointer user)
{
    (void) w, (void) ev;
    View *v = user;
    if (v->hover_sq >= 0) {
        queue_hover(v, v->hover_sq);
        v->hover_sq = -1;
    }
    if (v->hover_kifu != -1 || v->hover_expand) {
        v->hover_kifu = -1, v->hover_expand = FALSE;
        queue_kifu(v);
    }
    if (v->hover_bar != -1) {
        v->hover_bar = -1;
        view_queue_bar(v);
    }
    return FALSE;
}

/* ---- the position editor ---- */

static void edit_notify(View *v, int what)
{
    if (v->cb.edit_event)
        v->cb.edit_event(v, what, v->data);
}

/* Squares drawn dashed during a drag: the selection rectangle, or where the pieces would land. */
static gboolean edit_marked(View *v, int sq)
{
    if (v->ed_from < 0 || v->ed_cur < 0)
        return FALSE;
    int r = ROW(sq), c = COL(sq), fr = ROW(v->ed_from), fc = COL(v->ed_from), tr = ROW(v->ed_cur), tc = COL(v->ed_cur);
    if (v->ed_drag == DRAG_RECT)
        return v->ed_cur != v->ed_from && r >= MIN(fr, tr) && r <= MAX(fr, tr) && c >= MIN(fc, tc) && c <= MAX(fc, tc);
    if (v->ed_drag != DRAG_MOVE || v->ed_cur == v->ed_from)
        return FALSE;
    int sr = r - (tr - fr), sc = c - (tc - fc);
    return sr >= 0 && sr < board_h && sc >= 0 && sc < board_w && v->ed_sel[SQ(sr, sc)];
}

static void queue_edit_marks(View *v)
{
    for (int sq = 0; sq < SQUARES; sq++)
        if (v->ed_sel[sq] || edit_marked(v, sq))
            queue_hover(v, sq);
}

static int edit_selected_count(View *v)
{
    int n = 0;
    for (int sq = 0; sq < SQUARES; sq++)
        n += v->ed_sel[sq];
    return n;
}

static void edit_deselect(View *v)
{
    queue_edit_marks(v);
    memset(v->ed_sel, 0, sizeof v->ed_sel);
    edit_notify(v, EDIT_SELECTED);
}

/* Every change of a gesture comes after one EDIT_BEFORE, for a single undo step. */
static void edit_begin(View *v, gboolean *begun)
{
    if (!*begun)
        edit_notify(v, EDIT_BEFORE);
    *begun = TRUE;
}

static gboolean ed_begun;

/* One king a side: placing one takes away the other of its color. */
static void edit_set(View *v, int sq, uint8_t pc)
{
    Position *p = &v->game->pos;
    if (p->board[sq] == pc)
        return;
    edit_begin(v, &ed_begun);
    if (pc && TYPE(pc) == KING)
        for (int s = 0; s < SQUARES; s++)
            if (p->board[s] == pc)
                p->board[s] = EMPTY;
    p->board[sq] = pc;
    edit_notify(v, EDIT_CHANGED);
}

static void hand_add(View *v, int color, int type, int n)
{
    int t = hand_type(type);
    if (t == EMPTY || t == KING)
        return;
    uint8_t *count = &v->game->pos.hand[color][hand_slot(t)];
    if (*count + n < 0)
        return;
    edit_begin(v, &ed_begun);
    *count += n;
}

/* The selected pieces (not kings) go to a hand. */
static void edit_to_hand(View *v, int color)
{
    Position *p = &v->game->pos;
    for (int sq = 0; sq < SQUARES; sq++)
        if (v->ed_sel[sq] && p->board[sq] && TYPE(p->board[sq]) != KING) {
            hand_add(v, color, TYPE(p->board[sq]), 1);
            p->board[sq] = EMPTY;
        }
    edit_deselect(v);
    if (ed_begun)
        edit_notify(v, EDIT_CHANGED);
}

/* The selection moves by (from -> to); pieces it lands on are replaced. Off the board, nothing moves. */
static void edit_move(View *v, int from, int to)
{
    Position *p = &v->game->pos;
    int dr = ROW(to) - ROW(from), dc = COL(to) - COL(from);
    uint8_t moved[81] = {0};
    for (int sq = 0; sq < SQUARES; sq++) {
        if (!v->ed_sel[sq])
            continue;
        int r = ROW(sq) + dr, c = COL(sq) + dc;
        if (r < 0 || r >= board_h || c < 0 || c >= board_w)
            return;
    }
    edit_begin(v, &ed_begun);
    queue_edit_marks(v);
    gboolean sel[81] = {0};
    for (int sq = 0; sq < SQUARES; sq++)
        if (v->ed_sel[sq]) {
            int dest = SQ(ROW(sq) + dr, COL(sq) + dc);
            moved[dest] = p->board[sq], sel[dest] = TRUE;
            p->board[sq] = EMPTY;
        }
    for (int sq = 0; sq < SQUARES; sq++)
        if (sel[sq])
            p->board[sq] = moved[sq];
    memcpy(v->ed_sel, sel, sizeof sel);
    queue_edit_marks(v);
    edit_notify(v, EDIT_CHANGED);
    edit_notify(v, EDIT_SELECTED);
}

/* The hand under (x, y): its color, and the piece type there (0 for an empty spot). */
static int edit_hand_at(View *v, double x, double y, int *type)
{
    Position *p = &v->game->pos;
    *type = 0;
    for (int c = 0; c < 2; c++) {
        if (!in_rect(v->lay.hand[c], x, y) && !in_rect(v->lay.side[c], x, y))
            continue;
        int index = 0, count;
        const int *order = hand_order(&count);
        for (int i = 0; i < count; i++) {
            if (!p->hand[c][hand_slot(order[i])])
                continue;
            Rect r;
            hand_rect(v, c, index++, &r);
            if (in_rect(r, x, y))
                *type = order[i];
        }
        return c;
    }
    return -1;
}

static void edit_hand_tap(View *v, int color, int type)
{
    if (v->tool == TOOL_PLACE) {
        hand_add(v, color, TYPE(v->tool_piece), 1);
    } else if (v->tool == TOOL_ERASE) {
        if (type)
            hand_add(v, color, type, -1);
    } else if (edit_selected_count(v)) {
        edit_to_hand(v, color);
        return;
    } else if (type) {
        gboolean again = v->ed_hand_type == type && v->ed_hand_color == color;
        v->ed_hand_type = again ? 0 : type;
        v->ed_hand_color = color;
        gtk_widget_queue_draw(v->area);
        return;
    } else if (v->cb.edit_tap) {
        v->cb.edit_tap(v, -1, color, v->data);
        return;
    }
    if (ed_begun)
        edit_notify(v, EDIT_CHANGED);
}

static gboolean edit_press(View *v, GdkEventButton *ev)
{
    Position *p = &v->game->pos;
    gboolean add = (ev->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK)) != 0;
    int sq = square_at(v, ev->x, ev->y);
    ed_begun = FALSE;
    v->ed_drag = DRAG_NONE;
    v->ed_from = v->ed_cur = sq;
    if (sq < 0) {
        int type, color = edit_hand_at(v, ev->x, ev->y, &type);
        if (color >= 0)
            edit_hand_tap(v, color, type);
        else if (edit_selected_count(v))
            edit_deselect(v);
        return TRUE;
    }
    uint8_t pc = p->board[sq];
    if (v->tool != TOOL_MOVE) {
        v->ed_paint = v->tool == TOOL_ERASE || pc == v->tool_piece ? EMPTY : (uint8_t) v->tool_piece;
        v->ed_drag = DRAG_PAINT;
        edit_set(v, sq, v->ed_paint);
        return TRUE;
    }
    if (v->ed_hand_type) {
        /* A piece picked from a hand is put down here. */
        int color = v->ed_hand_color, type = v->ed_hand_type;
        v->ed_hand_type = 0;
        gtk_widget_queue_draw(v->area);
        hand_add(v, color, type, -1);
        edit_set(v, sq, PIECE(type, color));
        return TRUE;
    }
    if (pc) {
        v->ed_was_selected = v->ed_sel[sq];
        queue_edit_marks(v);
        if (!add && !v->ed_sel[sq])
            memset(v->ed_sel, 0, sizeof v->ed_sel);
        v->ed_sel[sq] = add ? !v->ed_sel[sq] : TRUE;
        v->ed_drag = v->ed_sel[sq] ? DRAG_MOVE : DRAG_NONE;
        queue_edit_marks(v);
        edit_notify(v, EDIT_SELECTED);
    } else {
        v->ed_drag = DRAG_RECT;
        v->ed_was_selected = add;
    }
    return TRUE;
}

static void edit_motion(View *v, double x, double y)
{
    int sq = square_at(v, x, y);
    if (sq == v->ed_cur)
        return;
    if (v->ed_drag == DRAG_PAINT) {
        /* Every square on the way: a fast pointer skips some between two events. */
        int from = v->ed_cur;
        v->ed_cur = sq;
        if (sq < 0)
            return;
        int steps = from < 0 ? 0 : MAX(ABS(ROW(sq) - ROW(from)), ABS(COL(sq) - COL(from)));
        for (int i = 1; i < steps; i++)
            edit_set(v, SQ(ROW(from) + (ROW(sq) - ROW(from)) * i / steps, COL(from) + (COL(sq) - COL(from)) * i / steps),
                     v->ed_paint);
        edit_set(v, sq, v->ed_paint);
        return;
    }
    queue_edit_marks(v);
    v->ed_cur = sq;
    queue_edit_marks(v);
}

static void edit_release(View *v, double x, double y)
{
    int drag = v->ed_drag, sq = square_at(v, x, y), from = v->ed_from;
    queue_edit_marks(v);
    v->ed_drag = DRAG_NONE;
    if (drag == DRAG_MOVE) {
        int type, color = sq < 0 ? edit_hand_at(v, x, y, &type) : -1;
        if (sq >= 0 && sq != from)
            edit_move(v, from, sq);
        else if (color >= 0)
            edit_to_hand(v, color);
        else if (sq < 0) /* dragged off the board: taken away */
            view_edit_op(v, EDIT_DELETE);
        else if (v->ed_was_selected) {
            v->ed_sel[sq] = FALSE; /* a second tap deselects */
            edit_notify(v, EDIT_SELECTED);
        }
    } else if (drag == DRAG_RECT && sq >= 0 && sq != from) {
        v->ed_drag = DRAG_RECT, v->ed_cur = sq;
        gboolean sel[81];
        for (int s = 0; s < SQUARES; s++)
            sel[s] = edit_marked(v, s) && v->game->pos.board[s];
        v->ed_drag = DRAG_NONE;
        if (!v->ed_was_selected)
            memset(v->ed_sel, 0, sizeof v->ed_sel);
        for (int s = 0; s < SQUARES; s++)
            v->ed_sel[s] |= sel[s];
        edit_notify(v, EDIT_SELECTED);
    } else if (drag == DRAG_RECT && sq >= 0) {
        /* A tap on an empty square: one selected piece moves there; otherwise deselect. */
        int n = edit_selected_count(v), one = -1;
        for (int s = 0; s < SQUARES && n == 1; s++)
            if (v->ed_sel[s])
                one = s;
        if (one >= 0 && !v->ed_was_selected)
            edit_move(v, one, sq);
        else if (!v->ed_was_selected)
            edit_deselect(v);
    }
    v->ed_from = v->ed_cur = -1;
    queue_edit_marks(v);
}

gboolean view_edit_op(View *v, EditOp op)
{
    Position *p = &v->game->pos;
    if (op == EDIT_SELECT_ALL) {
        for (int sq = 0; sq < SQUARES; sq++)
            v->ed_sel[sq] = p->board[sq] != EMPTY;
        queue_edit_marks(v);
        edit_notify(v, EDIT_SELECTED);
        return TRUE;
    }
    if (!edit_selected_count(v))
        return FALSE;
    if (op == EDIT_DESELECT) {
        edit_deselect(v);
        return TRUE;
    }
    ed_begun = FALSE;
    for (int sq = 0; sq < SQUARES; sq++) {
        uint8_t pc = p->board[sq];
        if (!v->ed_sel[sq] || !pc)
            continue;
        int t = TYPE(pc), n = promote_type(t);
        if (n == t)
            n = unpromote_type(t);
        uint8_t to = op == EDIT_DELETE ? EMPTY : op == EDIT_FLIP ? PIECE(t, !COLOR(pc)) : PIECE(n, COLOR(pc));
        if (to != pc) {
            edit_begin(v, &ed_begun);
            p->board[sq] = to;
        }
    }
    if (op == EDIT_DELETE)
        edit_deselect(v);
    if (ed_begun)
        edit_notify(v, EDIT_CHANGED);
    return TRUE;
}

int view_edit_selected(View *v) { return edit_selected_count(v); }

void view_set_edit_tool(View *v, EditTool tool, int piece)
{
    v->tool = tool;
    v->tool_piece = piece;
    if (v->ed_hand_type) {
        v->ed_hand_type = 0;
        gtk_widget_queue_draw(v->area);
    }
}

static void pdrag_start(View *v, GdkEventButton *ev, gboolean again)
{
    v->pdrag = PDRAG_PENDING, v->pdrag_again = again;
    v->pdrag_x0 = v->pdrag_x = ev->x, v->pdrag_y0 = v->pdrag_y = ev->y;
}

/* The dragged piece as drawn, under the mouse (only a mouse drags pieces; a finger draws arrows). */
static Rect pdrag_rect(View *v)
{
    return (Rect) {v->pdrag_x - v->lay.sw / 2, v->pdrag_y - v->lay.sh / 2, v->lay.sw, v->lay.sh};
}

static void queue_pdrag(View *v)
{
    queue_rect(v, pdrag_rect(v), MAX(2, v->lay.sw * 0.05) / 2 + 3); /* its outline is centered on the edge */
    int sq = square_at(v, v->pdrag_x, v->pdrag_y);
    if (sq >= 0)
        queue_hover(v, sq);
}

static void draw_pdrag(View *v, cairo_t *cr)
{
    Position *p = acting(v);
    int type = v->sel_drop                                 ? piece_kind(PIECE(v->sel_drop, p->side))
               : v->sel_sq >= 0 && p->board[v->sel_sq] ? piece_kind(p->board[v->sel_sq])
                                                       : 0;
    if (!type)
        return;
    int color = v->sel_drop ? p->side : COLOR(p->board[v->sel_sq]);
    gboolean upside = piece_upside(v, color);
    int sq = square_at(v, v->pdrag_x, v->pdrag_y);
    if (sq >= 0 && v->legal_to[sq]) { /* where it would land */
        Rect t;
        square_rect(v, sq, &t);
        ink(v, cr);
        cairo_set_line_width(cr, MAX(3, v->lay.sw * 0.08));
        cairo_rectangle(cr, t.x + 2, t.y + 2, t.w - 4, t.h - 4);
        cairo_stroke(cr);
    }
    Rect r = pdrag_rect(v);
    if (!v->hover) {
        rounded(cr, r, v->lay.sw * 0.12);
        set_rgb(cr, v->colors->board);
        cairo_fill_preserve(cr);
        set_rgb(cr, v->colors->last_corners ? v->colors->fg : v->colors->dot);
        cairo_set_line_width(cr, MAX(2, v->lay.sw * 0.05));
        cairo_stroke(cr);
    }
    style_draw(v->style, piece_theme(v, color, v->colors->board), cr, type, upside, r.x, r.y, r.w, r.h);
}

static gboolean press_cb(GtkWidget *w, GdkEventButton *ev, gpointer user)
{
    View *v = user;
    (void) w;
    if (g_getenv("DENBOKU_DEBUG"))
        g_printerr("%.3f press %d at %.0f,%.0f (event time %u)\n", g_get_monotonic_time() / 1e6, ev->button, ev->x,
                   ev->y, ev->time);
    if (v->overlay) {
        if (ev->button == 1 && ev->type == GDK_BUTTON_PRESS)
            v->overlay->press(v, ev->x, ev->y, v->overlay_data);
        return TRUE;
    }
    if (v->preview && (ev->button != 1 || bar_at(v, ev->x, ev->y) < 0))
        return TRUE; /* only the bar works under a preview */
    if (ev->button == 3) {
        v->drag_sq = square_at(v, ev->x, ev->y);
        return TRUE;
    }
    if (ev->button != 1)
        return FALSE;
    if (!v->synthetic && v->cursor_sq >= 0) {
        v->cursor_sq = -1; /* the mouse or a finger takes over */
        gtk_widget_queue_draw(v->area);
    }
    v->press_time = ev->time, v->pressed = TRUE;
    v->press_count = v->game->count;
    v->hold_sq = square_at(v, ev->x, ev->y);
    int item = bar_at(v, ev->x, ev->y);
    if (item >= 0) {
        v->pressed = FALSE; /* the bar may open the menu, which takes the release */
        if (ev->type == GDK_BUTTON_PRESS && bar_usable(v, item) && v->cb.bar)
            v->cb.bar(v, item, v->data);
        return TRUE;
    }
    if (in_rect(v->kifu_expand, ev->x, ev->y)) {
        v->pressed = FALSE;
        if (ev->type == GDK_BUTTON_PRESS)
            v->cb.bar(v, VIEW_MOVES, v->data);
        return TRUE;
    }
    if (v->notice_item >= 0 && *v->notice && in_rect(v->notice_hit, ev->x, ev->y) && v->cb.bar) {
        if (ev->type == GDK_BUTTON_PRESS)
            v->cb.bar(v, v->notice_item, v->data);
        return TRUE;
    }
    if (in_rect(v->lay.kifu, ev->x, ev->y)) {
        int mi = kifu_at(v, ev->x, ev->y);
        /* A move may also be the start of a drag that scrolls the list. */
        v->kifu_drag = mi >= 0;
        v->kifu_moved = FALSE;
        v->kifu_drag_x = ev->x, v->kifu_drag_y = ev->y;
        v->kifu_drag_first = v->kifu_first;
        return TRUE;
    }
    if (v->editing)
        return ev->type == GDK_BUTTON_PRESS ? edit_press(v, ev) : TRUE;
    if (ev->type != GDK_BUTTON_PRESS)
        return TRUE; /* GTK's double and triple click come after the press itself: a second tap that
                      * soon would put down the piece it just picked up */
    if (v->ply >= 0)
        return FALSE;
    int tapped = square_at(v, ev->x, ev->y);
    if (tapped >= 0 && v->cb.square_tap && v->cb.square_tap(v, tapped, v->data))
        return TRUE;
    Position *p = &v->game->pos;
    int peeked = v->peek_sq;
    if (v->peek_sq >= 0) {
        queue_hover(v, v->peek_sq);
        v->peek_sq = -1;
    }
    {
        /* Any promoted piece that this tap would not select or capture may be looked at; a
         * second tap on it puts it down. */
        int sq = square_at(v, ev->x, ev->y);
        if (sq >= 0 && sq == peeked)
            return TRUE;
        uint8_t pc = sq >= 0 ? p->board[sq] : EMPTY;
        gboolean own_turn = v->game->result == RESULT_NONE && v->cb.can_move && v->cb.can_move(v, p->side, v->data);
        gboolean selectable = pc && v->game->result == RESULT_NONE &&
                              (COLOR(pc) == p->side ? own_turn
                                                    : !own_turn && v->cb.can_premove &&
                                                          v->cb.can_premove(v, COLOR(pc), v->data));
        if (pc && v->other_side && !selectable && v->sel_sq < 0 && !v->sel_drop && unpromote_type(TYPE(pc)) != TYPE(pc)) {
            v->peek_sq = sq;
            queue_hover(v, sq);
            return TRUE;
        }
    }
    if (v->game->result != RESULT_NONE || !v->cb.can_move)
        return FALSE;
    if (!v->cb.can_move(v, p->side, v->data)) {
        /* The opponent's turn: a tap starts or cancels a premove of ours. */
        if (!v->cb.can_premove || !v->cb.can_premove(v, !p->side, v->data))
            return FALSE;
        if (v->has_premove) {
            view_clear_premove(v);
            return TRUE;
        }
        if (!v->pre) {
            v->pre_pos = *p;
            v->pre_pos.side = !p->side;
            v->pre = TRUE;
        }
        p = &v->pre_pos;
    } else {
        v->pre = FALSE;
    }
    if (v->asking) {
        for (int i = 0; i < 2; i++) {
            Rect r;
            promo_rect(v, i, &r);
            if (in_rect(r, ev->x, ev->y)) {
                Move m = v->ask;
                m.promote = i == 0;
                choose(v, m);
                return TRUE;
            }
        }
        clear_selection(v);
        gtk_widget_queue_draw(v->area);
        return TRUE;
    }
    /* Pieces in hand of the side to move. */
    int index = 0, hand_count;
    const int *order = hand_order(&hand_count);
    for (int i = 0; i < hand_count; i++) {
        int t = order[i];
        if (!p->hand[p->side][hand_slot(t)])
            continue;
        Rect r;
        hand_rect(v, p->side, index++, &r);
        if (in_rect(r, ev->x, ev->y)) {
            gboolean again = v->sel_drop == t;
            pdrag_start(v, ev, again);
            if (again)
                return TRUE; /* deselected on release, unless dragged */
            queue_marks(v);
            clear_selection(v);
            v->sel_drop = t;
            select_moves(v, DROP + t);
            queue_marks(v);
            return TRUE;
        }
    }
    int sq = square_at(v, ev->x, ev->y);
    if (sq < 0 || (p->board[sq] && COLOR(p->board[sq]) == p->side)) {
        /* Selecting, or deselecting: only the marks change. A piece may also be dragged. */
        gboolean again = sq >= 0 && v->sel_sq == sq;
        if (sq >= 0) {
            pdrag_start(v, ev, again);
            if (again)
                return TRUE; /* deselected on release, unless dragged */
        }
        queue_marks(v);
        clear_selection(v);
        if (sq >= 0) {
            v->sel_sq = sq;
            select_moves(v, sq);
        }
        queue_marks(v);
        return TRUE;
    }
    if (v->sel_sq >= 0 || v->sel_drop)
        tap_target(v, sq);
    if (v->asking) /* the promotion question */
        gtk_widget_queue_draw(v->area);
    return TRUE;
}

/* The same arrow again removes it. */
static void toggle_arrow(View *v, int from, int to)
{
    if (v->arrows_ply != v->game->count)
        v->narrows = 0, v->arrows_ply = v->game->count;
    for (int i = 0; i < v->narrows; i++)
        if (v->arrow_from[i] == from && v->arrow_to[i] == to) {
            v->narrows--;
            v->arrow_from[i] = v->arrow_from[v->narrows], v->arrow_to[i] = v->arrow_to[v->narrows];
            return;
        }
    if (v->narrows < 32)
        v->arrow_from[v->narrows] = from, v->arrow_to[v->narrows++] = to;
}

static gboolean release_cb(GtkWidget *w, GdkEventButton *ev, gpointer user)
{
    View *v = user;
    (void) w;
    if (g_getenv("DENBOKU_DEBUG"))
        g_printerr("%.3f release (event time %u)\n", g_get_monotonic_time() / 1e6, ev->time);
    if (v->overlay) {
        if (ev->button == 1)
            v->overlay->release(v, ev->x, ev->y, v->overlay_data);
        return TRUE;
    }
    if (v->kifu_drag) {
        gboolean moved = v->kifu_moved;
        v->kifu_drag = FALSE;
        int mi = kifu_at(v, ev->x, ev->y);
        if (!moved && mi >= 0) {
            view_set_ply(v, mi + 1);
            if (v->cb.ply_changed)
                v->cb.ply_changed(v, v->data);
        }
        return TRUE;
    }
    if (v->editing && ev->button == 1) {
        edit_release(v, ev->x, ev->y);
        return TRUE;
    }
    int sq = square_at(v, ev->x, ev->y);
    if (ev->button == 1 && !v->hover && v->pressed && v->hold_sq >= 0 && sq >= 0 && sq != v->hold_sq &&
        v->game->count == v->press_count) {
        v->pressed = FALSE, v->pdrag = PDRAG_NONE; /* a finger dragged: an arrow */
        queue_marks(v);
        clear_selection(v);
        toggle_arrow(v, v->hold_sq, sq);
        gtk_widget_queue_draw(v->area);
        return TRUE;
    }
    if (ev->button == 1 && v->pdrag != PDRAG_NONE) {
        gboolean dragged = v->pdrag == PDRAG_ON, again = v->pdrag_again;
        if (dragged && v->live_drag)
            queue_pdrag(v);
        v->pdrag = PDRAG_NONE;
        if (dragged && sq >= 0 && (v->sel_sq >= 0 || v->sel_drop) && sq != v->sel_sq) {
            tap_target(v, sq); /* dropped: as a tap on the square */
            if (v->asking)
                gtk_widget_queue_draw(v->area);
        } else if (!dragged && again) {
            queue_marks(v);
            clear_selection(v);
            queue_marks(v);
        }
        return TRUE;
    }
    if (ev->button == 3) {
        if (v->drag_sq >= 0 && sq >= 0)
            toggle_arrow(v, v->drag_sq, sq);
        v->drag_sq = -1;
    } else if (ev->button == 1) {
        if (!v->pressed)
            return TRUE;
        v->pressed = FALSE;
        gboolean held = ev->time - v->press_time > 600 && v->game->count == v->press_count;
        if (held && v->hold_sq >= 0 && sq >= 0 && !v->editing) {
            clear_selection(v); /* a long press draws instead of selecting */
            toggle_arrow(v, v->hold_sq, sq);
        } else if (!held && v->narrows && v->arrows_ply == v->game->count) {
            v->narrows = 0;
        } else {
            return TRUE; /* a plain tap: the press already drew what changed */
        }
    }
    gtk_widget_queue_draw(v->area);
    return TRUE;
}

void view_step(View *v, ViewStep step)
{
    int ply = v->ply < 0 ? v->game->count : v->ply;
    ply = step == STEP_FIRST ? 0 : step == STEP_LAST ? v->game->count
                                                     : CLAMP(ply + (step == STEP_PREV ? -1 : 1), 0, v->game->count);
    view_set_ply(v, ply == v->game->count ? -1 : ply);
    if (v->cb.ply_changed)
        v->cb.ply_changed(v, v->data);
}

/* A tap at (x, y), as the mouse makes it. */
static void tap_at(View *v, double x, double y)
{
    GdkEventButton ev = {0};
    ev.type = GDK_BUTTON_PRESS;
    ev.button = 1;
    ev.x = x, ev.y = y;
    ev.time = gtk_get_current_event_time();
    v->synthetic = TRUE;
    press_cb(v->area, &ev, v);
    ev.type = GDK_BUTTON_RELEASE;
    release_cb(v->area, &ev, v);
    v->synthetic = FALSE;
}

static void tap_square(View *v, int sq)
{
    Rect r;
    square_rect(v, sq, &r);
    tap_at(v, r.x + r.w / 2, r.y + r.h / 2);
}

/* The next piece in hand of the side we may move (or premove): a tap on it, as the mouse
 * would make; after the last one, the same piece again puts it back. */
static void cursor_hand(View *v)
{
    Position *p = &v->game->pos;
    int color = p->side;
    if (!v->cb.can_move || !v->cb.can_move(v, color, v->data)) {
        if (!v->cb.can_premove || !v->cb.can_premove(v, !color, v->data))
            return;
        color = !color;
    }
    int hand_count, n = 0, sel = -1;
    const int *order = hand_order(&hand_count);
    for (int i = 0; i < hand_count; i++)
        if (p->hand[color][hand_slot(order[i])]) {
            if (order[i] == v->sel_drop)
                sel = n;
            n++;
        }
    if (!n)
        return;
    Rect r;
    hand_rect(v, color, sel + 1 < n ? sel + 1 : sel, &r);
    tap_at(v, r.x + r.w / 2, r.y + r.h / 2);
}

/* What the keys' cursor frames: a square, or in the editor a hand. */
static void queue_cursor(View *v, int sq)
{
    if (sq >= SQUARES)
        queue_rect(v, cursor_hand_rect(v, sq - SQUARES), 4);
    else
        queue_square(v, sq, 0);
}

/* The cursor in the editor: on the squares and, past the top or bottom row, on the hand there.
 * Select taps as a click would, with the tool chosen; on a hand it takes the selected pieces
 * there, adds the palette's piece, or asks how many of each it holds. */
static gboolean edit_cursor(View *v, ViewCursor a)
{
    int center = SQ(board_h / 2, board_w / 2), top = !bottom_color(v);
    gboolean shown = v->cursor_sq >= 0;
    int hand = v->cursor_sq >= SQUARES ? v->cursor_sq - SQUARES : -1;
    switch (a) {
    case CURSOR_UP: case CURSOR_DOWN: case CURSOR_LEFT: case CURSOR_RIGHT: {
        int was = v->cursor_sq;
        if (!shown) {
            v->cursor_sq = center;
        } else if (hand >= 0) {
            if ((a == CURSOR_DOWN) == (hand == top)) {
                int screen_row = hand == top ? 0 : board_h - 1;
                v->cursor_sq = SQ(v->flipped ? board_h - 1 - screen_row : screen_row, board_w / 2);
            }
        } else {
            int dr = a == CURSOR_UP ? -1 : a == CURSOR_DOWN ? 1 : 0, dc = a == CURSOR_LEFT ? -1 : a == CURSOR_RIGHT ? 1 : 0;
            if (v->flipped)
                dr = -dr, dc = -dc;
            int r = ROW(v->cursor_sq) + dr, screen_row = v->flipped ? board_h - 1 - r : r;
            if (screen_row < 0 || screen_row >= board_h)
                v->cursor_sq = SQUARES + (screen_row < 0 ? top : !top);
            else
                v->cursor_sq = SQ(r, CLAMP(COL(v->cursor_sq) + dc, 0, board_w - 1));
        }
        queue_cursor(v, was);
        queue_cursor(v, v->cursor_sq);
        return TRUE;
    }
    case CURSOR_SELECT:
        if (!shown) {
            v->cursor_sq = center;
        } else if (hand < 0) {
            tap_square(v, v->cursor_sq);
        } else {
            ed_begun = FALSE;
            if (v->tool == TOOL_ERASE && v->cb.edit_tap)
                v->cb.edit_tap(v, -1, hand, v->data);
            else
                edit_hand_tap(v, hand, 0);
        }
        break;
    case CURSOR_HAND: /* the hand button: your hand, the other's, back to the board */
        if (hand < 0)
            v->cursor_sq = SQUARES + !top;
        else if (hand == !top)
            v->cursor_sq = SQUARES + top;
        else
            v->cursor_sq = center;
        break;
    case CURSOR_CANCEL:
        if (v->ed_hand_type) {
            v->ed_hand_type = 0;
        } else if (edit_selected_count(v)) {
            edit_deselect(v);
        } else if (shown) {
            v->cursor_sq = -1;
        } else {
            return FALSE;
        }
        break;
    default: return FALSE;
    }
    gtk_widget_queue_draw(v->area);
    return TRUE;
}

gboolean view_cursor(View *v, ViewCursor a)
{
    if (v->editing)
        return edit_cursor(v, a);
    int center = SQ(board_h / 2, board_w / 2);
    gboolean shown = v->cursor_sq >= 0 && v->cursor_sq < SQUARES;
    if (v->asking) {
        if (a == CURSOR_SELECT) {
            Move m = v->ask;
            m.promote = v->cursor_choice == 0;
            v->cursor_choice = 0;
            choose(v, m);
        } else if (a == CURSOR_CANCEL) {
            clear_selection(v);
        } else if (a <= CURSOR_RIGHT) {
            v->cursor_choice ^= 1;
        }
        if (!shown)
            v->cursor_sq = v->ask.to;
        gtk_widget_queue_draw(v->area);
        return TRUE;
    }
    /* A promoted piece's other side is looked at while the cursor stays on it. */
    if (v->peek_sq >= 0 && a != CURSOR_SELECT) {
        queue_hover(v, v->peek_sq);
        v->peek_sq = -1;
    }
    switch (a) {
    case CURSOR_UP: case CURSOR_DOWN: case CURSOR_LEFT: case CURSOR_RIGHT: {
        int was = v->cursor_sq;
        if (!shown) {
            v->cursor_sq = v->sel_sq >= 0 ? v->sel_sq : center;
        } else {
            int dr = a == CURSOR_UP ? -1 : a == CURSOR_DOWN ? 1 : 0, dc = a == CURSOR_LEFT ? -1 : a == CURSOR_RIGHT ? 1 : 0;
            if (v->flipped)
                dr = -dr, dc = -dc;
            v->cursor_sq = SQ(CLAMP(ROW(v->cursor_sq) + dr, 0, board_h - 1), CLAMP(COL(v->cursor_sq) + dc, 0, board_w - 1));
        }
        /* Its two squares, and the selected piece's, whose other side shows while the cursor is on it. */
        queue_cursor(v, was);
        queue_cursor(v, v->cursor_sq);
        queue_square(v, v->sel_sq, 0);
        return TRUE;
    }
    case CURSOR_SELECT:
        if (shown)
            tap_square(v, v->cursor_sq);
        else
            v->cursor_sq = center;
        break;
    case CURSOR_CANCEL:
        if (v->sel_sq >= 0 || v->sel_drop || v->pre) {
            queue_marks(v);
            clear_selection(v);
            v->pre = FALSE;
        } else if (v->has_premove) {
            view_clear_premove(v);
        } else if (shown) {
            v->cursor_sq = -1;
        } else {
            return FALSE;
        }
        break;
    case CURSOR_HAND:
        if (!shown)
            v->cursor_sq = center;
        cursor_hand(v);
        break;
    }
    gtk_widget_queue_draw(v->area);
    return TRUE;
}

View *view_new(Game *g, const ViewCallbacks *cb, void *data)
{
    View *v = g_new0(View, 1);
    v->game = g;
    v->cb = *cb;
    v->data = data;
    v->ply = -1;
    v->cursor_sq = -1;
    v->kifu_scroll = -1;
    v->hover_kifu = -1;
    v->hover_bar = -1;
    v->notice_item = -1;
    v->show_moves = TRUE;
    v->theme = &THEME_MONO;
    v->colors = &COLORS_MONO;
    clear_selection(v);
    v->area = gtk_drawing_area_new();
    gtk_widget_set_double_buffered(v->area, FALSE); /* the frame image already is the buffer */
    gtk_widget_add_events(v->area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK |
                                      GDK_LEAVE_NOTIFY_MASK | GDK_SCROLL_MASK);
    v->hover_sq = -1;
    v->drag_sq = v->hold_sq = -1;
    v->later_sq[0] = v->later_sq[1] = -1;
    g_signal_connect(v->area, "expose-event", G_CALLBACK(expose_cb), v);
    g_signal_connect(v->area, "button-press-event", G_CALLBACK(press_cb), v);
    g_signal_connect(v->area, "button-release-event", G_CALLBACK(release_cb), v);
    g_signal_connect(v->area, "motion-notify-event", G_CALLBACK(motion_cb), v);
    g_signal_connect(v->area, "leave-notify-event", G_CALLBACK(leave_cb), v);
    g_signal_connect(v->area, "scroll-event", G_CALLBACK(scroll_cb), v);
    return v;
}

GtkWidget *view_widget(View *v) { return v->area; }

void view_set_style(View *v, PieceStyle *s, const PieceTheme *theme, const BoardColors *colors)
{
    v->style = s;
    v->theme = theme;
    v->colors = colors;
    /* The style change happens behind Preferences: the board's pieces now, so the redraw when the
     * dialog closes does not rasterize glyphs; the hands' later. */
    warm(v, TRUE, FALSE);
    schedule_warm(v);
    gtk_widget_queue_draw(v->area);
}

void view_set_editing(View *v, gboolean editing)
{
    v->refreshed = FALSE; /* the next refresh redraws everything */
    v->editing = editing;
    clear_selection(v);
    memset(v->ed_sel, 0, sizeof v->ed_sel);
    v->ed_hand_type = 0;
    v->ed_drag = DRAG_NONE;
    v->ed_from = v->ed_cur = -1;
    v->cursor_sq = -1;
    gtk_widget_queue_draw(v->area);
}

void view_set_flipped(View *v, gboolean flipped)
{
    if (v->flipped == flipped)
        return; /* a whole redraw for nothing, on e-ink a full update */
    v->refreshed = FALSE; /* the next refresh redraws everything */
    v->flipped = flipped;
    gtk_widget_queue_draw(v->area);
}

gboolean view_flipped(View *v) { return v->flipped; }

void view_set_japanese(View *v, gboolean ja)
{
    if (v->japanese == ja)
        return; /* a whole redraw for nothing, on e-ink a full update */
    v->japanese = ja;
    gtk_widget_queue_draw(v->area);
}

void view_refresh(View *v);

/* Another game to show (the one to come, behind a lobby), or the usual one again. */
void view_set_game(View *v, Game *g)
{
    v->game = g;
    v->ply = v->kifu_scroll = -1;
    v->has_hint = v->has_premove = FALSE;
    v->nbest = v->narrows = 0;
    clear_selection(v);
    v->refreshed = FALSE;
    gtk_widget_queue_draw(v->area);
}

Game *view_game(View *v) { return v->game; }

void view_preview(View *v, Game *g, int variant, PieceStyle *s)
{
    v->preview = g, v->preview_variant = variant, v->preview_style = s;
    clear_selection(v);
    v->refreshed = FALSE;
    gtk_widget_queue_draw(v->area);
}

void view_set_ply(View *v, int ply)
{
    v->ply = ply >= v->game->count ? -1 : ply;
    v->kifu_scroll = -1; /* follow the shown move */
    queue_marks(v);
    clear_selection(v);
    if (v->colors && v->colors->last_corners && v->refreshed) {
        view_refresh(v); /* e-ink: only what differs from the position shown, as after a move */
        return;
    }
    view_queue_bar(v);
    v->refreshed = FALSE; /* the next refresh redraws everything */
    gtk_widget_queue_draw(v->area);
}

int view_ply(View *v) { return v->ply; }

void view_set_hint(View *v, const Move *m)
{
    if (!m && !v->has_hint)
        return; /* nothing to take away */
    queue_hint(v);
    v->has_hint = m != NULL;
    if (m)
        v->hint = *m;
    queue_hint(v);
}

void view_set_goals(View *v, const int *squares, int n)
{
    n = CLAMP(n, 0, 81);
    if (n == v->ngoals && (!n || !memcmp(v->goals, squares, n * sizeof *squares)))
        return;
    if (n)
        memcpy(v->goals, squares, n * sizeof *squares);
    v->ngoals = n;
    gtk_widget_queue_draw(v->area);
}

void view_set_best(View *v, const Move *moves, int n, int ply)
{
    n = CLAMP(n, 0, 5);
    if (n == v->nbest && (!n || (ply == v->best_ply && !memcmp(moves, v->best, n * sizeof *moves))))
        return;
    memcpy(v->best, moves, n * sizeof *moves);
    v->nbest = n, v->best_ply = ply;
    gtk_widget_queue_draw(v->area);
}

void view_set_area_hints(View *v, gboolean show, gboolean browsing)
{
    if (show == v->area_hints && browsing == v->browsing)
        return;
    v->area_hints = show, v->browsing = browsing;
    gtk_widget_queue_draw(v->area);
}

void view_set_board_marks(View *v, gboolean show)
{
    if (v->board_marks == show)
        return;
    v->board_marks = show;
    gtk_widget_queue_draw(v->area);
}

void view_set_marks(View *v, const char *marks, int n)
{
    char next[MAX_PLY] = {0};
    memcpy(next, marks, CLAMP(n, 0, MAX_PLY));
    if (memcmp(next, v->marks, MAX_PLY)) {
        memcpy(v->marks, next, MAX_PLY);
        v->any_marks = FALSE;
        for (int i = 0; i < MAX_PLY && !v->any_marks; i++)
            v->any_marks = next[i] != 0;
        gtk_widget_queue_draw(v->area);
    }
}

void view_set_auto_promote(View *v, int mode) { v->auto_promote = mode; }

void view_set_chess_colors(View *v, gboolean on)
{
    if (v->chess != on)
        v->chess = on, gtk_widget_queue_draw(v->area);
}

void view_set_other_side(View *v, gboolean show)
{
    if (v->other_side == show)
        return; /* a whole redraw for nothing, on e-ink a full update */
    v->other_side = show;
    gtk_widget_queue_draw(v->area);
}

void view_set_show_moves(View *v, gboolean show)
{
    if (v->show_moves == show)
        return; /* a whole redraw for nothing, on e-ink a full update */
    v->show_moves = show;
    gtk_widget_queue_draw(v->area);
}

static gboolean refresh_later_cb(gpointer data)
{
    View *v = data;
    v->later = 0;
    for (int i = 0; i < 2; i++) {
        if (v->later_sq[i] >= 0)
            queue_square(v, v->later_sq[i], 4);
        v->later_sq[i] = -1;
    }
    for (int c = 0; c < 2; c++) {
        if (v->later_hand[c])
            queue_rect(v, v->lay.side[c], 4);
        v->later_hand[c] = FALSE;
        queue_rect(v, v->lay.clock[c], 4);
        queue_rect(v, v->lay.label[c], 4);
    }
    if (v->lay.kifu.w > 0)
        queue_rect(v, v->lay.kifu, 4);
    return FALSE;
}

/* After a move: redraws only what changed since the last refresh (the squares, the last move's
 * marks, the hands, the kings' check glow, the move list and the clocks). The whole board takes
 * about half a second on the Kindle; a move this way takes a fraction of it. */
void view_refresh(View *v)
{
    v->kifu_scroll = -1; /* a move or a new position: show the current move again */
    /* A piece picked for a premove stays picked when our turn comes, as on lishogi (shogiground
     * unselects only when the move leaves or lands on its square); its targets become the legal moves. */
    int keep_sq = -1, keep_drop = 0;
    const Position *now = &v->game->pos;
    if (v->pre && v->ply < 0 && !v->asking && now->side == v->pre_pos.side) {
        if (v->sel_sq >= 0 && now->board[v->sel_sq] == v->pre_pos.board[v->sel_sq])
            keep_sq = v->sel_sq;
        if (v->sel_drop && now->hand[now->side][hand_slot(v->sel_drop)])
            keep_drop = v->sel_drop;
    }
    if (v->area_hints) /* the hand's badge goes to the other side */
        for (int c = 0; c < 2; c++)
            queue_rect(v, v->lay.side[c], 4);
    Position tmp, *p = shown(v, &tmp);
    int ply = v->ply < 0 ? v->game->count : v->ply, last[2] = {-1, -1};
    if (ply > 0) {
        const Move *m = &v->game->moves[ply - 1];
        last[0] = IS_DROP(*m) ? -1 : m->from, last[1] = m->to;
    }
    gboolean full = !v->refreshed || v->asking || v->narrows || v->has_hint || v->has_premove || v->nbest || v->ngoals ||
                    (v->board_marks && v->any_marks); /* a game not analyzed has no marks to move */
    if (!full) {
        queue_marks(v);
        /* The kings' squares only when a check comes or goes (its glow reaches past the square). */
        gboolean check = pos_in_check(p) || pos_in_check(&v->refreshed_pos);
        for (int sq = 0; sq < 81; sq++) {
            gboolean king = check && (TYPE(p->board[sq]) == KING || TYPE(v->refreshed_pos.board[sq]) == KING);
            if (p->board[sq] != v->refreshed_pos.board[sq] || king)
                queue_square(v, sq, king ? v->lay.sw * 0.35 : 4);
        }
        for (int i = 0; i < 2; i++) {
            if (last[i] >= 0)
                queue_square(v, last[i], 4);
            /* On e-ink the previous move's mark goes with the second batch: it is often far away. */
            int old = v->refreshed_last[i];
            if (old >= 0 && old != last[0] && old != last[1]) {
                /* A move before the second batch (a fast engine reply) keeps the one waiting. */
                if (v->later_sq[i] >= 0 && v->later_sq[i] != old)
                    queue_square(v, v->later_sq[i], 4);
                v->later_sq[i] = old;
            }
        }
        for (int c = 0; c < 2; c++) {
            if (memcmp(p->hand[c], v->refreshed_pos.hand[c], sizeof p->hand[c]))
                v->later_hand[c] = TRUE;
        }
        /* Elsewhere all of it in one update: in two, one square of the previous move's mark went
         * before the other when a piece was taken there. */
        if (v->live_drag)
            refresh_later_cb(v);
        /* Clear the selection before the synchronous update below: the queued marks are drawn
         * again otherwise, and stay on the board after the move. The premove position is stale
         * once the opponent has moved. */
        clear_selection(v);
        v->pre = FALSE;
        /* The squares go to the screen at once, alone; hands, clocks and the move list a moment
         * later. Together the e-ink updates them as one, as large as the box around all of them
         * (most of the screen), which is far slower than a few squares. */
        gdk_window_process_updates(gtk_widget_get_window(v->area), FALSE);
        gdk_flush();
        if (!v->later && !v->live_drag)
            v->later = g_timeout_add(200, refresh_later_cb, v);
    }
    clear_selection(v);
    if (keep_sq >= 0 || keep_drop) {
        v->pre = FALSE;
        v->sel_sq = keep_sq, v->sel_drop = keep_drop;
        select_moves(v, keep_drop ? DROP + keep_drop : keep_sq);
        if (!full)
            queue_marks(v);
    }
    v->refreshed = TRUE;
    v->refreshed_pos = *p;
    memcpy(v->refreshed_last, last, sizeof last);
    view_queue_bar(v); /* after the squares went to the screen */
    if (full)
        gtk_widget_queue_draw(v->area);
}

void view_reload(View *v)
{
    v->refreshed = FALSE;
    v->refreshed_last[0] = v->refreshed_last[1] = -1;
    gtk_widget_queue_draw(v->area);
}

/* Only the clock boxes are redrawn every second, so e-ink refreshes stay small. */
void view_refresh_clocks(View *v)
{
    /* Only a clock whose text changed: on e-ink each redrawn box is an update the next tap waits for. */
    for (int c = 0; c < 2; c++) {
        char label[128] = "", clock[64] = "", now[sizeof v->shown_text[c]];
        gboolean active = FALSE;
        if (v->cb.player_text)
            v->cb.player_text(v, c, label, clock, &active, v->data);
        snprintf(now, sizeof now, "%d%s\n%s", active, clock, label);
        if (!strcmp(now, v->shown_text[c]))
            continue;
        /* Both in one piece: the name runs into an empty clock's room. */
        Rect r = v->lay.clock[c], l = v->lay.label[c];
        double x0 = MIN(r.x, l.x), y0 = MIN(r.y, l.y), x1 = MAX(r.x + r.w, l.x + l.w), y1 = MAX(r.y + r.h, l.y + l.h);
        queue_rect(v, (Rect) {x0, y0, x1 - x0, y1 - y0}, 2);
    }
}

static gboolean unflash_cb(gpointer data)
{
    View *v = data;
    v->flashing = FALSE;
    gtk_widget_queue_draw(v->area);
    return FALSE;
}

void view_flash(View *v)
{
    v->flashing = TRUE;
    gtk_widget_queue_draw(v->area);
    gdk_window_process_updates(gtk_widget_get_window(v->area), TRUE);
    gdk_flush();
    g_timeout_add(120, unflash_cb, v);
}

void view_set_hover(View *v, gboolean on) { v->hover = on; }

void view_set_physical(View *v, int physical)
{
    if (v->physical == physical)
        return; /* a whole redraw for nothing, on e-ink a full update */
    v->physical = physical;
    clear_selection(v);
    gtk_widget_queue_draw(v->area);
}

void view_set_notation_ja(View *v, gboolean ja)
{
    if (v->notation_ja == ja)
        return; /* a whole redraw for nothing, on e-ink a full update */
    v->notation_ja = ja;
    gtk_widget_queue_draw(v->area);
}

void view_set_coords(View *v, gboolean show)
{
    if (v->show_coords == show)
        return; /* a whole redraw for nothing, on e-ink a full update */
    v->show_coords = show;
    gtk_widget_queue_draw(v->area);
}

void view_queue_bar(View *v)
{
    /* Only when what it shows changed: on e-ink the bar's update would join the squares' of a move
     * into one as tall as the screen. */
    char now[sizeof v->bar_shown] = "";
    bar_update(v);
    for (int i = 0, at = 0; i < VIEW_BAR_COUNT && at < (int) sizeof now - 80; i++)
        at += snprintf(now + at, sizeof now - (size_t) at, "%d%d%s|", v->bar_items[i].state, i == v->hover_bar,
                       v->bar_items[i].label);
    if (!strcmp(now, v->bar_shown))
        return;
    snprintf(v->bar_shown, sizeof v->bar_shown, "%s", now);
    if (v->lay.bar.w > 0)
        gtk_widget_queue_draw_area(v->area, (int) v->lay.bar.x - 4, (int) v->lay.bar.y - 4, (int) v->lay.bar.w + 8, (int) v->lay.bar.h + 8);
}

void view_set_notice_item(View *v, int item) { v->notice_item = item; }

void view_set_notice(View *v, const char *text)
{
    if (!strcmp(text, v->notice))
        return;
    snprintf(v->notice, sizeof v->notice, "%s", text);
    queue_kifu(v);
}

int view_bar_item_at(View *v, double x, double y) { return bar_at(v, x, y); }

void view_bar_activate(View *v, int item)
{
    if (bar_usable(v, item) && v->cb.bar)
        v->cb.bar(v, item, v->data);
}

void view_bar_area(View *v, GdkRectangle *r)
{
    *r = (GdkRectangle) {(int) v->lay.bar.x, (int) v->lay.bar.y, (int) v->lay.bar.w, (int) v->lay.bar.h};
}

void view_set_overlay(View *v, const ViewOverlay *o, void *data, const GdkRectangle *area)
{
    v->overlay = o, v->overlay_data = data;
    v->hover_bar = v->hover_kifu = -1;
    if (area)
        gtk_widget_queue_draw_area(v->area, area->x, area->y, area->width, area->height);
    else
        gtk_widget_queue_draw(v->area);
}

void view_column_area(View *v, GdkRectangle *r)
{
    GtkAllocation a;
    gtk_widget_get_allocation(v->area, &a);
    if (v->lay.landscape)
        *r = (GdkRectangle) {(int) v->lay.bar.x, 0, a.width - (int) v->lay.bar.x, a.height};
    else
        *r = (GdkRectangle) {0, 0, a.width, a.height};
}

void view_set_inset(View *v, int inset)
{
    if (v->inset != inset) {
        v->inset = inset;
        v->refreshed = FALSE;
        gtk_widget_queue_draw(v->area);
    }
}

int view_game_right(View *v, int W, int H)
{
    Layout l = v->lay;
    layout_ask(v, &l);
    layout_compute(&l, W - v->inset, H);
    return l.landscape ? (int) (l.bar.x + l.bar.w + 6) : W - v->inset;
}

void view_set_covered(View *v, const GdkRectangle *r)
{
    v->covered = r ? *r : (GdkRectangle) {0, 0, 0, 0};
}

void view_set_dimmed(View *v, gboolean on)
{
    if (v->dimmed != on) {
        v->dimmed = on;
        gtk_widget_queue_draw(v->area);
    }
}

void view_set_board_only(View *v, gboolean on)
{
    if (v->portrait != on) {
        v->portrait = on;
        gtk_widget_queue_draw(v->area);
    }
}

void view_set_align_left(View *v, gboolean on)
{
    if (v->align_left != on) {
        v->align_left = on;
        gtk_widget_queue_draw(v->area);
    }
}

int view_natural_width(View *v, int H)
{
    Layout l = v->lay;
    layout_ask(v, &l);
    return (int) (layout_natural_width(&l, H) + 1);
}

int view_natural_height(View *v, int W, int H)
{
    Layout l = v->lay;
    layout_ask(v, &l);
    return (int) (layout_natural_height(&l, W, H) + 1);
}

void view_set_live_drag(View *v, gboolean on) { v->live_drag = on; }

/* ---- passing notices ---- */

/* A pill at the top, the text color on the background's opposite; a long notice wraps instead of shrinking. */
static void draw_toast(View *v, cairo_t *cr, double W)
{
    /* At least the dialogs' text: on a phone the bar's 30 px cap was tiny. */
    double size = MAX(CLAMP(v->lay.bar.h > 0 ? v->lay.bar.h * 0.28 : 16, 13, 30), line_height() * 0.85), pad = size * 0.9;
    static PangoLayout *pl;
    static PangoFontDescription *fd;
    if (!pl) {
        pl = pango_cairo_create_layout(cr);
        pango_layout_set_alignment(pl, PANGO_ALIGN_CENTER);
        pango_layout_set_wrap(pl, PANGO_WRAP_WORD_CHAR);
        fd = pango_font_description_from_string(UI_FONT);
    } else {
        pango_cairo_update_layout(cr, pl);
    }
    pango_font_description_set_absolute_size(fd, size * PANGO_SCALE);
    pango_layout_set_font_description(pl, fd);
    pango_layout_set_text(pl, v->toast, -1);
    pango_layout_set_width(pl, (int) (MIN(W - 24 - 2 * pad, size * 32) * PANGO_SCALE));
    PangoRectangle ink, log;
    pango_layout_get_pixel_extents(pl, &ink, &log);
    double w = MIN(log.width + 2 * pad, W - 24), h = log.height + pad;
    Rect r = {(W - w) / 2, 12, w, h};
    v->toast_rect = r;
    rounded(cr, r, MIN(h / 2, size * 1.2));
    fg(v, cr);
    cairo_fill(cr);
    bg(v, cr);
    cairo_move_to(cr, r.x + pad - log.x, r.y + pad / 2);
    pango_cairo_show_layout(cr, pl);
}

static gboolean toast_gone_cb(gpointer data)
{
    View *v = data;
    v->toast_id = 0;
    v->toast[0] = 0;
    Rect r = v->toast_rect;
    gtk_widget_queue_draw_area(v->area, (int) r.x - 4, (int) r.y - 4, (int) r.w + 8, (int) r.h + 8);
    return FALSE;
}

void view_toast(View *v, const char *text)
{
    if (v->toast_id) {
        g_source_remove(v->toast_id);
        toast_gone_cb(v);
    }
    snprintf(v->toast, sizeof v->toast, "%s", text);
    v->toast_id = g_timeout_add(2500 + 40 * (guint) strlen(text), toast_gone_cb, v);
    /* Where the last one was, a band across the board (a longer text wraps to more lines): not the whole
     * screen, a full e-ink refresh on the Kindle. The first time its place is not known yet. */
    Rect r = v->toast_rect;
    if (r.h > 0)
        gtk_widget_queue_draw_area(v->area, 0, (int) (r.y - 2 * r.h), v->area->allocation.width, (int) (5 * r.h));
    else
        gtk_widget_queue_draw(v->area);
}
