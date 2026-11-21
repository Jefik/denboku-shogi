/* The chat of network and lishogi games: the lines as bubbles (ours at the right, in the accent
 * color), a field to type and Send. Landscape: a panel always there, against the board's left
 * side, the two and the column centered together. Portrait: the bar's chat button opens it as a
 * drawer under the board (over it while the Kindle keyboard is up; Android's makes the window
 * smaller); news while it is closed put a dot on the button and the line with the moves. */

#include "app.h"

typedef struct {
    char *who, *text;
    gboolean mine;
} ChatLine;

static GPtrArray *lines;
static GtkWidget *panel, *align, *log_area, *entry, *head, *side_box, *top_box;
static double head_y; /* a drag on the title closes the drawer */
static gboolean shown_portrait, hidden_landscape, unread;
static double scroll, drag_y, drag_scroll; /* pixels up from the newest line */
static gboolean dragging;

static gboolean online(void) { return app.net || app.mode == MODE_ONLINE; }

static gboolean landscape_window(void)
{
    if (app.kindle)
        return setting_int("Landscape", 0) != 0;
    int w, h;
    gtk_window_get_size(GTK_WINDOW(app.window), &w, &h);
    return w > h;
}

static void line_free(gpointer p)
{
    ChatLine *l = p;
    g_free(l->who);
    g_free(l->text);
    g_free(l);
}

static void set_rgb(cairo_t *cr, const double *c, double a) { cairo_set_source_rgba(cr, c[0], c[1], c[2], a); }

/* The layouts of the bubbles' text and of the names above them, kept until the font changes. */
static PangoLayout *text_layout, *name_layout;

static void layouts_drop_cb(GtkWidget *w, GtkStyle *old, gpointer data)
{
    (void) w, (void) old, (void) data;
    if (text_layout)
        g_object_unref(text_layout), g_object_unref(name_layout);
    text_layout = name_layout = NULL;
}

/* The bubbles from the newest, at the bottom, upwards; returns the height of them all. */
static double draw_lines(GtkWidget *w, cairo_t *cr, int W, int H)
{
    const BoardColors *c = view_colors(app.view);
    if (!text_layout) {
        text_layout = gtk_widget_create_pango_layout(w, NULL);
        pango_layout_set_wrap(text_layout, PANGO_WRAP_WORD_CHAR);
        name_layout = gtk_widget_create_pango_layout(w, NULL);
        PangoFontDescription *fd = pango_font_description_copy(gtk_widget_get_style(w)->font_desc);
        pango_font_description_set_size(fd, pango_font_description_get_size(fd) * 4 / 5);
        pango_layout_set_font_description(name_layout, fd);
        pango_font_description_free(fd);
    }
    PangoLayout *pl = text_layout;
    int lh = line_height();
    double pad = lh * 0.45, gap = lh * 0.35, rad = lh * 0.7, max_w = W * 0.8 - 2 * pad;
    double y = H - gap + scroll, total = gap;
    for (int i = lines ? (int) lines->len - 1 : -1; i >= 0; i--) {
        ChatLine *l = g_ptr_array_index(lines, i);
        ChatLine *prev = i > 0 ? g_ptr_array_index(lines, i - 1) : NULL;
        gboolean name = !l->mine && *l->who && (!prev || prev->mine || strcmp(prev->who, l->who));
        pango_layout_set_width(pl, (int) (max_w * PANGO_SCALE));
        pango_layout_set_text(pl, l->text, -1);
        int tw, th;
        pango_layout_get_pixel_size(pl, &tw, &th);
        double bw = tw + 2 * pad, bh = th + 2 * pad, nh = name ? lh * 1.1 : 0;
        double x = l->mine ? W - gap - bw : gap;
        y -= bh;
        total += bh + nh + gap;
        if (y + bh >= 0 && y - nh <= H) {
            view_rounded(cr, x, y, bw, bh, rad);
            if (l->mine)
                set_rgb(cr, c->last_corners ? c->fg : c->accent, 1);
            else if (c->last_corners)
                cairo_set_source_rgb(cr, c->bg[0] + (c->fg[0] - c->bg[0]) * 0.1, c->bg[1] + (c->fg[1] - c->bg[1]) * 0.1,
                                     c->bg[2] + (c->fg[2] - c->bg[2]) * 0.1);
            else
                view_panel_color(app.view, cr, 0);
            cairo_fill(cr);
            set_rgb(cr, l->mine ? c->bg : c->fg, 1);
            cairo_move_to(cr, x + pad, y + pad);
            pango_cairo_show_layout(cr, pl);
            if (name) {
                pango_layout_set_text(name_layout, l->who, -1);
                set_rgb(cr, c->fg, 0.6);
                cairo_move_to(cr, x + pad, y - nh);
                pango_cairo_show_layout(cr, name_layout);
            }
        }
        y -= nh + gap;
    }
    return total;
}

