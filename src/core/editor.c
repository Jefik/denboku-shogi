/* The position editor. */

#include "app.h"

/* ---- position editor ---- */

/* Undo steps of the editor, and the palette (tools and pieces of one color). */
#define UNDO_STEPS 100
static Position undo_stack[UNDO_STEPS], redo_stack[UNDO_STEPS];
static int undo_count, redo_count, palette_color;
static GtkWidget *sel_row, *palette_box, *tool_move, *tool_erase, *edit_hint, *sel_buttons[3], *color_button, *actions_box;
/* The tools and buttons above the palette, in as many rows as the window's width needs. */
static GtkWidget *actions[10];
static int n_actions, laid_width;
static EditTool cur_tool;

static void edit_refresh(void)
{
    app.game.start = app.game.pos;
    gtk_button_set_label(GTK_BUTTON(app.edit_side), tr(SIDE_TO_MOVE[app.game.pos.side]));
    app.can_undo = undo_count > 0;
    view_queue_bar(app.view);
    view_refresh(app.view);
}

static void push(Position *stack, int *count)
{
    if (*count == UNDO_STEPS)
        memmove(stack, stack + 1, sizeof stack[0] * (UNDO_STEPS - 1)), --*count;
    stack[(*count)++] = app.game.pos;
}

static void edit_save_undo(void)
{
    push(undo_stack, &undo_count);
    redo_count = 0;
}

/* Undo (or redo) one change; FALSE when not editing. */
static gboolean edit_step(gboolean redo)
{
    if (!app.edit_saved)
        return FALSE;
    Position *from = redo ? redo_stack : undo_stack, *to = redo ? undo_stack : redo_stack;
    int *from_count = redo ? &redo_count : &undo_count, *to_count = redo ? &undo_count : &redo_count;
    if (*from_count) {
        push(to, to_count);
        app.game.pos = from[--*from_count];
        view_edit_op(app.view, EDIT_DESELECT);
        edit_refresh();
    }
    return TRUE;
}

gboolean edit_undo(void) { return edit_step(FALSE); }
gboolean edit_redo(void) { return edit_step(TRUE); }

static void edit_selection_changed(void)
{
    gboolean any = view_edit_selected(app.view) > 0;
    GtkRequisition r; /* the hint as tall as the actions, so that the board does not move */
    gtk_widget_size_request(sel_row, &r);
    gtk_widget_set_size_request(edit_hint, -1, r.height);
    gtk_widget_set_visible(sel_row, any); /* the selection's actions take the hint's place */
    gtk_widget_set_visible(edit_hint, !any);
}

void view_edit_event_cb(View *v, int what, void *data)
{
    (void) v, (void) data;
    if (what == EDIT_BEFORE)
        edit_save_undo();
    else if (what == EDIT_CHANGED)
        edit_refresh();
    else
        edit_selection_changed();
}

static void edit_hand(int color)
{
    int types[16], n = setup_hand_types(types);
    GtkWidget *d = dialog_new(tr("Pieces in hand"));
    drawer_set_beside(d);
    GtkWidget *table = gtk_table_new(n, 2, FALSE);
    GtkWidget *spins[16];
    int cell = app.kindle ? 64 : 36;
    for (int i = 0; i < n; i++) {
        int kind = piece_kind(PIECE(types[i], color));
        GdkPixbuf *pb = pieces_pixbuf(app.style, &kind, 1, FALSE, cell);
        GtkWidget *img = gtk_image_new_from_pixbuf(pb);
        g_object_unref(pb);
        spins[i] = gtk_spin_button_new_with_range(0, 99, 1); /* any number, not only a set's */
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(spins[i]),
                                  app.game.pos.hand[color][hand_slot(types[i])]);
        gtk_table_attach(GTK_TABLE(table), img, 0, 1, i, i + 1, GTK_FILL, GTK_FILL, 8, 2);
        gtk_table_attach(GTK_TABLE(table), spins[i], 1, 2, i, i + 1, GTK_FILL, GTK_FILL, 8, 2);
    }
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), gtk_label_new(tr(SIDE_PALETTE[color])),
                       FALSE, FALSE, 6);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), table, TRUE, TRUE, 6);
    gtk_dialog_add_button(GTK_DIALOG(d), tr("Cancel"), GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(d), tr("OK"), GTK_RESPONSE_OK);
    gtk_widget_show_all(d);
    if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
        edit_save_undo();
        for (int i = 0; i < n; i++)
            app.game.pos.hand[color][hand_slot(types[i])] =
                gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spins[i]));
    }
    gtk_widget_destroy(d);
    edit_refresh();
}

