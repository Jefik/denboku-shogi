/* Keyboard and game controller: what each key and button does. Every action takes two keys and
 * two controller inputs, all set in Preferences → Controls and kept in settings.ini as
 * Key.<Action> and Pad.<Action> (two values joined by ";", empty for none). On the board the
 * actions play; over a dialog or menu the controller moves around it as the arrow keys, Enter,
 * Escape and Tab would. While they are in use, the buttons on screen (and the menu entries) show
 * the key or controller button that does the same, as console games do; a click or tap hides them. */

#include "app.h"
#include "sheet.h"

typedef enum {
    ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT, ACT_SELECT, ACT_CANCEL, ACT_HAND, /* as ViewCursor */
    ACT_HINT, ACT_UNDO, ACT_REDO, ACT_PREV, ACT_NEXT, ACT_FIRST, ACT_LAST, ACT_FLIP, ACT_MENU,
    ACT_PREFERENCES, ACT_HISTORY, ACT_FULLSCREEN, ACT_COUNT
} Action;

static const struct {
    const char *id, *label, *keys[2], *pads[2];
} ACTIONS[ACT_COUNT] = {
    /* Controller inputs are positions (A bottom, B right, X left, Y top). As on Japanese consoles,
     * the right button confirms and the bottom one cancels; the shoulders go through the moves
     * (and through a dialog's tabs), LT undoes and RT browses the moves. */
    {"Up", "Cursor up", {"Up", "w"}, {"DPadUp", "StickUp"}},
    {"Down", "Cursor down", {"Down", "s"}, {"DPadDown", "StickDown"}},
    {"Left", "Cursor left", {"Left", "a"}, {"DPadLeft", "StickLeft"}},
    {"Right", "Cursor right", {"Right", "d"}, {"DPadRight", "StickRight"}},
    {"Select", "Pick up or place a piece", {"Return", "space"}, {"B", ""}},
    {"Cancel", "Put the piece back", {"Escape", ""}, {"A", ""}},
    {"Hand", "Next piece in hand", {"Tab", "e"}, {"X", ""}},
    {"Hint", "Hint", {"h", ""}, {"Y", ""}},
    {"Undo", "Undo", {"<Control>z", "BackSpace"}, {"LT", ""}},
    {"Redo", "Redo", {"<Shift><Control>z", "<Control>y"}, {"LS", ""}},
    {"Previous", "Previous move", {"Page_Up", "comma"}, {"LB", ""}},
    {"Next", "Next move", {"Page_Down", "period"}, {"RB", ""}},
    {"First", "First move", {"Home", ""}, {"", ""}},
    {"Last", "Last move", {"End", ""}, {"", ""}},
    {"Flip", "Flip Board", {"f", ""}, {"RS", ""}},
    {"Menu", "Menu", {"Escape", "F10"}, {"Start", ""}},
    {"Preferences", "Preferences", {"<Control>comma", ""}, {"Back", ""}},
    {"History", "Browse the moves", {"m", ""}, {"RT", ""}},
    {"Fullscreen", "Full screen", {"F11", ""}, {"", ""}},
};

const char *const PAD_IDS[PAD_COUNT] = {
    "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "Back", "Start", "LS", "RS",
    "DPadUp", "DPadDown", "DPadLeft", "DPadRight", "StickUp", "StickDown", "StickLeft", "StickRight",
    "RStickUp", "RStickDown",
};

typedef enum { INPUT_POINTER, INPUT_KEYS, INPUT_PAD } Input;

#define MODS (GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK)

/* Bindings in use, and the copy the Controls page edits. */
static char *keys[ACT_COUNT][2], *pads[ACT_COUNT][2];
static char *edit_keys[ACT_COUNT][2], *edit_pads[ACT_COUNT][2];

static void set(char **slot, const char *value)
{
    g_free(*slot);
    *slot = g_strdup(value ? value : "");
}

static void load(char *k[ACT_COUNT][2], char *p[ACT_COUNT][2], gboolean defaults)
{
    for (int a = 0; a < ACT_COUNT; a++)
        for (int t = 0; t < 2; t++) {
            char key[32], **got = NULL;
            snprintf(key, sizeof key, "%s.%s", t ? "Pad" : "Key", ACTIONS[a].id);
            char *saved = defaults ? NULL : g_key_file_get_string(app.settings, "Settings", key, NULL);
            if (saved)
                got = g_strsplit(saved, ";", 2);
            for (int i = 0; i < 2; i++) {
                const char *value = got ? (got[0] && (i == 0 || got[1]) ? got[i] : "")
                                        : t ? ACTIONS[a].pads[i] : ACTIONS[a].keys[i];
                set(t ? &p[a][i] : &k[a][i], value);
            }
            g_strfreev(got);
            g_free(saved);
        }
}

void controls_load(void)
{
    load(keys, pads, FALSE);
}

static void normalized(guint keyval, GdkModifierType state, guint *key, GdkModifierType *mods)
{
    *key = gdk_keyval_to_lower(keyval);
    *mods = state & MODS;
}

static gboolean key_matches(const char *binding, const GdkEventKey *e)
{
    guint key, ek;
    GdkModifierType mods, em;
    if (!*binding)
        return FALSE;
    gtk_accelerator_parse(binding, &key, &mods);
    normalized(e->keyval, e->state, &ek, &em);
    return key && gdk_keyval_to_lower(key) == ek && (mods & MODS) == em;
}

static void toggle_fullscreen(void)
{
    GdkWindow *w = gtk_widget_get_window(app.window);
    if (gdk_window_get_state(w) & GDK_WINDOW_STATE_FULLSCREEN)
        gtk_window_unfullscreen(GTK_WINDOW(app.window));
    else
        gtk_window_fullscreen(GTK_WINDOW(app.window));
}


/* Browsing the moves (the History action, or a move picked in the list): the arrows go through
 * them, up/left back and down/right forward, as Previous/Next (LB/RB) do;
 * Select or Cancel goes back to the game, and so does an action on the game (the hand, hint, undo,
 * redo) before it acts. */
static gboolean browsing;
static void area_hints_update(void);

static gboolean browse(Action a)
{
    if (!browsing && view_ply(app.view) < 0)
        return FALSE;
    if (a == ACT_UP || a == ACT_LEFT || a == ACT_DOWN || a == ACT_RIGHT || a == ACT_PREV || a == ACT_NEXT) {
        view_step(app.view, a == ACT_UP || a == ACT_LEFT || a == ACT_PREV ? STEP_PREV : STEP_NEXT);
        return TRUE;
    }
    if (a == ACT_SELECT || a == ACT_CANCEL || a == ACT_HAND || a == ACT_HINT || a == ACT_UNDO || a == ACT_REDO) {
        browsing = FALSE;
        area_hints_update();
        view_step(app.view, STEP_LAST);
        return a == ACT_SELECT || a == ACT_CANCEL; /* the others go on to act */
    }
    return FALSE;
}

/* The Sandbox with a controller: the cursor works on the board and the hands (in view.c); the
 * hand button goes to the hands (yours, the other's, the board) and LB/RB through the palette, the hint button changes its color, and Browse
 * the moves goes to the buttons of the bar, where the d-pad moves, confirm presses and cancel
 * (or Browse again) goes back to the board. */
static gboolean in_bar;

static void can_focus_all(GtkWidget *w, gpointer on)
{
    if (GTK_IS_BUTTON(w))
        gtk_widget_set_can_focus(w, GPOINTER_TO_INT(on));
    if (GTK_IS_CONTAINER(w))
        gtk_container_forall(GTK_CONTAINER(w), can_focus_all, on);
}

