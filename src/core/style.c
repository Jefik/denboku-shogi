/* The widgets drawn as the game's view is: flat rounded buttons, fields, tabs and check boxes
 * instead of the bevels of GTK 2's default look. The default style's drawing functions are
 * replaced (a theme engine of the system's would keep its own); what is not listed here is
 * drawn as before. */

#include "app.h"

static GtkStyleClass parent;
static gboolean framed; /* black and white screens: buttons get an outline, fills are faint */
static double accent[3] = {0.2, 0.47, 0.78}, on_accent[3] = {1, 1, 1};

static gboolean is(const gchar *detail, const char *name) { return detail && !strcmp(detail, name); }

static cairo_t *begin(GdkWindow *window, GdkRectangle *area)
{
    cairo_t *cr = gdk_cairo_create(window);
    if (area) {
        gdk_cairo_rectangle(cr, area);
        cairo_clip(cr);
    }
    return cr;
}

/* The background mixed with the text color by `t`. */
static void mix(cairo_t *cr, GtkStyle *s, double t, double alpha)
{
    GdkColor b = s->bg[GTK_STATE_NORMAL], f = s->fg[GTK_STATE_NORMAL];
    cairo_set_source_rgba(cr, (b.red + (f.red - (double) b.red) * t) / 65535, (b.green + (f.green - (double) b.green) * t) / 65535,
                          (b.blue + (f.blue - (double) b.blue) * t) / 65535, alpha);
}

static double state_mix(GtkStateType state)
{
    double k = framed ? 0.5 : 1;
    switch (state) {
    case GTK_STATE_PRELIGHT: return 0.17 * k;
    case GTK_STATE_ACTIVE: return 0.26 * k;
    case GTK_STATE_INSENSITIVE: return framed ? 0 : 0.05; /* black and white: the background, framed */
    default: return framed ? 0 : 0.11;
    }
}

static void outline(cairo_t *cr, GtkStyle *s, double x, double y, double w, double h, double r, double t)
{
    view_rounded(cr, x + 0.5, y + 0.5, w - 1, h - 1, r);
    mix(cr, s, framed ? 1 : t, 1);
    cairo_set_line_width(cr, framed ? 1.5 : 1);
    cairo_stroke(cr);
}

static void fix_size(GdkWindow *window, gint *w, gint *h)
{
    if (*w < 0 || *h < 0) {
        gint ww, wh;
        gdk_drawable_get_size(window, &ww, &wh);
        if (*w < 0)
            *w = ww;
        if (*h < 0)
            *h = wh;
    }
}

static gboolean named(GtkWidget *w, const char *name) { return w && !strcmp(gtk_widget_get_name(w), name); }

static int row_min, chip_min; /* a finger's height, as large as the text (style_follow_text) */

/* The rows and buttons of the drawers and of the Sandbox's bar (inside a widget marked "finger-tall")
 * at least a finger tall, chips a little less and at least as wide (− and + round): GTK 2 asks only for
 * the text's height, thin to tap on a phone or the Kindle. */
static gboolean tall_hook(GSignalInvocationHint *hint, guint n, const GValue *values, gpointer data)
{
    (void) hint, (void) n, (void) data;
    GtkWidget *w = g_value_get_object(values);
    if (!GTK_IS_BUTTON(w) || gtk_button_get_relief(GTK_BUTTON(w)) == GTK_RELIEF_NONE ||
        (GTK_IS_CHECK_BUTTON(w) && gtk_toggle_button_get_mode(GTK_TOGGLE_BUTTON(w))))
        return TRUE;
    GtkWidget *a = w;
    while (a && !g_object_get_data(G_OBJECT(a), "finger-tall"))
        a = gtk_widget_get_parent(a);
    if (!a)
        return TRUE;
    GtkRequisition *r = g_value_get_boxed(values + 1);
    gboolean chip = named(w, "denboku-chip");
    r->height = MAX(r->height, chip || named(w, "denboku-preset") ? chip_min : row_min);
    if (chip)
        r->width = MAX(r->width, chip_min);
    return TRUE;
}

