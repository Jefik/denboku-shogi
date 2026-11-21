/* Dialogs as drawers in the game's window, as in the universal mockups: a dialog made with
 * dialog_new() never opens a window of its own; its contents go into a drawer over the game, at the
 * bottom in portrait (the top while the Kindle keyboard is up) and by the column in landscape, or,
 * for pages that keep the game in use or in sight (drawer_set_beside()), beside it: under the board
 * in portrait, at the right of the column in landscape. It has a title, ×, a handle to drag it
 * closed, the contents scrolling and the buttons wide at its foot. A dialog opened from another
 * stacks over it. GtkDialog's show and hide are replaced for these dialogs only, so
 * gtk_dialog_run() and the rest of GTK's dialog API work as before.
 *
 * Inside, short drop-downs become rows of chips and long ones a row that opens their list in a
 * drawer of its own (the drop-down stays, hidden, so the code that reads it is unchanged), and
 * tables of a label and a control per row put each label above its control. */

#include "app.h"
#include "sheet.h"
#include "view.h"

#include <math.h>

typedef struct {
    GtkWidget *dialog, *frame, *pad, *head, *scroll, *viewport, *tabs, *head_pad;
    GtkWidget *focus; /* what was in focus when another page covered this one */
    double press_x, press_y;
    gboolean pressed;
    gboolean from_menu; /* opened from the ☰ menu: ‹ goes back to it */
} Page;

/* `holder` has a window of its own: over the game, the X server then shows the game again only
 * where the drawer leaves it, and nothing of the game is drawn while it covers it. */
static GtkWidget *holder, *center, *pages, *column_box, *stage;
static gboolean narrowed; /* portrait, on a window wider than drawers go: they are centered */
static GList *stack; /* shown pages, the top one last */
static void (*parent_show)(GtkWidget *), (*parent_show_all)(GtkWidget *), (*parent_hide)(GtkWidget *);

static Page *page_of(GtkWidget *dialog) { return g_object_get_data(G_OBJECT(dialog), "drawer-page"); }

/* ---- the stage: the game, and a drawer over it ----
 * Most pages go over the game, which does not move: on e-ink only the drawer's part of the screen
 * changes, and closing it draws that part again from the game's picture. GTK 2 has no overlay
 * container: this one gives `main` all its room and `over` the bottom (or top, or right) of it. */

typedef struct {
    GtkContainer parent;
    GtkWidget *main, *over;
    DrawerEdge edge; /* the edge of the drawer facing the game: EDGE_TOP sits at the bottom */
    gboolean beside; /* landscape: beside the game (which keeps room for it), not over it */
} Stage;
typedef struct {
    GtkContainerClass parent;
} StageClass;

#define STAGE(o) ((Stage *) (o))

static void stage_init(GTypeInstance *s, gpointer k)
{
    (void) k;
    gtk_widget_set_has_window(GTK_WIDGET(s), FALSE);
}

static void stage_size_request(GtkWidget *w, GtkRequisition *r)
{
    Stage *s = STAGE(w);
    *r = (GtkRequisition) {0, 0};
    if (s->main)
        gtk_widget_size_request(s->main, r);
    if (s->over && gtk_widget_get_visible(s->over)) {
        GtkRequisition o;
        gtk_widget_size_request(s->over, &o); /* asked, as GTK wants; placed within the main's room */
    }
}

static guint show_focus_id;
static gboolean show_focus_cb(gpointer data);

static void stage_size_allocate(GtkWidget *w, GtkAllocation *a)
{
    Stage *s = STAGE(w);
    gtk_widget_set_allocation(w, a);
    if (s->main)
        gtk_widget_size_allocate(s->main, a);
    if (s->over && gtk_widget_get_visible(s->over)) {
        GtkRequisition o;
        gtk_widget_get_child_requisition(s->over, &o);
        GtkAllocation b = *a;
        if (s->edge == EDGE_LEFT) {
            /* Next to the game, not at the window's edge: on a wide screen the board and the column
             * are centered, and the drawer stays by them (beside, the whole is centered). */
            view_set_inset(app.view, s->beside ? MIN(o.width, a->width / 2) : 0);
            int right = view_game_right(app.view, a->width, a->height);
            if (s->beside) {
                /* At least as wide as it asks, over the game's edge if the game cannot shrink so far
                 * (the chat beside it too): its rows are laid out for that width. */
                b.width = MAX(a->width - right, MIN(o.width, a->width));
                b.x = a->x + a->width - b.width;
            } else {
                if (a->width - right <= 16)
                    right = a->width;
                b.width = MIN(o.width, a->width);
                b.x = a->x + MAX(0, right - b.width);
            }
        } else {
            b.height = MIN(o.height, a->height);
            if (narrowed) {
                b.width = MIN(o.width, a->width);
                b.x = a->x + (a->width - b.width) / 2;
            }
            if (s->edge == EDGE_TOP)
                b.y = a->y + a->height - b.height;
        }
        /* Above the Kindle keyboard: the drawer ends where it starts, and shows the field typed in. */
        int ky = keyboard_covering(), wy = 0;
        if (ky && gtk_widget_get_window(w))
            gdk_window_get_origin(gtk_widget_get_window(w), NULL, &wy);
        ky -= wy;
        if (ky > b.y && ky < b.y + b.height) {
            if (s->edge == EDGE_TOP) {
                b.height = MIN(b.height, ky - a->y);
                b.y = ky - b.height;
            } else
                b.height = ky - b.y;
            if (!show_focus_id)
                show_focus_id = g_idle_add(show_focus_cb, NULL);
        }
        gtk_widget_size_allocate(s->over, &b);
        GdkRectangle c = {b.x - a->x, b.y - a->y, b.width, b.height};
        view_set_covered(app.view, &c);
    }
}

static void stage_add(GtkContainer *c, GtkWidget *child)
{
    Stage *s = STAGE(c);
    if (!s->main)
        s->main = child;
    else
        s->over = child;
    gtk_widget_set_parent(child, GTK_WIDGET(c));
}

static void stage_remove(GtkContainer *c, GtkWidget *child)
{
    Stage *s = STAGE(c);
    gboolean visible = gtk_widget_get_visible(child);
    gtk_widget_unparent(child);
    if (s->main == child)
        s->main = NULL;
    if (s->over == child)
        s->over = NULL;
    if (visible)
        gtk_widget_queue_resize(GTK_WIDGET(c));
}

static void stage_forall(GtkContainer *c, gboolean internals, GtkCallback cb, gpointer data)
{
    (void) internals;
    Stage *s = STAGE(c);
    if (s->main)
        cb(s->main, data);
    if (s->over)
        cb(s->over, data);
}

static void stage_class_init(gpointer k, gpointer data)
{
    (void) data;
    GTK_WIDGET_CLASS(k)->size_request = stage_size_request;
    GTK_WIDGET_CLASS(k)->size_allocate = stage_size_allocate;
    GTK_CONTAINER_CLASS(k)->add = stage_add;
    GTK_CONTAINER_CLASS(k)->remove = stage_remove;
    GTK_CONTAINER_CLASS(k)->forall = stage_forall;
}

GtkWidget *drawer_stage(GtkWidget *main)
{
    GType type = g_type_register_static_simple(GTK_TYPE_CONTAINER, "DenbokuStage", sizeof(StageClass),
                                               stage_class_init, sizeof(Stage), stage_init, 0);
    stage = g_object_new(type, NULL);
    gtk_widget_set_redraw_on_allocate(stage, FALSE);
    gtk_container_add(GTK_CONTAINER(stage), main);
    return stage;
}

/* Pages that keep the game beside them, in use or in sight while they change it: the game over
 * notice, the lobby, Analysis, the Sandbox's hands, Preferences. A drawer takes its first page's way. */
void drawer_set_beside(GtkWidget *dialog) { g_object_set_data(G_OBJECT(dialog), "drawer-beside", GINT_TO_POINTER(TRUE)); }

static gboolean beside(Page *p) { return g_object_get_data(G_OBJECT(p->dialog), "drawer-beside") != NULL; }

/* A passive page (drawer_set_passive) holds nothing back: the board, the moves and the menu
 * stay in use beside it. */
static gboolean passive(Page *p) { return g_object_get_data(G_OBJECT(p->dialog), "drawer-passive") != NULL; }

static gboolean landscape_window(void)
{
    if (app.kindle)
        return setting_int("Landscape", 0) != 0;
    int w, h;
    gtk_window_get_size(GTK_WINDOW(app.window), &w, &h);
    return w > h;
}

/* ---- the drawer ---- */

/* The width of every drawer, and the ☰ menu's least: in landscape 460 px or 45% of the window,
 * whichever is less; in portrait the window's, up to 720 px, centered past that. The contents
 * are laid out for it (rows_width()), so pages do not change width from one to the next. */
int drawer_width(void)
{
    int W, H;
    gtk_window_get_size(GTK_WINDOW(app.window), &W, &H);
    return landscape_window() ? MIN(ui_limit(460), W * 45 / 100) : MIN(W, ui_limit(720));
}

/* The window's background, a rounded line on the side facing the board (DrawerEdge), and the
 * handle on it. */
static gboolean frame_expose_cb(GtkWidget *w, GdkEventExpose *e, gpointer data)
{
    (void) data;
    GtkAllocation a;
    gtk_widget_get_allocation(w, &a);
    GtkStyle *s = gtk_widget_get_style(w);
    cairo_t *cr = gdk_cairo_create(gtk_widget_get_window(w));
    gdk_cairo_region(cr, e->region);
    cairo_clip(cr);
    gdk_cairo_set_source_color(cr, &s->bg[GTK_STATE_NORMAL]);
    cairo_paint(cr);
    int edge = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "drawer-edge"));
    if (edge == EDGE_NONE) {
        cairo_destroy(cr);
        return FALSE;
    }
    double r = 16, lw = app.kindle || !color_screen() ? 1.5 : 1;
    /* The frame has a window of its own: its corner is (0, 0). */
    if (edge == EDGE_LEFT)
        view_rounded(cr, lw / 2, -r, a.width + r, a.height + 2 * r, r);
    else if (edge == EDGE_TOP && g_object_get_data(G_OBJECT(w), "drawer-narrow"))
        view_rounded(cr, lw / 2, lw / 2, a.width - lw, a.height + r, r); /* centered: its sides show */
    else if (edge == EDGE_TOP)
        view_rounded(cr, -r, lw / 2, a.width + 2 * r, a.height + r, r);
    else
        view_rounded(cr, -r, -r, a.width + 2 * r, a.height + r - lw / 2, r);
    GdkColor f = s->fg[GTK_STATE_NORMAL];
    cairo_set_source_rgba(cr, f.red / 65535.0, f.green / 65535.0, f.blue / 65535.0, lw > 1 ? 1 : 0.2);
    cairo_set_line_width(cr, lw);
    cairo_stroke(cr);
    cairo_set_source_rgba(cr, f.red / 65535.0, f.green / 65535.0, f.blue / 65535.0, 0.45);
    if (edge == EDGE_LEFT)
        view_rounded(cr, 5, a.height / 2.0 - 24, 4, 48, 2);
    else if (edge == EDGE_TOP)
        view_rounded(cr, a.width / 2.0 - 24, 5, 48, 4, 2);
    else
        view_rounded(cr, a.width / 2.0 - 24, a.height - 9, 48, 4, 2);
    cairo_fill(cr);
    cairo_destroy(cr);
    return FALSE;
}