static double content_h;

static gboolean log_expose_cb(GtkWidget *w, GdkEventExpose *ev, gpointer data)
{
    (void) ev, (void) data;
    GtkAllocation a;
    gtk_widget_get_allocation(w, &a);
    cairo_t *cr = gdk_cairo_create(gtk_widget_get_window(w));
    const BoardColors *c = view_colors(app.view);
    set_rgb(cr, c->bg, 1);
    cairo_paint(cr);
    content_h = draw_lines(w, cr, a.width, a.height);
    cairo_destroy(cr);
    return TRUE;
}

static void scroll_to(double s)
{
    GtkAllocation a;
    gtk_widget_get_allocation(log_area, &a);
    scroll = CLAMP(s, 0, MAX(0, content_h - a.height));
    gtk_widget_queue_draw(log_area);
}

static gboolean log_scroll_cb(GtkWidget *w, GdkEventScroll *ev, gpointer data)
{
    (void) w, (void) data;
    scroll_to(scroll + (ev->direction == GDK_SCROLL_UP ? 3 : ev->direction == GDK_SCROLL_DOWN ? -3 : 0) * line_height());
    return TRUE;
}

/* A drag scrolls, as on touch screens. */
static gboolean log_press_cb(GtkWidget *w, GdkEventButton *ev, gpointer data)
{
    (void) w, (void) data;
    dragging = ev->button == 1, drag_y = ev->y, drag_scroll = scroll;
    keyboard_hide();
    return TRUE;
}

static gboolean log_motion_cb(GtkWidget *w, GdkEventMotion *ev, gpointer data)
{
    (void) w, (void) data;
    if (dragging)
        scroll_to(drag_scroll + ev->y - drag_y);
    return TRUE;
}

static gboolean log_release_cb(GtkWidget *w, GdkEventButton *ev, gpointer data)
{
    (void) w, (void) ev, (void) data;
    dragging = FALSE;
    return TRUE;
}

void chat_add(const char *who, const char *text)
{
    if (!lines)
        lines = g_ptr_array_new_with_free_func(line_free);
    ChatLine *l = g_new0(ChatLine, 1);
    l->who = g_strdup(who), l->text = g_strdup(text);
    l->mine = !strcmp(who, local_name()) || online_is_me(who);
    g_ptr_array_add(lines, l);
    scroll = 0;
    if (log_area)
        gtk_widget_queue_draw(log_area);
    if (!l->mine && !gtk_widget_get_visible(panel)) {
        unread = TRUE;
        view_queue_bar(app.view);
        char *line = *who ? g_strdup_printf("%s: %s", who, text) : g_strdup(text);
        set_move_text(line);
        view_set_notice_item(app.view, VIEW_BAR_CHAT); /* a tap on it opens the chat */
        g_free(line);
    }
}

void chat_clear(void)
{
    if (lines)
        g_ptr_array_set_size(lines, 0);
    scroll = 0;
    if (unread) {
        unread = FALSE;
        view_queue_bar(app.view);
    }
    if (log_area)
        gtk_widget_queue_draw(log_area);
}

static void send_text(const char *text)
{
    if (app.mode == MODE_ONLINE) {
        online_chat(text);
    } else {
        char *line = g_strconcat("CHAT ", text, NULL);
        net_send(app.net, line);
        g_free(line);
        chat_add(local_name(), text);
    }
}

/* ---- quick lines, as Lishogi's: against a person, before the third move and after the game; two at
 * most, each once (ui/round/src/boot.ts getPresetGroup, ui/chat/src/preset.ts) ---- */

static GtkWidget *presets_row;
static int presets_group = -1, presets_ja = -1; /* -1: none shown */
static guint presets_said;

static int presets_group_now(void)
{
    if (!online() || online_vs_ai())
        return -1;
    if (app.game.result != RESULT_NONE)
        return 1;
    return app.game.count < 3 ? 0 : -1;
}

static void preset_cb(GtkWidget *w, gpointer data)
{
    int i = GPOINTER_TO_INT(data);
    const char *preset = CHAT_PRESETS[presets_ja][presets_group][i];
    if (presets_said & (1u << i))
        return;
    presets_said |= 1u << i;
    send_text(strchr(preset, '/') + 1);
    gtk_widget_set_sensitive(w, FALSE);
    chat_presets_update();
}