/* Filled with the accent: the button that confirms (drawer.c), and the chosen chip. */
static gboolean accented(GtkWidget *w)
{
    return named(w, "denboku-primary") ||
           (named(w, "denboku-chip") && GTK_IS_TOGGLE_BUTTON(w) && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w)));
}

static void draw_box(GtkStyle *s, GdkWindow *window, GtkStateType state, GtkShadowType shadow, GdkRectangle *area,
                     GtkWidget *widget, const gchar *detail, gint x, gint y, gint w, gint h)
{
    gboolean button = is(detail, "button") || is(detail, "optionmenu");
    gboolean trough = is(detail, "trough"), bar = is(detail, "bar") || is(detail, "slider");
    gboolean spin = detail && g_str_has_prefix(detail, "spinbutton");
    if (is(detail, "buttondefault"))
        return;
    if (!button && !trough && !bar && !spin && !is(detail, "menuitem")) {
        parent.draw_box(s, window, state, shadow, area, widget, detail, x, y, w, h);
        return;
    }
    fix_size(window, &w, &h);
    cairo_t *cr = begin(window, area);
    double r = MIN(10, MIN(w, h) / 2.0);
    if (is(detail, "menuitem")) {
        view_rounded(cr, x + 2, y + 1, w - 4, h - 2, 6);
        gdk_cairo_set_source_color(cr, &s->bg[GTK_STATE_PRELIGHT]);
        cairo_fill(cr);
    } else if (trough) {
        /* Scales get a thin rail; scroll bars and progress bars the whole box. */
        gboolean scale = widget && GTK_IS_SCALE(widget);
        double t = scale ? MIN(w, h) * 0.25 : 0;
        gboolean horizontal = w >= h;
        double tx = x + (horizontal ? 0 : (w - MAX(4, w - 2 * t)) / 2), ty = y + (horizontal ? (h - MAX(4, h - 2 * t)) / 2 : 0);
        double tw = horizontal ? w : MAX(4, w - 2 * t), th = horizontal ? MAX(4, h - 2 * t) : h;
        view_rounded(cr, tx, ty, tw, th, MIN(tw, th) / 2);
        mix(cr, s, framed ? 1 : 0.1, 1); /* black and white: an outline, no gray */
        if (framed) {
            cairo_set_line_width(cr, 1);
            cairo_stroke(cr);
        } else {
            cairo_fill(cr);
        }
    } else if (bar) {
        view_rounded(cr, x, y, w, h, MIN(w, h) / 2.0);
        gdk_cairo_set_source_color(cr, &s->bg[GTK_STATE_SELECTED]);
        if (is(detail, "slider"))
            mix(cr, s, state == GTK_STATE_PRELIGHT || state == GTK_STATE_ACTIVE ? 0.5 : 0.35, 1);
        cairo_fill(cr);
    } else if (named(widget, "denboku-row")) {
        /* A choice of a list (rows.c, drawer.c): a card; the chosen one framed in the accent, with
         * a check at its corner (solid black on e-ink: no half-tones). */
        gboolean on = GTK_IS_TOGGLE_BUTTON(widget) && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
        double lw = on ? 3 : framed ? 1.5 : 1;
        view_rounded(cr, x + lw / 2, y + lw / 2, w - lw, h - lw, 12);
        mix(cr, s, framed ? 0 : state == GTK_STATE_PRELIGHT ? 0.1 : 0.06, 1);
        cairo_fill_preserve(cr);
        if (on)
            cairo_set_source_rgb(cr, accent[0], accent[1], accent[2]);
        else
            mix(cr, s, framed ? 1 : 0.2, 1);
        cairo_set_line_width(cr, lw);
        cairo_stroke(cr);
        if (on) {
            double br = MIN(MAX(11, line_height() * 0.5), h / 3.0), bx = x + w - br - 12, by = y + br + 10;
            if (h < 4 * br + 16)
                by = y + h / 2.0;
            cairo_arc(cr, bx, by, br, 0, 2 * G_PI);
            cairo_fill(cr);
            cairo_set_source_rgb(cr, on_accent[0], on_accent[1], on_accent[2]);
            cairo_set_line_width(cr, MAX(2, br * 0.25));
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
            cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
            cairo_move_to(cr, bx - br * 0.5, by);
            cairo_line_to(cr, bx - br * 0.12, by + br * 0.4);
            cairo_line_to(cr, bx + br * 0.55, by - br * 0.4);
            cairo_stroke(cr);
        }
    } else if (named(widget, "denboku-flatrow")) {
        /* A row of a drawer page (rows.c): flat, lit only under the pointer. */
        if ((state == GTK_STATE_PRELIGHT && !TOUCH_ONLY) || state == GTK_STATE_SELECTED) {
            view_rounded(cr, x + 1, y + 1, w - 2, h - 2, 10);
            mix(cr, s, framed ? 1 : 0.08, 1); /* black and white: a line, no gray */
            if (framed) {
                cairo_set_line_width(cr, 2);
                cairo_stroke(cr);
            } else {
                cairo_fill(cr);
            }
        }
    } else if (accented(widget)) {
        view_rounded(cr, x, y, w, h, named(widget, "denboku-chip") ? h / 2.0 : r);
        cairo_set_source_rgba(cr, accent[0], accent[1], accent[2], state == GTK_STATE_INSENSITIVE ? 0.4 : 1);
        cairo_fill_preserve(cr);
        if (state == GTK_STATE_PRELIGHT) {
            cairo_set_source_rgba(cr, on_accent[0], on_accent[1], on_accent[2], 0.15);
            cairo_fill(cr);
        }
        cairo_new_path(cr);
    } else {
        gboolean chip = named(widget, "denboku-chip");
        view_rounded(cr, x, y, w, h, spin ? 4 : chip ? h / 2.0 : r);
        mix(cr, s, state_mix(state == GTK_STATE_ACTIVE && chip ? GTK_STATE_NORMAL : state), 1);
        cairo_fill(cr);
        if (framed && !spin)
            outline(cr, s, x, y, w, h, chip ? h / 2.0 : r, 1);
    }
    cairo_destroy(cr);
}

