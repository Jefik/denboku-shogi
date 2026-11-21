/* The sheet of tiles drawn over the view (see sheet.h). */

#include "sheet.h"

#include "app.h"
#include "layout.h"

#include <math.h>
#include <string.h>

#define MAX_TILES 24

typedef struct { double x, y, w, h; } Box;

/* What a point is on: a tile (its index, >= 0) or one of these. */
enum { HIT_OUTSIDE = -1, HIT_SHEET = -2, HIT_CLOSE = -3, HIT_BACK = -4 };

typedef struct {
    View *view;
    gboolean open, live;
    char title[64];
    SheetTile tiles[MAX_TILES];
    char labels[MAX_TILES][64];
    int n;
    SheetCallbacks cb;
    int focus, hover, pressed;
    gboolean down, dragging;
    double press_x, press_y, offset;
    /* Tiles that do not fit scroll: how far, the most it can, the top of the tiles' area, and
     * whether the drag going on scrolls them (else it closes the sheet). */
    double scroll, max_scroll, tiles_top, scroll0;
    gboolean scrolling;
    /* The layout of the last draw (in the view's coordinates, without the drag's offset). */
    gboolean landscape;
    double u, label_size;
    Box box, tile[MAX_TILES], close, back;
    int cols;
    /* The top page's labels: every page takes its size (sheet_set_reference()). */
    char ref[MAX_TILES][64];
    int ref_n;
    /* The width the labels ask for (`need_here` and `need_ref` in layout()), measured at the unit
     * need_u; 0 when the labels changed. */
    double need_here, need_ref, need_u;
} Sheet;
static Sheet S = {.live = TRUE, .focus = -1, .hover = HIT_OUTSIDE, .pressed = HIT_OUTSIDE};

/* "New Game…" opens a dialog: on a tile the dots only take room. */
static void tile_label(char *to, size_t size, const char *label)
{
    snprintf(to, size, "%s", label);
    char *dots = strstr(to, "…");
    if (dots && !dots[3])
        *dots = 0;
}

static gboolean in_box(Box b, double x, double y) { return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h; }

/* E-ink (not live): no fade over the game, so only the sheet's box is drawn, a small update
 * instead of the whole screen. */
static GdkRectangle box_area(void)
{
    double m = 2 * S.u + 2;
    return (GdkRectangle) {(int) floor(S.box.x - m), (int) floor(S.box.y - m), (int) ceil(S.box.w + 2 * m),
                           (int) ceil(S.box.h + 2 * m)};
}

static void queue(void)
{
    if (!S.view)
        return;
    if (S.live || S.box.w <= 0) {
        gtk_widget_queue_draw(view_widget(S.view));
    } else {
        GdkRectangle a = box_area();
        gtk_widget_queue_draw_area(view_widget(S.view), a.x, a.y, a.width, a.height);
    }
}

static double dx(void) { return S.landscape ? S.offset : 0; }
static double dy(void) { return S.landscape ? 0 : S.offset; }

/* Only the button or tile `h` drawn again, when its state alone changed (pressed, hovered): on the
 * Kindle the whole sheet took 90 ms. */
static void queue_hit(int h)
{
    if (!S.view || S.live || S.box.w <= 0 || h == HIT_OUTSIDE || h == HIT_SHEET) {
        if (h != HIT_OUTSIDE && h != HIT_SHEET)
            queue();
        return;
    }
    Box t = h == HIT_CLOSE ? S.close : h == HIT_BACK ? S.back : h >= 0 && h < S.n ? S.tile[h] : S.box;
    double m = 2 * S.u + 2;
    gtk_widget_queue_draw_area(view_widget(S.view), (int) floor(t.x + dx() - m), (int) floor(t.y + dy() - m),
                               (int) ceil(t.w + 2 * m), (int) ceil(t.h + 2 * m));
}

/* Tiles as large as the mockups' (96 units tall, labels of 13), the unit set by the bar's height.
 * Portrait: 4 columns, or 3 or 2 when the labels need it, as tall as the tiles need. Landscape:
 * 3 columns over the column at the right, the sheet widened leftwards for long labels. */
