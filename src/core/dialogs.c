/* Dialog helpers, and the Kindle keyboard for text fields. */

#include "app.h"

#ifndef G_OS_WIN32
#include <gdk/gdkx.h>
#include <X11/Xutil.h>
#endif

/* ---- dialogs ---- */

/* Open dialogs, newest first: a dialog opened over another must stack above it. */
static GSList *dialogs;

static void dialog_gone_cb(GtkWidget *d, gpointer data)
{
    (void) data;
    dialogs = g_slist_remove(dialogs, d);
}

/* A thin frame around the dialog: on the Kindle, and on screens without window borders, a
 * dialog over the board would otherwise run into it. */
static gboolean dialog_frame_cb(GtkWidget *w, GdkEventExpose *e, gpointer data)
{
    (void) e, (void) data;
    GtkAllocation a;
    gtk_widget_get_allocation(w, &a);
    GtkStyle *st = gtk_widget_get_style(w);
    gdk_draw_rectangle(gtk_widget_get_window(w), st->fg_gc[GTK_STATE_NORMAL], FALSE, 0, 0, a.width - 1, a.height - 1);
    return FALSE;
}

/* GTK2 on Windows leaves a line where the dialog used to end after it shrinks or its text
 * changes; repaint the whole window whenever its allocation changes. */
static void dialog_repaint_cb(GtkWidget *w, GtkAllocation *a, gpointer data)
{
    (void) a, (void) data;
    gtk_widget_queue_draw(w);
    GdkWindow *win = gtk_widget_get_window(w);
    if (win)
        gdk_window_invalidate_rect(win, NULL, TRUE);
}

/* Repaints a screen rectangle (all of each window for NULL) in every app window, from the main
 * loop: redrawing the whole board and dialog synchronously made the Kindle unresponsive. */
static void repaint_area(const GdkRectangle *area)
{
    GList *tops = gtk_window_list_toplevels();
    for (GList *l = tops; l; l = l->next) {
        GtkWidget *w = l->data;
        GdkWindow *win = gtk_widget_get_window(w);
        if (!gtk_widget_get_mapped(w) || !win)
            continue;
        GtkAllocation a;
        gtk_widget_get_allocation(w, &a);
        GdkRectangle r = {0, 0, a.width, a.height};
        if (area) {
            int ox = 0, oy = 0;
            gdk_window_get_origin(win, &ox, &oy);
            GdkRectangle in = {area->x - ox, area->y - oy, area->width, area->height};
            if (!gdk_rectangle_intersect(&r, &in, &r))
                continue;
        }
        gdk_window_invalidate_rect(win, &r, TRUE);
        gtk_widget_queue_draw_area(w, r.x, r.y, r.width, r.height);
    }
    g_list_free(tops);
}

static gboolean repaint_area_cb(gpointer data)
{
    repaint_area(data);
    g_free(data);
    return FALSE;
}

/* The Kindle can drop the e-ink update of a dialog that opens while the board is redrawn (the
 * "connection lost" one came up all white), and of what it covered when it closes (a white
 * square stayed): draw each once more a little later. */
static gboolean dialog_redraw_cb(gpointer data)
{
    GtkWidget *w = data;
    GdkWindow *win = gtk_widget_get_window(w);
    if (gtk_widget_get_mapped(w) && win)
        gdk_window_invalidate_rect(win, NULL, TRUE);
    g_object_unref(w);
    return FALSE;
}

static gboolean dialog_mapped_cb(GtkWidget *w, GdkEvent *e, gpointer data)
{
    (void) e, (void) data;
    g_timeout_add(500, dialog_redraw_cb, g_object_ref(w));
    return FALSE;
}

static void dialog_unmapped_cb(GtkWidget *w, gpointer data)
{
    (void) data;
    GdkWindow *win = gtk_widget_get_window(w);
    if (!win)
        return;
    GtkAllocation a;
    gtk_widget_get_allocation(w, &a);
    GdkRectangle *r = g_new(GdkRectangle, 1);
    gdk_window_get_origin(win, &r->x, &r->y);
    r->width = a.width, r->height = a.height;
    g_timeout_add(500, repaint_area_cb, r);
}

/* ✕ closes a dialog as a computer's title bar does (the Kindle shows none). */
static void close_dialog_cb(GtkWidget *w, gpointer d)
{
    (void) w;
    gtk_dialog_response(GTK_DIALOG(d), GTK_RESPONSE_DELETE_EVENT);
}

