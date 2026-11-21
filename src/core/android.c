/* Android: the game runs as on a computer, on an X server (Xvfb) inside the app. The Java side
 * (android/) shows the screen and talks to this file over stdin and stdout, one line each:
 *   in:  d|m|u X Y T (finger down, move, up; T Android's time of it in ms)   h (a long press)   p (pointer away)   w N (wheel button)   k KEYSYM   t TEXT
 *        b (Back)   v W H KB (view size, and how much of the screen the on-screen keyboard takes)   clip TEXT (the
 *        clipboard, asked by paste)   tm selectall|copy|cut (the text toolbar's choice)   g BITS and gn DRIVER NAME (a
 *        controller's buttons and its name)   installfail (the system did not install an update)
 *   out: f X Y W H (redraw that area)   kb 0|1 (keyboard)   kbreset (the field emptied: the keyboard's copy too)   snd FILE VOLUME   vib MS STRENGTH   rumble STRONG WEAK MS
 *        url URL   export FILE (a saved game, copied to Documents/Denboku)   install APK (an update, to the system's
 *        installer)   allowinstall (updates were turned on: ask to allow installs from this app now)   back   notch 0|1 (use the space beside the camera)   online 0|1 (a network game is on: the app
 *        keeps its connection in the background)   paste   textmenu X Y W H … (Android's text toolbar over a field)
 *        copy TEXT (to Android's clipboard) */

#ifdef __ANDROID__

#include "app.h"
#include "sheet.h"
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xfixes.h>
#include <gdk/gdkx.h>
#include <unistd.h>

static Display *dpy;
static Damage damage;
static int damage_base, view_w, view_h;
static double mm_per_px;
static gboolean keyboard_up;
static int keyboard_px; /* the on-screen keyboard's height, 0 when it is down */

gboolean android_keyboard(void) { return keyboard_px > 0; }

static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *line = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    size_t n = strlen(line);
    for (ssize_t w; n && (w = write(1, line, n)) > 0; n -= (size_t) w)
        memmove(line, line + w, n - (size_t) w);
    g_free(line);
}

void android_play(const char *path, int volume) { say("snd %s %d\n", path, volume); }

void android_open_url(const char *url) { say("url %s\n", url); }

void android_export(const char *path) { say("export %s\n", path); }

void android_install(const char *apk) { say("install %s\n", apk); }

void android_allow_install(void) { say("allowinstall\n"); }

void android_input_reset(void) { say("kbreset\n"); }

static GtkWidget *paste_target;

void android_paste(GtkWidget *entry)
{
    if (paste_target)
        g_object_remove_weak_pointer(G_OBJECT(paste_target), (gpointer *) &paste_target);
    paste_target = entry;
    g_object_add_weak_pointer(G_OBJECT(entry), (gpointer *) &paste_target);
    say("paste\n");
}

/* A long press on a text field: Android tells it (as it tells its own fields) and its own text
 * toolbar comes over the field the finger pressed (the keyboard may have moved it since), with its
 * Paste, from Android's clipboard. */
static GtkWidget *pressed_field;

static gboolean press_hook(GSignalInvocationHint *hint, guint n, const GValue *values, gpointer data)
{
    (void) hint, (void) n, (void) data;
    GtkWidget *w = g_value_get_object(&values[0]);
    if (pressed_field)
        g_object_remove_weak_pointer(G_OBJECT(pressed_field), (gpointer *) &pressed_field);
    pressed_field = GTK_IS_ENTRY(w) && gtk_editable_get_editable(GTK_EDITABLE(w)) ? w : NULL;
    if (pressed_field)
        g_object_add_weak_pointer(G_OBJECT(pressed_field), (gpointer *) &pressed_field);
    return TRUE;
}