static void layout(cairo_t *cr, double W, double H)
{
    GdkRectangle col;
    view_column_area(S.view, &col);
    double u = sheet_unit(S.view), gap = 8 * u, margin = 16 * u, head = 52 * u;
    S.u = u, S.landscape = view_landscape(S.view);
    S.label_size = 13 * u;
    if (S.need_u != u) {
        S.need_u = u, S.need_here = S.need_ref = 88 * u;
        for (int i = 0; i < S.n; i++)
            S.need_here = MAX(S.need_here, view_text_width(cr, S.labels[i], S.label_size) + 20 * u);
        for (int i = 0; i < S.ref_n; i++)
            S.need_ref = MAX(S.need_ref, view_text_width(cr, S.ref[i], S.label_size) + 20 * u);
    }
    double need_here = S.need_here, need_ref = S.need_ref, need = MAX(need_here, need_ref);
    double x0, y0, x1, y1;
    int cols;
    if (S.landscape) {
        /* As wide as the drawers, or as the column if wider; by the column, not at the window's
         * edge, when the game is centered on a wide screen. */
        double right = view_game_right(S.view, (int) W, (int) H);
        if (W - right <= 16)
            right = W;
        double handle = 28 * u, w = MAX(drawer_width(), right - col.x + 10 * u);
        cols = 3;
        if (w - handle - margin < cols * need + (cols - 1) * gap)
            w = MIN(right, handle + margin + cols * need + (cols - 1) * gap);
        if (w - handle - margin < cols * need + (cols - 1) * gap)
            cols = 2;
        w = MIN(w, right);
        S.box = (Box) {right - w, 0, w, H};
        x0 = S.box.x + handle, x1 = right - margin, y0 = head + margin * 0.5, y1 = H - margin;
    } else {
        double bw = MIN(W, drawer_width()), bx = (W - bw) / 2; /* as wide as the drawers */
        /* As tall as the top page, each laid out in the columns its own labels allow. */
        int ref_cols = 4;
        cols = 4;
        while (cols > 2 && (bw - 2 * margin - (cols - 1) * gap) / cols < need_here)
            cols--;
        while (ref_cols > 2 && (bw - 2 * margin - (ref_cols - 1) * gap) / ref_cols < need_ref)
            ref_cols--;
        int rows = MAX((S.n + cols - 1) / cols, (S.ref_n + ref_cols - 1) / ref_cols);
        double h = MIN(H * 0.88, 24 * u + head + rows * 96 * u + (rows - 1) * gap + margin);
        S.box = (Box) {bx, H - h, bw, h};
        x0 = bx + margin, x1 = bx + bw - margin, y0 = S.box.y + 20 * u + head, y1 = H - margin;
    }
    int rows = MAX(1, (S.n + cols - 1) / cols);
    double tw = (x1 - x0 - (cols - 1) * gap) / cols, th = (y1 - y0 - (rows - 1) * gap) / rows;
    th = MAX(MIN(th, S.landscape ? MAX(96 * u, tw * 0.85) : 96 * u), MIN(76 * u, tw)); /* no smaller: scroll */
    S.cols = cols;
    S.tiles_top = y0 - gap / 2;
    S.max_scroll = MAX(0, y0 + rows * th + (rows - 1) * gap - y1);
    S.scroll = CLAMP(S.scroll, 0, S.max_scroll);
    for (int i = 0; i < S.n; i++)
        S.tile[i] = (Box) {x0 + i % cols * (tw + gap), y0 + i / cols * (th + gap) - S.scroll, tw, th};
    double r = 20 * u, hy = y0 - head / 2 - 2 * u - r;
    S.close = (Box) {x1 - 2 * r, hy, 2 * r, 2 * r};
    S.back = S.cb.back ? (Box) {x0, hy, 2 * r, 2 * r} : (Box) {0, 0, 0, 0};
}

/* The drag moves the sheet towards the edge it came from. */

static int hit(double x, double y)
{
    x -= dx(), y -= dy();
    if (!in_box(S.box, x, y))
        return HIT_OUTSIDE;
    if (in_box(S.close, x, y))
        return HIT_CLOSE;
    if (S.cb.back && in_box(S.back, x, y))
        return HIT_BACK;
    for (int i = 0; i < S.n && y >= S.tiles_top; i++)
        if (in_box(S.tile[i], x, y))
            return i;
    return HIT_SHEET;
}

