/* The window, startup and main. */

#include "app.h"

#ifndef G_OS_WIN32
#include <sys/mman.h>
#include <gdk/gdkx.h>
#endif

App app;

/* ---- window ---- */

/* At least finger-sized: a size request would also cap the width and cut longer labels. */
static void min_width_cb(GtkWidget *w, GtkRequisition *r, gpointer data)
{
    (void) w, (void) data;
    r->width = MAX(r->width, MAX(90, gdk_screen_width() / 13));
}

/* GTK's dots per inch × 1024, -1 when it has none (Windows' GTK has no Xft). */
static int xft_dpi(void)
{
    GtkSettings *s = gtk_settings_get_default();
    int dpi = -1;
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(s), "gtk-xft-dpi"))
        g_object_get(s, "gtk-xft-dpi", &dpi, NULL);
    return dpi;
}

/* Sizes given in a computer's pixels, for the screen's density (Android's are 2-4 times as fine;
 * the Kindle has sizes of its own). */
double ui_scale(void)
{
    int dpi = xft_dpi();
    return app.kindle || dpi <= 0 ? 1 : MAX(1.0, dpi / 1024.0 / 96);
}

/* The height of a line of `w`'s text. */
static int measure_line(GtkWidget *w)
{
    PangoLayout *pl = gtk_widget_create_pango_layout(w, "M");
    int em, lh;
    pango_layout_get_pixel_size(pl, &em, &lh);
    g_object_unref(pl);
    return lh;
}

int line_height(void)
{
    static PangoFontDescription *font;
    static int dpi_was, lh;
    const PangoFontDescription *now = gtk_widget_get_style(app.window)->font_desc;
    int dpi = xft_dpi();
    if (font && dpi == dpi_was && pango_font_description_equal(font, now))
        return lh;
    if (font)
        pango_font_description_free(font);
    font = pango_font_description_copy(now), dpi_was = dpi;
    return lh = measure_line(app.window);
}

/* A width given in a computer's pixels as a limit on this screen: none on the Kindle, whose text is
 * more than twice a computer's while ui_scale() is 1 (limits made for it left a chip per row). */
int ui_limit(int px) { return app.kindle ? G_MAXINT / 4 : (int) (px * ui_scale()); }

GtkWidget *button(const char *label, GCallback cb, gpointer data)
{
    GtkWidget *b = gtk_button_new_with_label(label);
    gtk_widget_set_can_focus(b, FALSE);
    /* Flat toolbar buttons: a frame shows on hover (and on the Kindle, where they are pressed). */
    gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
    /* Finger-sized on the Kindle, even for one-symbol buttons. */
    if (app.kindle)
        g_signal_connect_after(b, "size-request", G_CALLBACK(min_width_cb), NULL);
    g_signal_connect(b, "clicked", cb, data);
    return b;
}

static gboolean icon_expose_cb(GtkWidget *w, GdkEventExpose *ev, gpointer data)
{
    (void) ev;
    Icon icon = (Icon) GPOINTER_TO_INT(data);
    GtkAllocation a;
    gtk_widget_get_allocation(w, &a);
    cairo_t *cr = gdk_cairo_create(gtk_widget_get_window(w));
    GdkColor *c = &gtk_widget_get_style(w)->fg[gtk_widget_get_state(w)];
    cairo_set_source_rgb(cr, c->red / 65535.0, c->green / 65535.0, c->blue / 65535.0);
    /* A drawing area has its own window: its coordinates start at 0. */
    double s = MIN(a.width, a.height), x = (a.width - s) / 2, y = (a.height - s) / 2;
    double m = s * 0.18; /* margin */
    if (icon == ICON_BACK) { /* ‹, drawn as × is */
        cairo_set_line_width(cr, MAX(1.5, s * 0.1));
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        cairo_move_to(cr, x + s * 0.62, y + m);
        cairo_line_to(cr, x + s * 0.3, y + s / 2);
        cairo_line_to(cr, x + s * 0.62, y + s - m);
        cairo_stroke(cr);
    } else if (icon == ICON_CLOSE) {
        cairo_set_line_width(cr, MAX(1.5, s * 0.1));
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_move_to(cr, x + m, y + m);
        cairo_line_to(cr, x + s - m, y + s - m);
        cairo_move_to(cr, x + s - m, y + m);
        cairo_line_to(cr, x + m, y + s - m);
        cairo_stroke(cr);
    }
    cairo_fill(cr);
    cairo_destroy(cr);
    return TRUE;
}

GtkWidget *icon_button(Icon icon, GCallback cb, gpointer data)
{
    GtkWidget *b = button("", cb, data);
    gtk_container_remove(GTK_CONTAINER(b), gtk_bin_get_child(GTK_BIN(b)));
    GtkWidget *area = gtk_drawing_area_new();
    /* As tall as a line of the button's text (in the default style: it is not in the window yet). */
    int h = measure_line(b);
    gtk_widget_set_size_request(area, h, h);
    g_signal_connect(area, "expose-event", G_CALLBACK(icon_expose_cb), GINT_TO_POINTER(icon));
    gtk_container_add(GTK_CONTAINER(b), area);
    return b;
}