GtkWidget *dialog_new(const char *title)
{
    GtkWidget *d = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(d), app.kindle ? KINDLE_DIALOG : title);
    gtk_window_set_transient_for(GTK_WINDOW(d), GTK_WINDOW(dialogs ? dialogs->data : app.window));
    dialogs = g_slist_prepend(dialogs, d);
    g_signal_connect(d, "destroy", G_CALLBACK(dialog_gone_cb), NULL);
    gtk_window_set_modal(GTK_WINDOW(d), TRUE);
    /* Dialogs that show or hide rows change size: keep them on the screen. */
    gtk_window_set_position(GTK_WINDOW(d), GTK_WIN_POS_CENTER_ALWAYS);
    if (drawer_ready()) {
        drawer_wrap(d, title);
        return d;
    }
    gtk_container_set_border_width(GTK_CONTAINER(d), 12);
    g_signal_connect_after(d, "expose-event", G_CALLBACK(dialog_frame_cb), NULL);
    g_signal_connect_after(d, "size-allocate", G_CALLBACK(dialog_repaint_cb), NULL);
    if (app.kindle) {
        GtkWidget *bar = gtk_hbox_new(FALSE, 0), *close = icon_button(ICON_CLOSE, G_CALLBACK(close_dialog_cb), d);
        gtk_widget_set_can_focus(close, FALSE);
        gtk_box_pack_end(GTK_BOX(bar), close, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), bar, FALSE, FALSE, 0);
        g_signal_connect(d, "map-event", G_CALLBACK(dialog_mapped_cb), NULL);
        g_signal_connect(d, "unmap", G_CALLBACK(dialog_unmapped_cb), NULL);
    }
    return d;
}

/* Shows a dialog until it is answered, then destroys it: the response. */
int run_dialog(GtkWidget *d)
{
    gtk_widget_show_all(d);
    int r = gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
    return r;
}

static GtkWidget *text_dialog(const char *title, const char *text)
{
    GtkWidget *d = dialog_new(title ? title : tr("Denboku Shōgi"));
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), row_para(text), TRUE, TRUE, 12);
    return d;
}

/* News that needs no answer: a passing notice over the board instead of a dialog with OK. */
void toast(const char *text)
{
    view_toast(app.view, text);
}

void message(const char *text)
{
    GtkWidget *d = text_dialog(NULL, text);
    gtk_dialog_add_button(GTK_DIALOG(d), tr("OK"), GTK_RESPONSE_OK);
    run_dialog(d);
}

gboolean confirm_titled(const char *title, const char *text, const char *ok)
{
    GtkWidget *d = text_dialog(title, text);
    gtk_dialog_add_button(GTK_DIALOG(d), tr("Cancel"), GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(d), ok, GTK_RESPONSE_OK);
    return run_dialog(d) == GTK_RESPONSE_OK;
}

/* Titled with what it does (Resign, Reset, Delete), as the other drawers are with what they hold. */
gboolean confirm_with(const char *text, const char *ok) { return confirm_titled(ok, text, ok); }

gboolean confirm(const char *text)
{
    return confirm_with(text, tr("Delete"));
}

/* An open drop-down list stays on screen when the user switches to another application (its
 * grab does not survive the window manager's switch). While one is open, watch which window the
 * window manager calls active, and close the list when it is another program's. (GTK's own
 * "is-active" cannot tell: the list's grab already makes the dialog inactive.) */
static gboolean popup_watch_cb(gpointer combo)
{
    gboolean shown;
    g_object_get(combo, "popup-shown", &shown, NULL);
    if (!shown) {
        g_object_set_data(combo, "popup-watch", NULL);
        return FALSE;
    }
    GdkWindow *active = gdk_screen_get_active_window(gdk_screen_get_default());
    if (!active) /* not known (no window manager, or Windows) */
        return TRUE;
    gboolean ours = FALSE;
    GList *all = gtk_window_list_toplevels();
    for (GList *l = all; l && !ours; l = l->next)
        ours = gtk_widget_get_window(l->data) == active;
    g_list_free(all);
    g_object_unref(active);
    if (!ours)
        gtk_combo_box_popdown(GTK_COMBO_BOX(combo));
    return TRUE;
}

static void popup_shown_cb(GObject *combo, GParamSpec *spec, gpointer data)
{
    (void) spec, (void) data;
    gboolean shown;
    g_object_get(combo, "popup-shown", &shown, NULL);
    if (shown && !g_object_get_data(combo, "popup-watch")) {
        g_object_set_data(combo, "popup-watch", GINT_TO_POINTER(1));
        g_timeout_add_full(G_PRIORITY_DEFAULT, 300, popup_watch_cb, g_object_ref(combo), g_object_unref);
    }
}