static void set_rgb(cairo_t *cr, const double *c, double alpha) { cairo_set_source_rgba(cr, c[0], c[1], c[2], alpha); }

/* Black and white screens: tiles and buttons on the background, framed (no gray, as e-ink shows it dithered). */
static void fill_tile(View *v, cairo_t *cr, const BoardColors *c, int level)
{
    if (c->last_corners)
        set_rgb(cr, c->bg, 1);
    else
        view_panel_color(v, cr, level);
}

/* A round button with an × or a ‹. */
static void round_button(View *v, cairo_t *cr, const BoardColors *c, Box b, gboolean close, gboolean lit)
{
    double cx = b.x + b.w / 2, cy = b.y + b.h / 2, r = b.w / 2;
    cairo_new_sub_path(cr);
    cairo_arc(cr, cx, cy, r, 0, 2 * G_PI);
    fill_tile(v, cr, c, 1);
    if (c->last_corners) {
        cairo_fill_preserve(cr);
        set_rgb(cr, c->fg, 1);
        cairo_set_line_width(cr, MAX(1, r * 0.06));
        cairo_stroke(cr);
    } else {
        cairo_fill(cr);
    }
    if (lit) {
        cairo_arc(cr, cx, cy, r, 0, 2 * G_PI);
        set_rgb(cr, c->fg, 0.12);
        cairo_fill(cr);
    }
    set_rgb(cr, c->fg, 1);
    cairo_set_line_width(cr, MAX(2, r * 0.14));
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    double k = r * 0.36;
    if (close) {
        cairo_move_to(cr, cx - k, cy - k), cairo_line_to(cr, cx + k, cy + k);
        cairo_move_to(cr, cx + k, cy - k), cairo_line_to(cr, cx - k, cy + k);
    } else {
        cairo_move_to(cr, cx + k * 0.4, cy - k), cairo_line_to(cr, cx - k * 0.6, cy), cairo_line_to(cr, cx + k * 0.4, cy + k);
    }
    cairo_stroke(cr);
}

void sheet_round_button(View *v, cairo_t *cr, double x, double y, double d, gboolean close, gboolean lit)
{
    round_button(v, cr, view_colors(v), (Box) {x, y, d, d}, close, lit);
}

/* The last one the bar gave: it is hidden while the board stands alone above a drawer. */
/* From the window, not the bar shown: a drawer that leaves only the board (the lobby, at the start
 * too) or the chat under it would make the heads and the menu tiny. */
double sheet_unit(View *v)
{
    (void) v;
    int W = 0, H = 0;
    if (app.window)
        gtk_window_get_size(GTK_WINDOW(app.window), &W, &H);
    return H > 0 ? CLAMP(layout_bar_height(W, H) / 62.0, 1, 3) : 1;
}

static void focus_ring(cairo_t *cr, const BoardColors *c, Box b, double rad)
{
    view_rounded(cr, b.x + 1.5 * S.u, b.y + 1.5 * S.u, b.w - 3 * S.u, b.h - 3 * S.u, rad);
    set_rgb(cr, c->last_corners ? c->fg : c->accent, 1);
    cairo_set_line_width(cr, MAX(2, 3 * S.u));
    cairo_stroke(cr);
}