/* The toolbar over field `f`: on a long press, and as soon as text in it is selected. */
static void text_menu(GtkWidget *f)
{
    if (!f || !gtk_widget_get_mapped(f))
        return;
    if (paste_target)
        g_object_remove_weak_pointer(G_OBJECT(paste_target), (gpointer *) &paste_target);
    paste_target = f;
    g_object_add_weak_pointer(G_OBJECT(f), (gpointer *) &paste_target);
    GtkAllocation a;
    gtk_widget_get_allocation(f, &a);
    int x, y;
    gdk_window_get_origin(gtk_widget_get_window(f), &x, &y);
    if (!gtk_widget_get_has_window(f))
        x += a.x, y += a.y;
    int start, end;
    gboolean selected = gtk_editable_get_selection_bounds(GTK_EDITABLE(f), &start, &end);
    say("textmenu %d %d %d %d %d %d\n", x, y, a.width, a.height, *gtk_entry_get_text(GTK_ENTRY(f)) != 0, selected);
}

static void long_press(void) { text_menu(pressed_field); }

/* A finger lifted from a field with text selected in it (dragged over): the toolbar, as Android's fields. */
static gboolean release_hook(GSignalInvocationHint *hint, guint n, const GValue *values, gpointer data)
{
    (void) hint, (void) n, (void) data;
    GtkWidget *w = g_value_get_object(&values[0]);
    if (w == pressed_field && gtk_editable_get_selection_bounds(GTK_EDITABLE(w), NULL, NULL))
        text_menu(w);
    return TRUE;
}

/* The text toolbar's other actions on the field it is over: "selectall", "copy", "cut". */
static void text_action(const char *what)
{
    GtkWidget *f = paste_target;
    if (!f || !GTK_IS_EDITABLE(f))
        return;
    GtkEditable *e = GTK_EDITABLE(f);
    if (!strcmp(what, "selectall")) {
        gtk_widget_grab_focus(f);
        gtk_editable_select_region(e, 0, -1);
        text_menu(f); /* now with Cut and Copy */
        return;
    }
    int start, end;
    if (!gtk_editable_get_selection_bounds(e, &start, &end))
        return;
    char *text = gtk_editable_get_chars(e, start, end);
    say("copy %s\n", text);
    g_free(text);
    if (!strcmp(what, "cut"))
        gtk_editable_delete_selection(e);
}

static GtkWindow *top_window(void);

/* The clipboard's text, into the field that asked (the paste button, the text toolbar) or else the
 * one being typed in (the keyboard's own Paste). */
static void pasted(const char *text)
{
    GtkWidget *f = paste_target;
    if (!f) {
        GtkWidget *top = drawer_open() ? app.window : GTK_WIDGET(top_window());
        f = top ? gtk_window_get_focus(GTK_WINDOW(top)) : NULL;
    }
    if (!f || !GTK_IS_EDITABLE(f))
        return;
    GtkEditable *e = GTK_EDITABLE(f);
    /* Not focused here: the keyboard follows the focus, and pasting leaves it as it was. */
    gtk_editable_delete_selection(e);
    int pos = gtk_editable_get_position(e);
    gtk_editable_insert_text(e, text, -1, &pos);
    gtk_editable_set_position(e, pos);
    if (paste_target)
        g_object_remove_weak_pointer(G_OBJECT(paste_target), (gpointer *) &paste_target);
    paste_target = NULL;
}

void android_rumble(int ms, int strong, int weak) { say("rumble %d %d %d\n", ms, CLAMP(strong, 0, 255), CLAMP(weak, 0, 255)); }

void android_notch(gboolean on) { say("notch %d\n", on ? 1 : 0); }

void android_vibrate(int ms, int strength) { say("vib %d %d\n", ms, CLAMP(strength, 1, 255)); }

/* ---- the screen ---- */

static GdkFilterReturn damage_filter(GdkXEvent *xev, GdkEvent *ev, gpointer data)
{
    (void) ev, (void) data;
    XEvent *e = xev;
    if (e->type != damage_base + XDamageNotify)
        return GDK_FILTER_CONTINUE;
    XserverRegion r = XFixesCreateRegion(dpy, NULL, 0);
    XDamageSubtract(dpy, damage, None, r);
    int n;
    XRectangle *rects = XFixesFetchRegion(dpy, r, &n);
    for (int i = 0; i < n; i++)
        say("f %d %d %d %d\n", rects[i].x, rects[i].y, rects[i].width, rects[i].height);
    if (rects)
        XFree(rects);
    XFixesDestroyRegion(dpy, r);
    return GDK_FILTER_REMOVE;
}