void close_popup_on_leave(GtkWidget *combo) { g_signal_connect(combo, "notify::popup-shown", G_CALLBACK(popup_shown_cb), NULL); }

GtkWidget *combo(const char *const *items, int n, int active, gboolean translate)
{
    GtkWidget *c = gtk_combo_box_new_text();
    close_popup_on_leave(c);
    for (int i = 0; i < n; i++)
        gtk_combo_box_append_text(GTK_COMBO_BOX(c), translate ? tr(items[i]) : items[i]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(c), active);
    return c;
}

#ifndef G_OS_WIN32
static gboolean keyboard_covers;
static GdkRectangle keyboard_area;
#endif

/* Open dialogs in the middle of the part of the screen an on-screen keyboard leaves free (the
 * Kindle's is a window of its own; Android's makes the screen smaller). One taller than that
 * space keeps its focused field just above the keyboard. */
/* The Kindle keyboard is up over the lower part of the screen: where it starts, else 0. */
int keyboard_covering(void)
{
#ifndef G_OS_WIN32
    return keyboard_covers && keyboard_area.y > gdk_screen_height() / 3 ? keyboard_area.y : 0;
#else
    return 0;
#endif
}

void center_dialogs(void)
{
    drawer_layout();
    chat_layout();
    int sw = gdk_screen_width(), free_h = gdk_screen_height();
#ifndef G_OS_WIN32
    if (keyboard_covers && keyboard_area.y > free_h / 3)
        free_h = MIN(free_h, keyboard_area.y);
#endif
    GList *all = gtk_window_list_toplevels();
    for (GList *l = all; l; l = l->next) {
        GtkWindow *w = l->data;
        if (!GTK_IS_DIALOG(w) || !gtk_widget_get_visible(GTK_WIDGET(w)))
            continue;
        int width, height;
        gtk_window_get_size(w, &width, &height);
        int y = MAX(0, (free_h - height) / 2), fx, fy;
        GtkWidget *focus = gtk_window_get_focus(w);
        if (height > free_h && focus && gtk_widget_translate_coordinates(focus, GTK_WIDGET(w), 0, 0, &fx, &fy)) {
            GtkAllocation fa;
            gtk_widget_get_allocation(focus, &fa);
            y = MIN(0, free_h - (fy + fa.height) - 8);
        }
        gtk_window_move(w, MAX(0, (sw - width) / 2), y);
    }
    g_list_free(all);
}

/* The Kindle shows its on-screen keyboard only when asked to, and hides it only when told.
 * Only the focused field owns it; a delayed close from the previous dialog must not hide the
 * keyboard that a new dialog just opened. */
static GtkWidget *keyboard_owner;

#ifndef G_OS_WIN32
static gboolean keyboard_open; /* the on-screen keyboard is already asked to show */
/* The Kindle keyboard is a separate X window of class "kb". When it hides, the e-ink screen does
 * not repaint the area it covered, so watch its map state and redraw every app window then.
 * The root window reports its children's changes (SubstructureNotify); without that, poll. */
static gboolean keyboard_watching, keyboard_seen, keyboard_area_ok, root_filtered, root_mask_added;
static guint keyboard_poll, keyboard_give_up, repaint_later_id;

static Window find_keyboard_window(Display *dpy)
{
    Window root = DefaultRootWindow(dpy), rr, parent, *kids = NULL, found = 0;
    unsigned n = 0;
    if (!XQueryTree(dpy, root, &rr, &parent, &kids, &n))
        return 0;
    for (unsigned i = 0; i < n && !found; i++) {
        XClassHint ch = {0};
        if (XGetClassHint(dpy, kids[i], &ch)) {
            /* The real keyboard has class "Kb" (name "kb") and a large window; there is also a
             * 10×10 placeholder with the same class. */
            if (ch.res_class && !g_ascii_strcasecmp(ch.res_class, "kb")) {
                XWindowAttributes a;
                if (XGetWindowAttributes(dpy, kids[i], &a) && a.width > 100 && a.height > 100)
                    found = kids[i];
            }
            if (ch.res_name)
                XFree(ch.res_name);
            if (ch.res_class)
                XFree(ch.res_class);
        }
    }
    if (kids)
        XFree(kids);
    return found;
}