static void draw(View *v, cairo_t *cr, double W, double H, void *data)
{
    (void) data;
    const BoardColors *c = view_colors(v);
    layout(cr, W, H);
    double u = S.u;
    /* The game behind, faded (not on black and white, where it would turn gray). */
    gboolean dark = c->bg[0] + c->bg[1] + c->bg[2] < 1.5;
    if (S.live && !c->last_corners) {
        cairo_rectangle(cr, 0, 0, W, H);
        cairo_set_source_rgba(cr, dark ? 0 : 1, dark ? 0 : 1, dark ? 0 : 1, dark ? 0.5 : 0.55);
        cairo_fill(cr);
    }
    cairo_save(cr);
    cairo_translate(cr, dx(), dy());
    /* Rounded only on the side that faces the game: the others go past the window's edges. */
    Box b = S.box;
    double r = 20 * u;
    if (S.landscape)
        view_rounded(cr, b.x, -r, b.x + b.w < W ? b.w : b.w + r, H + 2 * r, r);
    else
        view_rounded(cr, b.w < W ? b.x : -r, b.y, b.w < W ? b.w : W + 2 * r, b.h + r, r);
    set_rgb(cr, c->bg, 1);
    cairo_fill_preserve(cr);
    set_rgb(cr, c->fg, c->last_corners ? 1 : 0.15);
    cairo_set_line_width(cr, MAX(1, u));
    cairo_stroke(cr);
    /* The handle: a drag on it, or anywhere on the sheet, closes it. */
    set_rgb(cr, c->fg, 0.45);
    if (S.landscape)
        view_rounded(cr, b.x + 9 * u, H / 2 - 24 * u, 4 * u, 48 * u, 2 * u);
    else
        view_rounded(cr, W / 2 - 24 * u, b.y + 9 * u, 48 * u, 4 * u, 2 * u);
    cairo_fill(cr);
    round_button(S.view, cr, c, S.close, TRUE, S.hover == HIT_CLOSE || S.pressed == HIT_CLOSE || S.focus == HIT_CLOSE);
    double tx = S.tile[0].x;
    if (S.cb.back) {
        round_button(S.view, cr, c, S.back, FALSE, S.hover == HIT_BACK || S.pressed == HIT_BACK || S.focus == HIT_BACK);
        tx = S.back.x + S.back.w + 10 * u;
    }
    set_rgb(cr, c->fg, 1);
    double title_w = MIN(view_text_width(cr, S.title, 20 * u) * 1.2 + 4, S.close.x - tx - 8 * u);
    cairo_save(cr);
    cairo_translate(cr, tx, 0);
    view_label(cr, S.title, 0, S.close.y + S.close.h / 2 - 14 * u, title_w, 28 * u, TRUE);
    cairo_restore(cr);
    double rad = 12 * u;
    cairo_save(cr);
    cairo_rectangle(cr, b.x, S.tiles_top, b.w, H - S.tiles_top);
    cairo_clip(cr);
    for (int i = 0; i < S.n; i++) {
        Box t = S.tile[i];
        gboolean on = S.tiles[i].enabled;
        view_rounded(cr, t.x, t.y, t.w, t.h, rad);
        fill_tile(S.view, cr, c, 0);
        if (c->last_corners) { /* framed, as the drawers' buttons are on black and white */
            cairo_fill_preserve(cr);
            set_rgb(cr, c->fg, on ? 1 : 0.35);
            cairo_set_line_width(cr, 1.5 * u);
            cairo_stroke(cr);
        } else {
            cairo_fill(cr);
        }
        if (on && (S.hover == i || S.pressed == i)) {
            view_rounded(cr, t.x, t.y, t.w, t.h, rad);
            set_rgb(cr, c->fg, S.pressed == i ? 0.18 : 0.08);
            cairo_fill(cr);
        }
        if (S.focus == i)
            focus_ring(cr, c, t, rad);
        double alpha = on ? 1 : 0.35, s = MIN(32 * u, t.h * 0.38);
        gboolean label = t.h >= 52 * u;
        double icy = label ? t.y + t.h * 0.4 : t.y + t.h / 2;
        set_rgb(cr, c->fg, alpha);
        S.cb.icon(cr, S.tiles[i].id, t.x + t.w / 2 - s / 2, icy - s / 2, s);
        if (label) {
            set_rgb(cr, c->fg, alpha);
            double lh = MIN(S.label_size * 1.4, t.h * 0.22);
            view_label(cr, S.labels[i], t.x + 4 * u, t.y + t.h * 0.78 - lh / 2, t.w - 8 * u, lh, FALSE);
        }
    }
    cairo_restore(cr);
    if (S.max_scroll > 0) { /* where the scroll is */
        double track = H - S.tiles_top - 16 * u, len = track * track / (track + S.max_scroll);
        view_rounded(cr, W - 6 * u, S.tiles_top + (track - len) * S.scroll / S.max_scroll, 3 * u, len, 1.5 * u);
        set_rgb(cr, c->fg, 0.35);
        cairo_fill(cr);
    }
    cairo_restore(cr);
}