/* With no window manager, windows go where GTK puts them: dialogs in the middle of the view. */
static void center(GtkWindow *w)
{
    GtkRequisition r;
    gtk_widget_size_request(GTK_WIDGET(w), &r);
    int width, height;
    gtk_window_get_size(w, &width, &height);
    width = MAX(width, r.width), height = MAX(height, r.height);
    gtk_window_move(w, MAX(0, (view_w - width) / 2), MAX(0, (view_h - height) / 2));
}

static gboolean show_hook(GSignalInvocationHint *hint, guint n, const GValue *values, gpointer data)
{
    (void) hint, (void) n, (void) data;
    GtkWidget *w = g_value_get_object(&values[0]);
    if (GTK_IS_WINDOW(w) && w != app.window && gtk_window_get_window_type(GTK_WINDOW(w)) == GTK_WINDOW_TOPLEVEL)
        center(GTK_WINDOW(w));
    return TRUE;
}

/* The X screen is the view (Xvfb starts at the largest one and RandR cuts it down), plus a row
 * below it where the pointer waits between taps, so no button under it stays lit. GTK then
 * sizes and centers its windows for the view, as on a computer's screen. */
static void screen_size(int w, int h)
{
    static RRMode last;
    if (g_getenv("DENBOKU_DEBUG"))
        g_printerr("view %dx%d\n", w, h);
    view_w = w, view_h = h;
    Window root = DefaultRootWindow(dpy);
    if (!mm_per_px)
        mm_per_px = (double) DisplayWidthMM(dpy, DefaultScreen(dpy)) / DisplayWidth(dpy, DefaultScreen(dpy));
    XRRScreenResources *res = XRRGetScreenResources(dpy, root);
    if (!res || !res->ncrtc || !res->noutput) {
        if (res)
            XRRFreeScreenResources(res);
        return;
    }
    RRCrtc crtc = res->crtcs[0];
    RROutput out = res->outputs[0];
    /* The screen cannot get smaller than the mode shown on it: off, resize, then a mode as large. */
    XRRSetCrtcConfig(dpy, res, crtc, CurrentTime, 0, 0, None, RR_Rotate_0, NULL, 0);
    XRRSetScreenSize(dpy, root, w, h + 1, (int) (w * mm_per_px), (int) ((h + 1) * mm_per_px));
    char name[32];
    snprintf(name, sizeof name, "%dx%d", w, h + 1);
    XRRModeInfo info = {.width = (unsigned) w, .height = (unsigned) h + 1, .name = name, .nameLength = strlen(name)};
    RRMode mode = XRRCreateMode(dpy, root, &info);
    XRRAddOutputMode(dpy, out, mode);
    XRRSetCrtcConfig(dpy, res, crtc, CurrentTime, 0, 0, mode, RR_Rotate_0, &out, 1);
    if (last) {
        XRRDeleteOutputMode(dpy, out, last);
        XRRDestroyMode(dpy, last);
    }
    last = mode;
    XRRFreeScreenResources(res);
    XSync(dpy, False);
}

static void set_view(int w, int h)
{
    if (w == view_w && h == view_h)
        return;
    screen_size(w, h);
    gtk_window_resize(GTK_WINDOW(app.window), w, h);
}

/* The phone has more pixels for the same size than a computer screen (DENBOKU_DPI against 96):
 * text grows with the dpi, and the sizes GTK gives in pixels grow as much, so everything keeps
 * the proportions it has on a computer. The values are GTK 2's defaults. */