/* Kindle: a page that moves or changes size (another mode chosen in New Game) has its old pixels
 * copied along by the X server, and what no widget paints over them stayed (the windows have no
 * background there, kindle.c): drawn whole again. */
static void frame_allocate_cb(GtkWidget *f, GtkAllocation *a, gpointer data)
{
    (void) data;
    GtkAllocation *last = g_object_get_data(G_OBJECT(f), "last-allocation");
    if (!last) {
        last = g_new0(GtkAllocation, 1);
        g_object_set_data_full(G_OBJECT(f), "last-allocation", last, g_free);
    }
    if (memcmp(last, a, sizeof *a)) {
        *last = *a;
        gtk_widget_queue_draw(f);
    }
}

GtkWidget *drawer_frame_new(void)
{
    GtkWidget *f = gtk_event_box_new();
    if (app.kindle)
        g_signal_connect_after(f, "size-allocate", G_CALLBACK(frame_allocate_cb), NULL);
    g_object_set_data(G_OBJECT(f), "finger-tall", f); /* style.c: rows and buttons a finger tall */
    gtk_widget_set_app_paintable(f, TRUE);
    g_signal_connect(f, "expose-event", G_CALLBACK(frame_expose_cb), NULL);
    return f;
}

void drawer_frame_edge(GtkWidget *frame, DrawerEdge edge)
{
    if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(frame), "drawer-edge")) != (int) edge) {
        g_object_set_data(G_OBJECT(frame), "drawer-edge", GINT_TO_POINTER(edge));
        gtk_widget_queue_draw(frame);
    }
}

/* The top page's size: in landscape as wide as its contents ask (within limits), in portrait as
 * tall, up to most of the window; past that it scrolls. A page over the game is as wide as the ☰
 * menu, and one without buttons at least as tall, so that the menu and what it opens look alike. */
static void layout_page(Page *p)
{
    drawer_head_fit(p->head_pad, TRUE);
    /* At the screen's bottom, the buttons as far from its edge as the chat's field. */
    guint t, b, l, r_, bottom = landscape_window() ? 0 : (guint) MAX(0, (int) (16 * ui_scale()) - 14);
    gtk_alignment_get_padding(GTK_ALIGNMENT(p->pad), &t, &b, &l, &r_);
    if (b != bottom)
        gtk_alignment_set_padding(GTK_ALIGNMENT(p->pad), 0, bottom, 0, 0);
    int W, H;
    gtk_window_get_size(GTK_WINDOW(app.window), &W, &H);
    GtkRequisition r, head, foot;
    gtk_widget_size_request(GTK_DIALOG(p->dialog)->vbox, &r);
    gtk_widget_size_request(p->head, &head);
    gtk_widget_size_request(GTK_DIALOG(p->dialog)->action_area, &foot);
    int w = -1, h = -1, fw = -1, need = r.width;
    if (p->tabs) {
        /* The tabs scroll each on their own (scroll_tabs()): the drawer as wide as the shown one asks. */
        GtkNotebook *nb = GTK_NOTEBOOK(p->tabs);
        GtkWidget *page = gtk_notebook_get_nth_page(nb, gtk_notebook_get_current_page(nb));
        GtkWidget *inner = page && GTK_IS_SCROLLED_WINDOW(page) ? gtk_bin_get_child(GTK_BIN(gtk_bin_get_child(GTK_BIN(page)))) : NULL;
        if (inner) {
            GtkRequisition pr;
            gtk_widget_size_request(inner, &pr);
            need = MAX(need, pr.width + 40);
        }
    }
    gboolean over = stack && !beside(stack->data);
    int mw = 0, mh = 0;
    if (!over || !menu_size(&mw, &mh))
        mw = mh = 0;
    fw = MAX(drawer_width(), mw);
    if (!landscape_window() && !narrowed)
        fw = -1; /* the window's width, which it may leave */
    if (landscape_window()) {
        /* Every page as wide (what rows_width() counts on); wider only for what does not fit. */
        if (need + 48 > fw)
            fw = MIN(need + 48, W * 62 / 100);
    } else { /* over the game, more of the screen: there is no board to keep in sight above it */
        h = MAX(80, MIN(r.height + 8, H * (over ? 85 : 62) / 100 - head.height - foot.height - 40));
        /* Pages to read or go through (no buttons at the foot) as tall as the menu; a question or
         * a form as tall as it is. */
        GList *buttons = gtk_container_get_children(GTK_CONTAINER(GTK_DIALOG(p->dialog)->action_area));
        if (!buttons)
            h = MAX(h, mh - 28 - head.height - 8);
        g_list_free(buttons);
    }
    if (g_getenv("DENBOKU_DEBUG"))
        g_printerr("drawer: %s %dx%d, contents %dx%d\n", gtk_window_get_title(GTK_WINDOW(p->dialog)), fw, h, r.width,
                   r.height);
    /* A page shown whole has no bar down its side: the bar would narrow it, the bar across would
     * come for that and take height, and both would stay. */
    GtkPolicyType vbar = h >= r.height + 8 ? GTK_POLICY_NEVER : GTK_POLICY_AUTOMATIC, hnow, vnow;
    gtk_scrolled_window_get_policy(GTK_SCROLLED_WINDOW(p->scroll), &hnow, &vnow);
    if (vnow != vbar)
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(p->scroll), hnow, vbar);
    int ow, oh;
    gtk_widget_get_size_request(p->scroll, &ow, &oh);
    if (ow != w || oh != h)
        gtk_widget_set_size_request(p->scroll, w, h);
    gtk_widget_get_size_request(p->frame, &ow, &oh);
    if (ow != fw)
        gtk_widget_set_size_request(p->frame, fw, -1);
}

static void contents_request_cb(GtkWidget *w, GtkRequisition *r, gpointer data)
{
    (void) w, (void) r;
    Page *p = data;
    if (stack && g_list_last(stack)->data == p)
        layout_page(p);
}

/* Each change makes GTK size the window again, slow on the Kindle: only when something changed. */
static void move_to(GtkWidget *box, int position)
{
    if (gtk_widget_get_parent(holder) != box) {
        g_object_ref(holder);
        gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(holder)), holder);
        if (box == stage)
            gtk_container_add(GTK_CONTAINER(stage), holder);
        else
            gtk_box_pack_start(GTK_BOX(box), holder, FALSE, FALSE, 0);
        g_object_unref(holder);
    }
    if (box == stage)
        return;
    int at;
    gtk_container_child_get(GTK_CONTAINER(box), holder, "position", &at, NULL);
    if (position < 0) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(box));
        position = g_list_length(children) - 1;
        g_list_free(children);
    }
    if (at != position)
        gtk_box_reorder_child(GTK_BOX(box), holder, position);
}

/* The top page scrolled to the field that has the focus, when the keyboard took the room under it. */
static gboolean show_focus_cb(gpointer data)
{
    (void) data;
    show_focus_id = 0;
    if (!stack)
        return FALSE;
    Page *p = g_list_last(stack)->data;
    GtkWidget *f = gtk_window_get_focus(GTK_WINDOW(app.window)), *inner = gtk_bin_get_child(GTK_BIN(p->viewport));
    int fx, fy;
    if (!f || !inner || !gtk_widget_is_ancestor(f, inner) || !gtk_widget_translate_coordinates(f, inner, 0, 0, &fx, &fy))
        return FALSE;
    GtkAllocation fa;
    gtk_widget_get_allocation(f, &fa);
    GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(p->scroll));
    double v = gtk_adjustment_get_value(adj), page = gtk_adjustment_get_page_size(adj);
    if (fy < v)
        v = fy - 8;
    else if (fy + fa.height > v + page)
        v = fy + fa.height + 8 - page;
    gtk_adjustment_set_value(adj, CLAMP(v, gtk_adjustment_get_lower(adj), gtk_adjustment_get_upper(adj) - page));
    return FALSE;
}

void drawer_layout(void)
{
    if (!holder)
        return;
    gboolean over = stack && !beside(stack->data);
    gboolean land = landscape_window(), up = !land && !over && keyboard_covering();
    DrawerEdge edge = land ? EDGE_LEFT : up ? EDGE_BOTTOM : EDGE_TOP;
    int W, H;
    gtk_window_get_size(GTK_WINDOW(app.window), &W, &H);
    narrowed = !land && drawer_width() < W;
    gtk_alignment_set(GTK_ALIGNMENT(center), 0.5, 0, narrowed ? 0 : 1, 1); /* as wide as the page asks, in the middle */
    if (stack && (over || land) && stage) { /* in landscape beside the game too, kept next to it */
        STAGE(stage)->edge = edge;
        STAGE(stage)->beside = !over;
        move_to(stage, -1);
    } else {
        move_to(column_box, up ? 1 : -1);
    }
    for (GList *l = stack; l; l = l->next) {
        Page *p = l->data;
        drawer_frame_edge(p->frame, edge);
        g_object_set_data(G_OBJECT(p->frame), "drawer-narrow", GINT_TO_POINTER(narrowed));
        gtk_widget_set_visible(p->frame, !l->next);
    }
    gtk_widget_set_visible(holder, stack != NULL);
    if (stack && (over || land) && stage) { /* above the game's window, which was made first */
        gtk_widget_queue_resize(stage);
        if (gtk_widget_get_realized(holder))
            gdk_window_raise(gtk_widget_get_window(holder));
    }
    view_set_dimmed(app.view, over && !app.kindle);
    if (!stack || (!over && !land)) { /* its part of the game drawn again (the X server asks for it) */
        view_set_covered(app.view, NULL);
        view_set_inset(app.view, 0);
    }
    /* Above a drawer in portrait, the board alone, as large as the room left. */
    view_set_board_only(app.view, stack && !over && !landscape_window() && !passive(g_list_last(stack)->data));
    if (stack)
        layout_page(g_list_last(stack)->data);
}