void view_edit_cb(View *v, int sq, int hand_color, void *data)
{
    (void) v, (void) data, (void) sq;
    edit_hand(hand_color);
}

static void edit_end(void)
{
    view_set_editing(app.view, FALSE);
    gtk_widget_hide(app.edit_bar);
}

static void edit_side_cb(GtkWidget *w, gpointer data)
{
    (void) w, (void) data;
    edit_save_undo();
    app.game.pos.side = !app.game.pos.side;
    edit_refresh();
}

/* Clear: 0 everything, 1 the board, 2 Sente's pieces, 3 Gote's pieces (board and hand), 4 the hands. */
static void edit_clear_item_cb(GtkWidget *w, gpointer data)
{
    (void) w;
    int what = GPOINTER_TO_INT(data);
    edit_save_undo();
    Position *p = &app.game.pos;
    view_edit_op(app.view, EDIT_DESELECT);
    for (int sq = 0; sq < SQUARES; sq++)
        if (what <= 1 || (what <= 3 && p->board[sq] && COLOR(p->board[sq]) == what - 2))
            p->board[sq] = EMPTY;
    for (int c = 0; c < 2; c++)
        if (what == 0 || what == 4 || what == c + 2)
            memset(p->hand[c], 0, sizeof p->hand[c]);
    edit_refresh();
}

static void edit_clear_cb(GtkWidget *w, gpointer data)
{
    (void) data;
    static const char *items[] = {"Everything", "Board", SIDE_PIECES[FIRST], SIDE_PIECES[SECOND], "Pieces in hand"};
    GtkWidget *menu = gtk_menu_new();
    for (int i = 0; i < 5; i++) {
        GtkWidget *item = gtk_menu_item_new_with_label(tr(items[i]));
        g_signal_connect(item, "activate", G_CALLBACK(edit_clear_item_cb), GINT_TO_POINTER(i));
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }
    gtk_widget_show_all(menu);
    gtk_menu_attach_to_widget(GTK_MENU(menu), w, NULL);
    gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, 0, gtk_get_current_event_time());
}

static void edit_initial_cb(GtkWidget *w, gpointer data)
{
    (void) w, (void) data;
    edit_save_undo();
    view_edit_op(app.view, EDIT_DESELECT);
    pos_start(&app.game.pos);
    edit_refresh();
}

static void edit_op_cb(GtkWidget *w, gpointer data)
{
    (void) w;
    view_edit_op(app.view, (EditOp) GPOINTER_TO_INT(data));
}

static const char *tool_hint(EditTool t)
{
    return t == TOOL_MOVE    ? tr("Tap a piece, then a square or a hand.")
           : t == TOOL_ERASE ? tr("Tap or drag over pieces to remove them.")
                             : tr("Tap or drag to place; the same piece removes it.");
}

static void tool_cb(GtkToggleButton *b, gpointer data)
{
    (void) data;
    if (!gtk_toggle_button_get_active(b))
        return;
    int piece = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "piece"));
    EditTool t = GTK_WIDGET(b) == tool_move ? TOOL_MOVE : GTK_WIDGET(b) == tool_erase ? TOOL_ERASE : TOOL_PLACE;
    view_set_edit_tool(app.view, t, piece);
    gtk_label_set_text(GTK_LABEL(edit_hint), tool_hint(cur_tool = t));
}

/* The palette: Move and Erase, the color, then every piece of the variant in that color. */
static int bar_width(void)
{
    int w, h;
    gtk_window_get_size(GTK_WINDOW(app.window), &w, &h);
    return MIN(w, gdk_screen_width()) - 8; /* a bar too wide has made the window wider than the screen */
}