static void bar_focus(gboolean on)
{
    in_bar = on;
    can_focus_all(app.edit_bar, GINT_TO_POINTER(on));
    if (on)
        gtk_widget_child_focus(app.edit_bar, GTK_DIR_TAB_FORWARD);
    else
        gtk_window_set_focus(GTK_WINDOW(app.window), NULL);
    gtk_widget_queue_draw(app.edit_bar);
}

static gboolean edit_act(Action a)
{
    if (in_bar) {
        GtkWidget *focus = gtk_window_get_focus(GTK_WINDOW(app.window));
        static const GtkDirectionType DIRS[] = {GTK_DIR_UP, GTK_DIR_DOWN, GTK_DIR_LEFT, GTK_DIR_RIGHT};
        if (a <= ACT_RIGHT) {
            if (!gtk_widget_child_focus(app.edit_bar, DIRS[a]))
                gtk_widget_child_focus(app.edit_bar, a == ACT_UP || a == ACT_LEFT ? GTK_DIR_TAB_BACKWARD : GTK_DIR_TAB_FORWARD);
            gtk_widget_queue_draw(app.edit_bar);
            return TRUE;
        }
        if (a == ACT_SELECT) {
            GtkWidget *combo = focus;
            while (combo && !GTK_IS_COMBO_BOX(combo))
                combo = gtk_widget_get_parent(combo);
            if (combo)
                gtk_combo_box_popup(GTK_COMBO_BOX(combo));
            else if (focus && GTK_IS_BUTTON(focus))
                gtk_button_clicked(GTK_BUTTON(focus));
            if (!app.edit_saved) /* Play, Cancel or Done ended the editor */
                bar_focus(FALSE);
            return TRUE;
        }
        if (a == ACT_CANCEL || a == ACT_HISTORY) {
            bar_focus(FALSE);
            return TRUE;
        }
    }
    switch (a) {
    case ACT_NEXT: edit_tool_step(1); return TRUE;
    case ACT_PREV: edit_tool_step(-1); return TRUE;
    case ACT_HINT: edit_other_color(); return TRUE;
    case ACT_REDO: edit_redo(); return TRUE;
    case ACT_HISTORY: bar_focus(TRUE); return TRUE;
    case ACT_FIRST: case ACT_LAST: return TRUE;
    default: return FALSE;
    }
}

gboolean controls_in_bar(void) { return in_bar; }

/* An action on the board and the main window: FALSE when it did nothing. */
static gboolean act(Action a)
{
    if (a == ACT_FULLSCREEN) { /* whatever is open */
        if (app.kindle)
            return FALSE;
        toggle_fullscreen();
        return TRUE;
    }
    if (sheet_is_open()) {
        if (a == ACT_MENU)
            menu_open();
        else if (a <= ACT_CANCEL)
            sheet_cursor((ViewCursor) a);
        return TRUE;
    }
    if (in_bar && !app.edit_saved)
        bar_focus(FALSE);
    if (app.edit_saved && edit_act(a))
        return TRUE;
    if ((a <= ACT_REDO || a == ACT_PREV || a == ACT_NEXT) && browse(a))
        return TRUE;
    static const ViewStep STEPS[] = {STEP_PREV, STEP_NEXT, STEP_FIRST, STEP_LAST};
    switch (a) {
    case ACT_HINT:
        if ((app.hint_visible && app.can_hint) || replaying())
            hint_cb(NULL, NULL);
        return TRUE;
    case ACT_UNDO:
        if (app.can_undo || replaying())
            undo_cb(NULL, NULL);
        return TRUE;
    case ACT_REDO: redo(); return TRUE;
    case ACT_PREV: case ACT_NEXT: case ACT_FIRST: case ACT_LAST:
        view_step(app.view, STEPS[a - ACT_PREV]);
        return TRUE;
    case ACT_FLIP: view_set_flipped(app.view, !view_flipped(app.view)); return TRUE;
    case ACT_MENU: menu_open(); return TRUE;
    case ACT_PREFERENCES: preferences_dialog(); return TRUE;
    case ACT_HISTORY:
        browsing = !browsing;
        area_hints_update();
        if (browsing)
            view_cursor(app.view, CURSOR_CANCEL); /* the board's cursor goes away */
        else
            view_step(app.view, STEP_LAST);
        return TRUE;
    default: return view_cursor(app.view, (ViewCursor) a);
    }
}

static void legend_input(Input input);

/* A key may do more than one thing (Escape puts a piece back, or with none opens the menu): the
 * first of its actions that does something. */
gboolean controls_key(const GdkEventKey *e)
{
    for (int a = 0; a < ACT_COUNT; a++)
        for (int i = 0; i < 2; i++)
            if (key_matches(keys[a][i], e)) {
                legend_input(INPUT_KEYS);
                if (act((Action) a))
                    return TRUE;
                break;
            }
    return FALSE;
}

/* ---- the controller ---- */

static GtkWidget *capture; /* the dialog waiting for a key or button */
static char **capture_slot;
static gboolean capture_pad;
static Action repeat_action;
static PadInput repeat_input;
static guint repeat_id;
static gboolean sending; /* a key the controller types: still the controller's input */

/* A key as typed on `top` (a dialog or a menu). */
static void send_key(GtkWidget *top, guint keyval)
{
    sending = TRUE;
    GdkEvent *ev = gdk_event_new(GDK_KEY_PRESS);
    ev->key.window = g_object_ref(gtk_widget_get_window(top));
    ev->key.send_event = TRUE;
    ev->key.time = GDK_CURRENT_TIME;
    ev->key.keyval = keyval;
    GdkKeymapKey *found;
    gint n;
    if (gdk_keymap_get_entries_for_keyval(NULL, keyval, &found, &n)) {
        ev->key.hardware_keycode = (guint16) found[0].keycode;
        ev->key.group = (guint8) found[0].group;
        g_free(found);
    }
    gtk_main_do_event(ev);
    ev->type = GDK_KEY_RELEASE;
    gtk_main_do_event(ev);
    gdk_event_free(ev);
    sending = FALSE;
}

/* The first notebook (tabs) shown in `w`. */
static GtkWidget *find_tabs(GtkWidget *w)
{
    if (GTK_IS_NOTEBOOK(w) && gtk_widget_get_visible(w))
        return w;
    GtkWidget *found = NULL;
    if (GTK_IS_CONTAINER(w)) {
        GList *kids = gtk_container_get_children(GTK_CONTAINER(w));
        for (GList *l = kids; l && !found; l = l->next)
            found = find_tabs(l->data);
        g_list_free(kids);
    }
    return found;
}

/* The next or previous tab, with the focus in it. */
static void tabs_step(GtkNotebook *nb, int step)
{
    int n = gtk_notebook_get_n_pages(nb);
    gtk_notebook_set_current_page(nb, (gtk_notebook_get_current_page(nb) + step + n) % n);
    gtk_widget_child_focus(gtk_notebook_get_nth_page(nb, gtk_notebook_get_current_page(nb)), GTK_DIR_TAB_FORWARD);
}

static gboolean list_row(GtkWidget *w)
{
    return w && GTK_IS_RADIO_BUTTON(w) && !strcmp(gtk_widget_get_name(w), "denboku-row");
}

/* Keys in a drawer or a dialog that GTK would take another way. Enter presses the chip, switch or
 * row in focus, not the dialog's default button (to which GTK's check and radio buttons leave
 * it); an arrow with nothing in focus goes to the first field; the arrows go through a list of choices without choosing (GTK's radio buttons choose as
 * they go), so that Enter chooses; Ctrl+Page Up/Down go to the page before or after (the sections
 * of Preferences). */