/* The default button of the dialog (Enter in its fields) becomes the window's; else the one that
 * confirms (foot_added_hook()): GTK forgets the dialog's default when its foot moves into the drawer,
 * and Enter in the room's password did nothing. */
static void take_default(Page *p)
{
    GtkWidget *def = NULL, *primary = NULL;
    GList *kids = gtk_container_get_children(GTK_CONTAINER(GTK_DIALOG(p->dialog)->action_area));
    for (GList *l = kids; l; l = l->next) {
        if (gtk_widget_has_default(l->data) || GTK_WINDOW(p->dialog)->default_widget == l->data)
            def = l->data;
        if (!strcmp(gtk_widget_get_name(l->data), "denboku-primary") && gtk_widget_get_visible(l->data))
            primary = l->data;
    }
    g_list_free(kids);
    if (!def && (def = primary))
        gtk_widget_set_can_default(def, TRUE);
    gtk_window_set_default(GTK_WINDOW(app.window), def);
}

static void grab(Page *p)
{
    if (!passive(p))
        gtk_grab_add(p->frame);
}

void drawer_set_passive(GtkWidget *dialog)
{
    drawer_set_beside(dialog);
    g_object_set_data(G_OBJECT(dialog), "drawer-passive", GINT_TO_POINTER(TRUE));
    g_object_set_data(G_OBJECT(dialog), "browse-moves", GINT_TO_POINTER(TRUE));
    gtk_window_set_modal(GTK_WINDOW(dialog), FALSE);
}

/* E-ink: a page that comes up flashes its area (black, then drawn), as the board does after a move,
 * so that what was there before leaves no ghost. The black is painted on the game's own window, in
 * bands (in_bands()): black windows made the Kindle's window manager hold the screen for seconds,
 * as for a new window, and drawn over an update still on its way the e-ink joins the two into one
 * large update, which waits as long (kindle.c draws the page once the black has shown). */
static gboolean flash_wanted;

gboolean drawer_flash_wanted(void) { return flash_wanted; }

static void black_band(GdkRegion *band, void *data)
{
    GdkRectangle r;
    gdk_region_get_clipbox(band, &r);
    gdk_draw_rectangle(gtk_widget_get_window(app.window), data, TRUE, r.x, r.y, r.width, r.height);
}

GdkRegion *drawer_flash_now(void)
{
    if (!flash_wanted)
        return NULL;
    flash_wanted = FALSE;
    GdkWindow *top = gtk_widget_get_window(app.window);
    if (!stack || !top || !gtk_widget_get_mapped(holder))
        return NULL;
    GtkAllocation a;
    gtk_widget_get_allocation(holder, &a);
    int x, y;
    if (!gtk_widget_translate_coordinates(holder, app.window, 0, 0, &x, &y))
        return NULL;
    GdkRegion *area = gdk_region_rectangle(&(GdkRectangle) {x, y, a.width, a.height});
    GdkGC *gc = gdk_gc_new(top);
    gdk_gc_set_subwindow(gc, GDK_INCLUDE_INFERIORS); /* over the page's own windows too */
    GdkColor black = {0, 0, 0, 0};
    gdk_gc_set_rgb_fg_color(gc, &black);
    in_bands(area, top, black_band, gc);
    gdk_flush();
    if (g_getenv("DENBOKU_DEBUG"))
        g_printerr("%.3f flash %dx%d+%d+%d\n", g_get_monotonic_time() / 1e6, a.width, a.height, x, y);
    g_object_unref(gc);
    return area;
}

static void flash(void)
{
    if (app.kindle && setting_int("FlashDrawers", 0))
        flash_wanted = TRUE; /* done by kindle.c once the page is laid out, before it is drawn */
}

static gboolean opening_from_menu;

void drawer_from_menu(gboolean on) { opening_from_menu = on; }

static void push(Page *p)
{
    if (g_list_find(stack, p))
        return;
    p->from_menu = !stack && opening_from_menu;
    /* ‹ only where it goes back to something: the page under it or the menu, not just closing as ×. */
    gtk_widget_set_visible(g_object_get_data(G_OBJECT(p->head_pad), "back"),
                           stack || p->from_menu || g_object_get_data(G_OBJECT(p->dialog), "drawer-back"));
    if (stack) {
        Page *under = g_list_last(stack)->data;
        gtk_grab_remove(under->frame);
        under->focus = gtk_window_get_focus(GTK_WINDOW(app.window));
        if (under->focus)
            g_object_add_weak_pointer(G_OBJECT(under->focus), (gpointer *) &under->focus);
    }
    stack = g_list_append(stack, p);
    sheet_close();
    drawer_layout();
    grab(p);
    take_default(p);
    flash();
    kindle_bands_soon();
}

static void pop(Page *p)
{
    if (!g_list_find(stack, p))
        return;
    gboolean top = g_list_last(stack)->data == p;
    if (top)
        gtk_grab_remove(p->frame);
    stack = g_list_remove(stack, p);
    gtk_widget_hide(p->frame);
    if (top) {
        gtk_window_set_default(GTK_WINDOW(app.window), NULL);
        gtk_window_set_focus(GTK_WINDOW(app.window), NULL);
        if (stack) {
            grab(g_list_last(stack)->data);
            take_default(g_list_last(stack)->data);
            flash();
        }
    }
    drawer_layout();
    kindle_bands_soon();
    /* Back on the page under it: the cursor where it was. */
    Page *under = top && stack ? g_list_last(stack)->data : NULL;
    if (under && under->focus) {
        GtkWidget *focus = under->focus;
        g_object_remove_weak_pointer(G_OBJECT(focus), (gpointer *) &under->focus);
        under->focus = NULL;
        if (gtk_widget_is_ancestor(focus, under->frame))
            gtk_widget_grab_focus(focus);
    }
}

/* ---- GtkDialog's show and hide, for the dialogs in drawers ---- */

static void set_visible_flag(GtkWidget *w, gboolean on)
{
    if (on)
        GTK_WIDGET_SET_FLAGS(w, GTK_VISIBLE);
    else
        GTK_WIDGET_UNSET_FLAGS(w, GTK_VISIBLE);
    g_object_notify(G_OBJECT(w), "visible");
}

static void adapt(Page *p);

static void dialog_show(GtkWidget *w)
{
    Page *p = page_of(w);
    if (!p) {
        parent_show(w);
        return;
    }
    gtk_widget_show(p->frame);
    /* Its window exists (unmapped): GTK resizes "visible" windows (gtk_window_resize() when rows
     * show or hide), and crashed on one that had none. */
    gtk_widget_realize(w);
    if (!gtk_widget_get_visible(w))
        set_visible_flag(w, TRUE);
    adapt(p);
    push(p);
}

static void dialog_show_all(GtkWidget *w)
{
    Page *p = page_of(w);
    if (!p) {
        parent_show_all(w);
        return;
    }
    gtk_widget_show_all(gtk_bin_get_child(GTK_BIN(p->frame))); /* the frame itself is no_show_all */
    dialog_show(w);
}

static void dialog_hide(GtkWidget *w)
{
    Page *p = page_of(w);
    if (!p) {
        parent_hide(w);
        return;
    }
    if (gtk_widget_get_visible(w))
        set_visible_flag(w, FALSE);
    pop(p);
}

static void dialog_destroy_cb(GtkWidget *w, gpointer data)
{
    (void) w;
    Page *p = data;
    pop(p);
    gtk_widget_destroy(p->frame);
    g_free(p);
}

/* Closes the whole drawer: every page in it, as Cancel, but for a list that `confirm`s (a tap on the
 * board keeps the piece set just picked); a passive page under another (the game over notice under
 * Preferences) stays, to be there when the others close. */
static void close_all(gboolean confirm)
{
    GList *pages = g_list_copy(stack);
    gboolean keep = stack && !passive(g_list_last(stack)->data);
    for (GList *l = g_list_last(pages); l; l = l->prev) {
        GtkWidget *d = ((Page *) l->data)->dialog;
        if (!(keep && passive(l->data)))
            gtk_dialog_response(GTK_DIALOG(d), confirm && g_object_get_data(G_OBJECT(d), "drawer-outside-ok") ? GTK_RESPONSE_OK
                                                                                                         : GTK_RESPONSE_DELETE_EVENT);
    }
    g_list_free(pages);
}

void drawer_close_all(void) { close_all(FALSE); }

/* × cancels. */
static void close_cb(GtkWidget *w, gpointer d)
{
    (void) w, (void) d;
    close_all(FALSE);
}

static gboolean menu_back_cb(gpointer data)
{
    (void) data;
    if (!stack)
        menu_back();
    return FALSE;
}

/* ‹ (and Escape) goes back to the page under this one, or to the ☰ menu it came from; on the
 * first page of a drawer opened otherwise it closes it, as ×. */
static void go_back(Page *p)
{
    if (g_object_get_data(G_OBJECT(p->dialog), "drawer-back")) {
        gtk_dialog_response(GTK_DIALOG(p->dialog), GTK_RESPONSE_CLOSE);
        return;
    }
    gboolean menu = p->from_menu && stack && stack->data == p;
    gtk_dialog_response(GTK_DIALOG(p->dialog), GTK_RESPONSE_DELETE_EVENT);
    if (menu)
        g_idle_add(menu_back_cb, NULL); /* once the page is gone */
}

static void back_cb(GtkWidget *w, gpointer d)
{
    (void) w;
    go_back(page_of(d));
}

void drawer_set_back(GtkWidget *dialog) { g_object_set_data(G_OBJECT(dialog), "drawer-back", GINT_TO_POINTER(TRUE)); }

void drawer_set_outside_ok(GtkWidget *dialog) { g_object_set_data(G_OBJECT(dialog), "drawer-outside-ok", GINT_TO_POINTER(TRUE)); }