/* The tools and buttons, as many in a row as fit. */
static void layout_actions(int width)
{
    int sizes[G_N_ELEMENTS(actions)];
    for (int i = 0; i < n_actions; i++) { /* measured in the window, with its style */
        GtkRequisition r;
        gtk_widget_size_request(actions[i], &r);
        sizes[i] = r.width;
    }
    for (int i = 0; i < n_actions; i++)
        if (gtk_widget_get_parent(actions[i]))
            gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(actions[i])), actions[i]);
    GList *rows = gtk_container_get_children(GTK_CONTAINER(actions_box));
    for (GList *l = rows; l; l = l->next)
        gtk_widget_destroy(l->data);
    g_list_free(rows);
    GtkWidget *row = NULL;
    int used = 0;
    for (int i = 0; i < n_actions; i++) {
        if (!row || used + 4 + sizes[i] > width) {
            row = gtk_hbox_new(FALSE, 4);
            gtk_box_pack_start(GTK_BOX(actions_box), row, FALSE, FALSE, 0);
            used = -4;
        }
        gtk_box_pack_start(GTK_BOX(row), actions[i], FALSE, FALSE, 0);
        used += 4 + sizes[i];
    }
    gtk_widget_show_all(actions_box);
}

/* The pieces of one color as buttons, in the fewest rows where they stay large enough to tap,
 * filling the window's width. */
static void build_palette(void)
{
    int width = bar_width();
    laid_width = width;
    layout_actions(width);
    GList *old = gtk_container_get_children(GTK_CONTAINER(palette_box));
    for (GList *l = old; l; l = l->next)
        gtk_widget_destroy(l->data);
    g_list_free(old);
    int types[16], n = setup_piece_types(types);
    /* A button's frame around its picture, and the text's size. */
    GtkWidget *probe = gtk_toggle_button_new();
    gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(probe), FALSE);
    gtk_container_add(GTK_CONTAINER(probe), gtk_image_new_from_pixbuf(NULL));
    gtk_table_attach(GTK_TABLE(palette_box), probe, 0, 1, 0, 1, GTK_FILL, GTK_FILL, 0, 0); /* with the window's style */
    GtkRequisition r;
    gtk_widget_size_request(probe, &r);
    gtk_widget_destroy(probe);
    int lh = line_height(), pad = r.width, cols = n, cell = 0;
    for (int rows = 1; rows <= 4; rows++) {
        cols = (n + rows - 1) / rows;
        cell = (width - (cols - 1) * 2) / cols - pad;
        if (cell >= (app.kindle ? 56 : lh * 1.6))
            break;
    }
    cell = CLAMP(cell, 16, app.kindle ? 56 : (int) (lh * 2.4));
    gtk_table_resize(GTK_TABLE(palette_box), (n + cols - 1) / cols, cols);
    gtk_button_set_label(GTK_BUTTON(color_button), SIDE_MARKS[palette_color]);
    GSList *group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(tool_move));
    for (int i = 0; i < n; i++) {
        int t = piece_kind(PIECE(types[i], palette_color));
        GdkPixbuf *pb = pieces_pixbuf(app.style, &t, 1, palette_color == SECOND, cell);
        GtkWidget *b = gtk_radio_button_new(group);
        group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(b));
        gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(b), FALSE);
        gtk_widget_set_can_focus(b, FALSE);
        gtk_container_add(GTK_CONTAINER(b), gtk_image_new_from_pixbuf(pb));
        g_object_unref(pb);
        g_object_set_data(G_OBJECT(b), "piece", GINT_TO_POINTER(PIECE(types[i], palette_color)));
        g_signal_connect(b, "toggled", G_CALLBACK(tool_cb), NULL);
        gtk_table_attach(GTK_TABLE(palette_box), b, i % cols, i % cols + 1, i / cols, i / cols + 1, GTK_FILL | GTK_EXPAND,
                         GTK_FILL, 0, 0);
    }
    gtk_widget_show_all(palette_box);
}