static gboolean repaint_keyboard_area_cb(gpointer data)
{
    (void) data;
    repaint_later_id = 0;
    repaint_area(keyboard_area_ok ? &keyboard_area : NULL);
    return FALSE;
}

static void repaint_keyboard_area_later(void)
{
    if (!repaint_later_id)
        repaint_later_id = g_timeout_add(250, repaint_keyboard_area_cb, NULL);
}

static void keyboard_watch_stop(void)
{
    if (keyboard_covers) {
        keyboard_covers = FALSE;
        center_dialogs();
    }
    if (keyboard_poll)
        g_source_remove(keyboard_poll);
    if (keyboard_give_up)
        g_source_remove(keyboard_give_up);
    keyboard_poll = keyboard_give_up = 0;
    keyboard_watching = keyboard_seen = keyboard_open = FALSE;
}

static void keyboard_check(void)
{
    Display *dpy = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    /* The keyboard window can be destroyed, not only unmapped: look it up again instead of
     * touching a stale XID (a BadWindow there would end the program). */
    XWindowAttributes a;
    gdk_error_trap_push();
    Window w = find_keyboard_window(dpy);
    gboolean up = w && XGetWindowAttributes(dpy, w, &a) && a.map_state != IsUnmapped;
    /* Hidden, it often stays mapped: its name says it, "…_KBS:S_…" shown and "…_KBS:H_…" hidden. */
    char *name = NULL, shown = 0;
    if (w && XFetchName(dpy, w, &name) && name) {
        const char *kbs = strstr(name, "KBS:");
        shown = kbs ? kbs[4] : 0;
        XFree(name);
    }
    if (shown)
        up = up && shown == 'S';
    /* Asked for, and shown by its own account, but still unmapped by our close (unmap_keyboard_cb):
     * it does not map itself again, and stayed away until restarted (keyboard_kick_cb, 6 s). */
    if (!up && shown == 'S' && keyboard_open && w)
        XMapRaised(dpy, w);
    gdk_error_trap_pop();
    if (up) {
        keyboard_seen = TRUE;
        keyboard_area = (GdkRectangle) {a.x, a.y, a.width, a.height};
        keyboard_area_ok = a.width > 100 && a.height > 50;
        if (keyboard_area_ok && !keyboard_covers) {
            keyboard_covers = TRUE;
            center_dialogs();
        }
    } else if (keyboard_seen) {
        keyboard_watch_stop();
        repaint_keyboard_area_later();
    }
}

static gboolean keyboard_poll_cb(gpointer data)
{
    (void) data;
    keyboard_check();
    return keyboard_poll != 0;
}

/* Not up 10 s after it was asked for: stop waiting. */
static gboolean keyboard_give_up_cb(gpointer data)
{
    (void) data;
    keyboard_give_up = 0;
    if (!keyboard_seen)
        keyboard_watch_stop();
    return FALSE;
}

static GdkFilterReturn root_filter_cb(GdkXEvent *xevent, GdkEvent *event, gpointer data)
{
    (void) event, (void) data;
    XEvent *x = xevent;
    Window w;
    switch (x->type) {
    case MapNotify: w = x->xmap.window; break;
    case UnmapNotify: w = x->xunmap.window; break;
    case ConfigureNotify: w = x->xconfigure.window; break;
    case DestroyNotify: w = x->xdestroywindow.window; break;
    case ReparentNotify: w = x->xreparent.window; break;
    case CreateNotify: w = x->xcreatewindow.window; break;
    case GravityNotify: w = x->xgravity.window; break;
    case CirculateNotify: w = x->xcirculate.window; break;
    default: return GDK_FILTER_CONTINUE;
    }
    if (w == x->xany.window)
        return GDK_FILTER_CONTINUE;
    if (keyboard_watching && x->type != CreateNotify)
        keyboard_check();
    /* GDK would take a child's unmap for the root's own (and drop its grabs). */
    return root_mask_added ? GDK_FILTER_REMOVE : GDK_FILTER_CONTINUE;
}