void drawer_set_sticky(GtkWidget *dialog) { g_object_set_data(G_OBJECT(dialog), "drawer-sticky", GINT_TO_POINTER(TRUE)); }

/* A drag on the title towards the edge the drawer came from closes it. */
static gboolean head_press_cb(GtkWidget *w, GdkEventButton *e, gpointer data)
{
    (void) w;
    Page *p = data;
    p->pressed = e->button == 1, p->press_x = e->x_root, p->press_y = e->y_root;
    return FALSE;
}

static gboolean head_release_cb(GtkWidget *w, GdkEventButton *e, gpointer data)
{
    (void) w;
    Page *p = data;
    if (!p->pressed)
        return FALSE;
    p->pressed = FALSE;
    double d = landscape_window() ? e->x_root - p->press_x : keyboard_covering() ? p->press_y - e->y_root : e->y_root - p->press_y;
    if (d > 80)
        gtk_dialog_response(GTK_DIALOG(p->dialog), GTK_RESPONSE_DELETE_EVENT);
    return FALSE;
}

static gboolean bar_press_cb(gpointer data)
{
    if (!stack)
        view_bar_activate(app.view, GPOINTER_TO_INT(data));
    return FALSE;
}

/* A tap outside the drawer (routed here by its grab) closes it, as × but confirming a choice list
 * (drawer_set_outside_ok()), except for pages that wait on something (drawer_set_sticky()); on the action bar it also does what the button does, so ☰
 * goes from Preferences to the menu in one tap. */
static gboolean frame_press_cb(GtkWidget *w, GdkEventButton *e, gpointer data)
{
    (void) data;
    GdkWindow *win = e->window;
    while (win && win != gtk_widget_get_window(w))
        win = gdk_window_get_parent(win);
    if (win || e->type != GDK_BUTTON_PRESS || e->button != 1)
        return win == NULL;
    int item = e->window == gtk_widget_get_window(view_widget(app.view)) ? view_bar_item_at(app.view, e->x, e->y) : -1;
    if (item == VIEW_BAR_CHAT) { /* the chat beside any page (chat_event()) */
        chat_toggle();
        return TRUE;
    }
    for (GList *l = stack; l; l = l->next)
        if (g_object_get_data(G_OBJECT(((Page *) l->data)->dialog), "drawer-sticky"))
            return TRUE;
    close_all(TRUE);
    if (item >= 0)
        g_idle_add(bar_press_cb, GINT_TO_POINTER(item));
    return TRUE;
}

/* The buttons side by side in the order they were added, all as wide; the one that confirms
 * filled (style.c draws "denboku-primary"). Set as each one goes into a drawer's foot: GTK 2 tells
 * a box nothing when a child is packed into it, so this hooks every widget's "parent-set". */
static gboolean foot_added_hook(GSignalInvocationHint *hint, guint n, const GValue *values, gpointer data)
{
    (void) hint, (void) n, (void) data;
    GtkWidget *w = g_value_get_object(values), *box = gtk_widget_get_parent(w);
    GtkWidget *d = box ? g_object_get_data(G_OBJECT(box), "drawer-foot") : NULL;
    if (!d)
        return TRUE;
    gtk_box_set_child_packing(GTK_BOX(box), w, TRUE, TRUE, 0, GTK_PACK_START);
    GList *kids = gtk_container_get_children(GTK_CONTAINER(box));
    GtkWidget *primary = NULL;
    for (GList *l = kids; l; l = l->next) {
        int id = gtk_dialog_get_response_for_widget(GTK_DIALOG(d), l->data);
        if (id == GTK_RESPONSE_OK || id == GTK_RESPONSE_ACCEPT || id == GTK_RESPONSE_YES)
            primary = l->data;
    }
    for (GList *l = kids; l; l = l->next) {
        const char *want = l->data == primary ? "denboku-primary" : "";
        if (strcmp(gtk_widget_get_name(l->data), want) && (*want || !strcmp(gtk_widget_get_name(l->data), "denboku-primary")))
            gtk_widget_set_name(l->data, *want ? want : NULL);
    }
    g_list_free(kids);
    return TRUE;
}

/* ‹ and × as the ☰ menu draws them: round, as large as its. */
static void round_request_cb(GtkWidget *w, GtkRequisition *r, gpointer data)
{
    (void) w, (void) data;
    r->width = r->height = (int) (40 * sheet_unit(app.view));
}

static gboolean round_expose_cb(GtkWidget *w, GdkEventExpose *e, gpointer data)
{
    (void) e;
    GtkAllocation a;
    gtk_widget_get_allocation(w, &a);
    cairo_t *cr = gdk_cairo_create(gtk_widget_get_window(w));
    double d = MIN(a.width, a.height);
    GtkStateType st = gtk_widget_get_state(w);
    sheet_round_button(app.view, cr, a.x + (a.width - d) / 2, a.y + (a.height - d) / 2, d, GPOINTER_TO_INT(data),
                       st == GTK_STATE_PRELIGHT || st == GTK_STATE_ACTIVE);
    cairo_destroy(cr);
    return TRUE;
}

static GtkWidget *round_button_new(gboolean close, GCallback cb, gpointer data)
{
    GtkWidget *b = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
    gtk_widget_set_can_focus(b, FALSE);
    g_signal_connect_after(b, "size-request", G_CALLBACK(round_request_cb), NULL);
    g_signal_connect(b, "expose-event", G_CALLBACK(round_expose_cb), GINT_TO_POINTER(close));
    g_signal_connect(b, "clicked", cb, data);
    return b;
}

GtkWidget *drawer_head_new(const char *title, GCallback back, GCallback close, gpointer data)
{
    GtkWidget *row = gtk_hbox_new(FALSE, 8), *label = gtk_label_new(title), *pad = gtk_alignment_new(0, 0, 1, 1);
    if (back) {
        GtkWidget *b = round_button_new(FALSE, back, data);
        gtk_widget_set_no_show_all(b, TRUE); /* shown where it goes back somewhere */
        gtk_widget_show(b);
        gtk_box_pack_start(GTK_BOX(row), b, FALSE, FALSE, 0);
        g_object_set_data(G_OBJECT(pad), "back", b);
    }
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    gtk_box_pack_start(GTK_BOX(row), label, TRUE, TRUE, 4);
    gtk_box_pack_end(GTK_BOX(row), round_button_new(TRUE, close, data), FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(pad), row);
    g_object_set_data(G_OBJECT(pad), "title", label);
    return pad;
}

void drawer_head_fit(GtkWidget *head, gboolean margins)
{
    double u = sheet_unit(app.view);
    int px = (int) (22 * u);
    GtkWidget *title = g_object_get_data(G_OBJECT(head), "title");
    if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(head), "px")) == px)
        return;
    g_object_set_data(G_OBJECT(head), "px", GINT_TO_POINTER(px));
    if (margins) {
        int side = MAX(0, (int) (16 * u) - 14), top = MAX(0, (int) (24 * u) - 14);
        gtk_alignment_set_padding(GTK_ALIGNMENT(head), top, 0, side, side);
    }
    PangoFontDescription *fd = pango_font_description_from_string("Denboku Sans, Sans Bold");
    pango_font_description_set_absolute_size(fd, px * PANGO_SCALE);
    gtk_widget_modify_font(title, fd);
    pango_font_description_free(fd);
}

void drawer_set_title(GtkWidget *dialog, const char *title)
{
    Page *p = page_of(dialog);
    gtk_window_set_title(GTK_WINDOW(dialog), app.kindle ? KINDLE_DIALOG : title);
    if (p)
        gtk_label_set_text(g_object_get_data(G_OBJECT(p->head_pad), "title"), title);
}

void drawer_wrap(GtkWidget *d, const char *title)
{
    GtkDialog *dlg = GTK_DIALOG(d);
    Page *p = g_new0(Page, 1);
    p->dialog = d;
    g_object_set_data(G_OBJECT(d), "drawer-page", p);
    /* The buttons in a plain box of equal parts instead of GTK's button box. */
    gtk_container_remove(GTK_CONTAINER(dlg->vbox), dlg->action_area);
    dlg->action_area = gtk_hbox_new(TRUE, 8);
    g_object_set_data(G_OBJECT(dlg->action_area), "drawer-foot", d);
    p->frame = drawer_frame_new();
    g_signal_connect(p->frame, "button-press-event", G_CALLBACK(frame_press_cb), NULL);
    GtkWidget *box = gtk_vbox_new(FALSE, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 14);
    p->pad = gtk_alignment_new(0, 0, 1, 1);
    gtk_container_add(GTK_CONTAINER(p->pad), box);
    gtk_container_add(GTK_CONTAINER(p->frame), p->pad);
    p->head = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(p->head), FALSE);
    gtk_widget_add_events(p->head, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
    g_signal_connect(p->head, "button-press-event", G_CALLBACK(head_press_cb), p);
    g_signal_connect(p->head, "button-release-event", G_CALLBACK(head_release_cb), p);
    p->head_pad = drawer_head_new(title, G_CALLBACK(back_cb), G_CALLBACK(close_cb), d);
    gtk_container_add(GTK_CONTAINER(p->head), p->head_pad);
    gtk_box_pack_start(GTK_BOX(box), p->head, FALSE, FALSE, 0);
    p->scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(p->scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(p->scroll), GTK_SHADOW_NONE);
    p->viewport = gtk_viewport_new(NULL, NULL);
    gtk_viewport_set_shadow_type(GTK_VIEWPORT(p->viewport), GTK_SHADOW_NONE);
    g_object_ref(dlg->vbox);
    gtk_container_remove(GTK_CONTAINER(d), dlg->vbox);
    gtk_container_add(GTK_CONTAINER(p->viewport), dlg->vbox);
    g_object_unref(dlg->vbox);
    gtk_container_add(GTK_CONTAINER(p->scroll), p->viewport);
    /* The page scrolls to the field the keyboard or the controller goes to. */
    gtk_container_set_focus_vadjustment(GTK_CONTAINER(dlg->vbox),
                                        gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(p->scroll)));
    gtk_box_pack_start(GTK_BOX(box), p->scroll, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(box), dlg->action_area, FALSE, FALSE, 0);
    g_signal_connect_after(dlg->vbox, "size-request", G_CALLBACK(contents_request_cb), p);
    gtk_widget_show_all(p->pad);
    gtk_widget_set_no_show_all(p->frame, TRUE);
    gtk_box_pack_start(GTK_BOX(pages), p->frame, TRUE, TRUE, 0);
    g_signal_connect(d, "destroy", G_CALLBACK(dialog_destroy_cb), p);
}