void chat_presets_update(void)
{
    if (!presets_row)
        return;
    int g = presets_group_now(), ja = i18n_japanese();
    if (g != presets_group || ja != presets_ja) {
        presets_group = g, presets_ja = ja, presets_said = 0;
        GList *kids = gtk_container_get_children(GTK_CONTAINER(presets_row));
        for (GList *l = kids; l; l = l->next)
            gtk_widget_destroy(l->data);
        g_list_free(kids);
        for (int i = 0; g >= 0 && CHAT_PRESETS[ja][g][i]; i++) {
            const char *p = CHAT_PRESETS[ja][g][i];
            char *key = g_ascii_strup(p, strchr(p, '/') - p);
            GtkWidget *b = button(key, G_CALLBACK(preset_cb), GINT_TO_POINTER(i));
            gtk_widget_set_name(b, "denboku-preset");
            gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NORMAL);
            gtk_widget_set_tooltip_text(b, strchr(p, '/') + 1);
            GtkWidget *label = gtk_bin_get_child(GTK_BIN(b));
            if (GTK_IS_LABEL(label)) { /* a strip of small buttons, as Lishogi's, that fits a narrow chat */
                char *small = g_markup_printf_escaped("<small>%s</small>", key);
                gtk_label_set_markup(GTK_LABEL(label), small);
                g_free(small);
            }
            gtk_box_pack_start(GTK_BOX(presets_row), b, TRUE, TRUE, 0);
            gtk_widget_show(b);
            g_free(key);
        }
    }
    gtk_widget_set_visible(presets_row, g >= 0 && __builtin_popcount(presets_said) < 2);
}

static void send_cb(GtkWidget *w, gpointer data)
{
    (void) w, (void) data;
    char *text = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(entry))));
    for (char *c = text; *c; c++)
        if (*c == '\n' || *c == '\r')
            *c = ' ';
    if (*text)
        send_text(text);
    g_free(text);
    gtk_entry_set_text(GTK_ENTRY(entry), "");
#ifdef __ANDROID__
    android_input_reset();
#endif
    /* The Kindle keyboard covers half the screen: after sending, it goes (a tap on the field
     * brings it back). */
    if (app.kindle) {
        keyboard_hide();
        gtk_window_set_focus(GTK_WINDOW(app.window), NULL);
    }
}

static void close_cb(GtkWidget *w, gpointer data)
{
    (void) w, (void) data;
    chat_toggle();
}

static gboolean head_press_cb(GtkWidget *w, GdkEventButton *e, gpointer data)
{
    (void) w, (void) data;
    head_y = e->y_root;
    return FALSE;
}

static gboolean head_release_cb(GtkWidget *w, GdkEventButton *e, gpointer data)
{
    (void) w, (void) data;
    double d = keyboard_covering() ? head_y - e->y_root : e->y_root - head_y;
    if (d > 80 && !landscape_window())
        chat_toggle();
    return FALSE;
}

/* The panel: the drawers' head (‹ and × close it), the lines, the field and Send. It moves between `side`
 * (a row, at its start) and `top` (a column, after its first child) as the window turns. */