gboolean controls_dialog_key(GtkWidget *win, GtkWidget *dialog, const GdkEventKey *e)
{
    GtkWidget *focus = gtk_window_get_focus(GTK_WINDOW(win));
    guint mods = e->state & MODS;
    gboolean typing = focus && (GTK_IS_ENTRY(focus) || GTK_IS_TEXT_VIEW(focus));
    for (int i = 0; i < 2 && !typing; i++) {
        if (key_matches(keys[ACT_FULLSCREEN][i], e))
            return act(ACT_FULLSCREEN);
        /* Beside the analysis, the moves are gone through as on the board. */
        for (int a = ACT_PREV; a <= ACT_LAST && dialog; a++)
            if (key_matches(keys[a][i], e) && g_object_get_data(G_OBJECT(dialog), "browse-moves"))
                return act((Action) a);
        /* The menu's key closes every page, as × does; cancel (Escape) goes back one, also when
         * it is the menu's key too. */
        if (key_matches(keys[ACT_MENU][i], e) && win == app.window && dialog && !key_matches(keys[ACT_CANCEL][0], e) &&
            !key_matches(keys[ACT_CANCEL][1], e)) {
            if (drawer_open())
                drawer_close_all();
            else
                gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_DELETE_EVENT);
            return TRUE;
        }
    }
    if (mods == GDK_CONTROL_MASK && (e->keyval == GDK_Page_Up || e->keyval == GDK_Page_Down) && dialog &&
        g_object_get_data(G_OBJECT(dialog), "step-chips")) {
        chips_step(g_object_get_data(G_OBJECT(dialog), "step-chips"), e->keyval == GDK_Page_Down ? 1 : -1);
        return TRUE;
    }
    if (mods == GDK_CONTROL_MASK && (e->keyval == GDK_Page_Up || e->keyval == GDK_Page_Down) && dialog &&
        g_object_get_data(G_OBJECT(dialog), "page-step")) {
        gtk_dialog_response(GTK_DIALOG(dialog), e->keyval == GDK_Page_Down ? RESPONSE_PAGE_NEXT : RESPONSE_PAGE_PREV);
        return TRUE;
    }
    if (mods)
        return FALSE;
    if (!focus) { /* nothing in focus yet: an arrow starts at the first field */
        GtkWidget *box = win == app.window ? drawer_box() : dialog;
        if (!box || (e->keyval != GDK_Up && e->keyval != GDK_Down && e->keyval != GDK_Left && e->keyval != GDK_Right))
            return FALSE;
        gtk_widget_child_focus(box, e->keyval == GDK_Up ? GTK_DIR_TAB_BACKWARD : GTK_DIR_TAB_FORWARD);
        return TRUE;
    }
    if ((e->keyval == GDK_Return || e->keyval == GDK_KP_Enter || e->keyval == GDK_ISO_Enter) && GTK_IS_BUTTON(focus) &&
        !gtk_widget_get_receives_default(focus)) {
        /* A list of choices: the row chosen (and shown), and confirmed when it already was. */
        gboolean again = list_row(focus) && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(focus));
        gtk_button_clicked(GTK_BUTTON(focus));
        if (again && dialog)
            gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
        return TRUE;
    }
    /* From a button up or down to the next field, around at the ends and from the buttons at the
     * bottom (GTK stops where it finds nothing straight above or below). */
    if ((e->keyval == GDK_Up || e->keyval == GDK_Down) && GTK_IS_BUTTON(focus) && !list_row(focus)) {
        GtkWidget *box = win == app.window ? drawer_box() : dialog;
        gboolean up = e->keyval == GDK_Up;
        if (!box || !gtk_widget_child_focus(box, up ? GTK_DIR_UP : GTK_DIR_DOWN) ||
            gtk_window_get_focus(GTK_WINDOW(win)) == focus)
            gtk_widget_child_focus(win, up ? GTK_DIR_TAB_BACKWARD : GTK_DIR_TAB_FORWARD);
        return TRUE;
    }
    if ((e->keyval == GDK_Up || e->keyval == GDK_Down) && list_row(focus)) {
        GList *rows = gtk_container_get_children(GTK_CONTAINER(gtk_widget_get_parent(focus)));
        int n = (int) g_list_length(rows), at = g_list_index(rows, focus);
        gtk_widget_grab_focus(g_list_nth_data(rows, (guint) ((at + (e->keyval == GDK_Down ? 1 : n - 1)) % n)));
        g_list_free(rows);
        return TRUE;
    }
    return FALSE;
}

/* Ctrl+Page Up/Down change a dialog's tab wherever the focus is (GTK does it only inside the tabs). */
static gboolean dialog_tabs_key_cb(GtkWidget *top, GdkEventKey *e, gpointer data)
{
    (void) data;
    controls_nav_key(e->keyval);
    if (GTK_IS_DIALOG(top) && controls_dialog_key(top, top, e))
        return TRUE;
    GtkWidget *tabs = (e->state & MODS) == GDK_CONTROL_MASK ? find_tabs(top) : NULL;
    if (!tabs || (e->keyval != GDK_Page_Up && e->keyval != GDK_Page_Down))
        return FALSE;
    legend_input(INPUT_KEYS);
    tabs_step(GTK_NOTEBOOK(tabs), e->keyval == GDK_Page_Down ? 1 : -1);
    return TRUE;
}

/* The open menu the controller works on: the deepest submenu entered (one GTK opens by itself on
 * selecting its entry has nothing selected yet, and its parent is still the one in use). */
static GtkMenuShell *open_menu(GtkMenuShell *shell)
{
    for (;;) {
        GtkWidget *item = shell->active_menu_item;
        GtkWidget *sub = item && GTK_IS_MENU_ITEM(item) ? gtk_menu_item_get_submenu(GTK_MENU_ITEM(item)) : NULL;
        if (!sub || !gtk_widget_get_mapped(sub) || !GTK_MENU_SHELL(sub)->active_menu_item)
            return shell;
        shell = GTK_MENU_SHELL(sub);
    }
}

/* A submenu opened from the controller selects its first entry once it is up (GTK opens it a
 * moment later). */
static gboolean select_first_cb(gpointer data)
{
    GtkMenuShell *sub = data;
    if (gtk_widget_get_mapped(GTK_WIDGET(sub)) && !sub->active_menu_item)
        g_signal_emit_by_name(sub, "move-current", GTK_MENU_DIR_NEXT); /* as the d-pad down does */
    g_object_unref(sub);
    return FALSE;
}

static void submenu_mapped_cb(GtkWidget *sub, gpointer data)
{
    (void) data;
    g_signal_handlers_disconnect_by_func(sub, submenu_mapped_cb, NULL);
    g_timeout_add(250, select_first_cb, g_object_ref(sub)); /* after GTK settles it, which clears it */
}

/* A menu is driven by its own actions rather than by made-up keys, which need its keyboard grab
 * (not always given: another window in front, Wayland, Windows). Menu closes it again. */