/* The window changed size: the bar follows. */
void edit_bar_layout(void)
{
    if (app.edit_saved && gtk_widget_get_visible(app.edit_bar) && bar_width() != laid_width)
        build_palette();
}

/* The other color: the piece chosen stays chosen, in the new color. */
static void color_cb(GtkWidget *w, gpointer data)
{
    (void) w, (void) data;
    int chosen = -1, i = 0;
    GList *kids = g_list_reverse(gtk_container_get_children(GTK_CONTAINER(palette_box)));
    for (GList *l = kids; l; l = l->next)
        if (GTK_IS_RADIO_BUTTON(l->data)) {
            if (gtk_toggle_button_get_active(l->data))
                chosen = i;
            i++;
        }
    g_list_free(kids);
    palette_color = !palette_color;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tool_move), TRUE);
    build_palette();
    i = 0;
    kids = g_list_reverse(gtk_container_get_children(GTK_CONTAINER(palette_box)));
    for (GList *l = kids; l; l = l->next)
        if (GTK_IS_RADIO_BUTTON(l->data) && i++ == chosen)
            gtk_toggle_button_set_active(l->data, TRUE);
    g_list_free(kids);
}

/* The palette's next (or previous) tool or piece, as the controller goes through it. */
void edit_tool_step(int step)
{
    GList *kids = g_list_reverse(gtk_container_get_children(GTK_CONTAINER(palette_box)));
    GList *tools = g_list_append(g_list_append(NULL, tool_move), tool_erase);
    for (GList *l = kids; l; l = l->next)
        tools = g_list_append(tools, l->data);
    g_list_free(kids);
    int n = g_list_length(tools), at = 0;
    for (GList *l = tools; l; l = l->next, at++)
        if (gtk_toggle_button_get_active(l->data))
            break;
    gtk_toggle_button_set_active(g_list_nth_data(tools, ((at + step) % n + n) % n), TRUE);
    g_list_free(tools);
}

void edit_other_color(void) { color_cb(NULL, NULL); }

/* Keys while editing: Delete, Escape, Ctrl+A, Ctrl+Z. */
gboolean edit_key(guint key, guint state)
{
    if (!app.edit_saved)
        return FALSE;
    gboolean ctrl = (state & GDK_CONTROL_MASK) != 0;
    if (key == GDK_Delete || key == GDK_BackSpace)
        return view_edit_op(app.view, EDIT_DELETE);
    if (key == GDK_Escape && !controls_in_bar())
        return view_edit_op(app.view, EDIT_DESELECT);
    if (ctrl && (key == GDK_a || key == GDK_A))
        return view_edit_op(app.view, EDIT_SELECT_ALL);
    if (ctrl && (key == GDK_z || key == GDK_Z))
        return edit_step(state & GDK_SHIFT_MASK);
    return FALSE;
}

/* The game from before the editing, back in its place. */
static void restore_saved(void)
{
    app.game = *app.edit_saved;
    app.variant = app.edit_saved_variant;
    rules_set_variant(app.variant);
}

static void drop_saved(void)
{
    g_free(app.edit_saved);
    app.edit_saved = NULL;
}

/* The edited position as the start of the next game; FALSE (and why, shown) when it cannot be one. */
static gboolean edit_to_start(void)
{
    const char *problem = setup_problem(&app.game.pos);
    if (problem) {
        message(problem);
        return FALSE;
    }
    char sfen[256]; /* as pos_to_text() writes at most */
    app.game.pos.ply = 0;
    pos_to_text(&app.game.pos, sfen, sizeof sfen);
    g_free(app.start_sfen);
    app.start_sfen = g_strdup(sfen);
    return TRUE;
}

static void edit_cancel_cb(GtkWidget *w, gpointer data)
{
    (void) w, (void) data;
    restore_saved();
    drop_saved();
    edit_end();
    view_reload(app.view);
    next_turn();
}

/* The variant as a chip that opens the list in a drawer, as every other choice; the combo box,
 * never shown, keeps the value. */
static GtkWidget *variant_chip;
static int variant_picked;