static void activate(int what)
{
    if (what == HIT_CLOSE) {
        sheet_close();
    } else if (what == HIT_BACK && S.cb.back) {
        S.cb.back();
    } else if (what >= 0 && what < S.n && S.tiles[what].enabled) {
        S.pressed = HIT_OUTSIDE;
        S.cb.activate(S.tiles[what].id);
    }
}

static void press(View *v, double x, double y, void *data)
{
    (void) v, (void) data;
    int h = hit(x, y);
    if (h == HIT_OUTSIDE) {
        sheet_close();
        return;
    }
    S.down = TRUE, S.dragging = FALSE, S.offset = 0;
    S.press_x = x, S.press_y = y;
    S.pressed = h;
    queue_hit(h);
}

static void motion(View *v, double x, double y, void *data)
{
    (void) data;
    if (!S.down) {
        int h = view_hover(v) ? hit(x, y) : HIT_OUTSIDE;
        if (h != S.hover) {
            queue_hit(S.hover);
            S.hover = h;
            queue_hit(h);
        }
        return;
    }
    double d = S.landscape ? x - S.press_x : y - S.press_y, vy = y - S.press_y;
    if (!S.dragging && hypot(x - S.press_x, vy) > 12 * S.u) {
        S.dragging = TRUE;
        S.pressed = HIT_OUTSIDE;
        /* Up and down scroll the tiles that do not fit, except a pull down from the top in
         * portrait, which closes the sheet as a drag across does in landscape. */
        S.scrolling = S.max_scroll > 0 && fabs(vy) > fabs(x - S.press_x) && (S.landscape || vy < 0 || S.scroll > 0);
        S.scroll0 = S.scroll;
        queue();
    }
    if (S.dragging && S.scrolling) {
        S.scroll = CLAMP(S.scroll0 - vy, 0, S.max_scroll);
        queue();
    } else if (S.dragging && S.live) {
        S.offset = MAX(0, d);
        queue();
    }
}

static void release(View *v, double x, double y, void *data)
{
    (void) v, (void) data;
    if (!S.down)
        return;
    S.down = FALSE;
    if (S.dragging && S.scrolling) {
        S.dragging = S.scrolling = FALSE;
        return;
    }
    if (S.dragging) {
        double d = S.landscape ? x - S.press_x : y - S.press_y, extent = S.landscape ? S.box.w : S.box.h;
        S.dragging = FALSE, S.offset = 0;
        if (d > MIN(extent * 0.25, 120 * S.u))
            sheet_close();
        else
            queue();
        return;
    }
    int was = S.pressed;
    S.pressed = HIT_OUTSIDE;
    queue_hit(was);
    if (was != HIT_SHEET && was == hit(x, y))
        activate(was);
}

/* The sheet's paint: its box but the rounded edge that faces the game. */
static gboolean opaque(View *v, GdkRectangle *r, void *data)
{
    (void) v, (void) data;
    if (!S.open || S.box.w <= 0 || S.box.h <= 0)
        return FALSE;
    double rad = 20 * S.u, x = S.box.x + dx(), y = S.box.y + dy(), w = S.box.w, h = S.box.h;
    if (S.landscape)
        x += rad, w -= rad;
    else
        y += rad, h -= rad;
    *r = (GdkRectangle) {(int) ceil(x) + 1, (int) ceil(y) + 1, (int) floor(w) - 2, (int) floor(h) - 2};
    return r->width > 0 && r->height > 0;
}

static const ViewOverlay OVERLAY = {draw, press, motion, release, opaque};

void sheet_show(View *v, const char *title, const SheetTile *tiles, int n, const SheetCallbacks *cb, int focus)
{
    if (S.open && !S.live) { /* another page: the box it leaves may be larger */
        GdkRectangle old = box_area();
        gtk_widget_queue_draw_area(view_widget(S.view), old.x, old.y, old.width, old.height);
    }
    S.view = v;
    S.n = MIN(n, MAX_TILES);
    snprintf(S.title, sizeof S.title, "%s", title);
    for (int i = 0; i < S.n; i++) {
        S.tiles[i] = tiles[i];
        tile_label(S.labels[i], sizeof S.labels[i], tiles[i].label);
        S.tiles[i].label = S.labels[i];
    }
    S.cb = *cb;
    S.need_u = 0;
    S.focus = focus < S.n ? focus : -1;
    S.hover = S.pressed = HIT_OUTSIDE;
    S.down = S.dragging = S.scrolling = FALSE, S.offset = 0, S.scroll = 0;
    S.open = TRUE;
    S.box = (Box) {0, 0, 0, 0};
    if (!S.live) { /* the box before the first draw, to draw only it */
        GtkAllocation a;
        gtk_widget_get_allocation(view_widget(v), &a);
        cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 1, 1);
        cairo_t *cr = cairo_create(s);
        layout(cr, a.width, a.height);
        cairo_destroy(cr);
        cairo_surface_destroy(s);
    }
    GdkRectangle area = box_area();
    view_set_overlay(v, &OVERLAY, NULL, S.live ? NULL : &area);
}