static void keyboard_watch_start(void)
{
    if (keyboard_watching)
        return;
    keyboard_watching = TRUE;
    keyboard_seen = keyboard_area_ok = FALSE;
    keyboard_give_up = g_timeout_add(10000, keyboard_give_up_cb, NULL);
    Display *dpy = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    GdkWindow *root = gdk_get_default_root_window();
    XWindowAttributes ra;
    gdk_error_trap_push();
    gboolean ok = XGetWindowAttributes(dpy, GDK_WINDOW_XID(root), &ra);
    /* Each time: GDK may have set the root's events since. */
    if (ok)
        XSelectInput(dpy, GDK_WINDOW_XID(root), ra.your_event_mask | SubstructureNotifyMask);
    /* Polled too: the name that tells it hidden changes with no event on the root. */
    keyboard_poll = g_timeout_add(300, keyboard_poll_cb, NULL);
    if (!gdk_error_trap_pop() && ok && !root_filtered) {
        root_mask_added = !(ra.your_event_mask & SubstructureNotifyMask);
        gdk_window_add_filter(root, root_filter_cb, NULL);
        root_filtered = TRUE;
    }
    keyboard_check();
}

/* The keyboard daemon's close does not always unmap its container window (it can stay as a
 * blank white band). Unmap it ourselves after asking it to close. */
static gboolean unmap_keyboard_cb(gpointer data)
{
    (void) data;
    Display *dpy = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    if (!dpy)
        return FALSE;
    gdk_error_trap_push();
    Window w = find_keyboard_window(dpy);
    if (w)
        XUnmapWindow(dpy, w);
    gdk_error_trap_pop();
    return FALSE;
}
#endif

/* The keyboard asks the app it was opened for over D-Bus (its text around the cursor, on
 * Backspace): a name without a dot is not a valid bus name, and D-Bus then drops the keyboard
 * process, which closes it. */
#define KEYBOARD_APP "net.denboku"

#ifndef G_OS_WIN32
/* The keyboard asked for and still not up: the Kindle's keyboard service can hang (it then opens
 * for no app, not even the Kindle's own search), and only restarting it brings it back. Once per
 * hang: if that does not help either, it is left alone. */
static gboolean keyboard_kick_cb(gpointer data)
{
    (void) data;
    static gboolean kicked;
    if (!keyboard_open || keyboard_covers) {
        kicked = keyboard_covers ? FALSE : kicked;
        return FALSE;
    }
    if (kicked)
        return FALSE;
    kicked = TRUE;
    g_spawn_command_line_async("sh -c 'initctl stop kb >/dev/null 2>&1; sleep 1; initctl start kb >/dev/null 2>&1; "
                               "sleep 2; lipc-set-prop com.lab126.keyboard open " KEYBOARD_APP ":abc:0'", NULL);
    return FALSE;
}
#endif

static void kindle_keyboard(gboolean show)
{
#ifdef G_OS_WIN32
    (void) show;
#else
    if (g_getenv("DENBOKU_DEBUG"))
        g_printerr("%.3f keyboard %s (asked before: %d)\n", g_get_monotonic_time() / 1e6, show ? "open" : "close", keyboard_open);
    if (show) {
        if (keyboard_open)
            return; /* already up: reopening it makes the WM steal the focus again */
        keyboard_open = TRUE;
        keyboard_watch_start();
        /* The keyboard daemon may not be running after a framework restart; start it first. */
        g_spawn_command_line_async("sh -c 'pidof kb >/dev/null 2>&1 || { initctl start kb >/dev/null 2>&1; "
                                   "sleep 1; }; lipc-set-prop com.lab126.keyboard open " KEYBOARD_APP ":abc:0'", NULL);
        g_timeout_add(2500, keyboard_kick_cb, NULL);
    } else {
        keyboard_open = FALSE;
        repaint_keyboard_area_later();
        g_spawn_command_line_async("lipc-set-prop com.lab126.keyboard close " KEYBOARD_APP, NULL);
        g_timeout_add(250, unmap_keyboard_cb, NULL);
    }
#endif
}

/* Focus a field after the dialog has been shown and the window manager has given it the
 * focus. On the Kindle, focusing it immediately after show_all often left the keyboard closed,
 * and opening the keyboard can take the focus back to the game window: try again a few times. */
typedef struct {
    GtkWidget *entry;
    int tries;
} FocusLater;