static void menu_act(GtkMenuShell *menu, Action a)
{
    /* The grab may be on a submenu: start from the menu the others hang from. */
    for (;;) {
        GtkWidget *item = GTK_IS_MENU(menu) ? gtk_menu_get_attach_widget(GTK_MENU(menu)) : NULL;
        GtkWidget *parent = item ? gtk_widget_get_parent(item) : NULL;
        if (!item || !GTK_IS_MENU_ITEM(item) || !parent || !GTK_IS_MENU(parent))
            break;
        menu = GTK_MENU_SHELL(parent);
    }
    GtkMenuShell *shell = open_menu(menu);
    switch (a) {
    case ACT_UP: g_signal_emit_by_name(shell, "move-current", GTK_MENU_DIR_PREV); break;
    case ACT_DOWN: g_signal_emit_by_name(shell, "move-current", GTK_MENU_DIR_NEXT); break;
    case ACT_RIGHT: {
        /* Into the submenu of the item, its first entry selected. */
        GtkWidget *item = shell->active_menu_item;
        GtkWidget *sub = item ? gtk_menu_item_get_submenu(GTK_MENU_ITEM(item)) : NULL;
        if (sub && gtk_widget_is_sensitive(item)) {
            if (gtk_widget_get_mapped(sub))
                submenu_mapped_cb(sub, NULL);
            else
                g_signal_connect(sub, "map", G_CALLBACK(submenu_mapped_cb), NULL);
            g_signal_emit_by_name(shell, "move-current", GTK_MENU_DIR_CHILD);
        }
        break;
    }
    case ACT_SELECT: {
        GtkWidget *item = shell->active_menu_item;
        if (item && gtk_menu_item_get_submenu(GTK_MENU_ITEM(item)))
            menu_act(menu, ACT_RIGHT); /* an entry with a submenu opens it */
        else
            g_signal_emit_by_name(shell, "activate-current", TRUE);
        break;
    }
    case ACT_LEFT:
    case ACT_CANCEL:
        if (shell != menu)
            gtk_menu_popdown(GTK_MENU(shell)); /* back to the menu above, its entry still selected */
        else if (a == ACT_CANCEL)
            gtk_menu_shell_cancel(menu);
        break;
    case ACT_MENU:
    case ACT_PREFERENCES: gtk_menu_shell_cancel(menu); break;
    default: break;
    }
}

/* A dialog as on a console: up and down go from field to field (a list or a long text keeps
 * them, to move inside it), left and right change a drop-down, a number or a slider, and Select
 * opens a drop-down (which then works as a menu). FALSE: the key itself does it. */
static gboolean dialog_act(GtkWidget *win, GtkWidget *top, Action a)
{
    GtkWidget *focus = gtk_window_get_focus(GTK_WINDOW(win));
    gboolean inside = focus && (GTK_IS_TREE_VIEW(focus) || GTK_IS_TEXT_VIEW(focus) || list_row(focus));
    if ((a == ACT_UP || a == ACT_DOWN) && !inside) {
        GtkDirectionType dir = a == ACT_UP ? GTK_DIR_UP : GTK_DIR_DOWN;
        if (!gtk_widget_child_focus(top, dir)) /* at the end: around again */
            gtk_widget_child_focus(top, a == ACT_UP ? GTK_DIR_TAB_BACKWARD : GTK_DIR_TAB_FORWARD);
        return TRUE;
    }
    if (!focus)
        return FALSE;
    GtkWidget *combo = focus; /* the focus is a drop-down's own button inside it */
    while (combo && combo != top && !GTK_IS_COMBO_BOX(combo))
        combo = gtk_widget_get_parent(combo);
    if (combo && GTK_IS_COMBO_BOX(combo)) {
        GtkComboBox *c = GTK_COMBO_BOX(combo);
        if (a == ACT_SELECT) {
            gtk_combo_box_popup(c);
            return TRUE;
        }
        if (a == ACT_LEFT || a == ACT_RIGHT) {
            int n = gtk_tree_model_iter_n_children(gtk_combo_box_get_model(c), NULL);
            gtk_combo_box_set_active(c, CLAMP(gtk_combo_box_get_active(c) + (a == ACT_RIGHT ? 1 : -1), 0, n - 1));
            return TRUE;
        }
    }
    if (GTK_IS_SPIN_BUTTON(focus) && (a == ACT_LEFT || a == ACT_RIGHT)) {
        gtk_spin_button_spin(GTK_SPIN_BUTTON(focus), a == ACT_RIGHT ? GTK_SPIN_STEP_FORWARD : GTK_SPIN_STEP_BACKWARD, 1);
        return TRUE;
    }
    return FALSE;
}

static void pad_act(Action a)
{
    GtkWidget *grab = gtk_grab_get_current(), *top = grab ? gtk_widget_get_toplevel(grab) : NULL;
    if (grab && GTK_IS_MENU(grab)) {
        menu_act(GTK_MENU_SHELL(grab), a);
        return;
    }
    /* A dialog: in a drawer of the game's window, or a window of its own. */
    GtkWidget *dialog = drawer_open() ? drawer_dialog() : top && top != app.window ? top : NULL;
    if (dialog) {
        static const guint KEYS[] = {GDK_Up, GDK_Down, GDK_Left, GDK_Right, GDK_Return, GDK_Escape, GDK_Tab};
        GtkWidget *win = drawer_open() ? app.window : top, *box = drawer_open() ? drawer_box() : top;
        GtkWidget *tabs = GTK_IS_MENU(grab) ? NULL : find_tabs(box);
        if (a == ACT_FULLSCREEN) {
            act(a);
        } else if (a == ACT_MENU && drawer_open()) { /* Start closes every page, as × does */
            drawer_close_all();
        } else if (a == ACT_PREFERENCES && g_object_get_data(G_OBJECT(dialog), "prefs-ok")) {
            gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
        } else if (a == ACT_PREFERENCES && g_object_get_data(G_OBJECT(dialog), "prefs-page")) {
            drawer_close_all(); /* a section's page: Preferences closes, as × does */
        } else if (a <= ACT_HAND && gtk_widget_get_window(win)) { /* cancel is Escape: back a page, or closed */
            if (!dialog_act(win, box, a))
                send_key(win, KEYS[a]);
        } else if (tabs && (a == ACT_PREV || a == ACT_NEXT)) {
            tabs_step(GTK_NOTEBOOK(tabs), a == ACT_NEXT ? 1 : -1);
        } else if (a >= ACT_PREV && a <= ACT_LAST && g_object_get_data(G_OBJECT(dialog), "browse-moves")) {
            act(a);
        } else if ((a == ACT_PREV || a == ACT_NEXT) && g_object_get_data(G_OBJECT(dialog), "step-chips")) {
            chips_step(g_object_get_data(G_OBJECT(dialog), "step-chips"), a == ACT_NEXT ? 1 : -1);
        } else if ((a == ACT_PREV || a == ACT_NEXT) && g_object_get_data(G_OBJECT(dialog), "page-step")) {
            gtk_dialog_response(GTK_DIALOG(dialog), a == ACT_NEXT ? RESPONSE_PAGE_NEXT : RESPONSE_PAGE_PREV);
        }
        return;
    }
    act(a);
}

/* Run from an idle, not from the controller's own watch: an action can open a dialog, and the
 * watch would get no input while that dialog runs. */
static gboolean pad_act_cb(gpointer data)
{
    feedback_tick();
    pad_act((Action) GPOINTER_TO_INT(data));
    return FALSE;
}

static gboolean repeat_cb(gpointer data)
{
    pad_act(repeat_action);
    if (data) { /* the first repeat: then faster */
        repeat_id = g_timeout_add(110, repeat_cb, NULL);
        return FALSE;
    }
    return TRUE;
}

static void stop_repeat(void)
{
    if (repeat_id)
        g_source_remove(repeat_id);
    repeat_id = 0;
}

/* The right stick scrolls the open page or the menu, on and on while it is held. */
static guint scroll_id;
static int scroll_dir;

static gboolean scroll_cb(gpointer data)
{
    (void) data;
    if (drawer_open())
        drawer_scroll(scroll_dir);
    else if (sheet_is_open())
        sheet_scroll(scroll_dir);
    return TRUE;
}

static void stick_scroll(PadInput input, gboolean down)
{
    if (scroll_id)
        g_source_remove(scroll_id);
    scroll_id = 0;
    if (!down)
        return;
    legend_input(INPUT_PAD);
    scroll_dir = input == PAD_RSTICK_DOWN ? 1 : -1;
    scroll_cb(NULL);
    scroll_id = g_timeout_add(120, scroll_cb, NULL);
}