static void scale_style(void)
{
    const char *env = g_getenv("DENBOKU_DPI");
    int dpi = env ? atoi(env) : 0;
    if (dpi <= 96)
        return;
    g_object_set(gtk_settings_get_default(), "gtk-xft-dpi", dpi * 1024, NULL);
#define S(n) ((n) * dpi / 96)
    char *rc = g_strdup_printf(
        "style \"denboku-dpi\" {\n"
        "  xthickness = %d ythickness = %d\n"
        "  GtkCheckButton::indicator-size = %d GtkCheckButton::indicator-spacing = %d\n"
        "  GtkRange::slider-width = %d GtkRange::stepper-size = %d GtkRange::trough-border = %d\n"
        "  GtkScrollbar::min-slider-length = %d GtkScale::slider-length = %d\n"
        "  GtkComboBox::arrow-size = %d GtkExpander::expander-size = %d GtkTreeView::expander-size = %d\n"
        "  GtkPaned::handle-size = %d GtkNotebook::tab-overlap = %d GtkNotebook::tab-curvature = %d\n"
        "  GtkButton::inner-border = {%d, %d, %d, %d} GtkButton::default-border = {%d, %d, %d, %d}\n"
        "  GtkButton::child-displacement-x = %d GtkButton::child-displacement-y = %d\n"
        "  GtkEntry::inner-border = {%d, %d, %d, %d} GtkMenuItem::toggle-spacing = %d GtkMenuItem::arrow-spacing = %d\n"
        "  GtkMenu::vertical-padding = %d GtkWidget::scroll-arrow-hlength = %d GtkWidget::scroll-arrow-vlength = %d\n"
        "}\n"
        "class \"GtkWidget\" style \"denboku-dpi\"\n",
        S(2), S(2), S(13), S(2), S(14), S(14), S(1), S(21), S(31), S(15), S(10), S(12), S(5), S(2), S(1), S(1), S(1),
        S(1), S(1), S(1), S(1), S(1), S(1), S(1), S(1), S(2), S(2), S(2), S(2), S(5), S(10), S(1), S(16), S(16));
#undef S
    gtk_rc_parse_string(rc);
    g_free(rc);
}

void android_window(GtkWidget *window)
{
    dpy = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    scale_style();
    int w, h;
    const char *view = g_getenv("DENBOKU_VIEW");
    if (view && sscanf(view, "%dx%d", &w, &h) == 2)
        screen_size(w, h);
    else
        view_w = gdk_screen_width(), view_h = gdk_screen_height() - 1;
    gtk_window_set_default_size(GTK_WINDOW(window), view_w, view_h);
    gtk_window_move(GTK_WINDOW(window), 0, 0);
}

/* ---- input ---- */

/* The visible toplevel on top (the last one mapped), as a window manager would focus. */
static GtkWindow *top_window(void)
{
    GtkWindow *top = GTK_WINDOW(app.window);
    GList *all = gtk_window_list_toplevels();
    for (GList *l = all; l; l = l->next)
        if (l->data != app.window && gtk_widget_get_mapped(l->data) &&
            gtk_window_get_window_type(l->data) == GTK_WINDOW_TOPLEVEL && gtk_window_get_modal(l->data))
            top = l->data;
    for (GList *l = all; l && top == GTK_WINDOW(app.window); l = l->next)
        if (l->data != app.window && gtk_widget_get_mapped(l->data) &&
            gtk_window_get_window_type(l->data) == GTK_WINDOW_TOPLEVEL)
            top = l->data;
    g_list_free(all);
    return top;
}

static GtkWidget *focused_text(void)
{
    GtkWindow *top = top_window();
    GtkWidget *f = gtk_window_get_focus(top);
    if (f && gtk_widget_get_mapped(f) && gtk_widget_is_sensitive(f) &&
        ((GTK_IS_ENTRY(f) && gtk_editable_get_editable(GTK_EDITABLE(f))) ||
         (GTK_IS_TEXT_VIEW(f) && gtk_text_view_get_editable(GTK_TEXT_VIEW(f)))))
        return f;
    return NULL;
}