/* ---- drop-downs as chips or as a list in a drawer ---- */

/* The widget in `old`'s place in its container, with the same packing. */
static void replace(GtkWidget *old, GtkWidget *with)
{
    GtkWidget *parent = gtk_widget_get_parent(old);
    g_object_ref(old);
    if (GTK_IS_TABLE(parent)) {
        guint l, r, t, b, xp, yp;
        GtkAttachOptions xo, yo;
        gtk_container_child_get(GTK_CONTAINER(parent), old, "left-attach", &l, "right-attach", &r, "top-attach", &t,
                                "bottom-attach", &b, "x-options", &xo, "y-options", &yo, "x-padding", &xp,
                                "y-padding", &yp, NULL);
        gtk_container_remove(GTK_CONTAINER(parent), old);
        gtk_table_attach(GTK_TABLE(parent), with, l, r, t, b, xo, yo, xp, yp);
    } else if (GTK_IS_BOX(parent)) {
        gboolean expand, fill;
        guint pad;
        GtkPackType pack;
        int pos;
        gtk_box_query_child_packing(GTK_BOX(parent), old, &expand, &fill, &pad, &pack);
        gtk_container_child_get(GTK_CONTAINER(parent), old, "position", &pos, NULL);
        gtk_container_remove(GTK_CONTAINER(parent), old);
        gtk_box_pack_start(GTK_BOX(parent), with, expand, fill, pad);
        gtk_box_set_child_packing(GTK_BOX(parent), with, expand, fill, pad, pack);
        gtk_box_reorder_child(GTK_BOX(parent), with, pos);
    } else {
        gtk_container_remove(GTK_CONTAINER(parent), old);
        gtk_container_add(GTK_CONTAINER(parent), with);
    }
    /* The drop-down stays inside, never drawn and taking no room, for the code that reads it. */
    gtk_box_pack_end(GTK_BOX(with), old, FALSE, FALSE, 0);
    gtk_widget_set_child_visible(old, FALSE);
    gtk_widget_set_size_request(old, 0, 0);
    g_object_unref(old);
}

/* A drop-down of texts, or of a picture and a text each (the piece sets): the columns, or FALSE. */
static gboolean combo_columns(GtkComboBox *c, int *text, int *picture)
{
    GtkTreeModel *m = gtk_combo_box_get_model(c);
    *text = *picture = -1;
    for (int i = 0; m && i < gtk_tree_model_get_n_columns(m) && i < 2; i++) {
        GType t = gtk_tree_model_get_column_type(m, i);
        if (t == G_TYPE_STRING && *text < 0)
            *text = i;
        else if (t == GDK_TYPE_PIXBUF && *picture < 0)
            *picture = i;
    }
    return m && *text >= 0 && gtk_tree_model_get_n_columns(m) <= 2;
}

/* The texts of such a drop-down (and its pictures, when `pictures` is given), or NULL. */
static GPtrArray *combo_texts_pictures(GtkComboBox *c, GPtrArray **pictures)
{
    int tc, pc;
    if (!combo_columns(c, &tc, &pc))
        return NULL;
    GtkTreeModel *m = gtk_combo_box_get_model(c);
    GPtrArray *t = g_ptr_array_new_with_free_func(g_free);
    if (pictures)
        *pictures = g_ptr_array_new_with_free_func(g_object_unref);
    GtkTreeIter it;
    for (gboolean ok = gtk_tree_model_get_iter_first(m, &it); ok; ok = gtk_tree_model_iter_next(m, &it)) {
        char *s = NULL;
        GdkPixbuf *p = NULL;
        gtk_tree_model_get(m, &it, tc, &s, -1);
        if (pc >= 0)
            gtk_tree_model_get(m, &it, pc, &p, -1);
        g_ptr_array_add(t, s ? s : g_strdup(""));
        if (pictures)
            g_ptr_array_add(*pictures, p ? p : gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 1, 1));
        else if (p)
            g_object_unref(p);
    }
    return t;
}

static GPtrArray *combo_texts(GtkComboBox *c) { return combo_texts_pictures(c, NULL); }

static gboolean has_pictures(GtkComboBox *c)
{
    int tc, pc;
    return combo_columns(c, &tc, &pc) && pc >= 0;
}

typedef struct {
    GtkWidget *combo, *box, *value, *image; /* value and image: the picker's; box: the chips' rows */
    gboolean busy;
} Mirror;

static void mirror_visible_cb(GObject *o, GParamSpec *ps, gpointer data)
{
    (void) ps;
    GtkWidget *wrap = data;
    gtk_widget_set_visible(wrap, gtk_widget_get_visible(GTK_WIDGET(o)));
}

static void mirror_sensitive_cb(GObject *o, GParamSpec *ps, gpointer data)
{
    (void) ps;
    gtk_widget_set_sensitive(data, gtk_widget_get_sensitive(GTK_WIDGET(o)));
}

static void watch(GtkWidget *combo, GtkWidget *wrap)
{
    g_signal_connect(combo, "notify::visible", G_CALLBACK(mirror_visible_cb), wrap);
    g_signal_connect(combo, "notify::sensitive", G_CALLBACK(mirror_sensitive_cb), wrap);
    gtk_widget_set_sensitive(wrap, gtk_widget_get_sensitive(combo));
    if (gtk_widget_get_no_show_all(combo))
        gtk_widget_set_no_show_all(wrap, TRUE);
    gtk_widget_set_visible(wrap, gtk_widget_get_visible(combo));
}

static void chip_toggled_cb(GtkToggleButton *b, gpointer data)
{
    Mirror *m = data;
    if (m->busy || !gtk_toggle_button_get_active(b))
        return;
    m->busy = TRUE;
    gtk_combo_box_set_active(GTK_COMBO_BOX(m->combo), GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "index")));
    m->busy = FALSE;
}

static void build_chips(Mirror *m);

static void chips_changed_cb(GtkComboBox *c, gpointer data)
{
    (void) c;
    Mirror *m = data;
    if (m->busy)
        return;
    m->busy = TRUE;
    int active = gtk_combo_box_get_active(GTK_COMBO_BOX(m->combo)), i = 0;
    GList *rows = gtk_container_get_children(GTK_CONTAINER(m->box));
    for (GList *r = rows; r; r = r->next) {
        GList *kids = gtk_container_get_children(GTK_CONTAINER(r->data));
        for (GList *k = kids; k; k = k->next, i++)
            gtk_toggle_button_set_active(k->data, i == active);
        g_list_free(kids);
    }
    g_list_free(rows);
    m->busy = FALSE;
}

/* How wide the chips may go: the drawer's rows, less what sits beside them (Rules beside the variants). */
static int chips_room(GtkWidget *box)
{
    int room = rows_width() - 40;
    for (GtkWidget *w = box, *p = gtk_widget_get_parent(w); p && GTK_IS_HBOX(p); w = p, p = gtk_widget_get_parent(p)) {
        GList *kids = gtk_container_get_children(GTK_CONTAINER(p));
        for (GList *k = kids; k; k = k->next)
            if (k->data != w) {
                GtkRequisition r;
                gtk_widget_size_request(k->data, &r);
                room -= r.width + gtk_box_get_spacing(GTK_BOX(p));
            }
        g_list_free(kids);
    }
    return room;
}

/* Lines of chips as wide as the drawer's rows. */
static void build_chips(Mirror *m)
{
    GList *old = gtk_container_get_children(GTK_CONTAINER(m->box));
    for (GList *l = old; l; l = l->next)
        gtk_widget_destroy(l->data);
    g_list_free(old);
    GPtrArray *t = combo_texts(GTK_COMBO_BOX(m->combo));
    if (!t)
        return;
    GSList *group = NULL;
    int active = gtk_combo_box_get_active(GTK_COMBO_BOX(m->combo)), room = chips_room(m->box);
    for (guint i = 0; i < t->len; i++) {
        GtkWidget *chip = gtk_radio_button_new_with_label(group, g_ptr_array_index(t, i));
        group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(chip));
        gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(chip), FALSE);
        gtk_widget_set_name(chip, "denboku-chip");
        g_object_set_data(G_OBJECT(chip), "index", GINT_TO_POINTER(i));
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chip), (int) i == active);
        g_signal_connect(chip, "toggled", G_CALLBACK(chip_toggled_cb), m);
        chip_pack(m->box, chip, room);
    }
    g_ptr_array_free(t, TRUE);
    gtk_widget_show_all(m->box);
}

static int rebuilds; /* chips waiting to be built again: the page is not drawn before (kindle.c) */

gboolean drawer_settling(void) { return rebuilds > 0; }

static gboolean rebuild_cb(gpointer data)
{
    Mirror *m = data;
    m->busy = FALSE;
    rebuilds--;
    build_chips(m);
    return FALSE;
}

static void model_changed_cb(gpointer data)
{
    Mirror *m = data;
    if (!m->busy) {
        m->busy = TRUE; /* one rebuild for many changes */
        rebuilds++;
        g_idle_add(rebuild_cb, m);
    }
}

static void row_changed_cb(GtkTreeModel *t, GtkTreePath *p, GtkTreeIter *i, gpointer data)
{
    (void) t, (void) p, (void) i;
    model_changed_cb(data);
}

static void row_deleted_cb(GtkTreeModel *t, GtkTreePath *p, gpointer data)
{
    (void) t, (void) p;
    model_changed_cb(data);
}

static void watch_model(Mirror *m)
{
    GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(m->combo));
    g_signal_connect(model, "row-inserted", G_CALLBACK(row_changed_cb), m);
    g_signal_connect(model, "row-deleted", G_CALLBACK(row_deleted_cb), m);
}

/* The label of a drop-down's row, for the list's title: the one table_row() gave it or the box it
 * is in (Variant and its Rules). Two rows may share a line of the table, shown one at a time
 * (Clock and Lishogi's Time control), so the line alone does not tell. */
static const char *row_label(GtkWidget *w)
{
    for (; w; w = gtk_widget_get_parent(w)) {
        GtkWidget *l = g_object_get_data(G_OBJECT(w), "row-label");
        if (l)
            return gtk_label_get_text(GTK_LABEL(l));
        if (GTK_IS_TABLE(gtk_widget_get_parent(w)))
            return NULL;
    }
    return NULL;
}

