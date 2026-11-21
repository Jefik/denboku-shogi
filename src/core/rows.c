/* The rows of the drawer's pages, as in the Preferences mockups (mockups/history/2026-11-09-3-
 * preferences): rows in rounded cards under a small group title, each of one kind (a switch,
 * chips, a choice that opens a list, − / +, a section with its summary), drawn in the style of
 * style.c. */

#include "app.h"

static void free_data(gpointer data, GClosure *c)
{
    (void) c;
    g_free(data);
}

/* About how wide the drawer's rows get: the drawer's width less its margins. */
int rows_width(void)
{
    int w, h;
    gtk_window_get_size(GTK_WINDOW(app.window), &w, &h);
    gboolean land = app.kindle ? setting_int("Landscape", 0) != 0 : w > h;
    return drawer_width() - (land ? 40 : 60) * ui_scale();
}

/* How large the text is beside a computer's default (about 19 pixels a line): the icons, switches
 * and other parts drawn in pixels follow it, on phones, on the Kindle and with a larger text size. */
static double text_scale(void) { return MAX(1.0, line_height() / 19.0); }

/* A color between the background and the text, as markup ("#rrggbb"), for secondary text. */
static void dim_hex(char out[8])
{
    const BoardColors *c = view_colors(app.view);
    double t = c->last_corners ? 0.75 : 0.6;
    snprintf(out, 8, "#%02x%02x%02x", (int) ((c->bg[0] + (c->fg[0] - c->bg[0]) * t) * 255),
             (int) ((c->bg[1] + (c->fg[1] - c->bg[1]) * t) * 255), (int) ((c->bg[2] + (c->fg[2] - c->bg[2]) * t) * 255));
}

/* A paragraph as wide as the drawers: a wrapped GTK 2 label asks for no width and wraps narrow. */
GtkWidget *row_para(const char *text)
{
    GtkWidget *l = gtk_label_new(text);
    gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
    gtk_misc_set_alignment(GTK_MISC(l), 0, 0.5);
    gtk_widget_set_size_request(l, rows_width() - 20, -1);
    return l;
}

GtkWidget *row_text(const char *text, gboolean dim, gboolean bold, gboolean small)
{
    GtkWidget *l = gtk_label_new(NULL);
    char hex[8];
    dim_hex(hex);
    char *escaped = g_markup_escape_text(text, -1);
    char *m = g_strdup_printf("<span%s%s%s%s%s>%s</span>", dim ? " foreground=\"" : "", dim ? hex : "", dim ? "\"" : "",
                              bold ? " weight=\"bold\"" : "", small ? " size=\"small\"" : "", escaped);
    g_free(escaped);
    gtk_label_set_markup(GTK_LABEL(l), m);
    g_free(m);
    gtk_misc_set_alignment(GTK_MISC(l), 0, 0.5);
    return l;
}

static void set_mix(cairo_t *cr, double t)
{
    const BoardColors *c = view_colors(app.view);
    cairo_set_source_rgb(cr, c->bg[0] + (c->fg[0] - c->bg[0]) * t, c->bg[1] + (c->fg[1] - c->bg[1]) * t,
                         c->bg[2] + (c->fg[2] - c->bg[2]) * t);
}