void sheet_close(void)
{
    if (!S.open)
        return;
    S.open = FALSE;
    GdkRectangle area = box_area();
    view_set_overlay(S.view, NULL, NULL, S.live ? NULL : &area);
}

gboolean sheet_is_open(void) { return S.open; }

void sheet_set_reference(const SheetTile *tiles, int n)
{
    S.ref_n = MIN(n, MAX_TILES);
    for (int i = 0; i < S.ref_n; i++)
        tile_label(S.ref[i], sizeof S.ref[i], tiles[i].label);
    S.need_u = 0;
}

gboolean sheet_size(View *v, int *w, int *h)
{
    Sheet keep = S;
    GtkAllocation a;
    gtk_widget_get_allocation(view_widget(v), &a);
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 1, 1);
    cairo_t *cr = cairo_create(s);
    S.view = v, S.n = 0, S.scroll = 0, S.cb.back = NULL, S.need_u = 0;
    layout(cr, a.width, a.height);
    *w = (int) S.box.w, *h = (int) S.box.h;
    cairo_destroy(cr);
    cairo_surface_destroy(s);
    S = keep;
    return *w > 0 && *h > 0;
}

void sheet_set_live(gboolean live) { S.live = live; }

void sheet_scroll(int dir)
{
    if (!S.open || S.max_scroll <= 0)
        return;
    S.scroll = CLAMP(S.scroll + dir * 60 * S.u, 0, S.max_scroll);
    queue();
}

/* Arrows through the tiles (from the first row up to the × and ‹), Select chooses, Cancel goes
 * back a page or closes. */
gboolean sheet_cursor(ViewCursor a)
{
    if (!S.open)
        return FALSE;
    int f = S.focus, cols = MAX(1, S.cols);
    if (f == -1 && a <= CURSOR_RIGHT) {
        f = 0;
    } else if (f == HIT_CLOSE || f == HIT_BACK) {
        if (a == CURSOR_DOWN)
            f = f == HIT_CLOSE ? MIN(cols, S.n) - 1 : 0;
        else if (a == CURSOR_LEFT && f == HIT_CLOSE && S.cb.back)
            f = HIT_BACK;
        else if (a == CURSOR_RIGHT && f == HIT_BACK)
            f = HIT_CLOSE;
    } else {
        switch (a) {
        case CURSOR_UP: f = f >= cols ? f - cols : f % cols == 0 && S.cb.back ? HIT_BACK : HIT_CLOSE; break;
        case CURSOR_DOWN: f = f + cols < S.n ? f + cols : f; break;
        case CURSOR_LEFT: f = f > 0 ? f - 1 : f; break;
        case CURSOR_RIGHT: f = f + 1 < S.n ? f + 1 : f; break;
        default: break;
        }
    }
    if (a == CURSOR_SELECT) {
        activate(S.focus);
        return TRUE;
    }
    if (a == CURSOR_CANCEL) {
        if (S.cb.back)
            S.cb.back();
        else
            sheet_close();
        return TRUE;
    }
    S.focus = f;
    if (f >= 0 && S.max_scroll > 0) { /* the marked tile in sight */
        GtkAllocation area;
        gtk_widget_get_allocation(view_widget(S.view), &area);
        Box t = S.tile[f];
        if (t.y < S.tiles_top)
            S.scroll -= S.tiles_top - t.y + 8 * S.u;
        else if (t.y + t.h > area.height - 8 * S.u)
            S.scroll += t.y + t.h - area.height + 16 * S.u;
    }
    queue();
    return TRUE;
}