static gboolean focus_entry_cb(gpointer data)
{
    FocusLater *f = data;
    if (gtk_widget_get_mapped(f->entry)) {
        GtkWidget *top = gtk_widget_get_toplevel(f->entry);
        if (GTK_IS_WINDOW(top)) {
            GdkWindow *win = gtk_widget_get_window(top);
            if (win) {
                gdk_window_raise(win);
                gdk_window_focus(win, GDK_CURRENT_TIME);
#ifndef G_OS_WIN32
                if (app.kindle) {
                    /* The Kindle window manager often keeps the X focus on the game window,
                     * even after present(); set it on the dialog's GDK focus proxy directly. */
                    Display *dpy = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
                    Window parent = GDK_WINDOW_XID(win), rr, p, *kids = NULL;
                    unsigned n = 0;
                    if (dpy && XQueryTree(dpy, parent, &rr, &p, &kids, &n)) {
                        for (unsigned i = 0; i < n; i++) {
                            XWindowAttributes a;
                            if (XGetWindowAttributes(dpy, kids[i], &a) && a.width <= 1 && a.height <= 1) {
                                XSetInputFocus(dpy, kids[i], RevertToParent, CurrentTime);
                                break;
                            }
                        }
                        if (kids)
                            XFree(kids);
                    }
                }
#endif
            }
            gtk_window_present(GTK_WINDOW(top));
            gtk_window_set_focus(GTK_WINDOW(top), f->entry);
        }
        gtk_widget_grab_focus(f->entry);
        if (app.kindle && f->tries == 6) {
            keyboard_owner = f->entry;
            kindle_keyboard(TRUE);
        }
    }
    if (--f->tries > 0) {
        g_timeout_add(400, focus_entry_cb, f);
        return FALSE;
    }
    g_object_unref(f->entry);
    g_free(f);
    return FALSE;
}

void dialog_focus_entry(GtkWidget *e)
{
    GtkWidget *top = gtk_widget_get_toplevel(e);
    if (GTK_IS_WINDOW(top))
        gtk_window_set_focus(GTK_WINDOW(top), e);
    gtk_widget_grab_focus(e);
    FocusLater *f = g_new0(FocusLater, 1);
    f->entry = g_object_ref(e);
    f->tries = app.kindle ? 6 : 1;
    g_timeout_add(250, focus_entry_cb, f);
}

/* Shortly after a field lost the focus: close the keyboard only when the user moved to a
 * non-text control. A NULL focus (the on-screen keyboard itself, or Backspace) must not close
 * it: that was the bug where the keyboard disappeared and did not come back. */
static gboolean keyboard_check_cb(gpointer data)
{
    GtkWidget *w = data;
    GtkWidget *top = gtk_widget_get_toplevel(w);
    if (keyboard_owner != w) {
        g_object_unref(w);
        return FALSE;
    }
    if (!gtk_widget_get_mapped(w)) {
        keyboard_owner = NULL;
        kindle_keyboard(FALSE);
    } else {
        GtkWidget *focus = GTK_IS_WINDOW(top) ? gtk_window_get_focus(GTK_WINDOW(top)) : NULL;
        if (focus && focus != w && !GTK_IS_ENTRY(focus)) {
            keyboard_owner = NULL;
            kindle_keyboard(FALSE);
        }
    }
    g_object_unref(w);
    return FALSE;
}

static gboolean keyboard_focus_cb(GtkWidget *w, GdkEventFocus *e, gpointer data)
{
    (void) data;
    if (e->in) {
        keyboard_owner = w;
        kindle_keyboard(TRUE);
    } else {
        g_timeout_add(400, keyboard_check_cb, g_object_ref(w));
    }
    return FALSE;
}

static gboolean keyboard_click_cb(GtkWidget *w, GdkEventButton *e, gpointer data)
{
    (void) e, (void) data;
    keyboard_owner = w;
    kindle_keyboard(TRUE);
    return FALSE;
}

static void keyboard_hide_cb(GtkWidget *w, gpointer data)
{
    (void) data;
    if (keyboard_owner == w) {
        keyboard_owner = NULL;
        kindle_keyboard(FALSE);
    }
}

/* Enter only closes the keyboard. (Not gtk_window_activate_default(): without a default
 * button it activates the focused entry, which came back here forever.) */
void keyboard_enter_cb(GtkEntry *e, gpointer data)
{
    (void) data;
    if (keyboard_owner == GTK_WIDGET(e))
        keyboard_owner = NULL;
    kindle_keyboard(FALSE);
}

/* Closes the Kindle keyboard if it is up, whichever field opened it: a field left without the
 * focus (after sending a chat line, a tap on the board) does not close it by itself. */
void keyboard_hide(void)
{
#ifndef G_OS_WIN32
    if (!app.kindle || (!keyboard_owner && !keyboard_open))
        return;
#endif
    keyboard_owner = NULL;
    kindle_keyboard(FALSE);
}

/* At the start: a keyboard left up by a game killed with a field focused (a restart, an update)
 * stays over the screen, and nothing else closes it. */
void keyboard_hide_leftover(void)
{
    if (app.kindle)
        kindle_keyboard(FALSE);
}