void chat_panel_new(GtkWidget *side, GtkWidget *top)
{
    side_box = side, top_box = top;
    panel = drawer_frame_new();
    align = gtk_alignment_new(0, 0, 1, 1);
    gtk_container_add(GTK_CONTAINER(panel), align);
    GtkWidget *inner = gtk_vbox_new(FALSE, 6);
    gtk_container_add(GTK_CONTAINER(align), inner);
    GtkWidget *grip = gtk_event_box_new();
    head = drawer_head_new(tr("Chat"), NULL, G_CALLBACK(close_cb), NULL);
    follow_language(g_object_get_data(G_OBJECT(head), "title"), "Chat");
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(grip), FALSE);
    g_signal_connect(grip, "button-press-event", G_CALLBACK(head_press_cb), NULL);
    g_signal_connect(grip, "button-release-event", G_CALLBACK(head_release_cb), NULL);
    gtk_container_add(GTK_CONTAINER(grip), head);
    gtk_box_pack_start(GTK_BOX(inner), grip, FALSE, FALSE, 0);
    log_area = gtk_drawing_area_new();
    gtk_widget_add_events(log_area, GDK_SCROLL_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                                        GDK_POINTER_MOTION_MASK);
    g_signal_connect(log_area, "expose-event", G_CALLBACK(log_expose_cb), NULL);
    g_signal_connect(log_area, "style-set", G_CALLBACK(layouts_drop_cb), NULL);
    g_signal_connect(log_area, "scroll-event", G_CALLBACK(log_scroll_cb), NULL);
    g_signal_connect(log_area, "button-press-event", G_CALLBACK(log_press_cb), NULL);
    g_signal_connect(log_area, "motion-notify-event", G_CALLBACK(log_motion_cb), NULL);
    g_signal_connect(log_area, "button-release-event", G_CALLBACK(log_release_cb), NULL);
    gtk_box_pack_start(GTK_BOX(inner), log_area, TRUE, TRUE, 0);
    GtkWidget *row = gtk_hbox_new(FALSE, 6);
    entry = text_entry();
    g_signal_handlers_disconnect_matched(entry, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, G_CALLBACK(keyboard_enter_cb), NULL);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), FALSE);
    gtk_entry_set_max_length(GTK_ENTRY(entry), 140);
    g_signal_connect(entry, "activate", G_CALLBACK(send_cb), NULL);
    black_frame(entry);
    gtk_box_pack_start(GTK_BOX(row), entry, TRUE, TRUE, 0);
    GtkWidget *send = button(tr("Send"), G_CALLBACK(send_cb), NULL);
    follow_language(send, "Send");
    gtk_widget_set_name(send, "denboku-chip"); /* as Copy in the lobby: a button, not a word */
    gtk_button_set_relief(GTK_BUTTON(send), GTK_RELIEF_NORMAL);
    gtk_box_pack_start(GTK_BOX(row), send, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(inner), row, FALSE, FALSE, 0);
    presets_row = gtk_hbox_new(TRUE, 2);
    gtk_box_pack_start(GTK_BOX(inner), presets_row, FALSE, FALSE, 0);
    gtk_widget_set_no_show_all(presets_row, TRUE);
    gtk_widget_show_all(panel);
    gtk_widget_set_no_show_all(panel, TRUE);
    gtk_widget_hide(panel);
    chat_presets_update();
    gtk_box_pack_start(GTK_BOX(side), panel, FALSE, FALSE, 0);
}

/* Each change makes GTK size the window again, slow on the Kindle: only when something changed. */
static void move_to(GtkWidget *box, int position, int w, int h)
{
    if (gtk_widget_get_parent(panel) != box) {
        g_object_ref(panel);
        gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(panel)), panel);
        gtk_box_pack_start(GTK_BOX(box), panel, FALSE, FALSE, 0);
        g_object_unref(panel);
    }
    int at;
    gtk_container_child_get(GTK_CONTAINER(box), panel, "position", &at, NULL);
    if (at != position)
        gtk_box_reorder_child(GTK_BOX(box), panel, position);
    int ow, oh;
    gtk_widget_get_size_request(panel, &ow, &oh);
    if (ow != w || oh != h)
        gtk_widget_set_size_request(panel, w, h);
}

/* A button that opens the chat where the bar's is out of reach (the lobby's Chat row): shown only
 * in portrait, where the panel is not already beside the board. */
static GtkWidget *opener;

void chat_opener(GtkWidget *w)
{
    opener = w;
    g_object_add_weak_pointer(G_OBJECT(w), (gpointer *) &opener);
    gtk_widget_set_no_show_all(w, TRUE);
    chat_layout();
}