/* The card: a rounded panel, lines between its rows (drawn under them). */
static gboolean card_expose_cb(GtkWidget *w, GdkEventExpose *e, gpointer data)
{
    (void) data;
    GtkAllocation a;
    gtk_widget_get_allocation(w, &a);
    cairo_t *cr = gdk_cairo_create(gtk_widget_get_window(w));
    gdk_cairo_region(cr, e->region);
    cairo_clip(cr);
    const BoardColors *c = view_colors(app.view);
    view_rounded(cr, a.x + 0.75, a.y + 0.75, a.width - 1.5, a.height - 1.5, 14);
    set_mix(cr, c->last_corners ? 0 : 0.07); /* black and white: no gray, framed */
    cairo_fill_preserve(cr);
    if (c->last_corners) {
        cairo_set_source_rgb(cr, c->fg[0], c->fg[1], c->fg[2]);
        cairo_set_line_width(cr, 1.5);
        cairo_stroke(cr);
    }
    cairo_new_path(cr);
    GList *kids = gtk_container_get_children(GTK_CONTAINER(gtk_bin_get_child(GTK_BIN(w))));
    gboolean first = TRUE;
    for (GList *l = kids; l; l = l->next) {
        if (!gtk_widget_get_visible(l->data))
            continue;
        if (!first) {
            GtkAllocation r;
            gtk_widget_get_allocation(l->data, &r);
            cairo_rectangle(cr, a.x + 16, r.y - 1, a.width - 32, 1);
        }
        first = FALSE;
    }
    g_list_free(kids);
    set_mix(cr, c->last_corners ? 1 : 0.16);
    cairo_fill(cr);
    cairo_destroy(cr);
    return FALSE;
}

GtkWidget *rows_card(GtkWidget *page)
{
    GtkWidget *card = gtk_event_box_new(), *box = gtk_vbox_new(FALSE, 1);
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(card), FALSE);
    gtk_widget_set_app_paintable(card, TRUE);
    g_signal_connect(card, "expose-event", G_CALLBACK(card_expose_cb), NULL);
    gtk_container_set_border_width(GTK_CONTAINER(box), 4);
    gtk_container_add(GTK_CONTAINER(card), box);
    gtk_box_pack_start(GTK_BOX(page), card, FALSE, FALSE, 4);
    return box;
}

void rows_group(GtkWidget *page, const char *title)
{
    char *upper = g_utf8_strup(title, -1);
    GtkWidget *l = row_text(upper, TRUE, TRUE, TRUE);
    g_free(upper);
    gtk_misc_set_padding(GTK_MISC(l), 6, 0);
    gtk_box_pack_start(GTK_BOX(page), l, FALSE, FALSE, 6);
}

/* A whole row that can be pressed: flat, lit under the mouse and when the keyboard is on it. */
static GtkWidget *flat_button(GtkWidget *child)
{
    GtkWidget *b = gtk_button_new();
    gtk_widget_set_name(b, "denboku-flatrow");
    gtk_container_add(GTK_CONTAINER(b), child);
    return b;
}

GtkWidget *row_add(GtkWidget *card, GtkWidget *row)
{
    gtk_box_pack_start(GTK_BOX(card), row, FALSE, FALSE, 0);
    return row;
}

/* ---- a switch ---- */

static gboolean switch_expose_cb(GtkWidget *w, GdkEventExpose *e, gpointer toggle)
{
    (void) e;
    GtkAllocation a;
    gtk_widget_get_allocation(w, &a);
    cairo_t *cr = gdk_cairo_create(gtk_widget_get_window(w));
    const BoardColors *c = view_colors(app.view);
    gboolean on = gtk_toggle_button_get_active(toggle), off = !gtk_widget_is_sensitive(w);
    double h = MIN(a.height, 28 * text_scale()), sw = h * 1.75, x = a.x + a.width - sw, y = a.y + (a.height - h) / 2;
    view_rounded(cr, x, y, sw, h, h / 2);
    const double *acc = c->last_corners ? c->fg : c->accent;
    if (on)
        cairo_set_source_rgba(cr, acc[0], acc[1], acc[2], off ? 0.4 : 1);
    else
        set_mix(cr, c->last_corners ? 0 : 0.2);
    cairo_fill_preserve(cr);
    if (c->last_corners && !on) {
        cairo_set_source_rgb(cr, c->fg[0], c->fg[1], c->fg[2]);
        cairo_set_line_width(cr, 1.5);
        cairo_stroke(cr);
    }
    cairo_new_path(cr);
    double kx = on ? x + sw - h / 2 : x + h / 2;
    cairo_arc(cr, kx, y + h / 2, h / 2 - 3, 0, 2 * G_PI);
    if (on)
        cairo_set_source_rgb(cr, c->bg[0], c->bg[1], c->bg[2]);
    else
        cairo_set_source_rgb(cr, c->fg[0], c->fg[1], c->fg[2]);
    cairo_fill(cr);
    cairo_destroy(cr);
    return TRUE;
}

