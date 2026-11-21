/* Kindle: going to sleep and waking up, the front light, and drawing in bands for the e-ink. */

#include "app.h"
#include "view.h"

/* What GTK draws (a drawer, a page) goes in bands as the board does (in_bands() in view.c), not
 * all at once. */
static guint bands_id;

/* What GTK draws over several bands, held until the e-ink has shown what came before; for a drawer's
 * page that flashes, then the black (drawer.c), and the page once the black has shown. Drawn over
 * an update still on its way, the e-ink joins the two into one large update, which waits seconds. */
static GdkRegion *held;
static guint held_id;

static void draw_held(void)
{
    GdkWindow *win = gtk_widget_get_window(app.window);
    gtk_container_check_resize(GTK_CONTAINER(app.window));
    if (win && held)
        gdk_window_invalidate_region(win, held, TRUE);
    if (held)
        gdk_region_destroy(held);
    held = NULL;
    if (win)
        paint_in_bands(win);
}

static gboolean unflash_cb(gpointer data)
{
    (void) data;
    held_id = 0;
    draw_held();
    return FALSE;
}

static gboolean flash_cb(gpointer data)
{
    (void) data;
    if (drawer_settling()) { /* a page whose chips are built again would be drawn twice */
        held_id = g_timeout_add(30, flash_cb, NULL);
        return FALSE;
    }
    gtk_container_check_resize(GTK_CONTAINER(app.window));
    GdkRegion *black = drawer_flash_wanted() ? drawer_flash_now() : NULL;
    if (!black) {
        held_id = 0;
        draw_held();
        return FALSE;
    }
    gdk_region_union(held, black);
    gdk_region_destroy(black);
    held_id = g_timeout_add(eink_busy_ms() + 50, unflash_cb, NULL);
    return FALSE;
}

/* No background for the X server to fill: the game's windows are native on the Kindle (touches
 * reach only those), and the server filled a page's whole window as it came and went, one large
 * update besides the bands. GTK draws all of it anyway. GTK sets the background again with a
 * style, so before each drawing too. */
static void no_background(GdkWindow *w)
{
    gdk_window_set_back_pixmap(w, NULL, FALSE);
    for (GList *l = gdk_window_peek_children(w); l; l = l->next)
        no_background(l->data);
}

static gboolean map_hook(GSignalInvocationHint *hint, guint n, const GValue *params, gpointer data)
{
    (void) hint, (void) n, (void) data;
    GtkWidget *w = g_value_get_object(&params[0]);
    if (gtk_widget_get_window(w))
        no_background(gtk_widget_get_window(w));
    return TRUE;
}

static gboolean bands_cb(gpointer data)
{
    (void) data;
    bands_id = 0;
    GList *tops = gtk_window_list_toplevels();
    for (GList *l = tops; l; l = l->next) {
        GdkWindow *win = gtk_widget_get_window(l->data);
        if (!win || !gdk_window_is_viewable(win))
            continue;
        /* GTK's own layout step draws everything waiting at once, as it ends: laid out here first. */
        gtk_container_check_resize(l->data);
        no_background(win);
        if (l->data == app.window) {
            GdkRegion *pending = take_pending_area(win);
            /* Over more than one band while the e-ink still shows the last: held until it is done
             * (drawn over an update still on its way, the e-ink joins all of it into one large
             * update, which waits seconds). */
            if (!drawer_flash_wanted() && !held_id && !drawer_settling() && (in_one_band(pending, win) || !eink_busy_ms())) {
                gdk_window_invalidate_region(win, pending, TRUE);
                gdk_region_destroy(pending);
                paint_in_bands(win);
                continue;
            }
            if (held) {
                gdk_region_union(held, pending);
                gdk_region_destroy(pending);
            } else {
                held = pending;
            }
            if (!held_id)
                held_id = g_timeout_add(MAX(1, eink_busy_ms()), flash_cb, NULL);
            continue;
        }
        paint_in_bands(win);
    }
    g_list_free(tops);
    return FALSE;
}

void kindle_bands_soon(void)
{
    if (app.kindle && !bands_id) /* before GTK's layout (HIGH_IDLE + 10) and drawing (+ 20) */
        bands_id = g_idle_add_full(G_PRIORITY_HIGH_IDLE + 5, bands_cb, NULL, NULL);
}

/* Anything from the X server (a tap, an Expose of a window just mapped) may lead GTK to draw. */
static GdkFilterReturn x_event_filter(GdkXEvent *xev, GdkEvent *ev, gpointer data)
{
    (void) xev, (void) ev, (void) data;
    kindle_bands_soon();
    return GDK_FILTER_CONTINUE;
}

void kindle_draw_in_bands(void)
{
    if (!app.kindle)
        return;
    gdk_window_add_filter(NULL, x_event_filter, NULL);
    /* Before the widget's own handler maps its window. */
    g_signal_add_emission_hook(g_signal_lookup("map", GTK_TYPE_WIDGET), 0, map_hook, NULL, NULL);
    GList *tops = gtk_window_list_toplevels();
    for (GList *l = tops; l; l = l->next)
        if (gtk_widget_get_window(l->data))
            no_background(gtk_widget_get_window(l->data));
    g_list_free(tops);
}

#ifndef G_OS_WIN32

/* The Kindle draws its own lock screen over the game: save before it, redraw the whole game
 * when it goes away (the X server does not know the screen changed under it). */
static gboolean powerd_cb(GIOChannel *ch, GIOCondition cond, gpointer data)
{
    (void) data;
    char *line = NULL;
    if (cond & G_IO_IN && g_io_channel_read_line(ch, &line, NULL, NULL, NULL) == G_IO_STATUS_NORMAL) {
        if (strstr(line, "goingToScreenSaver"))
            autosave(); /* a save still waiting after the last move */
        else if (strstr(line, "outOfScreenSaver"))
            gtk_widget_queue_draw(app.window);
        g_free(line);
        return TRUE;
    }
    g_free(line);
    return !(cond & (G_IO_HUP | G_IO_ERR));
}

#endif

void watch_screensaver(void)
{
#ifndef G_OS_WIN32
    char *argv[] = {"lipc-wait-event", "-m", "com.lab126.powerd", "goingToScreenSaver,outOfScreenSaver", NULL};
    int out;
    if (!app.kindle || !g_spawn_async_with_pipes(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL,
                                                 NULL, NULL, NULL, NULL, &out, NULL, NULL))
        return;
    GIOChannel *ch = g_io_channel_unix_new(out);
    g_io_channel_set_flags(ch, G_IO_FLAG_NONBLOCK, NULL);
    g_io_add_watch(ch, G_IO_IN | G_IO_HUP | G_IO_ERR, powerd_cb, NULL);
#endif
}

/* The front light, 0 (off) to KINDLE_LIGHT_MAX, as the Kindle's own settings set it (powerd). */
int kindle_light(void)
{
    char *out = NULL;
    int level = -1;
    if (g_spawn_command_line_sync("lipc-get-prop com.lab126.powerd flIntensity", &out, NULL, NULL, NULL) && out)
        level = atoi(out);
    g_free(out);
    return level;
}

void kindle_set_light(int level)
{
    char *cmd = g_strdup_printf("lipc-set-prop com.lab126.powerd flIntensity %d", CLAMP(level, 0, KINDLE_LIGHT_MAX));
    g_spawn_command_line_async(cmd, NULL);
    g_free(cmd);
}