/* Widgets made once that show a text in the language in use: their English, for relabel(). */
static GSList *followers;

static void follower_gone(gpointer data, GObject *gone)
{
    (void) data;
    followers = g_slist_remove(followers, gone);
}

void follow_language(GtkWidget *w, const char *english)
{
    g_object_set_data(G_OBJECT(w), "english", (gpointer) english);
    followers = g_slist_prepend(followers, w);
    g_object_weak_ref(G_OBJECT(w), follower_gone, NULL);
}

void relabel(void)
{
    for (GSList *l = followers; l; l = l->next) {
        const char *en = g_object_get_data(G_OBJECT(l->data), "english");
        if (GTK_IS_BUTTON(l->data))
            gtk_button_set_label(GTK_BUTTON(l->data), tr(en));
        else if (GTK_IS_LABEL(l->data))
            gtk_label_set_text(GTK_LABEL(l->data), tr(en));
    }
    update_undo();
    edit_relabel();
    view_refresh(app.view);
}

static void install_eink_style(void)
{
    gtk_rc_parse_string("style \"eink\" {\n"
                        "  fg[NORMAL] = \"#000000\" fg[ACTIVE] = \"#000000\" fg[PRELIGHT] = \"#000000\"\n"
                        "  fg[SELECTED] = \"#ffffff\" fg[INSENSITIVE] = \"#888888\"\n"
                        "  text[NORMAL] = \"#000000\" text[SELECTED] = \"#ffffff\"\n"
                        "  base[NORMAL] = \"#ffffff\" base[SELECTED] = \"#000000\"\n"
                        "  bg[NORMAL] = \"#ffffff\" bg[ACTIVE] = \"#cccccc\" bg[PRELIGHT] = \"#eeeeee\"\n"
                        "  bg[SELECTED] = \"#000000\"\n"
                        "  font_name = \"Denboku Sans, Sans 9\"\n"
                        "}\n"
                        "gtk-button-images = 0\n"
                        "gtk-menu-images = 0\n"
                        "widget_class \"*\" style \"eink\"\n");
}

#ifndef G_OS_WIN32
/* Whether a mapped window, visible or not, is stacked above ours. */
static gboolean covered(GtkWidget *w)
{
    Display *d = GDK_WINDOW_XDISPLAY(gtk_widget_get_window(w));
    Window root = GDK_WINDOW_XID(gdk_get_default_root_window()), top = GDK_WINDOW_XID(gtk_widget_get_window(w));
    Window r, parent, *kids;
    unsigned n;
    /* Ours among the root's children: the window manager's frame around it, if any. */
    while (XQueryTree(d, top, &r, &parent, &kids, &n)) {
        if (kids)
            XFree(kids);
        if (parent == root)
            break;
        top = parent;
    }
    if (!XQueryTree(d, root, &r, &parent, &kids, &n))
        return TRUE;
    gboolean above = FALSE, seen = FALSE;
    for (unsigned i = 0; i < n && !above; i++) { /* bottom to top */
        XWindowAttributes at;
        if (kids[i] == top)
            seen = TRUE;
        else if (seen && XGetWindowAttributes(d, kids[i], &at) && at.map_state == IsViewable)
            above = TRUE;
    }
    if (kids)
        XFree(kids);
    return above || !seen;
}
#endif

/* Other apps (the web reader, the keyboard) can leave invisible windows above ours that
 * swallow taps; put the game on top whenever it is mapped. Only when one is there: raising a
 * window already on top makes the X server draw it all again, a full e-ink update. */
static gboolean present_cb(gpointer data)
{
    (void) data;
    if (!app.window)
        return FALSE;
#ifndef G_OS_WIN32
    if (!covered(app.window))
        return FALSE;
#endif
    /* Do not pull the game window over a dialog that is already open (a password prompt
     * opened while KUAL was still closing): that takes the focus and the keyboard away. */
    GList *all = gtk_window_list_toplevels();
    gboolean dialog = FALSE;
    for (GList *l = all; l && !dialog; l = l->next) {
        GtkWidget *w = l->data;
        dialog = GTK_IS_DIALOG(w) && gtk_widget_get_visible(w);
    }
    g_list_free(all);
    if (dialog)
        return FALSE;
    gtk_window_present(GTK_WINDOW(app.window));
    gdk_window_raise(gtk_widget_get_window(app.window));
    return FALSE;
}