static void switch_toggled_cb(GtkToggleButton *b, gpointer area)
{
    (void) b;
    gtk_widget_queue_draw(area);
}

GtkWidget *row_switch(const char *label, gboolean on)
{
    GtkWidget *b = gtk_toggle_button_new(), *row = gtk_hbox_new(FALSE, 12), *area = gtk_drawing_area_new();
    gtk_widget_set_name(b, "denboku-flatrow");
    GtkWidget *l = row_text(label, FALSE, FALSE, FALSE);
    gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
    int sw = (int) (50 * text_scale());
    /* Long ones wrap instead of widening the drawer: the row less the switch, the gap and the frame. */
    gtk_widget_set_size_request(l, rows_width() - sw - 60, -1);
    gtk_box_pack_start(GTK_BOX(row), l, TRUE, TRUE, 0);
    gtk_widget_set_size_request(area, sw, (int) (30 * text_scale()));
    gtk_widget_set_has_window(area, FALSE);
    gtk_box_pack_end(GTK_BOX(row), area, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(b), row);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b), on);
    g_signal_connect(area, "expose-event", G_CALLBACK(switch_expose_cb), b);
    g_signal_connect(b, "toggled", G_CALLBACK(switch_toggled_cb), area);
    return b;
}

/* ---- a choice: its value and ›, opening a list ---- */

GtkWidget *row_choice(const char *label, const char *value, GtkWidget **value_label)
{
    GtkWidget *row = gtk_hbox_new(FALSE, 12), *v = row_text(value, TRUE, FALSE, FALSE);
    gtk_box_pack_start(GTK_BOX(row), row_text(label, FALSE, FALSE, FALSE), FALSE, FALSE, 0);
    gtk_misc_set_alignment(GTK_MISC(v), 1, 0.5);
    gtk_label_set_ellipsize(GTK_LABEL(v), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(row), v, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(row), row_text("›", TRUE, TRUE, FALSE), FALSE, FALSE, 0);
    if (value_label)
        *value_label = v;
    return flat_button(row);
}

void row_set_value(GtkWidget *value_label, const char *value)
{
    char hex[8];
    dim_hex(hex);
    char *m = g_markup_printf_escaped("<span foreground=\"%s\">%s</span>", hex, value);
    gtk_label_set_markup(GTK_LABEL(value_label), m);
    g_free(m);
}

/* ---- a section: an icon, its name and what is chosen in it ---- */