static void paste_cb(GtkButton *b, gpointer entry)
{
    (void) b;
#ifdef __ANDROID__
    android_paste(entry); /* the X server's clipboard is not Android's */
#else
    gtk_widget_grab_focus(entry);
    gtk_editable_paste_clipboard(GTK_EDITABLE(entry));
#endif
}

GtkWidget *with_paste(GtkWidget *entry)
{
    if (app.kindle)
        return entry; /* no clipboard there */
    GtkWidget *box = gtk_hbox_new(FALSE, 6), *paste = gtk_button_new_with_label(tr("Paste"));
    g_signal_connect(paste, "clicked", G_CALLBACK(paste_cb), entry);
    /* The field keeps the focus, and with it the phone's keyboard as it was (a tap took it away). */
    gtk_button_set_focus_on_click(GTK_BUTTON(paste), FALSE);
    gtk_box_pack_start(GTK_BOX(box), entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), paste, FALSE, FALSE, 0);
    return box;
}

GtkWidget *text_entry(void)
{
    GtkWidget *e = gtk_entry_new();
    /* It asks for little and takes the room its row gives: GTK's own ~20 characters made drawers wider. */
    gtk_entry_set_width_chars(GTK_ENTRY(e), 4);
    if (app.kindle) {
        g_signal_connect(e, "focus-in-event", G_CALLBACK(keyboard_focus_cb), NULL);
        g_signal_connect(e, "focus-out-event", G_CALLBACK(keyboard_focus_cb), NULL);
        g_signal_connect(e, "button-press-event", G_CALLBACK(keyboard_click_cb), NULL);
        g_signal_connect(e, "unmap", G_CALLBACK(keyboard_hide_cb), NULL);
        g_signal_connect(e, "activate", G_CALLBACK(keyboard_enter_cb), NULL);
    } else {
        gtk_entry_set_activates_default(GTK_ENTRY(e), TRUE);
    }
    return e;
}

static void show_password_cb(GtkToggleButton *b, gpointer entry)
{
    gtk_entry_set_visibility(GTK_ENTRY(entry), gtk_toggle_button_get_active(b));
}

/* Hides a password field and returns a "Show password" chip for it. The chip does not take
 * the focus, so a tap on it while typing keeps the on-screen keyboard open. */
GtkWidget *show_password_check(GtkWidget *entry)
{
    gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);
    GtkWidget *show = gtk_toggle_button_new_with_label(tr("Show password"));
    gtk_widget_set_name(show, "denboku-chip"); /* a chip, as the drawers' other on/off choices */
    gtk_widget_set_can_focus(show, FALSE);
    g_signal_connect(show, "toggled", G_CALLBACK(show_password_cb), entry);
    return show;
}

/* GTK's sunken frame draws its right and bottom sides white, which e-ink does not show (the chat
 * and its field looked open): on the Kindle, draw the frame all around in black. */
static gboolean black_frame_cb(GtkWidget *w, GdkEventExpose *e, gpointer data)
{
    (void) data;
    GdkWindow *win = gtk_widget_get_window(w);
    if (e->window != win)
        return FALSE;
    GtkAllocation a;
    gtk_widget_get_allocation(w, &a);
    if (gtk_widget_get_has_window(w))
        a.x = a.y = 0, gdk_drawable_get_size(win, &a.width, &a.height);
    gdk_draw_rectangle(win, gtk_widget_get_style(w)->black_gc, FALSE, a.x, a.y, a.width - 1, a.height - 1);
    return FALSE;
}

void black_frame(GtkWidget *w)
{
    if (app.kindle)
        g_signal_connect_after(w, "expose-event", G_CALLBACK(black_frame_cb), NULL);
}

GtkWidget *table_row(GtkWidget *table, int row, const char *label, GtkWidget *w)
{
    GtkWidget *l = gtk_label_new(label);
    gtk_misc_set_alignment(GTK_MISC(l), 0, 0.5);
    gtk_table_attach(GTK_TABLE(table), l, 0, 1, row, row + 1, GTK_FILL, GTK_FILL, 8, 6);
    gtk_table_attach(GTK_TABLE(table), w, 1, 2, row, row + 1, GTK_EXPAND | GTK_FILL, GTK_FILL, 8, 6);
    g_object_set_data(G_OBJECT(w), "row-label", l); /* the title of the list a drop-down in it opens (drawer.c) */
    return l;
}