void controls_pad(PadInput input, gboolean down)
{
    if (input == PAD_RSTICK_UP || input == PAD_RSTICK_DOWN) {
        if (!capture)
            stick_scroll(input, down);
        return;
    }
    if (capture) {
        if (down && capture_pad) {
            set(capture_slot, PAD_IDS[input]);
            gtk_dialog_response(GTK_DIALOG(capture), GTK_RESPONSE_OK);
        } else if (down) /* a key is asked for: a button is the way out without a keyboard */
            gtk_dialog_response(GTK_DIALOG(capture), GTK_RESPONSE_CANCEL);
        return;
    }
    if (!down) {
        if (repeat_id && input == repeat_input)
            stop_repeat();
        return;
    }
    for (int a = 0; a < ACT_COUNT; a++)
        for (int i = 0; i < 2; i++)
            if (!strcmp(pads[a][i], PAD_IDS[input])) {
                legend_input(INPUT_PAD);
                g_idle_add(pad_act_cb, GINT_TO_POINTER(a));
                if (a <= ACT_RIGHT || a == ACT_PREV || a == ACT_NEXT) { /* held, it goes on */
                    stop_repeat();
                    repeat_action = (Action) a, repeat_input = input;
                    repeat_id = g_timeout_add(350, repeat_cb, GINT_TO_POINTER(1));
                }
                return;
            }
}

/* ---- the button legend ---- */

/* Each controller's names for A B X Y LB RB LT RT Back Start LS RS. The inputs are positions
 * (A the bottom face button, B the right one, X the left, Y the top), so a Nintendo controller's
 * A is our B, and PlayStation's ○ too. */
static const char *const PAD_NAMES[3][12] = {
    {"A", "B", "X", "Y", "LB", "RB", "LT", "RT", "View", "Menu", "LS", "RS"},
    {"B", "A", "Y", "X", "L", "R", "ZL", "ZR", "−", "+", "LS", "RS"},
    {"×", "○", "□", "△", "L1", "R1", "L2", "R2", "Create", "Options", "L3", "R3"},
};

static GtkWidget *legend_check, *layout_combo;
static Input last_input;

static PadLayout layout_of(int setting)
{
    return setting == LAYOUT_AUTO ? gamepad_layout() : (PadLayout) setting;
}

static int pad_input(const char *id)
{
    for (int i = 0; i < PAD_COUNT; i++)
        if (!strcmp(id, PAD_IDS[i]))
            return i;
    return -1;
}

typedef enum { SHAPE_KEY, SHAPE_FACE, SHAPE_PILL, SHAPE_ROUND, SHAPE_DPAD, SHAPE_STICK } Shape;

typedef struct {
    Shape shape;
    int input, dir; /* the PadInput of a face button; the arrow of a d-pad or stick, -1 none */
    PadLayout layout;
    char text[24];
} Glyph;

static void glyph_set(Glyph *g, Shape shape, const char *text)
{
    memset(g, 0, sizeof *g);
    g->shape = shape, g->dir = -1, g->input = -1, g->layout = layout_of(setting_int("PadLayout", LAYOUT_AUTO));
    g_strlcpy(g->text, text, sizeof g->text);
}

/* The glyph of a binding: a keycap, or the controller's button. FALSE for none. */
static gboolean glyph_of(Glyph *g, const char *binding, gboolean pad)
{
    if (!*binding)
        return FALSE;
    if (!pad) {
        guint key;
        GdkModifierType mods;
        gtk_accelerator_parse(binding, &key, &mods);
        char *label = gtk_accelerator_get_label(key, mods);
        glyph_set(g, SHAPE_KEY, label);
        g_free(label);
        return key != 0;
    }
    int in = pad_input(binding);
    if (in < 0)
        return FALSE;
    glyph_set(g, SHAPE_PILL, "");
    if (in >= PAD_UP) {
        g->shape = in >= PAD_STICK_UP ? SHAPE_STICK : SHAPE_DPAD;
        g->dir = (in - PAD_UP) % 4;
        return TRUE;
    }
    g_strlcpy(g->text, PAD_NAMES[g->layout - LAYOUT_XBOX][in], sizeof g->text);
    g->input = in;
    g->shape = in <= PAD_Y ? SHAPE_FACE : in >= PAD_LS || (g->layout == LAYOUT_NINTENDO && in >= PAD_BACK) ? SHAPE_ROUND
                                                                                                           : SHAPE_PILL;
    return TRUE;
}

/* The first binding of `a` for the device last used. */
static gboolean glyph(Glyph *g, Action a)
{
    gboolean pad = last_input == INPUT_PAD;
    char *const *b = pad ? pads[a] : keys[a];
    return glyph_of(g, *b[0] ? b[0] : b[1], pad);
}

/* The shorter of the two keys of `a` (a small button has no room for "Page Down"); a
 * controller's first button. */
static gboolean glyph_short(Glyph *g, Action a)
{
    Glyph other;
    if (last_input == INPUT_PAD || !glyph_of(&other, keys[a][1], FALSE))
        return glyph(g, a);
    if (!glyph(g, a) || strlen(other.text) < strlen(g->text))
        *g = other;
    return TRUE;
}

static void rgb(cairo_t *cr, guint32 c)
{
    cairo_set_source_rgb(cr, (c >> 16) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0);
}

/* Text in `w`'s font, bold: 85% of its size, or `px` pixels when given. */
static PangoLayout *glyph_text(GtkWidget *w, cairo_t *cr, double px, const char *text, int *tw, int *th)
{
    PangoLayout *l = cr ? pango_cairo_create_layout(cr) : gtk_widget_create_pango_layout(w, NULL);
    PangoFontDescription *f = pango_font_description_copy(gtk_widget_get_style(w)->font_desc);
    pango_font_description_set_weight(f, PANGO_WEIGHT_BOLD);
    if (px > 0)
        pango_font_description_set_absolute_size(f, px * PANGO_SCALE);
    else
        pango_font_description_set_size(f, pango_font_description_get_size(f) * 85 / 100);
    pango_layout_set_font_description(l, f);
    pango_font_description_free(f);
    pango_layout_set_text(l, text, -1);
    PangoRectangle ink, logical;
    pango_layout_get_pixel_extents(l, &ink, &logical);
    *tw = logical.width, *th = logical.height;
    return l;
}

/* A small triangle at (x, y) pointing up, left, down or right. */
static void arrow(cairo_t *cr, double x, double y, double s, int dir)
{
    static const double DX[] = {0, -1, 0, 1}, DY[] = {-1, 0, 1, 0};
    int d = dir == 0 ? 0 : dir == 1 ? 2 : dir == 2 ? 1 : 3; /* PadInput order: up down left right */
    double dx = DX[d], dy = DY[d];
    cairo_move_to(cr, x + dx * s, y + dy * s);
    cairo_line_to(cr, x - dx * s * 0.6 - dy * s, y - dy * s * 0.6 + dx * s);
    cairo_line_to(cr, x - dx * s * 0.6 + dy * s, y - dy * s * 0.6 - dx * s);
    cairo_close_path(cr);
    cairo_fill(cr);
}

/* The four face buttons in each controller's own colors: Xbox's green A, red B, blue X and yellow Y;
 * PlayStation's colored symbols on black; Nintendo's are plain. */
static const guint32 XBOX_COLORS[4] = {0x4caf2a, 0xe0393e, 0x2d7fe0, 0xf2b705};
static const guint32 PS_COLORS[4] = {0x7ba6e8, 0xff6b6b, 0xe878d8, 0x3fd0a0};