/* The chosen row's text and picture: only that row is read (a list filled again calls this per row). */
static void picker_set_value(Mirror *m)
{
    int tc, pc;
    GtkTreeIter it;
    char *text = NULL;
    GdkPixbuf *picture = NULL;
    if (combo_columns(GTK_COMBO_BOX(m->combo), &tc, &pc) && gtk_combo_box_get_active_iter(GTK_COMBO_BOX(m->combo), &it)) {
        GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(m->combo));
        gtk_tree_model_get(model, &it, tc, &text, -1);
        if (pc >= 0 && m->image)
            gtk_tree_model_get(model, &it, pc, &picture, -1);
    }
    gtk_label_set_text(GTK_LABEL(m->value), text ? text : "");
    if (m->image)
        gtk_image_set_from_pixbuf(GTK_IMAGE(m->image), picture);
    g_free(text);
    if (picture)
        g_object_unref(picture);
}

static void picker_model_cb(gpointer data)
{
    picker_set_value(data);
}

static void picker_changed_cb(GtkComboBox *c, gpointer data)
{
    (void) c;
    picker_set_value(data);
}

static void picker_picked(int i, gpointer data)
{
    gtk_combo_box_set_active(GTK_COMBO_BOX(((Mirror *) data)->combo), i);
}

/* The list in a drawer over this one, as every list of choices (rows_pick). */
static void picker_clicked_cb(GtkButton *b, gpointer data)
{
    (void) b;
    Mirror *m = data;
    GPtrArray *pictures = NULL, *t = combo_texts_pictures(GTK_COMBO_BOX(m->combo), m->image ? &pictures : NULL);
    if (!t)
        return;
    const char *title = g_object_get_data(G_OBJECT(m->value), "title");
    rows_pick(title ? title : "", (const char *const *) t->pdata, pictures ? (GdkPixbuf *const *) pictures->pdata : NULL,
              (int) t->len, gtk_combo_box_get_active(GTK_COMBO_BOX(m->combo)), picker_picked, m);
    g_ptr_array_free(t, TRUE);
    if (pictures)
        g_ptr_array_free(pictures, TRUE);
}

/* Texts that are one word and the numbers in order ("Level 1" … "Level 10"): a − / + row. */
static gboolean numbered(GPtrArray *t)
{
    if (t->len < 3)
        return FALSE;
    for (guint i = 0; i < t->len; i++) {
        const char *a = g_ptr_array_index(t, i), *sp = strrchr(a, ' ');
        const char *b = g_ptr_array_index(t, 0), *bsp = strrchr(b, ' ');
        if (!sp || !bsp || sp - a != bsp - b || strncmp(a, b, sp - a) || atoi(sp + 1) != atoi(bsp + 1) + (int) i)
            return FALSE;
    }
    return TRUE;
}

static void step_cb(GtkButton *b, gpointer data)
{
    Mirror *m = data;
    GtkComboBox *c = GTK_COMBO_BOX(m->combo);
    int n = gtk_tree_model_iter_n_children(gtk_combo_box_get_model(c), NULL);
    int step = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "step"));
    gtk_combo_box_set_active(c, CLAMP(gtk_combo_box_get_active(c) + step, 0, n - 1));
}

static void convert_combo(GtkWidget *combo)
{
    if (g_object_get_data(G_OBJECT(combo), "drawer-mirror"))
        return;
    GPtrArray *t = combo_texts(GTK_COMBO_BOX(combo));
    if (!t)
        return; /* another kind of drop-down: as it is */
    gboolean pictures = has_pictures(GTK_COMBO_BOX(combo));
    int total = 0, longest = 0;
    for (guint i = 0; i < t->len; i++) {
        int n = g_utf8_strlen(g_ptr_array_index(t, i), -1);
        total += n, longest = MAX(longest, n);
    }
    gboolean chips = !pictures && t->len >= 2 && t->len <= 6 && longest <= 20 && total <= 64;
    gboolean stepper = !pictures && numbered(t);
    g_ptr_array_free(t, TRUE);
    Mirror *m = g_new0(Mirror, 1);
    m->combo = combo;
    g_object_set_data_full(G_OBJECT(combo), "drawer-mirror", m, g_free);
    GtkWidget *wrap = gtk_hbox_new(FALSE, 0);
    if (stepper) {
        /* − value + across the row, as in the New Game mockup. */
        GtkWidget *row = gtk_hbox_new(FALSE, 8);
        for (int i = 0; i < 2; i++) {
            GtkWidget *b = gtk_button_new_with_label(i ? "+" : "−");
            gtk_widget_set_name(b, "denboku-chip");
            g_object_set_data(G_OBJECT(b), "step", GINT_TO_POINTER(i ? 1 : -1));
            g_signal_connect(b, "clicked", G_CALLBACK(step_cb), m);
            gtk_box_pack_start(GTK_BOX(row), b, FALSE, FALSE, 0);
            if (!i) {
                m->value = gtk_label_new(NULL);
                gtk_box_pack_start(GTK_BOX(row), m->value, TRUE, TRUE, 0);
            }
        }
        gtk_box_pack_start(GTK_BOX(wrap), row, TRUE, TRUE, 0);
        replace(combo, wrap);
        gtk_widget_show_all(row);
        picker_set_value(m);
        g_signal_connect(combo, "changed", G_CALLBACK(picker_changed_cb), m);
    } else if (chips) {
        m->box = gtk_vbox_new(FALSE, 6);
        gtk_box_pack_start(GTK_BOX(wrap), m->box, TRUE, TRUE, 0);
        replace(combo, wrap);
        build_chips(m);
        g_signal_connect(combo, "changed", G_CALLBACK(chips_changed_cb), m);
        watch_model(m);
    } else {
        GtkWidget *b = gtk_button_new(), *row = gtk_hbox_new(FALSE, 8);
        m->value = gtk_label_new(NULL);
        gtk_label_set_ellipsize(GTK_LABEL(m->value), PANGO_ELLIPSIZE_END);
        gtk_misc_set_alignment(GTK_MISC(m->value), 0, 0.5);
        const char *title = row_label(combo);
        g_object_set_data_full(G_OBJECT(m->value), "title", g_strdup(title ? title : ""), g_free);
        if (pictures) {
            m->image = gtk_image_new();
            gtk_box_pack_start(GTK_BOX(row), m->image, FALSE, FALSE, 0);
        }
        gtk_box_pack_start(GTK_BOX(row), m->value, TRUE, TRUE, 0);
        gtk_box_pack_end(GTK_BOX(row), gtk_label_new("›"), FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(b), row);
        gtk_box_pack_start(GTK_BOX(wrap), b, TRUE, TRUE, 0);
        replace(combo, wrap);
        gtk_widget_show_all(b);
        picker_set_value(m);
        g_signal_connect(combo, "changed", G_CALLBACK(picker_changed_cb), m);
        g_signal_connect(b, "clicked", G_CALLBACK(picker_clicked_cb), m);
        /* Lists filled again (the piece sets for another variant or color mode). */
        GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(combo));
        g_signal_connect_swapped(model, "row-inserted", G_CALLBACK(picker_model_cb), m);
        g_signal_connect_swapped(model, "row-changed", G_CALLBACK(picker_model_cb), m);
    }
    watch(combo, wrap);
}

/* A table of a label and a control per row: each label above its control, as in the mockups. */
static void stack_table(GtkWidget *table)
{
    guint cols, rows;
    g_object_get(table, "n-columns", &cols, "n-rows", &rows, NULL);
    if (cols != 2 || g_object_get_data(G_OBJECT(table), "drawer-stacked"))
        return;
    g_object_set_data(G_OBJECT(table), "drawer-stacked", GINT_TO_POINTER(1));
    GList *kids = gtk_container_get_children(GTK_CONTAINER(table));
    gboolean *paired = g_new0(gboolean, rows + 1), *labelled = g_new0(gboolean, rows + 1);
    for (GList *l = kids; l; l = l->next) {
        guint left, right, top;
        gtk_container_child_get(GTK_CONTAINER(table), l->data, "left-attach", &left, "right-attach", &right,
                                "top-attach", &top, NULL);
        if (top < rows && left == 1)
            paired[top] = TRUE;
        if (top < rows && left == 0 && right == 1 && GTK_IS_LABEL(l->data))
            labelled[top] = TRUE;
    }
    /* Only tables of a label and a control per row (or a row across both): a grid of buttons
     * stays as it is. */
    gboolean stackable = TRUE;
    for (GList *l = kids; l; l = l->next) {
        guint left, right, top;
        gtk_container_child_get(GTK_CONTAINER(table), l->data, "left-attach", &left, "right-attach", &right,
                                "top-attach", &top, NULL);
        if ((left == 0 && right == 1 && !GTK_IS_LABEL(l->data)) || (left == 1 && top < rows && !labelled[top]))
            stackable = FALSE;
    }
    if (!stackable) {
        g_free(paired);
        g_free(labelled);
        g_list_free(kids);
        return;
    }
    for (guint r = 0; r < rows; r++)
        paired[r] = paired[r] && labelled[r];
    guint *first = g_new0(guint, rows + 1);
    for (guint r = 0, at = 0; r <= rows; r++)
        first[r] = at, at += r < rows && paired[r] ? 2 : 1;
    gtk_table_resize(GTK_TABLE(table), MAX(1, first[rows]), 1);
    for (GList *l = kids; l; l = l->next) {
        guint left, top, bottom;
        gtk_container_child_get(GTK_CONTAINER(table), l->data, "left-attach", &left, "top-attach", &top,
                                "bottom-attach", &bottom, NULL);
        top = MIN(top, rows - 1), bottom = MIN(MAX(bottom, top + 1), rows);
        gboolean label = paired[top] && left == 0 && GTK_IS_LABEL(l->data), control = paired[top] && left == 1;
        guint t = first[top] + control, b = bottom > top + 1 ? first[bottom] : label || control ? t + 1 : first[top + 1];
        gtk_container_child_set(GTK_CONTAINER(table), l->data, "left-attach", 0, "right-attach", 1, "top-attach", t,
                                "bottom-attach", b, "y-padding", label ? 0 : 4, NULL);
        if (label)
            gtk_misc_set_alignment(GTK_MISC(l->data), 0, 1);
    }
    g_free(labelled);
    g_free(first);
    g_free(paired);
    g_list_free(kids);
}