static gboolean icon_expose_cb(GtkWidget *w, GdkEventExpose *e, gpointer data)
{
    (void) e;
    GtkAllocation a;
    gtk_widget_get_allocation(w, &a);
    cairo_t *cr = gdk_cairo_create(gtk_widget_get_window(w));
    double s = MIN(a.width, a.height);
    view_rounded(cr, a.x + (a.width - s) / 2, a.y + (a.height - s) / 2, s, s, s * 0.28);
    const BoardColors *c = view_colors(app.view);
    set_mix(cr, c->last_corners ? 0 : 0.16);
    if (c->last_corners) { /* black and white: framed, no gray */
        cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, c->fg[0], c->fg[1], c->fg[2]);
        cairo_set_line_width(cr, 1.5);
        cairo_stroke(cr);
    } else {
        cairo_fill(cr);
    }
    PangoLayout *pl = gtk_widget_create_pango_layout(w, data);
    PangoFontDescription *fd = pango_font_description_from_string("Denboku Sans, Sans");
    pango_font_description_set_absolute_size(fd, s * 0.5 * PANGO_SCALE);
    pango_layout_set_font_description(pl, fd);
    /* Every glyph's ink as large as the others (♟ and ⭯ draw smaller than 王 at one size). */
    PangoRectangle ink;
    pango_layout_get_pixel_extents(pl, &ink, NULL);
    int big = MAX(ink.width, ink.height);
    if (big > 0) {
        pango_font_description_set_absolute_size(fd, s * 0.5 * 0.46 * s / big * PANGO_SCALE);
        pango_layout_set_font_description(pl, fd);
        pango_layout_get_pixel_extents(pl, &ink, NULL);
    }
    pango_font_description_free(fd);
    cairo_set_source_rgb(cr, c->fg[0], c->fg[1], c->fg[2]);
    cairo_move_to(cr, a.x + (a.width - ink.width) / 2.0 - ink.x, a.y + (a.height - ink.height) / 2.0 - ink.y);
    pango_cairo_show_layout(cr, pl);
    g_object_unref(pl);
    cairo_destroy(cr);
    return TRUE;
}

GtkWidget *row_section(const char *icon, const char *title, const char *summary)
{
    GtkWidget *row = gtk_hbox_new(FALSE, 14), *area = gtk_drawing_area_new(), *texts = gtk_vbox_new(TRUE, 2);
    gtk_widget_set_has_window(area, FALSE);
    int side = (int) (38 * text_scale());
    gtk_widget_set_size_request(area, side, side);
    g_signal_connect_data(area, "expose-event", G_CALLBACK(icon_expose_cb), g_strdup(icon), free_data, 0);
    gtk_box_pack_start(GTK_BOX(row), area, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(texts), row_text(title, FALSE, TRUE, FALSE), FALSE, FALSE, 0);
    if (*summary) {
        GtkWidget *sub = row_text(summary, TRUE, FALSE, TRUE);
        gtk_label_set_ellipsize(GTK_LABEL(sub), PANGO_ELLIPSIZE_END);
        gtk_box_pack_start(GTK_BOX(texts), sub, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(row), texts, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(row), row_text("›", TRUE, TRUE, FALSE), FALSE, FALSE, 0);
    return flat_button(row);
}

/* ---- chips: a few short choices ---- */

static void chip_cb(GtkToggleButton *b, gpointer data)
{
    if (!gtk_toggle_button_get_active(b))
        return;
    void (*cb)(int, gpointer) = g_object_get_data(G_OBJECT(b), "cb");
    cb(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "index")), data);
}

/* Puts `chip` at the end of the last line of chips in `box`, or on a line of its own when it would
 * make that line wider than `width`. Measured as GTK draws it, so the lines are as full as fits. */
void chip_pack(GtkWidget *box, GtkWidget *chip, int width)
{
    GList *kids = gtk_container_get_children(GTK_CONTAINER(box)), *last = g_list_last(kids);
    GtkWidget *line = last && g_object_get_data(last->data, "chip-line") ? last->data : NULL;
    g_list_free(kids);
    gtk_widget_show(chip);
    g_object_ref_sink(chip);
    if (line) {
        gtk_box_pack_start(GTK_BOX(line), chip, FALSE, FALSE, 0);
        GtkRequisition r;
        gtk_widget_size_request(line, &r);
        if (r.width > width)
            gtk_container_remove(GTK_CONTAINER(line), chip);
    }
    if (!gtk_widget_get_parent(chip)) {
        line = gtk_hbox_new(FALSE, 6);
        g_object_set_data(G_OBJECT(line), "chip-line", line);
        gtk_widget_show(line);
        gtk_box_pack_start(GTK_BOX(box), line, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(line), chip, FALSE, FALSE, 0);
    }
    g_object_unref(chip);
}