static void ps_symbol(cairo_t *cr, int input, double cx, double cy, double r)
{
    rgb(cr, PS_COLORS[input]);
    cairo_set_line_width(cr, r * 0.2);
    double s = r * 0.42;
    switch (input) {
    case PAD_A:
        cairo_move_to(cr, cx - s, cy - s), cairo_line_to(cr, cx + s, cy + s);
        cairo_move_to(cr, cx + s, cy - s), cairo_line_to(cr, cx - s, cy + s);
        break;
    case PAD_B: cairo_arc(cr, cx, cy, s * 1.05, 0, 2 * G_PI); break;
    case PAD_X: cairo_rectangle(cr, cx - s * 0.9, cy - s * 0.9, s * 1.8, s * 1.8); break;
    default:
        cairo_move_to(cr, cx, cy - s * 1.1);
        cairo_line_to(cr, cx + s * 1.05, cy + s * 0.7);
        cairo_line_to(cr, cx - s * 1.05, cy + s * 0.7);
        cairo_close_path(cr);
    }
    cairo_stroke(cr);
}

static void drgb(cairo_t *cr, const double *c) { cairo_set_source_rgb(cr, c[0], c[1], c[2]); }

/* A glyph in the box (x, y, w, h): `fg` its shape, `bg` the words on it; `font` gives the face,
 * `px` the size of its words (0: 85% of the font's). Faded to `alpha`. */
static void paint_glyph(cairo_t *cr, const Glyph *g, double x, double y, double w, double hh, const double *fg,
                        const double *bg, GtkWidget *font, double px, double alpha)
{
    if (alpha < 1)
        cairo_push_group(cr);
    double h = hh - 2, cx = x + w / 2.0, cy = y + hh / 2.0, r = h / 2;
    const char *text = g->text;
    guint32 ink = 0;
    gboolean own_ink = FALSE;
    cairo_new_path(cr);
    switch (g->shape) {
    case SHAPE_KEY:
    case SHAPE_PILL:
        view_rounded(cr, x + 1, y + 1, w - 2, h, g->shape == SHAPE_KEY ? 3 : r);
        drgb(cr, fg);
        cairo_fill(cr);
        break;
    case SHAPE_ROUND:
        cairo_arc(cr, cx, cy, r, 0, 2 * G_PI);
        drgb(cr, fg);
        cairo_fill(cr);
        break;
    case SHAPE_FACE:
        cairo_arc(cr, cx, cy, r, 0, 2 * G_PI);
        if (g->layout == LAYOUT_XBOX && color_screen()) {
            rgb(cr, XBOX_COLORS[g->input]);
            cairo_fill(cr);
            ink = g->input == PAD_Y ? 0x222222 : 0xffffff, own_ink = TRUE;
        } else if (g->layout == LAYOUT_PLAYSTATION) {
            rgb(cr, 0x202020);
            cairo_fill_preserve(cr);
            drgb(cr, fg);
            cairo_set_line_width(cr, 1);
            cairo_stroke(cr);
            ps_symbol(cr, g->input, cx, cy, r);
            text = "";
        } else {
            drgb(cr, fg);
            cairo_fill(cr);
        }
        break;
    case SHAPE_DPAD: {
        double t = h / 3;
        cairo_rectangle(cr, cx - t / 2, cy - h / 2, t, h);
        cairo_rectangle(cr, cx - h / 2, cy - t / 2, h, t);
        drgb(cr, fg);
        cairo_fill(cr);
        if (g->dir >= 0) {
            static const double OX[] = {0, 0, -1, 1}, OY[] = {-1, 1, 0, 0};
            drgb(cr, bg);
            arrow(cr, cx + OX[g->dir] * h / 3, cy + OY[g->dir] * h / 3, t * 0.35, g->dir);
        }
        break;
    }
    case SHAPE_STICK:
        drgb(cr, fg);
        cairo_set_line_width(cr, 1.5);
        cairo_arc(cr, cx, cy, r - 1, 0, 2 * G_PI);
        cairo_stroke(cr);
        cairo_arc(cr, cx, cy, r * 0.4, 0, 2 * G_PI);
        cairo_fill(cr);
        if (g->dir >= 0) {
            static const double OX[] = {0, 0, -1, 1}, OY[] = {-1, 1, 0, 0};
            arrow(cr, cx + OX[g->dir] * r * 0.72, cy + OY[g->dir] * r * 0.72, r * 0.22, g->dir);
        }
        break;
    }
    if (*text) {
        int tw, th;
        PangoLayout *l = glyph_text(font, cr, px, text, &tw, &th);
        if (own_ink)
            rgb(cr, ink);
        else
            drgb(cr, bg);
        cairo_move_to(cr, cx - tw / 2.0, cy - th / 2.0);
        pango_cairo_show_layout(cr, l);
        g_object_unref(l);
    }
    if (alpha < 1) {
        cairo_pop_group_to_source(cr);
        cairo_paint_with_alpha(cr, alpha);
    }
}

static gboolean showing(void) { return last_input != INPUT_POINTER && setting_int("ButtonLegend", 1); }

/* The board, the hand and the move list show their key or button while those are in use. */
static void area_hints_update(void)
{
    if (app.view)
        view_set_area_hints(app.view, showing(), browsing);
}

void controls_area_badge(View *v, cairo_t *cr, int area, double right, double top, double h, gboolean active,
                         const double *fg, const double *bg, void *data)
{
    (void) v, (void) data;
    Glyph g;
    static const int BAR_ACTION[VIEW_BAR_COUNT] = {[VIEW_BAR_MENU] = ACT_MENU, [VIEW_BAR_UNDO] = ACT_UNDO, [VIEW_BAR_HINT] = ACT_HINT,
                                                   [VIEW_BAR_CHAT] = -1, [VIEW_BAR_PREV] = ACT_PREV, [VIEW_BAR_NEXT] = ACT_NEXT,
                                                   [VIEW_BAR_INFO] = -1, [VIEW_BAR_ASK] = -1, [VIEW_BAR_YES] = ACT_UNDO, [VIEW_BAR_NO] = -1};
    if (area == VIEW_AREA_BOARD) {
        glyph_set(&g, SHAPE_DPAD, ""); /* the arrows or the d-pad */
    } else if (area >= VIEW_AREA_BAR) {
        int a = BAR_ACTION[area - VIEW_AREA_BAR];
        if (a < 0 || !glyph_short(&g, (Action) a))
            return;
    } else if (!glyph(&g, area == VIEW_AREA_HAND ? ACT_HAND : ACT_HISTORY)) {
        return;
    }
    double px = h * 0.62, w = h;
    if (g.shape == SHAPE_KEY || g.shape == SHAPE_PILL) {
        int tw, th;
        g_object_unref(glyph_text(app.window, cr, px, g.text, &tw, &th));
        w = MAX(h, tw + 8);
    }
    paint_glyph(cr, &g, right - w, top, w, h, fg, bg, app.window, px, active ? 1 : 0.3);
}

/* ---- the field in use in a dialog ---- */

/* What the frame goes around: a drop-down rather than its own inner button. */
static GtkWidget *framed(GtkWidget *focus, GtkWidget *top)
{
    for (GtkWidget *w = focus; w && w != top; w = gtk_widget_get_parent(w))
        if (GTK_IS_COMBO_BOX(w))
            return w;
    return focus;
}

/* GTK 2 themes mark the focus with a faint dotted line, on a drop-down only around its arrow: a
 * clear frame goes around the field in use while the keyboard or a controller drives the dialog. */