/* Opens a web page in the browser; the Kindle has none to hand it to, so it shows the address. */
void open_url(const char *url)
{
#ifdef G_OS_WIN32
    gunichar2 *w = g_utf8_to_utf16(url, -1, NULL, NULL, NULL);
    ShellExecuteW(NULL, L"open", (wchar_t *) w, NULL, NULL, SW_SHOWNORMAL);
    g_free(w);
#elif defined(__ANDROID__)
    android_open_url(url);
#else
    if (app.kindle)
        return;
    /* The system's opener, without the bundle's library path and theme (denboku.sh sets them, and
     * a browser started with them fails); GTK's own lookup only if there is no xdg-open. */
    char *argv[] = {"env", "-u", "LD_LIBRARY_PATH", "-u", "GTK2_RC_FILES", "xdg-open", (char *) url, NULL};
    char *opener = g_find_program_in_path("xdg-open");
    if (!opener || !g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL))
        gtk_show_uri(NULL, url, GDK_CURRENT_TIME, NULL);
    g_free(opener);
#endif
}

static void open_url_cb(GtkButton *b, gpointer url)
{
    (void) b;
    open_url(url);
}

/* A web address to visit: on computers a link that opens `url` (which may carry more, as the
 * token page's preset scope); on the Kindle, with no browser to open, text to read and type. */
/* An address in lines of at most `max` px: whole when it fits, else broken before a "/" (not the
 * "//" after the scheme), the line below starting with it, as addresses are broken in print. */
static char *url_lines(GtkWidget *w, const char *url, int max)
{
    PangoLayout *layout = gtk_widget_create_pango_layout(w, NULL);
    GString *out = g_string_new(NULL), *line = g_string_new(NULL);
    for (const char *p = url; *p;) {
        const char *end = p + 1; /* the next piece: up to the following single "/" */
        while (*end && !(*end == '/' && end[-1] != '/' && end[1] != '/'))
            end++;
        GString *try = g_string_new(line->str);
        g_string_append_len(try, p, end - p);
        pango_layout_set_text(layout, try->str, -1);
        int tw, th;
        pango_layout_get_pixel_size(layout, &tw, &th);
        if (tw > max && line->len) {
            g_string_append_printf(out, "%s%s", out->len ? "\n" : "", line->str);
            g_string_assign(line, "");
            g_string_append_len(line, p, end - p);
        } else {
            g_string_assign(line, try->str);
        }
        g_string_free(try, TRUE);
        p = end;
    }
    g_string_append_printf(out, "%s%s", out->len ? "\n" : "", line->str);
    g_string_free(line, TRUE);
    g_object_unref(layout);
    return g_string_free(out, FALSE);
}

/* The address in lines for the label's font as it is now (it changes when the label gets its style). */
static void link_lines(GtkWidget *l)
{
    const char *text = g_object_get_data(G_OBJECT(l), "url-text");
    int max = rows_width() - 60;
    char *lines = url_lines(l, text, max);
    char *markup = g_markup_printf_escaped(app.kindle ? "%s" : "<span foreground=\"#2a6ebb\" underline=\"single\">%s</span>",
                                           lines);
    gtk_label_set_markup(GTK_LABEL(l), markup);
    g_free(markup);
    g_free(lines);
    /* Measured unwrapped: a wrapped GTK 2 label with no width asked lays out at a guessed one. */
    PangoLayout *layout = gtk_widget_create_pango_layout(l, gtk_label_get_text(GTK_LABEL(l)));
    int tw, th;
    pango_layout_get_pixel_size(layout, &tw, &th);
    g_object_unref(layout);
    gtk_widget_set_size_request(l, MIN(tw, max), -1);
}

static void link_style_cb(GtkWidget *l, GtkStyle *previous, gpointer data)
{
    (void) previous, (void) data;
    link_lines(l);
}

GtkWidget *link_button(const char *text, const char *url)
{
    GtkWidget *l = gtk_label_new(NULL);
    g_object_set_data_full(G_OBJECT(l), "url-text", g_strdup(text), g_free);
    /* A piece longer than a line (no "/" to break at) still breaks, rather than widen the drawer. */
    gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(l), PANGO_WRAP_CHAR);
    link_lines(l);
    g_signal_connect(l, "style-set", G_CALLBACK(link_style_cb), NULL);
    if (app.kindle) {
        gtk_label_set_selectable(GTK_LABEL(l), TRUE);
        return l;
    }
    GtkWidget *b = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(b), l);
    gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
    g_signal_connect(b, "clicked", G_CALLBACK(open_url_cb), (gpointer) url);
    return b;
}
