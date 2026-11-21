#include "layout.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(x, lo, hi) ((x) > (hi) ? (hi) : (x) < (lo) ? (lo) : (x))

/* A square of the usual board in a view whose board has squares `sw` wide. */
static double usual_square(const Layout *l, double sw, double hand_min)
{
    return MIN(sw, MAX(sw * l->board_w / (double) l->usual_w, hand_min));
}

/* The height of a panel whose pieces in hand, in one row below the text, are `cell` wide. */
static double panel_height_for(double cell, double square)
{
    double ph = 0;
    for (int i = 0; i < 8; i++)
        ph = 3 * MAX(4, ph * 0.04) + CLAMP(ph * 0.3, 30, MAX(64, square * 0.7)) + cell * (1.1 * 1.18 + 0.18);
    return ph;
}

/* The largest a piece in hand gets: a usual square, or 80% of the board's own when they are
 * larger (beside a small board's huge squares a usual one looks tiny). */
static double hand_most(double square, double sw) { return MAX(square, sw * 0.8); }

/* Portrait: the least height of a panel beside squares `sw` wide, high enough for pieces in hand
 * at 85% of their largest (a small board as wide as the screen would leave them tiny). */
static double panel_least(const Layout *l, double sw, double W, double H, double panel_min)
{
    double square = usual_square(l, sw, MIN(sw / 2, MIN(W, H) / 21));
    return MAX(panel_min, panel_height_for(hand_most(square, sw) * 0.85, square));
}

/* One player's panel: name and clock in a row with the pieces in hand below, or, when the panel
 * is too low for that, the hand at the left, the panel's height, and the name above the clock in
 * what is left. The pieces are as large as rows of them allow, up to the board's. */
static void panel_layout(Layout *l, int color, Rect s)
{
    double pad = MAX(4, MIN(s.w, s.h) * 0.04);
    l->side[color] = s;
    int n = l->hand_kinds;
    /* Text and the hand's place measured against the usual board's square: a small board's huge
     * squares would otherwise put the hand at the left and the name above the clock in Dōbutsu and
     * not in Goro Goro, the name larger in one than the other. The pieces grow up to hand_most(). */
    double square = usual_square(l, l->sw, l->hand_min), most = hand_most(square, l->sw);
    double row_h = CLAMP(s.h * 0.3, 30, MAX(64, square * 0.7)); /* text that grows with the board on large screens */
    /* The hand below the text: as many rows as make its pieces largest. */
    Rect below = {s.x + pad, s.y + 2 * pad + row_h, s.w - 2 * pad, s.h - 3 * pad - row_h};
    double best = 0;
    int best_cols = n;
    for (int r = 1; r <= 3; r++) {
        int cols = (n + r - 1) / r;
        double cw = MIN(MIN(below.w / (cols * 1.18 + 0.18), below.h / (r * 1.1 * 1.18 + 0.18)), most);
        if (cw > best + 0.5)
            best = cw, best_cols = cols;
    }
    /* The hand at the left. */
    double text_min = CLAMP(s.w * 0.3, 110, 300), hh = s.h - 2 * pad, lh = hh / 2;
    double cw = MIN(MIN(hh / (1.1 * 1.18 + 0.18), (s.w - 3 * pad - text_min) / (n * 1.18 + 0.18)), most);
    if ((best >= square * 0.47 && best >= l->hand_min) || best >= cw) {
        double clock_w = CLAMP(s.w * 0.45, 110, 300);
        l->label[color] = (Rect) {s.x + pad, s.y + pad, s.w - clock_w - 3 * pad, row_h};
        l->clock[color] = (Rect) {s.x + s.w - pad - clock_w, s.y + pad, clock_w, row_h};
        l->hand[color] = below;
        l->hand_cell[color] = best, l->hand_cols[color] = best_cols;
    } else {
        double hw = cw * (n * 1.18 + 0.18), tx = s.x + 2 * pad + hw, tw = s.w - 3 * pad - hw;
        l->hand[color] = (Rect) {s.x + pad, s.y + pad, hw, hh};
        l->label[color] = (Rect) {tx, s.y + pad, tw, lh};
        l->clock[color] = (Rect) {tx, s.y + pad + lh, tw, lh};
        l->hand_cell[color] = cw, l->hand_cols[color] = n;
    }
}

static double coords_band(const Layout *l, double W, double H) { return l->coords ? MAX(18, MIN(W, H) * 0.022) : 4; }

/* Portrait: the bar's height, a row of moves' and the least a panel gets. */
static void portrait_heights(double H, double *bar_h, double *strip_h, double *panel_min)
{
    *bar_h = layout_bar_height(0, H), *strip_h = CLAMP(H * 0.042, 30, 70), *panel_min = MAX(56, H * 0.085);
}

/* Landscape: the least width of the column at the right, and the most for squares `sw` wide. */
static double column_least(double W, double H) { return CLAMP(W * 0.28, 200, MAX(520, H * 0.3)); }
/* On large screens wide enough for four pieces in hand at half a square in a row (a square of the
 * usual board, with its coordinates: the small boards' are huge). */
static double column_most(const Layout *l, double sw, double H)
{
    return MIN(H * 0.62, MAX(560, MIN(sw, H / (l->usual_w + 0.8)) * 2.7));
}

/* Every screen the same way: a panel for each player, the board, the moves and a bar of actions
 * (bar_rect), the bar where a thumb is. Portrait: the moves at the top (more rows when the panels
 * would get more height than they use), the panels above and below the board, the bar at the
 * bottom. Landscape: the board at the left, its full height, and a column at the right with the
 * opponent's panel, the moves, the player's panel and the bar; the column stops growing at a
 * width that suits it, and what is left goes around both. */