static gboolean keyboard_watch(gpointer data);
static guint watch_id;

static void keyboard_watch_start(void)
{
    if (!watch_id)
        watch_id = g_timeout_add(500, keyboard_watch, NULL);
}

static gboolean keyboard_check(gpointer data)
{
    (void) data;
    GtkWidget *f = focused_text();
    gboolean want = f != NULL;
    if (want != keyboard_up) {
        say("kb %d\n", want);
        if (g_getenv("DENBOKU_DEBUG"))
            g_printerr("keyboard %d: %s in %s\n", want, f ? G_OBJECT_TYPE_NAME(f) : "-",
                       f ? gtk_window_get_title(GTK_WINDOW(gtk_widget_get_toplevel(f))) : "-");
    }
    if (want)
        keyboard_watch_start();
    keyboard_up = want;
    return FALSE;
}

/* While the keyboard is up, its field is looked for now and then: a drawer that takes the place
 * of another (the password's, then the lobby) can leave no focus change to say it went. */
static gboolean keyboard_watch(gpointer data)
{
    if (keyboard_up)
        keyboard_check(data);
    if (!keyboard_up)
        watch_id = 0;
    return keyboard_up;
}

/* A tap on the text field that has the focus asks for the keyboard again: closed with Back or
 * its own button, it left the focus there, so nothing changed to open it the next time. */
static gboolean tap_check(gpointer data)
{
    GtkWidget *w = data, *f = focused_text();
    if (f && f == w) {
        say("kb 1\n");
        keyboard_up = TRUE;
        keyboard_watch_start();
    }
    g_object_unref(w);
    return FALSE;
}

static gboolean tap_hook(GSignalInvocationHint *hint, guint n, const GValue *values, gpointer data)
{
    (void) hint, (void) n, (void) data;
    GtkWidget *w = g_value_get_object(&values[0]);
    if (GTK_IS_ENTRY(w) || GTK_IS_TEXT_VIEW(w))
        g_idle_add(tap_check, g_object_ref(w));
    return TRUE;
}

/* A tick under the finger: a button, and a tap on the game (the board, the bar, the menu). */
static gboolean tick_hook(GSignalInvocationHint *hint, guint n, const GValue *values, gpointer data)
{
    (void) hint, (void) n, (void) values, (void) data;
    feedback_tick();
    return TRUE;
}

static gboolean view_tick_cb(GtkWidget *w, GdkEventButton *e, gpointer data)
{
    (void) w, (void) e, (void) data;
    feedback_tick();
    return FALSE;
}

static gboolean refocus(gpointer data);

static gboolean focus_hook(GSignalInvocationHint *hint, guint n, const GValue *values, gpointer data)
{
    (void) hint, (void) n, (void) data;
    GtkWidget *w = g_value_get_object(&values[0]);
    if (GTK_IS_WINDOW(w) && gtk_window_get_window_type(GTK_WINDOW(w)) == GTK_WINDOW_TOPLEVEL && w != app.window)
        g_idle_add(refocus, NULL);
    g_idle_add(keyboard_check, NULL);
    return TRUE;
}

static void focus_window(GtkWidget *w)
{
    GdkWindow *g = gtk_widget_get_window(w);
    if (!g)
        return;
    gdk_error_trap_push();
    XSetInputFocus(dpy, GDK_WINDOW_XID(g), RevertToPointerRoot, CurrentTime);
    gdk_flush();
    gdk_error_trap_pop();
}

/* A window manager's focus: a window that opens takes it, and it goes back when that closes. */
static gboolean map_hook(GSignalInvocationHint *hint, guint n, const GValue *values, gpointer data)
{
    (void) hint, (void) n, (void) data;
    GtkWidget *w = g_value_get_object(&values[0]);
    if (GTK_IS_WINDOW(w) && gtk_window_get_window_type(GTK_WINDOW(w)) == GTK_WINDOW_TOPLEVEL)
        focus_window(w);
    return TRUE;
}