static void variant_chip_update(void)
{
    int v = gtk_combo_box_get_active(GTK_COMBO_BOX(app.edit_variant));
    char *text = g_strdup_printf("%s ›", variant_label(CLAMP(v, 0, V_COUNT - 1)));
    gtk_button_set_label(GTK_BUTTON(variant_chip), text);
    g_free(text);
}

static void variant_pick_cb(int i, gpointer data)
{
    (void) data;
    variant_picked = i;
}

/* Only a confirmed choice counts: a new variant starts the board again, and the list's taps
 * (and its Cancel) would each have wiped the position being set up. */
static void variant_chip_cb(GtkButton *b, gpointer data)
{
    (void) b, (void) data;
    const char *labels[V_COUNT];
    for (int v = 0; v < V_COUNT; v++)
        labels[v] = variant_label(v);
    int active = gtk_combo_box_get_active(GTK_COMBO_BOX(app.edit_variant));
    variant_picked = active;
    if (rows_pick(tr("Variant"), labels, NULL, V_COUNT, active, variant_pick_cb, NULL) == GTK_RESPONSE_OK &&
        variant_picked != active)
        gtk_combo_box_set_active(GTK_COMBO_BOX(app.edit_variant), variant_picked);
}

static void edit_variant_cb(GtkComboBox *c, gpointer data)
{
    (void) data;
    int v = gtk_combo_box_get_active(c);
    if (v < 0 || v >= V_COUNT)
        return;
    app.variant = v;
    variant_chip_update();
    rules_set_variant(v);
    pos_start(&app.game.pos);
    app.game.pos.ply = 0;
    undo_count = redo_count = 0; /* another board size */
    view_edit_op(app.view, EDIT_DESELECT);
    apply_variant_style(); /* the palette shows the variant's pieces */
    build_palette();
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tool_move), TRUE);
    edit_refresh();
    view_reload(app.view);
}

/* Sandbox: play the position at once, both sides human and no clock. */
static void edit_sandbox_cb(GtkWidget *w, gpointer data)
{
    (void) w, (void) data;
    if (!edit_to_start())
        return;
    app.game = *app.edit_saved;
    drop_saved();
    edit_end();
    app.time_index = TIME_CUSTOM;
    app.custom_tc = (TimeControl) {NULL, 0, 0, 0};
    start_game(HANDICAP_NONE, MODE_TWO_PLAYERS);
}

/* Done: the position becomes the start of the next game, set up as usual in New Game. */
static void edit_done_cb(GtkWidget *w, gpointer data)
{
    (void) w, (void) data;
    if (!edit_to_start())
        return;
    edit_end();
    new_game_dialog();
    if (app.start_sfen) {
        /* No game started from it: back to the game before. */
        g_free(app.start_sfen);
        app.start_sfen = NULL;
        restore_saved();
        view_reload(app.view);
        next_turn();
    }
    drop_saved();
}

void set_up_position(void)
{
    if (app.net || server_game() || app.edit_saved) {
        if (!app.edit_saved)
            toast(tr("Finish the online game first."));
        return;
    }
    if (app.engine)
        engine_cancel(app.engine);
    app.engine_thinking = app.hint_pending = FALSE;
    app.edit_saved = g_memdup(&app.game, sizeof app.game);
    app.edit_saved_variant = app.variant;
    rules_set_variant(app.variant);
    Position p;
    int ply = view_ply(app.view);
    game_position_at(&app.game, ply < 0 ? app.game.count : ply, &p);
    game_new(&app.game, HANDICAP_NONE);
    app.game.pos = p;
    app.game.pos.ply = 0;
    g_signal_handlers_block_by_func(app.edit_variant, edit_variant_cb, NULL);
    gtk_combo_box_set_active(GTK_COMBO_BOX(app.edit_variant), app.variant);
    g_signal_handlers_unblock_by_func(app.edit_variant, edit_variant_cb, NULL);
    variant_chip_update();
    view_set_ply(app.view, -1);
    view_set_editing(app.view, TRUE);
    undo_count = redo_count = 0;
    build_palette(); /* the pieces in the current style */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tool_move), TRUE);
    view_set_edit_tool(app.view, TOOL_MOVE, 0);
    gtk_label_set_text(GTK_LABEL(edit_hint), tool_hint(cur_tool = TOOL_MOVE));
    edit_selection_changed();
    gtk_widget_show(app.edit_bar);
    edit_refresh();
}