double layout_bar_height(double W, double H) { return W > H * 1.05 ? CLAMP(H * 0.085, 44, 100) : CLAMP(H * 0.06, 44, 110); }

void layout_compute(Layout *l, double W, double H)
{
    int board_w = l->board_w, board_h = l->board_h;
    l->landscape = !l->board_only && W > H * 1.05;
    double band = coords_band(l, W, H);
    int top = !l->bottom, bottom = l->bottom;
    l->band = band;
    l->hand_min = 0;
    if (l->board_only) {
        /* The rest off the view: nothing there can be used while a drawer is open. */
        double sh = MIN((H - band - 8) / board_h, (W - band - 8) / board_w * l->square_ratio), sw = sh / l->square_ratio;
        l->sw = sw, l->sh = sh;
        l->bx = (W - board_w * sw - band) / 2 + 2;
        l->by = (H - board_h * sh - band) / 2 + band;
        Rect away = {-10000, -10000, 100, 100};
        for (int c = 0; c < 2; c++)
            panel_layout(l, c, away);
        l->kifu = l->bar = (Rect) {0, 0, 0, 0};
        l->kifu_rows = 1;
        return;
    }
    if (!l->landscape) {
        double pad = 4, bar_h, strip_h, panel_min;
        portrait_heights(H, &bar_h, &strip_h, &panel_min);
        double avail = H - 6 * pad - strip_h - bar_h;
        double sw_most = (W - band - 8) / board_w, sw = sw_most;
        for (int i = 0; i < 8; i++)
            sw = MIN(sw_most, (avail - band - 2 * panel_least(l, sw, W, H, panel_min)) / board_h / l->square_ratio);
        double sh = sw * l->square_ratio, frame_w = board_w * sw + band, frame_h = board_h * sh + band;
        double ph = (avail - frame_h) / 2, cap = MAX(panel_min, CLAMP(H * 0.04, 30, 64) + sh * 1.3 + 12);
        int rows = ph > cap ? MIN(4, 1 + (int) (2 * (ph - cap) / strip_h)) : 1;
        ph -= (rows - 1) * strip_h / 2;
        l->sw = sw, l->sh = sh, l->hand_min = MIN(sw / 2, MIN(W, H) / 21);
        l->kifu = (Rect) {4, pad, W - 8, rows * strip_h};
        l->kifu_rows = rows, l->kifu_row_h = strip_h;
        double y = 2 * pad + rows * strip_h;
        panel_layout(l, top, (Rect) {4, y, W - 8, ph});
        l->bx = (W - frame_w) / 2 + 2;
        l->by = y + ph + pad + band;
        panel_layout(l, bottom, (Rect) {4, l->by + board_h * sh + pad, W - 8, ph});
        l->bar = (Rect) {4, H - pad - bar_h, W - 8, bar_h};
        return;
    }
    double pad = 6, col_min = column_least(W, H);
    double sh = (H - band - 2 * pad) / board_h, sw = sh / l->square_ratio;
    if (3 * pad + board_w * sw + band + col_min > W)
        sw = (W - 3 * pad - band - col_min) / board_w, sh = sw * l->square_ratio;
    l->sw = sw, l->sh = sh, l->hand_min = MIN(sw / 2, MIN(W, H) / 21);
    double col_max = MAX(col_min, column_most(l, sw, H));
    double cw = MIN(col_max, W - 3 * pad - board_w * sw - band);
    l->bx = pad + (l->align_left ? 0 : (W - 3 * pad - board_w * sw - band - cw) / 2);
    l->by = pad + band + (H - 2 * pad - band - board_h * sh) / 2;
    double cx = l->bx + board_w * sw + band + pad;
    double bar_h = layout_bar_height(W, H), avail = H - 5 * pad - bar_h;
    /* On large screens high enough for two rows of pieces in hand at half a square or more. */
    double ph = MIN(CLAMP(H * 0.26, 80, MAX(280, sh * 2.3)), avail * 0.36);
    l->bar = (Rect) {cx, H - pad - bar_h, cw, bar_h};
    panel_layout(l, top, (Rect) {cx, pad, cw, ph});
    panel_layout(l, bottom, (Rect) {cx, l->bar.y - pad - ph, cw, ph});
    l->kifu = (Rect) {cx, 2 * pad + ph, cw, l->bar.y - 4 * pad - 2 * ph};
}

double layout_natural_width(const Layout *l, double H)
{
    double band = coords_band(l, H, H), pad = 6, sw = (H - band - 2 * pad) / l->board_h / l->square_ratio;
    double board = 3 * pad + l->board_w * sw + band, most = column_most(l, sw, H), W = board;
    /* The column's least width grows with the window's: the narrowest window it fits in is where they meet. */
    for (int i = 0; i < 30; i++)
        W = board + MAX(column_least(W, H), most);
    return W;
}

double layout_natural_height(const Layout *l, double W, double H)
{
    double band = coords_band(l, W, H), sw = (W - band - 8) / l->board_w, bar_h, strip_h, panel_min;
    portrait_heights(H, &bar_h, &strip_h, &panel_min);
    return 6 * 4 + strip_h + bar_h + l->board_h * sw * l->square_ratio + band + 2 * panel_least(l, sw, W, H, panel_min);
}

void layout_hand_rect(const Layout *l, int color, int index, Rect *r)
{
    const Rect *h = &l->hand[color];
    int n = l->hand_kinds, cols = MAX(1, l->hand_cols[color]);
    int rows = (n + cols - 1) / cols;
    double cw = l->hand_cell[color], ch = cw * 1.1, gap = cw * 0.18;
    int col = index % cols, row = index / cols;
    double y0 = h->y + (h->h - rows * ch - (rows - 1) * gap) / 2;
    *r = (Rect) {h->x + gap + col * (cw + gap), y0 + row * (ch + gap), cw, ch};
}