static gboolean refocus(gpointer data)
{
    (void) data;
    focus_window(GTK_WIDGET(top_window()));
    return FALSE;
}

/* After a tap, the X focus goes to the window under it (click to focus). Not before the press:
 * a focus change then breaks the grab of what the press opens (a combo box's list). */
static void focus_under(void)
{
    Window root, child;
    int rx, ry, x, y;
    unsigned mask;
    if (XQueryPointer(dpy, DefaultRootWindow(dpy), &root, &child, &rx, &ry, &x, &y, &mask) && child) {
        gdk_error_trap_push();
        XSetInputFocus(dpy, child, RevertToPointerRoot, CurrentTime);
        gdk_flush();
        gdk_error_trap_pop();
    }
}

/* A key with no keycode (a letter of another script) borrows a free one for a moment. */
static void type_keysym(KeySym ks)
{
    static int spare[8], nspare = -1, next;
    KeyCode code = XKeysymToKeycode(dpy, ks);
    if (!code || XkbKeycodeToKeysym(dpy, code, 0, 0) != ks) {
        if (nspare < 0) {
            int min, max, per;
            XDisplayKeycodes(dpy, &min, &max);
            KeySym *map = XGetKeyboardMapping(dpy, (KeyCode) min, max - min + 1, &per);
            nspare = 0;
            for (int c = max; c >= min && nspare < 8; c--) {
                gboolean empty = TRUE;
                for (int i = 0; i < per; i++)
                    empty &= map[(c - min) * per + i] == NoSymbol;
                if (empty)
                    spare[nspare++] = c;
            }
            XFree(map);
        }
        if (!nspare)
            return;
        code = (KeyCode) spare[next++ % nspare];
        KeySym syms[2] = {ks, ks};
        XChangeKeyboardMapping(dpy, code, 2, syms, 1);
        XSync(dpy, False);
    }
    XTestFakeKeyEvent(dpy, code, True, CurrentTime);
    XTestFakeKeyEvent(dpy, code, False, CurrentTime);
    XFlush(dpy);
}

/* Backspace in the focused field straight away, not through the X server: the keyboard's
 * corrections send Backspaces and then text, which goes in straight away (type_text). */
static gboolean erase_back(void)
{
    GtkWidget *f = focused_text();
    if (GTK_IS_ENTRY(f)) {
        GtkEditable *e = GTK_EDITABLE(f);
        int pos = gtk_editable_get_position(e);
        if (gtk_editable_get_selection_bounds(e, NULL, NULL))
            gtk_editable_delete_selection(e);
        else if (pos > 0)
            gtk_editable_delete_text(e, pos - 1, pos);
        return TRUE;
    }
    if (GTK_IS_TEXT_VIEW(f)) {
        GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(f));
        GtkTextIter at;
        if (!gtk_text_buffer_delete_selection(b, TRUE, TRUE)) {
            gtk_text_buffer_get_iter_at_mark(b, &at, gtk_text_buffer_get_insert(b));
            gtk_text_buffer_backspace(b, &at, TRUE, TRUE);
        }
        return TRUE;
    }
    return FALSE;
}

/* Typed text goes into the focused field, as GTK would put it; without one, as key presses. */
static void type_text(const char *text)
{
    GtkWidget *f = focused_text();
    if (GTK_IS_ENTRY(f)) {
        GtkEditable *e = GTK_EDITABLE(f);
        gtk_editable_delete_selection(e);
        int pos = gtk_editable_get_position(e);
        gtk_editable_insert_text(e, text, -1, &pos);
        gtk_editable_set_position(e, pos);
    } else if (GTK_IS_TEXT_VIEW(f))
        gtk_text_buffer_insert_interactive_at_cursor(gtk_text_view_get_buffer(GTK_TEXT_VIEW(f)), text, -1, TRUE);
    else
        for (const char *p = text; *p; p = g_utf8_next_char(p)) {
            gunichar c = g_utf8_get_char(p);
            type_keysym(c < 0x100 ? c : 0x1000000 + c);
        }
}