/* After a language change: the texts that relabel() cannot know. */
void edit_relabel(void)
{
    if (!app.edit_variant)
        return;
    int active = gtk_combo_box_get_active(GTK_COMBO_BOX(app.edit_variant));
    g_signal_handlers_block_by_func(app.edit_variant, edit_variant_cb, NULL);
    for (int v = V_COUNT - 1; v >= 0; v--)
        gtk_combo_box_remove_text(GTK_COMBO_BOX(app.edit_variant), v);
    for (int v = 0; v < V_COUNT; v++)
        gtk_combo_box_append_text(GTK_COMBO_BOX(app.edit_variant), variant_label(v));
    gtk_combo_box_set_active(GTK_COMBO_BOX(app.edit_variant), active);
    g_signal_handlers_unblock_by_func(app.edit_variant, edit_variant_cb, NULL);
    variant_chip_update();
    gtk_label_set_text(GTK_LABEL(edit_hint), tool_hint(cur_tool));
    if (app.edit_saved)
        edit_refresh();
}

static GtkWidget *bar_button(const char *label, GCallback cb, gpointer data)
{
    GtkWidget *b = gtk_button_new_with_label(tr(label));
    follow_language(b, label);
    gtk_widget_set_can_focus(b, FALSE);
    g_signal_connect(b, "clicked", cb, data);
    return b;
}

