/* Mate problems (tsume shogi): the list, the dialog that picks one, what is solved. The game
 * flow around a problem being played is in play.c. */

#include "app.h"
#include "problems.h"

static GArray *list;

static GArray *problem_list(void)
{
    if (!list) {
        char *path = g_build_filename(app.base, "problems", "problems.txt", NULL), *text = NULL;
        list = problems_parse(g_file_get_contents(path, &text, NULL, NULL) ? text : "", V_STANDARD);
        g_free(text);
        g_free(path);
        for (int v = 1; v < V_COUNT; v++) { /* problems-<variant>.txt beside it, for each variant that has them */
            char *name = g_strdup_printf("problems-%s.txt", variant_engine_name(v));
            path = g_build_filename(app.base, "problems", name, NULL);
            if (g_file_get_contents(path, &text, NULL, NULL))
                problems_add(list, text, v);
            g_free(text);
            text = NULL;
            g_free(name);
            g_free(path);
        }
    }
    return list;
}

static char *solved_ids(void) { return setting_string("ProblemsSolved", ""); }

/* "Mate in 2" for 3 plies; in Japanese the plies, "3手詰". */
char *problem_label(int plies)
{
    if (i18n_japanese())
        return g_strdup_printf("%d手詰", plies);
    return g_strdup_printf(tr("Mate in %d"), problem_moves(plies));
}

gboolean problem_playing(void) { return app.problem_plies > 0 && !app.loaded_record; }

/* The problem on the board as the defender's name: "Mate in 2, problem 12" (its number in the grid). */
const char *problem_title(void)
{
    static char title[96];
    GArray *l = problem_list();
    char *what = problem_label(app.problem_plies);
    if (app.problem_index >= 0 && app.problem_index < (int) l->len)
        snprintf(title, sizeof title, tr("%s, problem %d"), what, problems_rank(l, app.problem_index));
    else
        snprintf(title, sizeof title, "%s", what);
    g_free(what);
    return title;
}

/* Marks the problem on the board as solved. */
void problem_solved_now(void)
{
    GArray *l = problem_list();
    if (!problem_playing() || app.problem_index < 0 || app.problem_index >= (int) l->len)
        return;
    char *ids = solved_ids(), *now = problem_mark_solved(ids, &g_array_index(l, Problem, app.problem_index));
    g_key_file_set_string(app.settings, "Settings", "ProblemsSolved", now);
    save_settings();
    g_free(ids);
    g_free(now);
}

/* The first problem not solved yet of the length being played, after this one; once all are
 * solved, the one after this. */
static void start_next(int variant, int plies, int after)
{
    GArray *l = problem_list();
    char *ids = solved_ids();
    int i = problems_next_unsolved(l, variant, plies, ids, after);
    g_free(ids);
    if (i < 0)
        i = problems_next(l, variant, plies, after);
    if (i >= 0)
        start_problem(variant, plies, i, g_array_index(l, Problem, i).position);
}

void problem_next(void) { start_next(app.variant, app.problem_plies, app.problem_index); }

typedef struct {
    GtkWidget *info, *holder, *table, *card, *lengths, *clear, *clear_value; /* the card, and the row of lengths it holds */
    int variants[V_COUNT], nvar, vactive;
    char *labels[8];
    GPtrArray *chips; /* the numbers of the length chosen, as chips */
    int plies[8], n, active;
    int variant; /* the one chosen */
    int number, count; /* the problem chosen, 1 to count, among those of the length chosen */
    int cols, width, passes; /* the grid's columns, the width they were made for, the measures since the last change */
    guint idle;
} Pick;

static void pick_show(Pick *p)
{
    GArray *l = problem_list();
    int plies = p->plies[p->active], done;
    p->count = problems_count(l, p->variant, plies, NULL, NULL);
    p->number = CLAMP(p->number, 1, p->count);
    char *ids = solved_ids();
    problems_count(l, p->variant, plies, ids, &done);
    g_free(ids);
    char *text = g_strdup_printf(tr("%d of %d solved"), done, p->count);
    gtk_label_set_text(GTK_LABEL(p->info), text);
    g_free(text);
}

static void number_cb(GtkToggleButton *b, gpointer data)
{
    Pick *p = data;
    if (gtk_toggle_button_get_active(b))
        p->number = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "number"));
}

/* The chips in as many columns as the width the page has now fits, all cells as wide as the widest
 * chip: a narrow portrait drawer gets fewer columns, a wide one more, and a change of width asks again. */
static gboolean grid_idle(gpointer data);