/* Back closes what is open (a menu, a dialog, the menu sheet, or a drawer's page, back to the
 * page under it, as Escape does); with only the board, the app goes to the background. */
static void back(void)
{
    GtkWindow *top = top_window();
    gboolean popup = FALSE;
    GList *all = gtk_window_list_toplevels();
    for (GList *l = all; l; l = l->next)
        popup |= gtk_widget_get_mapped(l->data) && gtk_window_get_window_type(l->data) == GTK_WINDOW_POPUP &&
                 GTK_IS_MENU(gtk_bin_get_child(l->data));
    g_list_free(all);
    if (top == GTK_WINDOW(app.window) && !popup && !drawer_open() && !sheet_is_open())
        say("back\n");
    else
        type_keysym(GDK_Escape);
}

/* The finger's last places, each with Android's time: an X event's own time is when this process
 * injected it, and a busy main loop injects several at once, which would make a flick's speed. */
#define TOUCHES 32
static struct {
    int x, y;
    guint32 t;
} touches[TOUCHES];
static int touch_next;
static gboolean move_pending; /* touches' newest: a move not injected yet */

guint32 android_touch_time(int x, int y, guint32 time)
{
    for (int i = 1; i <= TOUCHES; i++) {
        int k = (touch_next + TOUCHES - i) % TOUCHES;
        if (touches[k].t && touches[k].x == x && touches[k].y == y)
            return touches[k].t;
    }
    return time;
}

static void touch(char what, int x, int y, guint32 t)
{
    if (!move_pending)
        touch_next = (touch_next + 1) % TOUCHES;
    touches[(touch_next + TOUCHES - 1) % TOUCHES] = (typeof(*touches)) {x, y, t ? t : 1};
    move_pending = what == 'm';
    if (move_pending)
        return;
    XTestFakeMotionEvent(dpy, -1, x, y, CurrentTime);
    XTestFakeButtonEvent(dpy, 1, what == 'd', CurrentTime);
    if (what == 'u')
        focus_under();
    XFlush(dpy);
}

/* The moves that came in a bunch go to X as one, the last: each would redraw a scrolled list. */
static void inject_move(void)
{
    if (!move_pending)
        return;
    move_pending = FALSE;
    const typeof(*touches) *m = &touches[(touch_next + TOUCHES - 1) % TOUCHES];
    XTestFakeMotionEvent(dpy, -1, m->x, m->y, CurrentTime);
    XFlush(dpy);
}

static void input_line(char *line)
{
    line[strcspn(line, "\r\n")] = 0; /* not g_strchomp(): a space typed alone is "t " */
    int x, y;
    unsigned t = 0;
    unsigned long ks;
    if (sscanf(line, "%*c %d %d %u", &x, &y, &t) >= 2 && strchr("dmu", line[0]) && line[1] == ' ') {
        touch(line[0], x, y, t);
        return;
    }
    inject_move();
    if (!strcmp(line, "p")) {
        XTestFakeMotionEvent(dpy, -1, 0, view_h, CurrentTime);
        XFlush(dpy);
    } else if (sscanf(line, "w %d", &x) == 1) {
        XTestFakeButtonEvent(dpy, (unsigned) x, True, CurrentTime);
        XTestFakeButtonEvent(dpy, (unsigned) x, False, CurrentTime);
        XFlush(dpy);
    } else if (sscanf(line, "k %lu", &ks) == 1) {
        if (ks != GDK_BackSpace || !erase_back())
            type_keysym(ks);
    }
    else if (g_str_has_prefix(line, "clip "))
        pasted(line + 5);
    else if (!strcmp(line, "h"))
        long_press();
    else if (g_str_has_prefix(line, "tm "))
        text_action(line + 3);
    else if (g_str_has_prefix(line, "t "))
        type_text(line + 2);
    else if (sscanf(line, "g %lu", &ks) == 1)
        gamepad_android_bits((guint32) ks);
    else if (g_str_has_prefix(line, "gn ")) {
        char **f = g_strsplit(line + 3, " ", 2);
        if (f[0] && f[1])
            gamepad_android_name(strcmp(f[0], "-") ? f[0] : NULL, f[1]);
        g_strfreev(f);
    } else if (!strcmp(line, "b"))
        back();
    else if (!strcmp(line, "installfail"))
        toast(tr("The update could not be installed; nothing was changed."));
    else if (sscanf(line, "v %d %d", &x, &y) == 2) {
        int kb = 0;
        sscanf(line, "v %*d %*d %d", &kb);
        keyboard_px = kb;
        set_view(x, y);
    }
}