static gboolean focus_frame_cb(GtkWidget *top, GdkEventExpose *ev, gpointer data)
{
    (void) ev, (void) data;
    GtkWidget *focus = gtk_window_get_focus(GTK_WINDOW(top));
    if (!showing() || !focus || !gtk_widget_get_mapped(focus))
        return FALSE;
    if (top == app.window && (!in_bar || !gtk_widget_is_ancestor(focus, app.edit_bar)))
        return FALSE;
    GtkWidget *w = framed(focus, top);
    int x, y;
    GtkAllocation a;
    gtk_widget_get_allocation(w, &a);
    if (!gtk_widget_translate_coordinates(w, top, 0, 0, &x, &y))
        return FALSE;
    cairo_t *cr = gdk_cairo_create(gtk_widget_get_window(top));
    const GdkColor *c = &gtk_widget_get_style(top)->bg[GTK_STATE_SELECTED];
    cairo_set_source_rgb(cr, c->red / 65535.0, c->green / 65535.0, c->blue / 65535.0);
    cairo_set_line_width(cr, 2);
    view_rounded(cr, x - 3, y - 3, a.width + 6, a.height + 6, 4);
    cairo_stroke(cr);
    cairo_destroy(cr);
    return FALSE;
}

/* Every dialog gets the frame; a focus change redraws it. */
static gboolean set_focus_hook(GSignalInvocationHint *hint, guint n, const GValue *values, gpointer data)
{
    (void) hint, (void) n, (void) data;
    GtkWidget *top = g_value_get_object(&values[0]);
    if (top == app.window && in_bar)
        gtk_widget_queue_draw(top);
    if (!GTK_IS_WINDOW(top) || top == app.window || GTK_IS_MENU(gtk_bin_get_child(GTK_BIN(top))))
        return TRUE;
    if (!g_object_get_data(G_OBJECT(top), "focus-frame")) {
        g_object_set_data(G_OBJECT(top), "focus-frame", top);
        g_signal_connect_after(top, "expose-event", G_CALLBACK(focus_frame_cb), NULL);
        g_signal_connect(top, "key-press-event", G_CALLBACK(dialog_tabs_key_cb), NULL);
    }
    gtk_widget_queue_draw(top);
    return TRUE;
}

static void focus_frames_redraw(void)
{
    GList *all = gtk_window_list_toplevels();
    for (GList *l = all; l; l = l->next)
        if (l->data != app.window && g_object_get_data(G_OBJECT(l->data), "focus-frame"))
            gtk_widget_queue_draw(l->data);
    g_list_free(all);
}

static void legend_input(Input input)
{
    if (input == last_input || (sending && input == INPUT_KEYS))
        return;
    last_input = input;
    area_hints_update();
    if (app.view && showing()) /* the keys' legends become the controller's, or back */
        gtk_widget_queue_draw(view_widget(app.view));
    focus_frames_redraw();
    GtkWidget *f = gtk_window_get_focus(GTK_WINDOW(app.window));
    if (f)
        gtk_widget_queue_draw(f); /* its focus frame shows or goes */
}

/* Focus frames only for the keyboard and the controller: after a tap or a click they would stay
 * on what was chosen. */
gboolean controls_pointer(void) { return last_input == INPUT_POINTER; }

gboolean controls_pad_used(void) { return last_input == INPUT_PAD; }

/* Tab and the arrows move the focus: from then on it shows. */
void controls_nav_key(guint keyval)
{
    if (keyval == GDK_Tab || keyval == GDK_ISO_Left_Tab || keyval == GDK_Up || keyval == GDK_Down ||
        keyval == GDK_Left || keyval == GDK_Right)
        legend_input(INPUT_KEYS);
}

/* A real click or tap anywhere: the mouse took over. */
static gboolean pointer_hook(GSignalInvocationHint *hint, guint n, const GValue *values, gpointer data)
{
    (void) hint, (void) n, (void) values, (void) data;
    browsing = FALSE;
    legend_input(INPUT_POINTER);
    area_hints_update();
    return TRUE;
}

void controls_init(void)
{
    g_signal_add_emission_hook(g_signal_lookup("button-press-event", GTK_TYPE_WIDGET), 0, pointer_hook, NULL, NULL);
    g_type_class_unref(g_type_class_ref(GTK_TYPE_WINDOW)); /* its signals exist once the class does */
    g_signal_add_emission_hook(g_signal_lookup("set-focus", GTK_TYPE_WINDOW), 0, set_focus_hook, NULL, NULL);
    g_signal_connect_after(app.window, "expose-event", G_CALLBACK(focus_frame_cb), NULL); /* the Sandbox's bar */
}

/* ---- Preferences → Controls ---- */

/* The four buttons of the action open (NULL otherwise), and each action's row of keys. */
static GtkWidget *slot_buttons[ACT_COUNT][4], *action_keys[ACT_COUNT];
static GtkWidget *pad_status;

static char *pad_label(const char *id)
{
    if (!*id)
        return g_strdup("—");
    static const char *const ARROWS[] = {"↑", "↓", "←", "→"};
    for (int i = 0; i < 4; i++) {
        if (!strcmp(id, PAD_IDS[PAD_UP + i]))
            return g_strdup_printf("%s %s", tr("D-pad"), ARROWS[i]);
        if (!strcmp(id, PAD_IDS[PAD_STICK_UP + i]))
            return g_strdup_printf("%s %s", tr("Stick"), ARROWS[i]);
    }
    int in = pad_input(id);
    PadLayout l = layout_of(layout_combo ? gtk_combo_box_get_active(GTK_COMBO_BOX(layout_combo))
                                         : setting_int("PadLayout", LAYOUT_AUTO));
    return g_strdup(in >= 0 ? PAD_NAMES[l - LAYOUT_XBOX][in] : id);
}

static char *key_label(const char *binding)
{
    guint key;
    GdkModifierType mods;
    gtk_accelerator_parse(binding, &key, &mods);
    return key ? gtk_accelerator_get_label(key, mods) : g_strdup("—");
}

static void relabel_slots(void)
{
    for (int a = 0; a < ACT_COUNT; a++) {
        GString *shown = g_string_new(NULL);
        for (int s = 0; s < 4; s++) {
            char *text = s < 2 ? key_label(edit_keys[a][s]) : pad_label(edit_pads[a][s - 2]);
            if (slot_buttons[a][s])
                gtk_button_set_label(GTK_BUTTON(slot_buttons[a][s]), text);
            if (strcmp(text, "—"))
                g_string_append_printf(shown, "%s%s", shown->len ? "  ·  " : "", text);
            g_free(text);
        }
        if (action_keys[a])
            row_set_value(action_keys[a], shown->str);
        g_string_free(shown, TRUE);
    }
    const char *name = gamepad_name();
    char *status = name ? g_strdup_printf("%s: %s", tr("Controller"), name) : g_strdup(tr("No controller connected"));
    gtk_label_set_text(GTK_LABEL(pad_status), status);
    g_free(status);
}

static gboolean capture_key_cb(GtkWidget *w, GdkEventKey *e, gpointer data)
{
    (void) data;
    if (capture_pad && e->keyval == GDK_Escape) /* a way out without a controller */
        gtk_dialog_response(GTK_DIALOG(w), GTK_RESPONSE_CANCEL);
    if (e->is_modifier || capture_pad)
        return TRUE;
    guint key;
    GdkModifierType mods;
    normalized(e->keyval, e->state, &key, &mods);
    char *name = gtk_accelerator_name(key, mods);
    set(capture_slot, name);
    g_free(name);
    gtk_dialog_response(GTK_DIALOG(w), GTK_RESPONSE_OK);
    return TRUE;
}

/* A key or input set for one action leaves the others. */
static void unbind_elsewhere(char **slot, gboolean pad)
{
    for (int a = 0; a < ACT_COUNT; a++)
        for (int i = 0; i < 2; i++) {
            char **other = pad ? &edit_pads[a][i] : &edit_keys[a][i];
            if (other != slot && **slot && !strcmp(*other, *slot))
                set(other, "");
        }
}