static gboolean map_cb(GtkWidget *w, GdkEvent *ev, gpointer data)
{
    (void) ev, (void) data;
    gdk_window_raise(gtk_widget_get_window(w));
    /* KUAL may still be closing when we start; come to the front again once it is gone. */
    if (app.kindle) {
        g_timeout_add(1500, present_cb, NULL);
        g_timeout_add(4000, present_cb, NULL);
    }
    return FALSE;
}

/* After the configure event: during it the window still gives its old size. */
static guint layout_pending; /* one layout for a run of configure events */

static gboolean layout_idle(gpointer data)
{
    (void) data;
    layout_pending = 0;
    chat_layout();
    drawer_layout();
    edit_bar_layout();
    return FALSE;
}

static gboolean configure_cb(GtkWidget *w, GdkEventConfigure *ev, gpointer data)
{
    (void) data;
    if (!layout_pending)
        layout_pending = g_idle_add(layout_idle, NULL);
    GdkWindow *win = gtk_widget_get_window(w);
    if (!app.kindle && win && !(gdk_window_get_state(win) & (GDK_WINDOW_STATE_MAXIMIZED | GDK_WINDOW_STATE_FULLSCREEN))) {
        g_key_file_set_integer(app.settings, "Settings", "WindowWidth", ev->width);
        g_key_file_set_integer(app.settings, "Settings", "WindowHeight", ev->height);
    }
    return FALSE;
}

/* Kindle: the window manager rotates the screen for the app in front. */
void apply_orientation(gboolean leaving)
{
#ifdef G_OS_WIN32
    (void) leaving;
#else
    if (!app.kindle)
        return;
    static const char *const LOCKS[] = {"U", "L", "R"}; /* "D", portrait upside down, it does not do */
    const char *lock = leaving ? "" : LOCKS[CLAMP(setting_int("Landscape", 0), 0, 2)];
    char *argv[] = {"lipc-set-prop", "com.lab126.winmgr", "orientationLock", (char *) lock, NULL};
    g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                  NULL, NULL, NULL, NULL);
#endif
}

/* The screen turned (Kindle orientation) or got smaller (Android's keyboard): open dialogs keep
 * their old place, partly off the new screen. */
static void screen_changed_cb(GdkScreen *screen, gpointer data)
{
    (void) screen, (void) data;
    center_dialogs();
}

/* A tap on the board leaves a text field (the chat beside it), which closes the Kindle keyboard. */
static gboolean board_unfocus_cb(GtkWidget *w, GdkEventButton *e, gpointer data)
{
    (void) w, (void) e, (void) data;
    keyboard_hide();
    gtk_window_set_focus(GTK_WINDOW(app.window), NULL);
    return FALSE;
}

static char move_text[256];

void set_move_text(const char *text)
{
    if (!strcmp(text, move_text))
        return;
    snprintf(move_text, sizeof move_text, "%s", text);
    view_set_notice(app.view, text);
    view_set_notice_item(app.view, -1);
}

/* The view's bar of actions (src/core/view.c). */
static int bar_state_cb(View *v, int item, char *label, int size, int *icon, void *data)
{
    (void) v, (void) data;
    int ply = view_ply(app.view);
    /* The other player's takeback request takes the bar, but for the menu, until it is answered. */
    static const char *const ASK[] = {"Takeback?", "✓ Accept", "✗ Decline"};
    gboolean asked = takeback_asked();
    if (item >= VIEW_BAR_ASK) {
        snprintf(label, size, "%s", tr(ASK[item - VIEW_BAR_ASK]));
        return !asked ? BAR_HIDDEN : item == VIEW_BAR_ASK ? BAR_OFF : BAR_ON;
    }
    if (asked && item != VIEW_BAR_MENU)
        return BAR_HIDDEN;
    switch (item) {
    case VIEW_BAR_MENU:
        snprintf(label, size, "%s", tr("Menu"));
        return BAR_ON;
    case VIEW_BAR_UNDO:
    case VIEW_BAR_HINT: {
        if (replaying()) {
            snprintf(label, size, "%s", item == VIEW_BAR_UNDO ? replay_speed_label() : tr(replay_paused() ? "Resume" : "Pause"));
            *icon = item == VIEW_BAR_UNDO ? BAR_ICON_LETTER : replay_paused() ? BAR_ICON_RESUME : BAR_ICON_PAUSE;
            return BAR_ON;
        }
        if (lesson_playing()) /* Again and Hint are in the lesson's drawer */
            return BAR_HIDDEN;
        if (item == VIEW_BAR_HINT && engines_game() && !app.loaded_record) { /* nobody to give a hint to */
            snprintf(label, size, "%s", tr(app.paused ? "Resume" : "Pause"));
            *icon = app.paused ? BAR_ICON_RESUME : BAR_ICON_PAUSE;
            return app.game.result == RESULT_NONE;
        }
        if (item == VIEW_BAR_UNDO) {
            snprintf(label, size, "%s", tr(app.undo_label));
            return app.can_undo;
        }
        snprintf(label, size, "%s", tr("Hint"));
        if (!app.hint_visible)
            return BAR_HIDDEN; /* network games */
        return app.can_hint;
    }
    case VIEW_BAR_PREV:
        return (ply < 0 ? app.game.count : ply) > 0;
    case VIEW_BAR_NEXT:
        return ply >= 0 && ply < app.game.count;
    case VIEW_BAR_CHAT:
        snprintf(label, size, "%s", tr("Chat"));
        return chat_bar_state();
    }
    return BAR_ON;
}