/* Fields and framed boxes: a thin rounded line. */
static void draw_shadow(GtkStyle *s, GdkWindow *window, GtkStateType state, GtkShadowType shadow, GdkRectangle *area,
                        GtkWidget *widget, const gchar *detail, gint x, gint y, gint w, gint h)
{
    if (shadow == GTK_SHADOW_NONE || !(is(detail, "entry") || is(detail, "frame") || is(detail, "scrolled_window"))) {
        parent.draw_shadow(s, window, state, shadow, area, widget, detail, x, y, w, h);
        return;
    }
    fix_size(window, &w, &h);
    cairo_t *cr = begin(window, area);
    gboolean focus = is(detail, "entry") && widget && gtk_widget_has_focus(widget);
    if (is(detail, "entry")) {
        /* The field's corners outside the rounded line take the background's color. */
        cairo_rectangle(cr, x, y, w, h);
        view_rounded(cr, x, y, w, h, 6);
        cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
        GtkWidget *p = widget ? gtk_widget_get_parent(widget) : NULL;
        gdk_cairo_set_source_color(cr, &(p ? gtk_widget_get_style(p) : s)->bg[GTK_STATE_NORMAL]);
        cairo_fill(cr);
    }
    outline(cr, s, x, y, w, h, 6, focus ? 0.6 : 0.25);
    cairo_destroy(cr);
}