void chat_layout(void)
{
    if (!panel)
        return;
    gboolean on = online(), land = landscape_window();
    if (opener && gtk_widget_get_visible(opener) == land)
        gtk_widget_set_visible(opener, !land);
    if (!on)
        shown_portrait = hidden_landscape = unread = FALSE;
    if (!on && !gtk_widget_get_visible(panel))
        return; /* no network game, nothing shown: every move would lay it out for nothing */
    int w, h;
    gtk_window_get_size(GTK_WINDOW(app.window), &w, &h);
    int up = keyboard_covering();
#ifdef __ANDROID__
    /* Android's keyboard leaves a shorter window: the chat then takes all of it but the first row,
     * over the board, as above the Kindle's, or the field and a line or two were all that showed. */
    if (!up && !land && android_keyboard())
        up = gdk_screen_height() - 1; /* the view (android.c), current before the window has followed it */
#endif
    gboolean show = on && (land ? !hidden_landscape : shown_portrait);
    /* The text field kept off the screen's bottom edge, more so on a phone's fine pixels. */
    guint u = (guint) (8 * ui_scale()), pl = u, pt = u, pb = 2 * u;
    if (land) {
        /* A fifth of the window (in the screen's density), and what the view leaves beside the
         * board and the column besides. */
        int cw = CLAMP(w / 5, (int) (200 * ui_scale()), (int) (380 * ui_scale()));
        cw += MAX(0, w - cw - view_natural_width(app.view, h) - 16);
        move_to(side_box, 0, cw + pl, -1);
        drawer_frame_edge(panel, EDGE_NONE);
        if (up) /* the field above the Kindle keyboard */
            pb += (guint) MAX(0, h - up);
    } else {
        int at;
        gtk_container_child_get(GTK_CONTAINER(top_box), side_box, "position", &at, NULL);
        /* Over the board while the Kindle keyboard is up: all the space above it, under the first
         * row (the takeback question). */
        GtkAllocation t, f = {0, 0, 0, 0};
        GList *kids = gtk_container_get_children(GTK_CONTAINER(top_box));
        GtkWidget *first = kids ? kids->data : NULL;
        g_list_free(kids);
        if (first && first != panel && gtk_widget_get_visible(first))
            gtk_widget_get_allocation(first, &f);
        gtk_widget_get_allocation(top_box, &t);
        int above = t.y + f.height;
        move_to(top_box, up ? 1 : at + 1, -1, up ? MAX(160, up - above - 4) : CLAMP(h - view_natural_height(app.view, w, h), MAX(160, h / 4), h * 2 / 5));
        drawer_frame_edge(panel, up ? EDGE_BOTTOM : EDGE_TOP);
        pt = up ? u : 18 * u / 8, pb = up ? 18 * u / 8 : 2 * u;
    }
    guint ot, ob, ol, orr;
    gtk_alignment_get_padding(GTK_ALIGNMENT(align), &ot, &ob, &ol, &orr);
    if (ot != pt || ob != pb || ol != pl || orr != u)
        gtk_alignment_set_padding(GTK_ALIGNMENT(align), pt, pb, pl, u);
    view_set_align_left(app.view, show && land);
    if (!up) /* the view, and the unit it gives, is smaller while a keyboard is up */
        drawer_head_fit(head, FALSE);

    /* Under the chat the board would be squeezed behind the keyboard: not drawn there. */
    GtkWidget *board = view_widget(app.view);
    if (gtk_widget_get_child_visible(board) != !(show && up && !land))
        gtk_widget_set_child_visible(board, !(show && up && !land));
    if (show != gtk_widget_get_visible(panel)) {
        if (!show && gtk_widget_has_focus(entry)) { /* the keyboard was its */
            keyboard_hide();
            gtk_window_set_focus(GTK_WINDOW(app.window), NULL);
        }
        gtk_widget_set_visible(panel, show);
        if (show && unread) {
            unread = FALSE;
            update_move_label(); /* the line it showed is in the chat now */
        }
    }
    view_queue_bar(app.view);
}

/* A drawer's grab (the lobby, the game's end) would take the panel's taps as taps outside it: they go
 * to the panel, so that the chat beside them is used then too (お願いします before the game). */
gboolean chat_event(GdkEvent *ev)
{
    GtkWidget *grab = gtk_grab_get_current(), *w;
    if (!panel || !grab || !gtk_widget_get_visible(panel) || !(w = gtk_get_event_widget(ev)))
        return FALSE;
    if ((w != panel && !gtk_widget_is_ancestor(w, panel)) || grab == panel || gtk_widget_is_ancestor(grab, panel))
        return FALSE;
    switch (ev->type) {
    case GDK_ENTER_NOTIFY:
    case GDK_LEAVE_NOTIFY:
        gtk_widget_event(w, ev);
        return TRUE;
    case GDK_BUTTON_PRESS:
    case GDK_2BUTTON_PRESS:
    case GDK_3BUTTON_PRESS:
    case GDK_BUTTON_RELEASE:
    case GDK_MOTION_NOTIFY:
    case GDK_SCROLL:
        gtk_propagate_event(w, ev);
        return TRUE;
    default:
        return FALSE;
    }
}

/* The bar's chat button: opens or closes the drawer in portrait, shows or hides the panel beside
 * the board in landscape (there by default). */
void chat_toggle(void)
{
    gboolean open = !gtk_widget_get_visible(panel);
    chat_presets_update();
    if (landscape_window())
        hidden_landscape = !open;
    else
        shown_portrait = open;
    chat_layout();
    if (open && !app.kindle)
        gtk_widget_grab_focus(entry);
}

int chat_bar_state(void)
{
    if (!panel || !online())
        return BAR_HIDDEN;
    return unread ? BAR_ALERT : BAR_ON;
}