static void bar_cb(View *v, int item, void *data)
{
    (void) v, (void) data;
    switch (item) {
    case VIEW_BAR_MENU:
        menu_open();
        break;
    case VIEW_BAR_CHAT:
        chat_toggle();
        break;
    case VIEW_BAR_UNDO:
        undo_cb(NULL, NULL);
        break;
    case VIEW_BAR_HINT:
        hint_cb(NULL, NULL);
        break;
    case VIEW_BAR_PREV:
    case VIEW_BAR_NEXT:
        history_step(item == VIEW_BAR_NEXT ? 1 : -1);
        break;
    case VIEW_BAR_INFO:
        info_cb(NULL, NULL);
        break;
    case VIEW_MOVES:
        moves_dialog();
        break;
    case VIEW_BAR_YES:
    case VIEW_BAR_NO:
        if (takeback_asked())
            takeback_answer(item == VIEW_BAR_YES);
        break;
    }
    view_queue_bar(app.view);
}

/* A tap anywhere but on the bar (which has the replay's own buttons) stops a replay. */
static gboolean stop_replay_cb(GtkWidget *w, GdkEventButton *ev, gpointer data)
{
    (void) w, (void) data;
    if (view_bar_item_at(app.view, ev->x, ev->y) < 0)
        replay_stop();
    return FALSE;
}

/* Keys play as set in Preferences → Controls (the Sandbox has its own first); not while typing. */
static gboolean key_cb(GtkWidget *w, GdkEventKey *e, gpointer data)
{
    (void) data;
    controls_nav_key(e->keyval);
    if (drawer_open())
        return drawer_key(e);
    if (edit_key(e->keyval, e->state))
        return TRUE;
    GtkWidget *focus = gtk_window_get_focus(GTK_WINDOW(w));
    if (focus && (GTK_IS_ENTRY(focus) || GTK_IS_TEXT_VIEW(focus)))
        return FALSE;
    return controls_key(e);
}

/* On the Kindle the window manager can keep the X focus on the game window while a dialog is
 * up (the on-screen keyboard then types nowhere). Forward keys to the dialog's focused text
 * field; buttons are left alone so an Enter cannot be routed twice. */
static gboolean route_dialog_key_cb(GtkWidget *w, GdkEventKey *e, gpointer data)
{
    (void) w, (void) data;
    static gboolean routing;
    if (routing)
        return FALSE;
    GtkWidget *target = NULL;
    GList *all = gtk_window_list_toplevels();
    for (GList *l = all; l; l = l->next) {
        GtkWidget *top = l->data;
        if (GTK_IS_DIALOG(top) && gtk_widget_get_visible(top)) {
            GtkWidget *focus = gtk_window_get_focus(GTK_WINDOW(top));
            if (focus && GTK_IS_ENTRY(focus))
                target = focus;
        }
    }
    g_list_free(all);
    if (!target)
        return FALSE;
    routing = TRUE;
    gboolean handled = gtk_widget_event(target, (GdkEvent *) e);
    routing = FALSE;
    return handled;
}

#ifndef __ANDROID__ /* Android's window is the screen */
/* Where a window may go on the screen: on Windows the work area (without the taskbar), elsewhere the
 * first monitor (GTK 2 knows no work area). */
static void work_area(GdkRectangle *r)
{
#ifdef G_OS_WIN32
    RECT a;
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &a, 0)) {
        *r = (GdkRectangle) {a.left, a.top, a.right - a.left, a.bottom - a.top};
        return;
    }
#endif
    gdk_screen_get_monitor_geometry(gdk_screen_get_default(), 0, r);
}
#endif

static void build_window(void)
{
    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.window), app.kindle ? KINDLE_TITLE : tr("Denboku Shōgi"));
#ifdef __ANDROID__
    android_window(app.window);
#else
    if (app.kindle)
        gtk_window_set_default_size(GTK_WINDOW(app.window), gdk_screen_width(), gdk_screen_height());
    else {
        /* Maximized on computers; the saved size is the one it goes back to, within the screen (a
         * laptop's 1280×720 is lower than the default size, and a window not maximized went past it). */
        GdkRectangle a;
        work_area(&a);
        gtk_window_set_default_size(GTK_WINDOW(app.window), MIN(setting_int("WindowWidth", 1100), a.width),
                                    MIN(setting_int("WindowHeight", 760), a.height - 40)); /* its title bar */
        gtk_window_move(GTK_WINDOW(app.window), a.x, a.y);
        gtk_window_maximize(GTK_WINDOW(app.window));
    }