/* Notebook tabs as pills: the current one filled. */
static void draw_extension(GtkStyle *s, GdkWindow *window, GtkStateType state, GtkShadowType shadow, GdkRectangle *area,
                           GtkWidget *widget, const gchar *detail, gint x, gint y, gint w, gint h, GtkPositionType gap)
{
    (void) shadow, (void) widget, (void) detail, (void) gap;
    fix_size(window, &w, &h);
    cairo_t *cr = begin(window, area);
    view_rounded(cr, x + 1, y + 2, w - 2, h - 4, (h - 4) / 2.0);
    mix(cr, s, state == GTK_STATE_NORMAL ? (framed ? 0.2 : 0.24) : framed ? 0.05 : 0.07, 1);
    cairo_fill_preserve(cr);
    if (framed && state == GTK_STATE_NORMAL) {
        mix(cr, s, 1, 1);
        cairo_set_line_width(cr, 1.5);
        cairo_stroke(cr);
    }
    cairo_new_path(cr);
    cairo_destroy(cr);
}

/* The notebook's page: a rounded panel with no line where the tabs meet it. */
static void draw_box_gap(GtkStyle *s, GdkWindow *window, GtkStateType state, GtkShadowType shadow, GdkRectangle *area,
                         GtkWidget *widget, const gchar *detail, gint x, gint y, gint w, gint h, GtkPositionType side,
                         gint gap_x, gint gap_w)
{
    (void) state, (void) shadow, (void) widget, (void) detail, (void) side, (void) gap_x, (void) gap_w;
    fix_size(window, &w, &h);
    cairo_t *cr = begin(window, area);
    view_rounded(cr, x, y, w, h, 10);
    mix(cr, s, framed ? 0 : 0.04, 1);
    cairo_fill_preserve(cr);
    mix(cr, s, framed ? 1 : 0.15, 1);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    cairo_destroy(cr);
}

static void draw_check(GtkStyle *s, GdkWindow *window, GtkStateType state, GtkShadowType shadow, GdkRectangle *area,
                       GtkWidget *widget, const gchar *detail, gint x, gint y, gint w, gint h)
{
    (void) widget, (void) detail;
    cairo_t *cr = begin(window, area);
    gboolean on = shadow == GTK_SHADOW_IN, off = state == GTK_STATE_INSENSITIVE;
    double sz = MIN(w, h) + 2, bx = x + (w - sz) / 2, by = y + (h - sz) / 2;
    view_rounded(cr, bx, by, sz, sz, 4);
    if (on)
        mix(cr, s, framed ? 1 : 0.85, off ? 0.4 : 1);
    else
        mix(cr, s, state_mix(state), 1);
    cairo_fill(cr);
    if (!on && framed)
        outline(cr, s, bx, by, sz, sz, 4, 1);
    if (on) {
        mix(cr, s, 0, 1);
        cairo_set_line_width(cr, MAX(1.5, sz * 0.14));
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        cairo_move_to(cr, bx + sz * 0.25, by + sz * 0.52);
        cairo_line_to(cr, bx + sz * 0.43, by + sz * 0.7);
        cairo_line_to(cr, bx + sz * 0.76, by + sz * 0.32);
        cairo_stroke(cr);
    }
    cairo_destroy(cr);
}

static void draw_option(GtkStyle *s, GdkWindow *window, GtkStateType state, GtkShadowType shadow, GdkRectangle *area,
                        GtkWidget *widget, const gchar *detail, gint x, gint y, gint w, gint h)
{
    (void) widget, (void) detail;
    cairo_t *cr = begin(window, area);
    gboolean on = shadow == GTK_SHADOW_IN;
    double r = (MIN(w, h) + 2) / 2.0, cx = x + w / 2.0, cy = y + h / 2.0;
    cairo_arc(cr, cx, cy, r, 0, 2 * G_PI);
    mix(cr, s, on ? (framed ? 1 : 0.85) : state_mix(state), 1);
    cairo_fill(cr);
    if (!on && framed) {
        cairo_arc(cr, cx, cy, r - 0.75, 0, 2 * G_PI);
        mix(cr, s, 1, 1);
        cairo_set_line_width(cr, 1.5);
        cairo_stroke(cr);
    }
    if (on) {
        cairo_arc(cr, cx, cy, r * 0.4, 0, 2 * G_PI);
        mix(cr, s, 0, 1);
        cairo_fill(cr);
    }
    cairo_destroy(cr);
}