GtkWidget *edit_bar_new(void)
{
    GtkWidget *bar = gtk_vbox_new(FALSE, 4);
    gtk_container_set_border_width(GTK_CONTAINER(bar), 4);
    g_object_set_data(G_OBJECT(bar), "finger-tall", bar); /* style.c: chips and buttons a finger tall */
    GtkWidget *rows[3];
    rows[0] = palette_box = gtk_table_new(1, 1, TRUE);
    gtk_table_set_col_spacings(GTK_TABLE(palette_box), 2);
    gtk_table_set_row_spacings(GTK_TABLE(palette_box), 2);
    rows[1] = actions_box = gtk_vbox_new(FALSE, 4);
    rows[2] = gtk_vbox_new(FALSE, 6);
    tool_move = gtk_radio_button_new_with_label(NULL, tr("Move"));
    tool_erase = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(tool_move), tr("Erase"));
    follow_language(tool_move, "Move");
    follow_language(tool_erase, "Erase");
    GtkWidget *tools[] = {tool_move, tool_erase};
    for (int i = 0; i < 2; i++) {
        gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(tools[i]), FALSE);
        gtk_widget_set_can_focus(tools[i], FALSE);
        g_signal_connect(tools[i], "toggled", G_CALLBACK(tool_cb), NULL);
    }
    color_button = bar_button(SIDE_MARKS[FIRST], G_CALLBACK(color_cb), NULL);
    app.edit_side = bar_button(SIDE_TO_MOVE[FIRST], G_CALLBACK(edit_side_cb), NULL); /* measured with its label */
    app.edit_variant = gtk_combo_box_new_text();
    for (int v = 0; v < V_COUNT; v++)
        gtk_combo_box_append_text(GTK_COMBO_BOX(app.edit_variant), variant_label(v));
    gtk_combo_box_set_active(GTK_COMBO_BOX(app.edit_variant), app.variant);
    g_signal_connect(app.edit_variant, "changed", G_CALLBACK(edit_variant_cb), NULL);
    g_object_ref_sink(app.edit_variant); /* kept, never packed: the chip shows it */
    variant_chip = gtk_button_new_with_label("");
    gtk_widget_set_can_focus(variant_chip, FALSE);
    g_signal_connect(variant_chip, "clicked", G_CALLBACK(variant_chip_cb), NULL);
    variant_chip_update();
    static const char *ops[] = {"Promote", "Flip side", "Delete"};
    static const EditOp op_ids[] = {EDIT_PROMOTE, EDIT_FLIP, EDIT_DELETE};
    for (int i = 0; i < 3; i++)
        sel_buttons[i] = bar_button(ops[i], G_CALLBACK(edit_op_cb), GINT_TO_POINTER(op_ids[i]));
    GtkWidget *clear = bar_button("Clear…", G_CALLBACK(edit_clear_cb), NULL);
    GtkWidget *initial = bar_button("Initial position", G_CALLBACK(edit_initial_cb), NULL);
    GtkWidget *sandbox = bar_button("Play", G_CALLBACK(edit_sandbox_cb), NULL);
    GtkWidget *cancel = bar_button("Cancel", G_CALLBACK(edit_cancel_cb), NULL);
    GtkWidget *done = bar_button("Done", G_CALLBACK(edit_done_cb), NULL);
    edit_hint = gtk_label_new("");
    gtk_misc_set_alignment(GTK_MISC(edit_hint), 0, 0.5);
    gtk_label_set_ellipsize(GTK_LABEL(edit_hint), PANGO_ELLIPSIZE_END);
    /* Tools and selection as chips (the mockup 2026-11-15-1-ui-review/5-sandbox); the actions as wide buttons at the foot,
     * Done the main one. */
    GtkWidget *chips[] = {tool_move, tool_erase, color_button, app.edit_side, variant_chip, sel_buttons[0], sel_buttons[1], sel_buttons[2]};
    for (size_t i = 0; i < G_N_ELEMENTS(chips); i++)
        gtk_widget_set_name(chips[i], "denboku-chip");
    gtk_widget_set_name(done, "denboku-primary");
    GtkWidget *row1[] = {tool_move, tool_erase, color_button, app.edit_side, variant_chip};
    sel_row = gtk_hbox_new(FALSE, 4);
    for (int i = 0; i < 3; i++)
        gtk_box_pack_start(GTK_BOX(sel_row), sel_buttons[i], FALSE, FALSE, 0);
    GtkWidget *first = gtk_hbox_new(FALSE, 4); /* until the window's width is known */
    gtk_box_pack_start(GTK_BOX(actions_box), first, FALSE, FALSE, 0);
    for (size_t i = 0; i < G_N_ELEMENTS(row1); i++) {
        actions[n_actions++] = g_object_ref(row1[i]); /* kept while the rows are made again */
        gtk_box_pack_start(GTK_BOX(first), row1[i], FALSE, FALSE, 0);
    }
    /* Five buttons on one line where the window is wide; on a narrow one Cancel and Done, then the
     * rest. Wide in a computer's pixels (a phone's 1080 are too few at its scale), and the window's
     * width, not the screen's: Android's X screen is a square as large as its longer side. */
    gboolean narrow = app.kindle || bar_width() < ui_limit(900);
    GtkWidget *foot = gtk_vbox_new(FALSE, 6), *line = gtk_hbox_new(TRUE, 6);
    GtkWidget *foot_buttons[] = {clear, initial, sandbox, cancel, done};
    gtk_box_pack_start(GTK_BOX(foot), line, FALSE, FALSE, 0);
    for (size_t i = 0; i < G_N_ELEMENTS(foot_buttons); i++) {
        if (foot_buttons[i] != done)
            gtk_widget_set_name(foot_buttons[i], "denboku-wide");
        if (narrow && i == 3) {
            line = gtk_hbox_new(TRUE, 6);
            gtk_box_pack_start(GTK_BOX(foot), line, FALSE, FALSE, 0);
            gtk_box_reorder_child(GTK_BOX(foot), line, 0); /* Cancel and Done first */
        }
        gtk_box_pack_start(GTK_BOX(line), foot_buttons[i], TRUE, TRUE, 0);
    }
    gtk_box_pack_start(GTK_BOX(rows[2]), edit_hint, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rows[2]), sel_row, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rows[2]), foot, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), rows[1], FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), rows[0], FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), rows[2], FALSE, FALSE, 0);
    gtk_widget_show_all(bar);
    gtk_widget_set_no_show_all(bar, TRUE);
    gtk_widget_hide(bar);
    return bar;
}