#endif
    g_signal_connect(app.window, "delete-event", G_CALLBACK(delete_cb), NULL);
    g_signal_connect(app.window, "map-event", G_CALLBACK(map_cb), NULL);
    g_signal_connect(app.window, "key-press-event", G_CALLBACK(key_cb), NULL);
    if (app.kindle)
        g_signal_connect_after(app.window, "key-press-event", G_CALLBACK(route_dialog_key_cb), NULL);
    g_signal_connect(app.window, "configure-event", G_CALLBACK(configure_cb), NULL);
    g_signal_connect(gdk_screen_get_default(), "size-changed", G_CALLBACK(screen_changed_cb), NULL);

    GtkWidget *vbox = gtk_vbox_new(FALSE, 0);
    app.undo_label = "Undo";
    app.can_undo = app.can_hint = app.hint_visible = TRUE;
    if (!app.kindle)
        controls_init();
    app.edit_bar = edit_bar_new();
    gtk_box_pack_start(GTK_BOX(vbox), app.edit_bar, FALSE, FALSE, 0);

    ViewCallbacks cb = {view_move_cb, view_can_move_cb, view_text_cb, view_ply_cb, view_edit_cb, view_edit_event_cb,
                       view_can_premove_cb, controls_area_badge, bar_cb, bar_state_cb, view_square_tap_cb};
    app.view = view_new(&app.game, &cb, NULL);
#ifdef __ANDROID__
    view_set_hover(app.view, FALSE); /* touch: no hover marks, a drag draws an arrow */
#else
    view_set_hover(app.view, !app.kindle);
#endif
    view_set_live_drag(app.view, !app.kindle);
    g_signal_connect(view_widget(app.view), "button-press-event", G_CALLBACK(stop_replay_cb), NULL);
    GtkWidget *row = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), drawer_stage(view_widget(app.view)), TRUE, TRUE, 0);
    chat_panel_new(row, vbox);
    drawer_init(vbox);
    g_signal_connect(view_widget(app.view), "button-press-event", G_CALLBACK(board_unfocus_cb), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), row, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(app.window), vbox);
}

/* The game left open when the app closed, if it was not over. */
static gboolean read_autosave(Game *g)
{
    char *path = g_build_filename(app.home, "autosave." RECORD_EXT, NULL);
    FILE *f = g_fopen(path, "r");
    g_free(path);
    if (!f)
        return FALSE;
    gboolean ok = record_read(g, f) && g->result == RESULT_NONE;
    fclose(f);
    return ok;
}

/* A joiner whose app closed during a network game goes back to it: the host waits for it and
 * sends the game again. If the host is gone, the lobby closes and the game resumes here. */
static void rejoin(void)
{
    Game g;
    char *host = setting_string("LastHost", "");
    if (g_key_file_get_integer(app.settings, "Game", "Mode", NULL) == MODE_JOIN && *host && read_autosave(&g)) {
        g_free(app.net_password); /* the room's password, given before the app closed */
        app.net_password = g_key_file_get_string(app.settings, "Game", "RoomPassword", NULL);
        if (app.net_password && !*app.net_password)
            g_free(app.net_password), app.net_password = NULL;
        network_game(HANDICAP_NONE, MODE_JOIN, host);
    }
    rules_set_variant(app.variant); /* reading the autosave may have changed it */
    g_free(host);
}

gboolean resume(void)
{
    Game g;
    gboolean ok = read_autosave(&g) && g.count > 0;
    if (!ok) {
        rules_set_variant(app.variant); /* a failed read may have changed it */
        return FALSE;
    }
    app.variant = CLAMP(rules_variant, 0, V_COUNT - 1);
    GError *err = NULL;
    int mode = g_key_file_get_integer(app.settings, "Game", "Mode", &err);
    if (err) {
        g_error_free(err);
        return FALSE;
    }
    app.mode = (Mode) CLAMP(mode, 0, MODE_COUNT - 1);
    if (connected_mode(app.mode))
        app.mode = MODE_TWO_PLAYERS; /* the connection is gone: both sides play here */
    app.loaded_record = g_key_file_get_integer(app.settings, "Game", "Record", NULL);
    app.players[FIRST] = g_key_file_get_integer(app.settings, "Game", "SenteEngine", NULL) ? PLAYER_ENGINE
                                                                                          : PLAYER_HUMAN;
    app.players[SECOND] = g_key_file_get_integer(app.settings, "Game", "GoteEngine", NULL) ? PLAYER_ENGINE
                                                                                        : PLAYER_HUMAN;
    app.level = CLAMP(g_key_file_get_integer(app.settings, "Game", "Level", NULL), 1, LEVELS);
    app.level_first = g_key_file_get_integer(app.settings, "Game", "LevelFirst", NULL);
    app.level_first = app.level_first ? CLAMP(app.level_first, 1, LEVELS) : app.level;
    app.paused = FALSE;
    app.time_index = CLAMP(g_key_file_get_integer(app.settings, "Game", "Time", NULL), 0, NTIMES);
    if (g_key_file_has_key(app.settings, "Game", "ClockMain", NULL))
        app.custom_tc = (TimeControl) {NULL, MAX(0, g_key_file_get_integer(app.settings, "Game", "ClockMain", NULL)),
                                       MAX(0, g_key_file_get_integer(app.settings, "Game", "ClockByoyomi", NULL)),
                                       MAX(0, g_key_file_get_integer(app.settings, "Game", "ClockIncrement", NULL))};
    app.remaining[FIRST] = g_key_file_get_integer(app.settings, "Game", "SenteMs", NULL);
    app.remaining[SECOND] = g_key_file_get_integer(app.settings, "Game", "GoteMs", NULL);
    app.game = g;
    turn_clock_new();
    app.handicap = g.handicap;
    prepare_engine();
    view_set_flipped(app.view, app.players[SECOND] == PLAYER_HUMAN && app.players[FIRST] == PLAYER_ENGINE);
    next_turn();
    return TRUE;
}

