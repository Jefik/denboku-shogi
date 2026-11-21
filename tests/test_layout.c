/* The game screen's layout (layout.c) over a grid of window sizes, for every board: nothing off
 * the window, nothing over the board or each other, and the pieces in hand at least half a square. */
#include "layout.h"

#include <stdio.h>
#include <string.h>

/* Board width and height and the kinds of piece in hand: shogi, minishogi, Kyoto, Dōbutsu, Goro Goro. */
static const int BOARDS[][3] = {{9, 9, 7}, {5, 5, 5}, {5, 5, 4}, {3, 4, 3}, {5, 6, 3}};
/* The smallest window the layout is asked to serve. */
#define MIN_W 320
#define MIN_H 320

static int failures;
static const char *what;
static double W, H;
static Layout l;

/* Each kind of failure once, with the first window that showed it and how many did. */
static struct { const char *what, *why; char first[96]; int count; } kinds[64];

static void fail(const char *why)
{
    int k = 0;
    failures++;
    while (kinds[k].count && (strcmp(kinds[k].what, what) || strcmp(kinds[k].why, why)))
        k++;
    if (!kinds[k].count++) {
        kinds[k].what = what, kinds[k].why = why;
        snprintf(kinds[k].first, sizeof kinds[k].first, "%.0fx%.0f board %dx%d%s%s%s", W, H, l.board_w, l.board_h,
                 l.coords ? " coords" : "", l.board_only ? " board only" : "", l.align_left ? " left" : "");
    }
}

static int inside(Rect in, Rect out)
{
    const double e = 0.01;
    return in.x >= out.x - e && in.y >= out.y - e && in.x + in.w <= out.x + out.w + e && in.y + in.h <= out.y + out.h + e;
}

static int apart(Rect a, Rect b)
{
    const double e = 0.01;
    return a.x + a.w <= b.x + e || b.x + b.w <= a.x + e || a.y + a.h <= b.y + e || b.y + b.h <= a.y + e;
}

/* Panels, the moves and the bar each drawn with a frame: some space between them, or they read as one. */
static int spaced(Rect a, Rect b)
{
    const double g = 2;
    return a.x + a.w + g <= b.x || b.x + b.w + g <= a.x || a.y + a.h + g <= b.y || b.y + b.h + g <= a.y;
}

static void check(void)
{
    Rect window = {0, 0, W, H};
    /* The board with its band of coordinates, above and at the left. */
    Rect board = {l.bx, l.by, l.board_w * l.sw, l.board_h * l.sh};
    what = "board";
    if (!(l.sw > 8 && l.sh > 8))
        fail("too small");
    if (!inside(board, window))
        fail("off the window");
    if (l.board_only)
        return;
    what = "bar";
    if (!inside(l.bar, window) || l.bar.h < 40)
        fail("off the window or too low");
    if (!apart(l.bar, board))
        fail("over the board");
    what = "moves";
    if (!inside(l.kifu, window) || l.kifu.h < 20 || l.kifu.w < 100)
        fail("off the window or too small");
    if (!apart(l.kifu, board) || !apart(l.kifu, l.bar))
        fail("over the board or the bar");
    for (int c = 0; c < 2; c++) {
        what = c ? "panel 1" : "panel 0";
        if (!spaced(l.side[c], l.kifu) || !spaced(l.side[c], l.bar))
            fail("touching the moves or the bar");
        if (!inside(l.side[c], window))
            fail("off the window");
        if (!apart(l.side[c], board) || !apart(l.side[c], l.bar) || !apart(l.side[c], l.kifu))
            fail("over the board, the bar or the moves");
        if (!inside(l.hand[c], l.side[c]) || !inside(l.label[c], l.side[c]) || !inside(l.clock[c], l.side[c]))
            fail("its hand, name or clock out of it");
        if (!apart(l.hand[c], l.label[c]) || !apart(l.hand[c], l.clock[c]) || !apart(l.label[c], l.clock[c]))
            fail("its hand, name and clock over each other");
        if (l.label[c].w < 40 || l.label[c].h < 12 || l.clock[c].w < 60 || l.clock[c].h < 12)
            fail("name or clock too small");
        what = c ? "hand 1" : "hand 0";
        /* Half a square, or on the small boards, whose squares are huge, about what that is on a
         * 9×9 one in a square window. */
        double shorter = W < H ? W : H, half = l.sw / 2 < shorter / 21 ? l.sw / 2 : shorter / 21;
        if (l.hand_cell[c] < half - 0.01)
            fail("pieces under half a square");
        if (l.hand_cell[c] > l.sw + 0.01)
            fail("pieces larger than the board's");
        for (int i = 0; i < l.hand_kinds; i++) {
            Rect r;
            layout_hand_rect(&l, c, i, &r);
            if (!inside(r, l.hand[c])) {
                fail("a piece out of it");
                break;
            }
        }
    }
    if (!apart(l.side[0], l.side[1]))
        what = "panels", fail("over each other");
}

/* layout_natural_width and _height against layout_compute: the narrowest window that gives the
 * board and the column all they take, and the least height that keeps the board as wide as it can be. */
static void check_natural(void)
{
    Layout n = l;
    what = "natural width";
    double nw = layout_natural_width(&l, H);
    if (nw > H * 1.05 + 1) {
        n.align_left = 1;
        layout_compute(&n, nw, H);
        Layout wide = n, more = n;
        layout_compute(&wide, 100000, H);
        layout_compute(&more, nw + 1, H);
        double spare = nw - (n.kifu.x + n.kifu.w) - 6;
        if (n.sw < wide.sw - 0.01 || spare < -0.01 || spare > 0.01 || more.kifu.w - n.kifu.w > 0.29)
            fail("not what the layout gives");
    }
    what = "natural height";
    n = l;
    double nh = layout_natural_height(&l, W, H);
    layout_compute(&n, W, H);
    double keep = (W - n.band - 8) / l.board_w; /* the squares as wide as the window allows */
    if (!n.landscape && ((H >= nh + 0.01 && n.sw < keep - 0.01) || (H < nh - 0.01 && n.sw >= keep)))
        fail("not what the layout gives");
}

int main(void)
{
    int sizes = 0;
    for (size_t b = 0; b < sizeof BOARDS / sizeof *BOARDS; b++)
        for (int options = 0; options < 16; options++)
            for (W = MIN_W; W <= 3900; W += W < 1400 ? 23 : 97)
                for (H = MIN_H; H <= 2600; H += H < 1400 ? 29 : 89) {
                    memset(&l, 0, sizeof l);
                    l.board_w = BOARDS[b][0], l.board_h = BOARDS[b][1], l.hand_kinds = BOARDS[b][2];
                    l.square_ratio = 1.075, l.usual_w = 9;
                    l.coords = options & 1, l.bottom = options >> 1 & 1, l.align_left = options >> 2 & 1;
                    l.board_only = options >> 3 & 1;
                    if (!l.board_only && !l.align_left)
                        check_natural();
                    layout_compute(&l, W, H);
                    check();
                    sizes++;
                }
    for (int k = 0; kinds[k].count; k++)
        fprintf(stderr, "%s %s: %d layouts, the first %s\n", kinds[k].what, kinds[k].why, kinds[k].count, kinds[k].first);
    if (failures) {
        fprintf(stderr, "layout: %d failures in %d layouts\n", failures, sizes);
        return 1;
    }
    printf("layout tests passed (%d layouts)\n", sizes);
    return 0;
}