/* Once in the window the chips have their final style (the drawer's font): their lines again, by it. */
static void chips_anchored_cb(GtkWidget *box, GtkWidget *old_top, gpointer data)
{
    (void) old_top, (void) data;
    if (!GTK_WIDGET_TOPLEVEL(gtk_widget_get_toplevel(box)) || g_object_get_data(G_OBJECT(box), "chips-anchored"))
        return;
    g_object_set_data(G_OBJECT(box), "chips-anchored", box);
    GList *chips = NULL, *lines = gtk_container_get_children(GTK_CONTAINER(box));
    for (GList *l = lines; l; l = l->next) {
        if (!g_object_get_data(l->data, "chip-line"))
            continue;
        GList *kids = gtk_container_get_children(GTK_CONTAINER(l->data));
        for (GList *k = kids; k; k = k->next) {
            chips = g_list_append(chips, g_object_ref(k->data));
            gtk_container_remove(GTK_CONTAINER(l->data), k->data);
        }
        g_list_free(kids);
        gtk_widget_destroy(l->data);
    }
    g_list_free(lines);
    int width = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(box), "chips-width"));
    for (GList *c = chips; c; c = c->next) {
        chip_pack(box, c->data, width);
        g_object_unref(c->data);
    }
    g_list_free(chips);
}

/* The chips in lines about as wide as `width`; `cb(index, data)` when one is chosen. */
GtkWidget *row_chips(const char *label, const char *const *items, int n, int active, int width,
                     void (*cb)(int, gpointer), gpointer data)
{
    GtkWidget *box = gtk_vbox_new(FALSE, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 8);
    if (label)
        gtk_box_pack_start(GTK_BOX(box), row_text(label, TRUE, FALSE, TRUE), FALSE, FALSE, 0);
    GSList *group = NULL;
    for (int i = 0; i < n; i++) {
        GtkWidget *chip = gtk_radio_button_new_with_label(group, items[i]);
        group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(chip));
        gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(chip), FALSE);
        gtk_widget_set_name(chip, "denboku-chip");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chip), i == active);
        g_object_set_data(G_OBJECT(chip), "index", GINT_TO_POINTER(i));
        g_object_set_data(G_OBJECT(chip), "cb", cb);
        g_signal_connect(chip, "toggled", G_CALLBACK(chip_cb), data);
        chip_pack(box, chip, width - 16);
    }
    g_object_set_data(G_OBJECT(box), "chips-width", GINT_TO_POINTER(width - 16));
    g_object_set_data(G_OBJECT(box), "chips", group);
    g_signal_connect(box, "hierarchy-changed", G_CALLBACK(chips_anchored_cb), NULL);
    return box;
}

void chips_step(GtkWidget *chips, int step)
{
    GSList *group = g_object_get_data(G_OBJECT(chips), "chips"); /* the last chip first */
    int n = (int) g_slist_length(group), active = 0;
    for (GSList *c = group; c; c = c->next)
        if (gtk_toggle_button_get_active(c->data))
            active = GPOINTER_TO_INT(g_object_get_data(c->data, "index"));
    GtkWidget *chip = g_slist_nth_data(group, (guint) (n - 1 - (active + step + n) % n));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chip), TRUE);
    gtk_widget_grab_focus(chip);
}

/* ---- − value + ---- */

GtkWidget *row_stepper(const char *label, GtkWidget **value, GtkWidget **minus, GtkWidget **plus)
{
    GtkWidget *row = gtk_hbox_new(FALSE, 8);
    gtk_container_set_border_width(GTK_CONTAINER(row), 6);
    gtk_box_pack_start(GTK_BOX(row), row_text(label, FALSE, FALSE, FALSE), TRUE, TRUE, 6);
    *minus = gtk_button_new_with_label("−"), *plus = gtk_button_new_with_label("+");
    *value = gtk_label_new(NULL);
    gtk_widget_set_size_request(*value, (int) (64 * text_scale()), -1);
    gtk_widget_set_name(*minus, "denboku-chip");
    gtk_widget_set_name(*plus, "denboku-chip");
    gtk_box_pack_start(GTK_BOX(row), *minus, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), *value, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), *plus, FALSE, FALSE, 0);
    return row;
}