/* GTK 2 takes a list drop-down's list apart when the drop-down is finalized, telling the list with
 * the drop-down already gone (a GLib warning for each one in a closed drawer, and the file chooser's):
 * taken apart at its destroy instead, while it is still there, by turning it into a menu one. */
static void (*combo_destroy_parent)(GtkObject *o);

static void combo_destroy(GtkObject *o)
{
    gtk_widget_set_name(GTK_WIDGET(o), "denboku-gone-combo");
    combo_destroy_parent(o);
}

/* Every event: a drag in a scrolled area scrolls it (drawer.c); with DENBOKU_DEBUG, how long each took. */
static void events_cb(GdkEvent *ev, gpointer data)
{
    (void) data;
    gint64 t = g_get_monotonic_time();
    /* Kindle: what the X server or GTK's own layout step asks drawn goes in bands (kindle.c). */
    if (app.kindle && ev->type == GDK_EXPOSE && !painting_in_bands()) {
        gdk_window_invalidate_region(ev->expose.window, ev->expose.region, TRUE);
        kindle_bands_soon();
        return;
    }
    if (!touch_scroll_event(ev) && !chat_event(ev))
        gtk_main_do_event(ev);
    static int debug = -1;
    if (debug < 0)
        debug = g_getenv("DENBOKU_DEBUG") != NULL;
    if (!debug)
        return;
    gpointer widget = NULL;
    if (ev->any.window)
        gdk_window_get_user_data(ev->any.window, &widget);
    if (ev->type != GDK_MOTION_NOTIFY)
        g_printerr("%.3f event %d %s %.0f ms\n", t / 1e6, ev->type, widget ? G_OBJECT_TYPE_NAME(widget) : "-",
                   (g_get_monotonic_time() - t) / 1e3);
}

/* The choices of the last New Game, from the settings. */
static void load_game_choices(void)
{
    app.level = CLAMP(setting_int("Level", 4), 1, LEVELS);
    app.level_first = CLAMP(setting_int("LevelFirst", app.level), 1, LEVELS);
    app.variant = CLAMP(setting_int("Variant", 0), 0, V_COUNT - 1);
    rules_set_variant(app.variant);
    app.time_index = CLAMP(setting_int("Time", 0), 0, NTIMES);
    app.custom_min = CLAMP(setting_int("CustomMin", 10), 0, 180);
    app.custom_byoyomi = CLAMP(setting_int("CustomByoyomi", 30), 0, 300);
    app.custom_increment = CLAMP(setting_int("CustomIncrement", 0), 0, 60);
    app.custom_tc = (TimeControl) {NULL, app.custom_min * 60, app.custom_byoyomi, app.custom_increment};
}

/* The first start's questions, one after the other: updates, then the tutorial. */
static gboolean prompts_cb(gpointer fresh)
{
    if (drawer_open()) { /* a drawer the game came back with: once it is closed */
        g_timeout_add(1000, prompts_cb, fresh);
        return FALSE;
    }
    updater_ask();
    if (fresh)
        tutorial_offer();
    return FALSE;
}

/* --svg: the view into the file, then out at once (nothing saved: a screenshot leaves no trace). */
static gboolean svg_cb(gpointer path)
{
    gboolean ok = view_save_svg(app.view, path);
    if (!ok)
        fprintf(stderr, "denboku: could not write %s\n", (char *) path);
    exit(ok ? 0 : 1);
}