static gboolean relayout_cb(gpointer data)
{
    Page *p = data;
    if (stack && g_list_last(stack)->data == p)
        layout_page(p);
    return FALSE;
}

static void tab_switched_cb(GtkNotebook *nb, gpointer page, guint n, gpointer data)
{
    (void) nb, (void) page, (void) n;
    g_idle_add(relayout_cb, data);
}

/* Each tab in a scrolled area of its own, across only: a wide tab (Controls) no longer makes the
 * others as wide, with empty room at their right. */
static void scroll_tabs(GtkWidget *tabs, Page *p)
{
    if (g_object_get_data(G_OBJECT(tabs), "drawer-scrolls"))
        return;
    g_object_set_data(G_OBJECT(tabs), "drawer-scrolls", GINT_TO_POINTER(1));
    p->tabs = tabs;
    GtkNotebook *nb = GTK_NOTEBOOK(tabs);
    int current = gtk_notebook_get_current_page(nb);
    for (int i = 0; i < gtk_notebook_get_n_pages(nb); i++) {
        GtkWidget *page = gtk_notebook_get_nth_page(nb, i), *label = gtk_notebook_get_tab_label(nb, page);
        g_object_ref(page), g_object_ref(label);
        gtk_notebook_remove_page(nb, i);
        GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL), *vp = gtk_viewport_new(NULL, NULL);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
        gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw), GTK_SHADOW_NONE);
        gtk_viewport_set_shadow_type(GTK_VIEWPORT(vp), GTK_SHADOW_NONE);
        gtk_container_add(GTK_CONTAINER(vp), page);
        gtk_container_add(GTK_CONTAINER(sw), vp);
        gtk_widget_show(vp), gtk_widget_show(sw);
        gtk_notebook_insert_page(nb, sw, label, i);
        g_object_unref(page), g_object_unref(label);
    }
    gtk_notebook_set_current_page(nb, current);
    g_signal_connect(tabs, "switch-page", G_CALLBACK(tab_switched_cb), p);
}

static void adapt_tree(GtkWidget *w, gpointer data)
{
    if (GTK_IS_NOTEBOOK(w))
        scroll_tabs(w, data);
    if (GTK_IS_COMBO_BOX(w) && !GTK_IS_COMBO_BOX_ENTRY(w)) {
        convert_combo(w);
        return;
    }
    if (GTK_IS_TABLE(w))
        stack_table(w);
    if (GTK_IS_CONTAINER(w) && !GTK_IS_TREE_VIEW(w) && !GTK_IS_FILE_CHOOSER(w))
        gtk_container_forall(GTK_CONTAINER(w), adapt_tree, data);
}

static void adapt(Page *p)
{
    GList *kids = gtk_container_get_children(GTK_CONTAINER(GTK_DIALOG(p->dialog)->vbox));
    for (GList *l = kids; l; l = l->next)
        adapt_tree(l->data, p);
    g_list_free(kids);
}

/* ---- a drag scrolls, as on touch screens ---- */

/* GTK 2 scrolls only with the bars and the wheel. A press inside a scrolled area (not on a text
 * field or a scroll bar) that then moves scrolls it, and the button pressed gets a leave event, so
 * the release does not click it. The window pressed seldom asks for motion (a button's does not),
 * so the press grabs the pointer with motion added, as the implicit grab would be otherwise. */
static struct {
    GtkWidget *scroll;
    GdkWindow *window;
    double x, y, h0, v0;
    gboolean dragging, grabbed;
    double vx, vy, lx, ly; /* pixels per ms of the last moves, where the last one was */
    guint32 lt, t0; /* the last move's time, the press's */
    double want_dx, want_dy; /* e-ink: where the finger is, shown at its pace */
    gboolean wheeled;        /* the Kindle sent this swipe as a wheel step too: done */
    gint64 turned;           /* the last page turned, by either */
    gint64 shown;
    guint show_id;
} drag;

/* A drag let go while moving glides on, slowing down (a flick reaches a long list's end). */
#define FLING_TAU 600.0 /* ms for the speed to fall to a third: it goes speed × FLING_TAU in all */
static struct {
    GtkAdjustment *adj[2];
    double v[2];
    gint64 last;
    guint timer;
} fling;

static void fling_stop(void)
{
    if (fling.timer)
        g_source_remove(fling.timer), fling.timer = 0;
    for (int i = 0; i < 2; i++)
        if (fling.adj[i])
            g_object_unref(fling.adj[i]), fling.adj[i] = NULL;
}

/* Moves adj by delta; FALSE when it was already at that end. */
static gboolean adj_move(GtkAdjustment *adj, double delta)
{
    double now = gtk_adjustment_get_value(adj), lo = gtk_adjustment_get_lower(adj),
           hi = gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj), to = CLAMP(now + delta, lo, MAX(lo, hi));
    if (to != now)
        gtk_adjustment_set_value(adj, to);
    return to != now;
}

static gboolean fling_cb(gpointer data)
{
    (void) data;
    gint64 now = g_get_monotonic_time();
    double dt = (double) (now - fling.last) / 1000, keep = exp(-dt / FLING_TAU);
    fling.last = now;
    gboolean on = FALSE;
    for (int i = 0; i < 2; i++) {
        /* the distance the falling speed covers in dt */
        if (fabs(fling.v[i]) > 0.02 && adj_move(fling.adj[i], -fling.v[i] * FLING_TAU * (1 - keep)))
            on = TRUE;
        else
            fling.v[i] = 0;
        fling.v[i] *= keep;
    }
    if (on)
        return TRUE;
    fling.timer = 0;
    fling_stop();
    return FALSE;
}

static void set_unseen(GtkScrolledWindow *sw, GtkAdjustment *adj, double value);

static void fling_start(GtkScrolledWindow *sw, double vx, double vy)
{
    fling_stop();
    if (hypot(vx, vy) < 0.3)
        return;
    fling.adj[0] = g_object_ref(gtk_scrolled_window_get_hadjustment(sw));
    fling.adj[1] = g_object_ref(gtk_scrolled_window_get_vadjustment(sw));
    fling.v[0] = vx, fling.v[1] = vy;
    if (app.kindle) { /* e-ink: no frames in between, straight to where it would stop */
        for (int i = 0; i < 2; i++) {
            GtkAdjustment *a = fling.adj[i];
            double lo = gtk_adjustment_get_lower(a), hi = MAX(lo, gtk_adjustment_get_upper(a) - gtk_adjustment_get_page_size(a)),
                   to = CLAMP(gtk_adjustment_get_value(a) - fling.v[i] * FLING_TAU, lo, hi);
            if (to != gtk_adjustment_get_value(a))
                set_unseen(sw, a, to);
        }
        fling_stop();
        return;
    }
    fling.last = g_get_monotonic_time();
    fling.timer = g_timeout_add(16, fling_cb, NULL);
}

static void scroll_to(double dx, double dy);

static void drag_end(guint32 time)
{
    if (drag.show_id) { /* where the finger let go */
        g_source_remove(drag.show_id), drag.show_id = 0;
        if (drag.scroll)
            scroll_to(drag.want_dx, drag.want_dy);
    }
    GdkWindow *grab = NULL;
    if (drag.grabbed && gdk_pointer_grab_info_libgtk_only(gdk_display_get_default(), &grab, NULL) && grab == drag.window)
        gdk_pointer_ungrab(time);
    drag.grabbed = FALSE;
    if (drag.scroll)
        g_object_unref(drag.scroll), drag.scroll = NULL;
}

/* Scrolls to where the finger is, dx, dy from the press; the first time past a few pixels the
 * pressed button is left, so that the release does not click it. */
static void leave_pressed(guint32 time)
{
    GdkEvent *leave = gdk_event_new(GDK_LEAVE_NOTIFY);
    leave->crossing.window = g_object_ref(drag.window);
    leave->crossing.send_event = TRUE;
    leave->crossing.time = time;
    leave->crossing.mode = GDK_CROSSING_NORMAL;
    leave->crossing.detail = GDK_NOTIFY_ANCESTOR;
    gtk_main_do_event(leave);
    gdk_event_free(leave);
}

/* Long pages turn instead of scrolling (Preferences, on by default on the Kindle). */
static gboolean page_turning(void) { return setting_int("PageTurn", app.kindle); }

/* E-ink: the page's window hidden while it moves. Moved in sight, the X server copied what showed
 * to its new place at once (the rows moved a little, the rest stale), and what waited to be drawn
 * (in_bands()) stayed where it was and smeared the rows; hidden (no window has a background, so
 * the screen keeps what it shows), it comes back to be drawn whole, once. */
static void set_unseen(GtkScrolledWindow *sw, GtkAdjustment *adj, double value)
{
    GtkWidget *child = gtk_bin_get_child(GTK_BIN(sw));
    GdkWindow *bin = app.kindle && GTK_IS_VIEWPORT(child) ? gtk_viewport_get_bin_window(GTK_VIEWPORT(child)) : NULL;
    if (bin && gdk_window_is_visible(bin)) {
        gdk_window_hide(bin);
        gtk_adjustment_set_value(adj, value);
        gdk_window_show(bin);
        return;
    }
    gtk_adjustment_set_value(adj, value);
    if (app.kindle)
        gtk_widget_queue_draw(GTK_WIDGET(sw));
}

static void scroll_to(double dx, double dy)
{
    GtkScrolledWindow *sw = GTK_SCROLLED_WINDOW(drag.scroll);
    GtkAdjustment *adj[2] = {gtk_scrolled_window_get_hadjustment(sw), gtk_scrolled_window_get_vadjustment(sw)};
    double want[2] = {drag.h0 - dx, drag.v0 - dy};
    for (int i = 0; i < 2; i++) {
        double to = CLAMP(want[i], gtk_adjustment_get_lower(adj[i]), gtk_adjustment_get_upper(adj[i]) - gtk_adjustment_get_page_size(adj[i]));
        if (to != gtk_adjustment_get_value(adj[i]))
            set_unseen(sw, adj[i], to);
    }
    drag.shown = g_get_monotonic_time();
}

static gboolean show_drag_cb(gpointer data)
{
    (void) data;
    drag.show_id = 0;
    if (drag.scroll)
        scroll_to(drag.want_dx, drag.want_dy);
    return FALSE;
}