/* ---- a list to choose from, in a drawer over this one ---- */

static void pick_cb(GtkToggleButton *b, gpointer d)
{
    if (!gtk_toggle_button_get_active(b) || g_object_get_data(G_OBJECT(d), "filling"))
        return;
    void (*picked)(int, gpointer) = g_object_get_data(G_OBJECT(d), "picked");
    int i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "index"));
    g_object_set_data(G_OBJECT(d), "chosen", GINT_TO_POINTER(i + 1));
    picked(i, g_object_get_data(G_OBJECT(d), "picked-data"));
}

/* Each choice counts at once (`picked(index, data)`), so that it shows; Confirm (or a tap outside the
 * drawer) keeps the last one, and Cancel, back or × goes back to `active`. */
int rows_pick(const char *title, const char *const *items, GdkPixbuf *const *pictures, int n, int active,
              void (*picked)(int, gpointer), gpointer data)
{
    GtkWidget *d = dialog_new(title);
    if (prefs_section_page(title))
        g_object_set_data(G_OBJECT(d), "page-step", GINT_TO_POINTER(TRUE));
    drawer_set_back(d);
    drawer_set_beside(d); /* where Preferences' pages are, the board in sight as the choice shows */
    drawer_set_outside_ok(d);
    follow_language(gtk_dialog_add_button(GTK_DIALOG(d), tr("Cancel"), GTK_RESPONSE_CANCEL), "Cancel");
    follow_language(gtk_dialog_add_button(GTK_DIALOG(d), tr("Confirm"), GTK_RESPONSE_OK), "Confirm"); /* Language */
    GtkWidget *list = gtk_vbox_new(FALSE, 4);
    GSList *group = NULL;
    g_object_set_data(G_OBJECT(d), "filling", GINT_TO_POINTER(1));
    g_object_set_data(G_OBJECT(d), "picked", picked);
    g_object_set_data(G_OBJECT(d), "picked-data", data);
    for (int i = 0; i < n; i++) {
        GtkWidget *r = gtk_radio_button_new(group);
        group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(r));
        gtk_widget_set_name(r, "denboku-row");
        gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(r), FALSE); /* a card (style.c), no dot at its side */
        GtkWidget *inner = gtk_vbox_new(FALSE, 4);
        /* Without a picture the text fills the card's height, so it sits in the middle. */
        gtk_box_pack_start(GTK_BOX(inner), row_text(items[i], FALSE, FALSE, FALSE), !pictures || !pictures[i], TRUE, 0);
        if (pictures && pictures[i]) {
            GtkWidget *img = gtk_image_new_from_pixbuf(pictures[i]);
            gtk_misc_set_alignment(GTK_MISC(img), 0, 0.5);
            gtk_box_pack_start(GTK_BOX(inner), img, FALSE, FALSE, 0);
        }
        gtk_container_add(GTK_CONTAINER(r), inner);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(r), i == active);
        g_object_set_data(G_OBJECT(r), "index", GINT_TO_POINTER(i));
        g_signal_connect(r, "toggled", G_CALLBACK(pick_cb), d);
        gtk_box_pack_start(GTK_BOX(list), r, FALSE, FALSE, 0);
    }
    g_object_set_data(G_OBJECT(d), "filling", NULL);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), list, FALSE, FALSE, 0);
    gtk_widget_show_all(d);
    int r = gtk_dialog_run(GTK_DIALOG(d));
    int chosen = r == GTK_RESPONSE_OK ? -1 : GPOINTER_TO_INT(g_object_get_data(G_OBJECT(d), "chosen")) - 1;
    gtk_widget_destroy(d);
    if (chosen >= 0 && chosen != active)
        picked(active, data);
    return r;
}