static const char HELP[] =
    "Usage: denboku [option...]\n"
    "Denboku Sh\xc5\x8dgi: Sh\xc5\x8dgi Everywhere.\n\n"
    "  -h, --help            show this help and exit\n"
    "  -v, --version         show the build number and exit\n"
    "  --dev                 developer mode: a profile of its own (dev/ in the data folder) with the\n"
    "                        default settings at every start; the player's are left alone\n"
    "  --debug               timings, HTTP requests and LAN discovery on stderr\n"
    "  --home DIR            the data folder (settings, game in progress, history) in DIR\n"
    "  --engine PROGRAM      another USI engine for everything\n"
    "  --sfen SFEN           every new game starts from this position\n"
    "  --net host[:PASSWORD] host a LAN room at start (with that password)\n"
    "  --net join[:ADDRESS]  join a LAN room at start (at that address)\n"
    "  --kindle-look         the Kindle's look and sizes on a computer (screenshots)\n"
    "  --svg FILE[:SECONDS]  the game's view as vectors into FILE that many seconds (4) after\n"
    "                        the start, then quit (screenshots; the drawers are not in it)\n\n"
    "Each option is also an environment variable: DENBOKU_DEV (=keep keeps the developer\n"
    "settings between starts), DENBOKU_DEBUG, DENBOKU_HOME, DENBOKU_ENGINE, DENBOKU_SFEN,\n"
    "DENBOKU_NET, DENBOKU_KINDLE_LOOK and DENBOKU_SVG; besides them DENBOKU_BASE (the game's\n"
    "own files from another folder) and, with DENBOKU_DEBUG, DENBOKU_UPDATE_URL (where to\n"
    "look for updates).\n";

/* The options, before GTK starts (so --help needs no screen): each sets its environment variable,
 * which the rest of the game reads. GTK's own (--display, --sync, --g…) are left to it. */
static void command_line(int argc, char **argv)
{
    static const char *const VALUED[][2] = {{"--home", "DENBOKU_HOME"}, {"--engine", "DENBOKU_ENGINE"},
                                            {"--sfen", "DENBOKU_SFEN"}, {"--net", "DENBOKU_NET"},
                                            {"--svg", "DENBOKU_SVG"}};
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-h") || !strcmp(a, "--help") || !strcmp(a, "-v") || !strcmp(a, "--version")) {
#ifdef G_OS_WIN32 /* a windowed program: its text goes to the console it was started from */
            if (AttachConsole(ATTACH_PARENT_PROCESS)) {
                freopen("CONOUT$", "w", stdout);
                SetConsoleOutputCP(CP_UTF8);
            }
#endif
            if (a[1] == 'v' || a[2] == 'v')
                printf("Denboku Sh\xc5\x8dgi %s, build %s\n", *DENBOKU_VERSION ? DENBOKU_VERSION : "(development)", DENBOKU_BUILD);
            else
                fputs(HELP, stdout);
            exit(0);
        }
        if (!strcmp(a, "--dev") || !strcmp(a, "--debug") || !strcmp(a, "--kindle-look")) {
            g_setenv(!strcmp(a, "--dev") ? "DENBOKU_DEV" : !strcmp(a, "--debug") ? "DENBOKU_DEBUG" : "DENBOKU_KINDLE_LOOK", "1", TRUE);
            continue;
        }
        gboolean known = FALSE;
        for (size_t k = 0; k < G_N_ELEMENTS(VALUED) && !known; k++) {
            size_t n = strlen(VALUED[k][0]);
            if (strncmp(a, VALUED[k][0], n) || (a[n] && a[n] != '='))
                continue;
            const char *value = a[n] == '=' ? a + n + 1 : i + 1 < argc ? argv[++i] : NULL;
            if (!value) {
                fprintf(stderr, "denboku: %s needs a value (see --help)\n", VALUED[k][0]);
                exit(2);
            }
            g_setenv(VALUED[k][1], value, TRUE);
            known = TRUE;
        }
        if (!known && !g_str_has_prefix(a, "--g") && !g_str_has_prefix(a, "--display") && strcmp(a, "--sync") &&
            !g_str_has_prefix(a, "--name") && !g_str_has_prefix(a, "--class") && !g_str_has_prefix(a, "--screen")) {
            fprintf(stderr, "denboku: unknown option %s (see --help)\n", a);
            exit(2);
        }
    }
}