static void grid_layout(Pick *p)
{
    int width = p->holder->allocation.width, cell = 1;
    if (width <= 1 || !p->chips->len)
        return;
    for (guint i = 0; i < p->chips->len; i++) {
        GtkRequisition r;
        gtk_widget_size_request(g_ptr_array_index(p->chips, i), &r);
        cell = MAX(cell, r.width);
    }
    int cols = CLAMP((width + 6) / (cell + 6), 1, (int) p->chips->len);
    if (cols == p->cols && p->table)
        return;
    p->cols = cols;
    /* Chips have the drawer's font and margins only once in the window: measured again until the columns hold. */
    if (p->passes++ < 4 && !p->idle)
        p->idle = g_idle_add(grid_idle, p);
    GtkWidget *table = gtk_table_new(1, cols, TRUE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 6);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_widget_set_size_request(table, 1, -1); /* the page's width, not the chips', decides */
    for (guint i = 0; i < p->chips->len; i++) {
        GtkWidget *chip = g_ptr_array_index(p->chips, i);
        if (gtk_widget_get_parent(chip)) {
            g_object_ref(chip);
            gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(chip)), chip);
            gtk_table_attach(GTK_TABLE(table), chip, i % cols, i % cols + 1, i / cols, i / cols + 1, GTK_FILL, GTK_FILL, 0, 0);
            g_object_unref(chip);
        } else {
            gtk_table_attach(GTK_TABLE(table), chip, i % cols, i % cols + 1, i / cols, i / cols + 1, GTK_FILL, GTK_FILL, 0, 0);
        }
    }
    if (p->table)
        gtk_widget_destroy(p->table);
    p->table = table;
    gtk_box_pack_start(GTK_BOX(p->holder), table, FALSE, FALSE, 0);
    gtk_widget_show_all(table);
}

static gboolean grid_idle(gpointer data)
{
    Pick *p = data;
    p->idle = 0;
    grid_layout(p);
    return FALSE;
}

static void grid_allocated_cb(GtkWidget *holder, GtkAllocation *a, gpointer data)
{
    Pick *p = data;
    (void) holder;
    if (a->width != p->width && !p->idle) {
        p->width = a->width;
        p->passes = 0;
        p->idle = g_idle_add(grid_idle, p);
    }
}

/* The numbers of the length chosen as a grid of chips, a ✓ on those solved. */
static void grid_build(Pick *p)
{
    GArray *l = problem_list();
    int plies = p->plies[p->active], count = problems_count(l, p->variant, plies, NULL, NULL);
    char *ids = solved_ids();
    if (p->table) {
        gtk_widget_destroy(p->table);
        p->table = NULL;
    }
    g_ptr_array_set_size(p->chips, 0);
    p->count = count;
    p->number = CLAMP(p->number, 1, MAX(count, 1));
    GSList *group = NULL;
    for (int i = 1; i <= count; i++) {
        gboolean done = problem_solved(ids, &g_array_index(l, Problem, problems_nth(l, p->variant, plies, i)));
        char *label = g_strdup_printf(done ? "%d ✓" : "%d", i);
        GtkWidget *chip = gtk_radio_button_new_with_label(group, label);
        g_free(label);
        group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(chip));
        gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(chip), FALSE);
        gtk_widget_set_name(chip, "denboku-chip");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chip), i == p->number);
        g_object_set_data(G_OBJECT(chip), "number", GINT_TO_POINTER(i));
        g_signal_connect(chip, "toggled", G_CALLBACK(number_cb), p);
        g_ptr_array_add(p->chips, g_object_ref_sink(chip));
    }
    g_free(ids);
    p->cols = 0;
    p->passes = 0;
    grid_layout(p);
}

/* How many problems are solved, in every variant: the value of the Clear solved row, which does nothing when there are none. */
static void clear_update(Pick *p)
{
    GArray *l = problem_list();
    char *ids = solved_ids();
    int n = 0;
    for (guint i = 0; i < l->len; i++)
        n += problem_solved(ids, &g_array_index(l, Problem, i));
    g_free(ids);
    char *text = g_strdup_printf("%d", n);
    row_set_value(p->clear_value, n ? text : "");
    g_free(text);
    gtk_widget_set_sensitive(p->clear, n > 0);
}

static void pick_cb(int i, gpointer data);

static void clear_cb(GtkButton *b, gpointer data)
{
    Pick *p = data;
    (void) b;
    if (!confirm_with(tr("Forget which problems you have solved?"), tr("Clear solved")))
        return;
    g_key_file_set_string(app.settings, "Settings", "ProblemsSolved", "");
    save_settings();
    clear_update(p);
    pick_cb(p->active, p); /* the first problem again, no ✓ */
}

/* A new length starts at the first problem of it not solved yet. */
static void pick_cb(int i, gpointer data)
{
    Pick *p = data;
    GArray *l = problem_list();
    char *ids = solved_ids();
    int first = problems_next_unsolved(l, p->variant, p->plies[i], ids, -1);
    g_free(ids);
    p->active = i;
    p->number = first >= 0 ? problems_rank(l, first) : 1;
    pick_show(p);
    grid_build(p);
}