static void slot_cb(GtkWidget *b, gpointer data)
{
    (void) b;
    int a = GPOINTER_TO_INT(data) / 4, s = GPOINTER_TO_INT(data) % 4;
    capture_pad = s >= 2;
    capture_slot = capture_pad ? &edit_pads[a][s - 2] : &edit_keys[a][s];
    char *old = g_strdup(*capture_slot);
    capture = dialog_new(tr("Controls"));
    char *text = g_strdup_printf(capture_pad ? tr("Press a button on the controller for “%s”.")
                                             : tr("Press a key for “%s”."),
                                 tr(ACTIONS[a].label));
    GtkWidget *label = gtk_label_new(text);
    g_free(text);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(capture)->vbox), label, TRUE, TRUE, 12);
    gtk_dialog_add_button(GTK_DIALOG(capture), tr("Clear"), RESPONSE_DELETE);
    gtk_dialog_add_button(GTK_DIALOG(capture), tr("Cancel"), GTK_RESPONSE_CANCEL);
    /* Buttons take no focus: Enter and Space are keys to set here. */
    GList *kids = gtk_container_get_children(GTK_CONTAINER(GTK_DIALOG(capture)->action_area));
    for (GList *l = kids; l; l = l->next)
        gtk_widget_set_can_focus(l->data, FALSE);
    g_list_free(kids);
    g_signal_connect(capture, "key-press-event", G_CALLBACK(capture_key_cb), NULL);
    g_object_set_data(G_OBJECT(capture), "drawer-keys", capture_key_cb); /* the keys of its drawer */
    int r = run_dialog(capture);
    capture = NULL;
    if (r == RESPONSE_DELETE)
        set(capture_slot, "");
    else if (r != GTK_RESPONSE_OK)
        set(capture_slot, old);
    g_free(old);
    unbind_elsewhere(capture_slot, capture_pad);
    relabel_slots();
}

static void layout_changed_cb(GtkWidget *c, gpointer data)
{
    (void) c, (void) data;
    relabel_slots();
}

static void defaults_cb(GtkWidget *b, gpointer data)
{
    (void) b, (void) data;
    load(edit_keys, edit_pads, TRUE);
    relabel_slots();
}

/* An action: its two keys and two controller buttons, large, each pressed to set it. */
static void action_cb(GtkButton *b, gpointer data)
{
    (void) b;
    int a = GPOINTER_TO_INT(data);
    GtkWidget *d = dialog_new(tr(ACTIONS[a].label));
    drawer_set_back(d);
    GtkWidget *grid = gtk_table_new(4, 2, TRUE);
    gtk_table_set_row_spacings(GTK_TABLE(grid), 8);
    gtk_table_set_col_spacings(GTK_TABLE(grid), 8);
    gtk_table_attach_defaults(GTK_TABLE(grid), row_text(tr("Keyboard"), TRUE, FALSE, TRUE), 0, 2, 0, 1);
    gtk_table_attach_defaults(GTK_TABLE(grid), row_text(tr("Controller"), TRUE, FALSE, TRUE), 0, 2, 2, 3);
    for (int s = 0; s < 4; s++) {
        GtkWidget *slot = slot_buttons[a][s] = gtk_button_new_with_label("");
        gtk_widget_set_size_request(slot, -1, 52);
        g_signal_connect(slot, "clicked", G_CALLBACK(slot_cb), GINT_TO_POINTER(a * 4 + s));
        gtk_table_attach_defaults(GTK_TABLE(grid), slot, s % 2, s % 2 + 1, 1 + s / 2 * 2, 2 + s / 2 * 2);
    }
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), grid, FALSE, FALSE, 8);
    relabel_slots();
    run_dialog(d);
    for (int s = 0; s < 4; s++)
        slot_buttons[a][s] = NULL;
}

static void keys_gone_cb(GtkWidget *w, gpointer data)
{
    (void) w;
    action_keys[GPOINTER_TO_INT(data)] = NULL;
}

/* Preferences → Controls: the controller's names and the legend, then a row per action with its
 * keys and buttons; a tap opens the action (action_cb). */
GtkWidget *controls_page(void)
{
    load(edit_keys, edit_pads, FALSE);
    GtkWidget *box = gtk_vbox_new(FALSE, 2), *card = rows_card(box);
    static const char *layouts[] = {"Automatic", "Xbox", "Nintendo", "PlayStation"};
    layout_combo = combo(layouts, 4, setting_int("PadLayout", LAYOUT_AUTO), TRUE);
    g_signal_connect(layout_combo, "changed", G_CALLBACK(layout_changed_cb), NULL);
    g_signal_connect(layout_combo, "destroy", G_CALLBACK(gtk_widget_destroyed), &layout_combo);
    GtkWidget *layout_row = gtk_vbox_new(FALSE, 4);
    gtk_container_set_border_width(GTK_CONTAINER(layout_row), 8);
    gtk_box_pack_start(GTK_BOX(layout_row), row_text(tr("Controller buttons"), TRUE, FALSE, TRUE), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(layout_row), layout_combo, FALSE, FALSE, 0);
    row_add(card, layout_row);
    legend_check = row_add(card, row_switch(tr("Show keys and controller buttons beside the buttons on screen"),
                                            setting_int("ButtonLegend", 1)));
    g_signal_connect(legend_check, "destroy", G_CALLBACK(gtk_widget_destroyed), &legend_check);
    pad_status = row_text("", TRUE, FALSE, TRUE);
    gtk_misc_set_padding(GTK_MISC(pad_status), 8, 6);
    row_add(card, pad_status);
    rows_group(box, tr("Controls"));
    card = rows_card(box);
    for (int a = 0; a < ACT_COUNT; a++) {
        GtkWidget *b = row_add(card, row_choice(tr(ACTIONS[a].label), "", &action_keys[a]));
        g_signal_connect(b, "clicked", G_CALLBACK(action_cb), GINT_TO_POINTER(a));
        g_signal_connect(b, "destroy", G_CALLBACK(keys_gone_cb), GINT_TO_POINTER(a));
    }
    GtkWidget *defaults = gtk_button_new_with_label(tr("Restore defaults")), *bottom = gtk_hbox_new(FALSE, 6);
    g_signal_connect(defaults, "clicked", G_CALLBACK(defaults_cb), NULL);
    gtk_box_pack_end(GTK_BOX(bottom), defaults, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), bottom, FALSE, FALSE, 8);
    relabel_slots();
    return box;
}

void controls_save(void)
{
    if (legend_check)
        g_key_file_set_integer(app.settings, "Settings", "ButtonLegend",
                               gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(legend_check)));
    if (layout_combo)
        g_key_file_set_integer(app.settings, "Settings", "PadLayout", gtk_combo_box_get_active(GTK_COMBO_BOX(layout_combo)));
    for (int a = 0; a < ACT_COUNT; a++)
        for (int t = 0; t < 2; t++) {
            char key[32], **v = t ? edit_pads[a] : edit_keys[a];
            if (!v[0])
                return; /* the page was never opened */
            snprintf(key, sizeof key, "%s.%s", t ? "Pad" : "Key", ACTIONS[a].id);
            gboolean same = !strcmp(v[0], t ? ACTIONS[a].pads[0] : ACTIONS[a].keys[0]) &&
                            !strcmp(v[1], t ? ACTIONS[a].pads[1] : ACTIONS[a].keys[1]);
            if (same) {
                g_key_file_remove_key(app.settings, "Settings", key, NULL);
            } else {
                char *value = g_strdup_printf("%s;%s", v[0], v[1]);
                g_key_file_set_string(app.settings, "Settings", key, value);
                g_free(value);
            }
        }
    controls_load();
    area_hints_update();
}