int main(int argc, char **argv)
{
    gboolean fresh = FALSE;
    command_line(argc, argv);
    /* Kindle touches only reach native X windows (GTK 2.18+ uses client-side ones). */
    if (g_file_test("/mnt/us", G_FILE_TEST_IS_DIR))
        g_setenv("GDK_NATIVE_WINDOWS", "1", FALSE);
    gtk_init(&argc, &argv);
    gdk_event_handler_set(events_cb, NULL, NULL);
    rules_init();
    find_paths();
    /* The drawers in the board's and the menu's font, not the system's (a dated bitmap one on Windows). */
    g_object_set(gtk_settings_get_default(), "gtk-font-name", "Denboku Sans, Sans 10", NULL);
    if (!app.kindle) {
        char *icon = g_build_filename(app.base, "art", "icon.png", NULL);
        gtk_window_set_default_icon_from_file(icon, NULL);
        g_free(icon);
    }
    apply_language();
    GtkObjectClass *combo_class = g_type_class_ref(GTK_TYPE_COMBO_BOX); /* before any subclass is made */
    combo_destroy_parent = combo_class->destroy, combo_class->destroy = combo_destroy;
    /* Combo boxes as lists: the menu style needs a pointer grab and closes on release under
     * XWayland and with taps. */
    gtk_rc_parse_string("style \"combo\" { GtkComboBox::appears-as-list = 1 }\n"
                        "class \"GtkComboBox\" style \"combo\"\n"
                        "style \"gone-combo\" { GtkComboBox::appears-as-list = 0 }\n" /* combo_destroy() */
                        "widget \"*.denboku-gone-combo\" style \"gone-combo\"\n"
                        /* No dotted focus line: the keyboard and the controller get the frame of controls.c. */
                        "style \"nofocus\" { GtkWidget::focus-line-width = 0 }\n"
                        "class \"GtkWidget\" style \"nofocus\"\n"
                        /* Rows a finger can hit, in menus and in the lists of combo boxes. */
                        "style \"touchrow\" { ythickness = 10 xthickness = 8 }\n"
                        "class \"GtkMenuItem\" style \"touchrow\"\n"
                        "style \"touchsep\" { ythickness = 3 }\n"
                        "class \"GtkSeparatorMenuItem\" style \"touchsep\"\n"
                        "style \"touchlist\" { GtkTreeView::vertical-separator = 22 }\n"
                        "widget \"gtk-combobox-popup-window.*\" style \"touchlist\"\n");
    if (app.kindle)
        install_eink_style();
    style_install();
    load_game_choices();
    build_window();
    load_style_list();
    apply_settings();
    apply_orientation(FALSE);
    gtk_widget_show_all(app.window);
    keyboard_hide_leftover();
#ifdef __ANDROID__
    android_start();
#endif
#ifndef __ANDROID__ /* Android keeps /dev/input from apps */
    if (!app.kindle)
        gamepad_start();
#endif
#ifndef G_OS_WIN32
    /* The Kindle runs short of memory: the code of GTK, Pango and cairo was dropped between taps
     * and read back from storage on every redraw (hundreds of page faults, a few hundred ms).
     * Keep the game in memory. */
    if (app.kindle && mlockall(MCL_CURRENT | MCL_FUTURE) && g_getenv("DENBOKU_DEBUG"))
        g_printerr("mlockall: %s\n", g_strerror(errno));
#endif
    watch_screensaver();
    kindle_draw_in_bands();
    updater_start();
    const char *net = g_getenv("DENBOKU_NET");
    if (net && g_str_has_prefix(net, "host")) {
        if (net[4] == ':') /* "host:<room password>" */
            app.room_password = g_strdup(net + 5);
        network_game(HANDICAP_NONE, MODE_HOST, NULL);
    }
    else if (net && g_str_has_prefix(net, "join"))
        network_game(HANDICAP_NONE, MODE_JOIN, net[4] == ':' ? net + 5 : NULL);
    else
        rejoin();
    /* A lishogi game left going on comes back when its event stream reports it. */
    if (setting_int("Mode", MODE_FIRST) == MODE_ONLINE && !app.net) {
        char *token = online_token();
        if (*token)
            online_events_cb(GINT_TO_POINTER(1));
        g_free(token);
    }
    if (!app.net && resume())
        host_rejoin();
    else if (!app.net) {
        fresh = TRUE; /* nothing saved: maybe the first start */
        /* A network game needs its connection: start a game against the engine instead. */
        Mode m = (Mode) CLAMP(setting_int("Mode", MODE_FIRST), 0, MODE_COUNT - 1);
        start_game((Handicap) CLAMP(setting_int("Handicap", 0), 0, HANDICAP_COUNT - 1),
                   connected_mode(m) ? MODE_FIRST : m);
    }
    /* Before the first tap: low priority comes after the first draw. */
    g_idle_add_full(G_PRIORITY_LOW, prompts_cb, GINT_TO_POINTER(fresh), NULL);
    const char *svg = g_getenv("DENBOKU_SVG");
    if (svg) {
        const char *after = strrchr(svg, ':');
        gboolean timed = after && after[1] && strspn(after + 1, "0123456789") == strlen(after + 1);
        g_timeout_add_seconds(timed ? (guint) atoi(after + 1) : 4, svg_cb,
                              g_strndup(svg, timed ? (gsize) (after - svg) : strlen(svg)));
    }
    gtk_main();
    engine_free(app.engine);
    style_free(app.style);
    return 0;
}