static void drag_to(double dx, double dy, guint32 time)
{
    if (!drag.dragging) {
        drag.dragging = TRUE;
        leave_pressed(time);
    }
    if (page_turning())
        return; /* the page turns as the finger lifts (page_turn()) */
    if (!app.kindle) {
        scroll_to(dx, dy);
        return;
    }
    /* E-ink: at most a step an update (~300 ms), the last where the finger is. */
    drag.want_dx = dx, drag.want_dy = dy;
    int wait = 300 - (int) ((g_get_monotonic_time() - drag.shown) / 1000);
    if (wait <= 0)
        scroll_to(dx, dy);
    else if (!drag.show_id)
        drag.show_id = g_timeout_add(wait, show_drag_cb, NULL);
}

/* As the Kindle's own lists: a swipe turns a page (what shows, less a row's worth kept in
 * sight), up for the next. Following the finger, each step was an e-ink update and the moved
 * rows smeared. */
static void page_turn(double dx, double dy)
{
    GtkScrolledWindow *sw = GTK_SCROLLED_WINDOW(drag.scroll);
    gboolean across = fabs(dx) > fabs(dy);
    GtkAdjustment *adj = across ? gtk_scrolled_window_get_hadjustment(sw) : gtk_scrolled_window_get_vadjustment(sw);
    double d = across ? dx : dy, now = gtk_adjustment_get_value(adj), lo = gtk_adjustment_get_lower(adj),
           hi = MAX(lo, gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj)),
           to = CLAMP(now + (d < 0 ? 1 : -1) * gtk_adjustment_get_page_size(adj) * 0.9, lo, hi);
    if (to != now)
        set_unseen(sw, adj, to);
    drag.turned = g_get_monotonic_time();
}

/* An event's time, Android's own there (see android_touch_time()). */
static guint32 touch_time(guint32 time, double x, double y)
{
#ifdef __ANDROID__
    return android_touch_time((int) x, (int) y, time);
#else
    (void) x, (void) y;
    return time;
#endif
}

/* The Kindle's touch driver also sends a swipe as a wheel step: GTK scrolled with it, the rows
 * copied on screen. A page turned instead (a step when scrolling), once a swipe. */
static gboolean wheel_turn(GdkEventScroll *ev)
{
    GtkWidget *s = gtk_get_event_widget((GdkEvent *) ev);
    while (s && !GTK_IS_SCROLLED_WINDOW(s))
        s = gtk_widget_get_parent(s);
    if (!s)
        return FALSE;
    if (drag.scroll) { /* the swipe's own press: no click, and no second turn as it lifts */
        drag.dragging = TRUE;
        drag.wheeled = TRUE;
        leave_pressed(ev->time);
    }
    if (g_get_monotonic_time() - drag.turned < 600000) /* the swipe's lift turned it already */
        return TRUE;
    drag.turned = g_get_monotonic_time();
    GtkScrolledWindow *sw = GTK_SCROLLED_WINDOW(s);
    gboolean across = ev->direction == GDK_SCROLL_LEFT || ev->direction == GDK_SCROLL_RIGHT;
    int sign = ev->direction == GDK_SCROLL_DOWN || ev->direction == GDK_SCROLL_RIGHT ? 1 : -1;
    GtkAdjustment *adj = across ? gtk_scrolled_window_get_hadjustment(sw) : gtk_scrolled_window_get_vadjustment(sw);
    double page = gtk_adjustment_get_page_size(adj), lo = gtk_adjustment_get_lower(adj),
           hi = MAX(lo, gtk_adjustment_get_upper(adj) - page), now = gtk_adjustment_get_value(adj),
           to = CLAMP(now + sign * (page_turning() ? page * 0.9 : pow(page, 2.0 / 3.0)), lo, hi);
    if (to != now)
        set_unseen(sw, adj, to);
    return TRUE;
}

gboolean touch_scroll_event(GdkEvent *ev)
{
    if (app.kindle && ev->type == GDK_SCROLL)
        return wheel_turn(&ev->scroll);
    if (ev->type == GDK_BUTTON_PRESS && ev->button.button == 1) {
        GtkWidget *w = gtk_get_event_widget(ev), *s = w;
        while (s && !GTK_IS_SCROLLED_WINDOW(s))
            s = gtk_widget_get_parent(s);
        drag_end(ev->button.time);
        fling_stop();
        if (s && w && !GTK_IS_ENTRY(w) && !GTK_IS_TEXT_VIEW(w) && !GTK_IS_RANGE(w)) {
            drag.scroll = g_object_ref(s), drag.window = ev->button.window;
            drag.x = ev->button.x_root, drag.y = ev->button.y_root, drag.dragging = FALSE;
            drag.lx = drag.x, drag.ly = drag.y, drag.vx = drag.vy = 0;
            drag.wheeled = FALSE;
            drag.lt = drag.t0 = touch_time(ev->button.time, drag.x, drag.y);
            drag.grabbed = gdk_pointer_grab(drag.window, FALSE, GDK_BUTTON_MOTION_MASK | GDK_BUTTON_RELEASE_MASK |
                                            GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK, NULL, NULL,
                                            ev->button.time) == GDK_GRAB_SUCCESS;
            drag.h0 = gtk_adjustment_get_value(gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(s)));
            drag.v0 = gtk_adjustment_get_value(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(s)));
        }
        return FALSE;
    }
    if (!drag.scroll)
        return FALSE;
    /* The grab's own crossing (Android, a quick flick) would put the button back under the finger. */
    if (drag.dragging && ev->type == GDK_ENTER_NOTIFY && ev->crossing.window == drag.window)
        return TRUE;
    if (ev->type == GDK_MOTION_NOTIFY && (ev->motion.state & GDK_BUTTON1_MASK)) {
        double dx = ev->motion.x_root - drag.x, dy = ev->motion.y_root - drag.y;
        guint32 t = touch_time(ev->motion.time, ev->motion.x_root, ev->motion.y_root), dt = t - drag.lt;
        if (dt > 0) { /* the newest moves weigh most */
            double w = MIN(1.0, dt / 50.0);
            drag.vx = (1 - w) * drag.vx + w * (ev->motion.x_root - drag.lx) / dt;
            drag.vy = (1 - w) * drag.vy + w * (ev->motion.y_root - drag.ly) / dt;
            drag.lx = ev->motion.x_root, drag.ly = ev->motion.y_root, drag.lt = t;
        }
        if (drag.dragging || hypot(dx, dy) > 12) {
            drag_to(dx, dy, ev->motion.time);
            return TRUE;
        }
        return FALSE;
    }
    if (ev->type == GDK_BUTTON_RELEASE) {
        double dx = ev->button.x_root - drag.x, dy = ev->button.y_root - drag.y;
        guint32 t = touch_time(ev->button.time, ev->button.x_root, ev->button.y_root);
        /* A quick flick: its moves reached X before the press here took the pointer, under the
         * pressed button's window, which asks for none of them; only the release tells. */
        if (!drag.dragging && hypot(dx, dy) > 12) {
            guint32 dt = MAX(16u, t - drag.t0);
            drag_to(dx, dy, ev->button.time);
            drag.vx = dx / dt, drag.vy = dy / dt, drag.lt = t;
        }
        if (drag.dragging)
            leave_pressed(ev->button.time); /* whatever came in between: a scroll is no click */
        if (drag.wheeled)
            ;
        else if (drag.dragging && page_turning())
            page_turn(dx, dy);
        else if (drag.dragging && t - drag.lt < 100) /* still moving when let go */
            fling_start(GTK_SCROLLED_WINDOW(drag.scroll), drag.vx, drag.vy);
        drag_end(ev->button.time);
    }
    return FALSE;
}

/* ---- setup ---- */

void drawer_init(GtkWidget *column)
{
    column_box = column;
    holder = gtk_event_box_new();
    pages = gtk_vbox_new(FALSE, 0);
    center = gtk_alignment_new(0.5, 0, 1, 1);
    gtk_container_add(GTK_CONTAINER(center), pages);
    gtk_container_add(GTK_CONTAINER(holder), center);
    gtk_widget_show_all(center);
    gtk_widget_set_redraw_on_allocate(holder, FALSE); /* its window shows what it has; the game's is left alone */
    gtk_widget_set_no_show_all(holder, TRUE);
    gtk_box_pack_end(GTK_BOX(column), holder, FALSE, FALSE, 0);
    GtkWidgetClass *c = g_type_class_ref(GTK_TYPE_DIALOG);
    parent_show = c->show, parent_show_all = c->show_all, parent_hide = c->hide;
    c->show = dialog_show, c->show_all = dialog_show_all, c->hide = dialog_hide;
    g_signal_add_emission_hook(g_signal_lookup("parent-set", GTK_TYPE_WIDGET), 0, foot_added_hook, NULL, NULL);
}

gboolean drawer_open(void) { return stack != NULL; }

gboolean drawer_ready(void) { return holder != NULL; }

GtkWidget *drawer_dialog(void) { return stack ? ((Page *) g_list_last(stack)->data)->dialog : NULL; }

void drawer_scroll(int dir)
{
    if (!stack)
        return;
    Page *p = g_list_last(stack)->data;
    GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(p->scroll));
    double page = gtk_adjustment_get_page_size(adj);
    gtk_adjustment_set_value(adj, CLAMP(gtk_adjustment_get_value(adj) + dir * page * 0.25, gtk_adjustment_get_lower(adj),
                                        gtk_adjustment_get_upper(adj) - page));
}

GtkWidget *drawer_box(void) { return stack ? ((Page *) g_list_last(stack)->data)->frame : NULL; }

/* Escape closes the top drawer; other keys go on to its fields (not to the board). */
gboolean drawer_key(const GdkEventKey *e)
{
    if (!stack)
        return FALSE;
    /* A page that takes keys itself (setting a key in Controls). */
    GtkWidget *top = drawer_dialog();
    gboolean (*keys)(GtkWidget *, GdkEventKey *, gpointer) = g_object_get_data(G_OBJECT(top), "drawer-keys");
    if (keys && keys(top, (GdkEventKey *) e, NULL))
        return TRUE;
    if (controls_dialog_key(app.window, top, e))
        return TRUE;
    if (e->keyval == GDK_Escape) {
        go_back(g_list_last(stack)->data);
        return TRUE;
    }
    return FALSE;
}