/* A scale's knob round, a scroll bar's thumb a pill. */
static void draw_slider(GtkStyle *s, GdkWindow *window, GtkStateType state, GtkShadowType shadow, GdkRectangle *area,
                        GtkWidget *widget, const gchar *detail, gint x, gint y, gint w, gint h, GtkOrientation o)
{
    (void) shadow, (void) o;
    cairo_t *cr = begin(window, area);
    if (widget && GTK_IS_SCALE(widget)) {
        double r = MIN(w, h) / 2.0;
        cairo_arc(cr, x + w / 2.0, y + h / 2.0, r, 0, 2 * G_PI);
        mix(cr, s, state == GTK_STATE_PRELIGHT || state == GTK_STATE_ACTIVE ? 1 : 0.8, 1);
    } else {
        view_rounded(cr, x + 2, y + 2, w - 4, h - 4, MIN(w, h) / 2.0);
        mix(cr, s, state == GTK_STATE_PRELIGHT || state == GTK_STATE_ACTIVE ? 0.5 : 0.35, 1);
    }
    (void) detail;
    cairo_fill(cr);
    cairo_destroy(cr);
}

static void draw_focus(GtkStyle *s, GdkWindow *window, GtkStateType state, GdkRectangle *area, GtkWidget *widget,
                       const gchar *detail, gint x, gint y, gint w, gint h)
{
    (void) state, (void) detail;
    if (controls_pointer())
        return;
    fix_size(window, &w, &h);
    cairo_t *cr = begin(window, area);
    /* Seen on a chosen chip too, which has the accent's color (black, in black and white). */
    GtkWidget *b = !widget || GTK_IS_BUTTON(widget) ? widget : gtk_widget_get_ancestor(widget, GTK_TYPE_BUTTON);
    view_rounded(cr, x + 1, y + 1, w - 2, h - 2, 8);
    if (b && accented(b))
        cairo_set_source_rgb(cr, on_accent[0], on_accent[1], on_accent[2]);
    else
        mix(cr, s, 0.9, 1);
    cairo_set_line_width(cr, 2);
    cairo_stroke(cr);
    cairo_destroy(cr);
}

/* Disabled text once, dimmed (the default drew it twice, as if embossed). */
static void draw_layout(GtkStyle *s, GdkWindow *window, GtkStateType state, gboolean use_text, GdkRectangle *area,
                        GtkWidget *widget, const gchar *detail, gint x, gint y, PangoLayout *layout)
{
    (void) detail;
    GdkGC *gc = use_text ? s->text_gc[state] : s->fg_gc[state];
    if (area)
        gdk_gc_set_clip_rectangle(gc, area);
    GtkWidget *b = widget ? gtk_widget_get_ancestor(widget, GTK_TYPE_BUTTON) : NULL;
    if (b && accented(b)) {
        GdkColor c = {0, (guint16) (on_accent[0] * 65535), (guint16) (on_accent[1] * 65535), (guint16) (on_accent[2] * 65535)};
        gdk_draw_layout_with_colors(window, gc, x, y, layout, &c, NULL);
    } else {
        gdk_draw_layout(window, gc, x, y, layout);
    }
    if (area)
        gdk_gc_set_clip_rectangle(gc, NULL);
}