static gboolean input_cb(GIOChannel *ch, GIOCondition cond, gpointer data)
{
    (void) data;
    do {
        char *line = NULL;
        GIOStatus st = cond & G_IO_IN ? g_io_channel_read_line(ch, &line, NULL, NULL, NULL) : G_IO_STATUS_EOF;
        if (st == G_IO_STATUS_EOF || st == G_IO_STATUS_ERROR) {
            gtk_main_quit(); /* the app is gone */
            return FALSE;
        }
        if (!line)
            break;
        input_line(line);
        g_free(line);
    } while (g_io_channel_get_buffer_condition(ch) & G_IO_IN);
    inject_move();
    return TRUE;
}

/* Android cuts the network of an app in the background: while a network game is on (a LAN room,
 * reconnecting, or a lishogi or CSA game being played), the app stays a foreground service. */
static gboolean online_check(gpointer data)
{
    (void) data;
    static int was = -1;
    /* Not once the game is over: its notification would stay for as long as the room does. */
    gboolean playing = app.game.result == RESULT_NONE;
    int on = ((app.net || app.net_suspended) && (playing || app.wait_dialog)) || (server_game() && playing);
    if (on != was)
        say("online %d\n", on);
    was = on;
    return TRUE;
}

void android_start(void)
{
    int err;
    if (XDamageQueryExtension(dpy, &damage_base, &err)) {
        damage = XDamageCreate(dpy, DefaultRootWindow(dpy), XDamageReportNonEmpty);
        gdk_window_add_filter(NULL, damage_filter, NULL);
    }
    say("f 0 0 %d %d\n", gdk_screen_width(), gdk_screen_height());
    g_signal_add_emission_hook(g_signal_lookup("show", GTK_TYPE_WIDGET), 0, show_hook, NULL, NULL);
    g_signal_add_emission_hook(g_signal_lookup("focus-in-event", GTK_TYPE_WIDGET), 0, focus_hook, NULL, NULL);
    g_signal_add_emission_hook(g_signal_lookup("focus-out-event", GTK_TYPE_WIDGET), 0, focus_hook, NULL, NULL);
    g_signal_add_emission_hook(g_signal_lookup("unmap", GTK_TYPE_WIDGET), 0, focus_hook, NULL, NULL);
    g_signal_add_emission_hook(g_signal_lookup("map-event", GTK_TYPE_WIDGET), 0, map_hook, NULL, NULL);
    g_signal_add_emission_hook(g_signal_lookup("button-release-event", GTK_TYPE_WIDGET), 0, tap_hook, NULL, NULL);
    g_signal_add_emission_hook(g_signal_lookup("clicked", GTK_TYPE_BUTTON), 0, tick_hook, NULL, NULL);
    g_signal_add_emission_hook(g_signal_lookup("button-press-event", GTK_TYPE_WIDGET), 0, press_hook, NULL, NULL);
    g_signal_add_emission_hook(g_signal_lookup("button-release-event", GTK_TYPE_WIDGET), 0, release_hook, NULL, NULL);
    g_signal_connect(view_widget(app.view), "button-press-event", G_CALLBACK(view_tick_cb), NULL);
    GIOChannel *ch = g_io_channel_unix_new(0);
    g_io_add_watch(ch, G_IO_IN | G_IO_HUP | G_IO_ERR, input_cb, NULL);
    g_timeout_add_seconds(1, online_check, NULL);
}

#endif