/* The row of lengths the variant chosen has (the last one played, if it has it). */
static void lengths_build(Pick *p)
{
    GArray *l = problem_list();
    GList *old = gtk_container_get_children(GTK_CONTAINER(p->lengths));
    for (GList *o = old; o; o = o->next)
        gtk_widget_destroy(o->data);
    g_list_free(old);
    for (int i = 0; i < p->n; i++)
        g_free(p->labels[i]);
    p->n = problems_lengths(l, p->variant, p->plies, 8);
    const char *items[8];
    int last = CLAMP(setting_int("ProblemPlies", p->plies[0]), 1, 99);
    p->active = 0;
    for (int i = 0; i < p->n; i++) {
        p->labels[i] = problem_label(p->plies[i]);
        items[i] = p->labels[i];
        if (p->plies[i] == last)
            p->active = i;
    }
    gtk_box_pack_start(GTK_BOX(p->lengths), row_chips(NULL, items, p->n, p->active, rows_width(), pick_cb, p), FALSE, FALSE, 0);
    gtk_widget_show_all(p->lengths);
}

static void variant_cb(int i, gpointer data)
{
    Pick *p = data;
    p->vactive = i;
    p->variant = p->variants[i];
    lengths_build(p);
    pick_cb(p->active, p);
}

/* Mate problems…: the variant (when there are problems of more than one) and the length to try,
 * and Start, which gives the first one not solved yet. */
void problems_dialog(void)
{
    Pick p = {.number = 1, .count = 1};
    p.chips = g_ptr_array_new_with_free_func(g_object_unref);
    p.nvar = problems_variants(problem_list(), p.variants, V_COUNT);
    if (!p.nvar) {
        g_ptr_array_free(p.chips, TRUE);
        message(tr("There are no mate problems in this installation."));
        return;
    }
    char *saved = setting_string("ProblemVariant", "");
    for (int i = 0; i < p.nvar; i++)
        if (!strcmp(variant_name(p.variants[i]), saved))
            p.vactive = i;
    g_free(saved);
    p.variant = p.variants[p.vactive];
    GtkWidget *d = dialog_new(tr("Mate problems"));
    GtkWidget *card = p.card = rows_card(GTK_DIALOG(d)->vbox);
    row_add(card, row_para(tr(PROBLEM_INTRO_TEXT)));
    if (p.nvar > 1) {
        const char *names[V_COUNT];
        for (int i = 0; i < p.nvar; i++)
            names[i] = variant_label(p.variants[i]);
        row_add(card, row_chips(NULL, names, p.nvar, p.vactive, rows_width(), variant_cb, &p));
    }
    p.lengths = gtk_vbox_new(FALSE, 0);
    row_add(card, p.lengths);
    p.info = row_text("", TRUE, FALSE, TRUE);
    gtk_misc_set_padding(GTK_MISC(p.info), 8, 4);
    row_add(card, p.info);
    p.clear = row_add(card, row_choice(tr("Clear solved"), "", &p.clear_value));
    g_signal_connect(p.clear, "clicked", G_CALLBACK(clear_cb), &p);
    clear_update(&p);
    p.holder = gtk_vbox_new(FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(p.holder), 6);
    g_signal_connect(p.holder, "size-allocate", G_CALLBACK(grid_allocated_cb), &p);
    row_add(card, p.holder);
    lengths_build(&p);
    pick_cb(p.active, &p); /* the first one not solved yet */
    gtk_dialog_add_button(GTK_DIALOG(d), tr("Cancel"), GTK_RESPONSE_CANCEL);
    GtkWidget *start = gtk_dialog_add_button(GTK_DIALOG(d), tr("Start"), GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
    gtk_widget_grab_focus(start);
    gtk_widget_show_all(d);
    int r = gtk_dialog_run(GTK_DIALOG(d));
    int variant = p.variant, plies = p.plies[p.active], number = p.number;
    g_signal_handlers_disconnect_by_func(p.holder, grid_allocated_cb, &p);
    if (p.idle)
        g_source_remove(p.idle);
    g_ptr_array_free(p.chips, TRUE);
    for (int i = 0; i < p.n; i++)
        g_free(p.labels[i]);
    gtk_widget_destroy(d);
    if (r != GTK_RESPONSE_OK)
        return;
    g_key_file_set_integer(app.settings, "Settings", "ProblemPlies", plies);
    g_key_file_set_string(app.settings, "Settings", "ProblemVariant", variant_name(variant));
    save_settings();
    GArray *l = problem_list();
    int i = problems_nth(l, variant, plies, number);
    if (i >= 0)
        start_problem(variant, plies, i, g_array_index(l, Problem, i).position);
}