void style_install(void)
{
    GtkStyleClass *c = g_type_class_ref(GTK_TYPE_STYLE);
    parent = *c;
    c->draw_box = draw_box;
    c->draw_shadow = draw_shadow;
    c->draw_extension = draw_extension;
    c->draw_box_gap = draw_box_gap;
    c->draw_check = draw_check;
    c->draw_option = draw_option;
    c->draw_slider = draw_slider;
    c->draw_focus = draw_focus;
    c->draw_layout = draw_layout;
    g_signal_add_emission_hook(g_signal_lookup("size-request", GTK_TYPE_WIDGET), 0, tall_hook, NULL, NULL);
    /* Room for the rounded corners, tabs that are not glued together, and the text of a pressed
     * button (a chosen chip) where it was. */
    gtk_rc_parse_string("style \"denboku-round\" { GtkButton::inner-border = { 10, 10, 4, 4 }\n"
                        "  GtkNotebook::tab-overlap = -4 GtkNotebook::tab-curvature = 0\n"
                        "  GtkButton::default-border = { 0, 0, 0, 0 } GtkButton::default-outside-border = { 0, 0, 0, 0 }\n"
                        "  GtkButton::child-displacement-x = 0 GtkButton::child-displacement-y = 0\n"
                        "  GtkEntry::inner-border = { 6, 6, 3, 3 } GtkScale::slider-length = 18\n"
                        "  GtkRange::trough-border = 0 GtkScrollbar::min-slider-length = 32\n"
                        "  GtkScrollbar::has-backward-stepper = 0 GtkScrollbar::has-forward-stepper = 0 }\n"
                        "widget_class \"*\" style \"denboku-round\"\n"
                        "style \"denboku-tabs\" { xthickness = 10 ythickness = 5 }\n"
                        "widget_class \"*GtkNotebook\" style \"denboku-tabs\"\n"
                        "style \"denboku-chip\" { GtkButton::inner-border = { 14, 14, 6, 6 } }\n"
                        "widget \"*.denboku-chip\" style \"denboku-chip\"\n"
                        "style \"denboku-row\" { GtkButton::inner-border = { 14, 40, 5, 5 } }\n"
                        "widget \"*.denboku-row\" style \"denboku-row\"\n"
                        "style \"denboku-primary\" { GtkButton::inner-border = { 14, 14, 8, 8 } }\n"
                        "widget \"*.denboku-primary\" style \"denboku-primary\"\n"
                        "style \"denboku-wide\" { GtkButton::inner-border = { 14, 14, 8, 8 } }\n"
                        "widget \"*.denboku-wide\" style \"denboku-wide\"\n"
                        "style \"denboku-preset\" { GtkButton::inner-border = { 2, 2, 4, 4 } }\n"
                        "widget \"*.denboku-preset\" style \"denboku-preset\"\n");
}

/* Check boxes, and the room for the chosen card's check, as large as the text: GTK takes these in
 * pixels, which on the Kindle, on a phone or with a larger text size left them tiny. */
void style_follow_text(void)
{
    static int was;
    int lh = line_height();
    if (lh == was)
        return;
    was = lh;
    double ts = MAX(1.0, lh / 19.0);
    row_min = (int) (48 * ts), chip_min = (int) (42 * ts);
    if (app.kindle) /* its large text would make them 13 mm at 300 ppi: a finger's 9 and 8 are enough */
        row_min = MIN(row_min, 110), chip_min = MIN(chip_min, 96);
    char *rc = g_strdup_printf("style \"denboku-marks\" { GtkCheckButton::indicator-size = %d"
                               " GtkCheckButton::indicator-spacing = %d }\n"
                               "class \"GtkCheckButton\" style \"denboku-marks\"\n"
                               "style \"denboku-row-marks\" { GtkButton::inner-border = { 14, %d, 5, 5 } }\n"
                               "widget \"*.denboku-row\" style \"denboku-row-marks\"\n",
                               (int) (13 * ts), (int) (2 * ts), (int) (40 * ts));
    gtk_rc_parse_string(rc);
    g_free(rc);
    gtk_rc_reset_styles(gtk_settings_get_default());
}

void style_set_accent(const double *a, const double *on)
{
    memcpy(accent, a, sizeof accent);
    memcpy(on_accent, on, sizeof on_accent);
}

void style_set_framed(gboolean on)
{
    if (framed == on)
        return;
    framed = on;
    GList *all = gtk_window_list_toplevels();
    for (GList *l = all; l; l = l->next)
        gtk_widget_queue_draw(l->data);
    g_list_free(all);
}
