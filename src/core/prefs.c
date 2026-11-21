/* Preferences as pages of the drawer (mockups/history/2026-11-09-3-preferences): a list of
 * sections with what is chosen in each; a section opens over it. Every change counts at once;
 * × closes them all. */

#include "app.h"
#include "online.h"
#include "release.h"
#include "texts.h"

static void free_data(gpointer data, GClosure *c)
{
    (void) c;
    g_free(data);
}

/* ---- piece samples ---- */

/* Cairo stores premultiplied native-endian ARGB; GdkPixbuf wants straight RGBA. */
static GdkPixbuf *surface_pixbuf(cairo_surface_t *surf)
{
    cairo_surface_flush(surf);
    int w = cairo_image_surface_get_width(surf), h = cairo_image_surface_get_height(surf);
    GdkPixbuf *pb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, w, h);
    const unsigned char *src = cairo_image_surface_get_data(surf);
    int sstride = cairo_image_surface_get_stride(surf), dstride = gdk_pixbuf_get_rowstride(pb);
    unsigned char *dst = gdk_pixbuf_get_pixels(pb);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t px = *(const uint32_t *) (src + y * sstride + x * 4);
            unsigned a = px >> 24;
            unsigned char *d = dst + y * dstride + x * 4;
            d[0] = a ? ((px >> 16 & 255) * 255 + a / 2) / a : 0;
            d[1] = a ? ((px >> 8 & 255) * 255 + a / 2) / a : 0;
            d[2] = a ? ((px & 255) * 255 + a / 2) / a : 0;
            d[3] = a;
        }
    return pb;
}

/* Pieces side by side on a transparent background, as on a color or a black and white screen. */
static GdkPixbuf *pieces_pixbuf_as(PieceStyle *s, const int *types, int n, gboolean upside, int cell, gboolean color)
{
    int w = n * cell, h = (int) (cell * 1.1);
    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t *cr = cairo_create(surf);
    const PieceTheme *theme = color ? &THEME_COLOR : &THEME_MONO;
    for (int i = 0; i < n; i++) {
        if (!types[i])
            continue;
        if (style_no_shape(s)) {
            /* Text pieces have no koma of their own: a board square behind them, so they read
             * on any menu or button color. */
            cairo_rectangle(cr, i * cell + 1.5, 1.5, cell - 3, h - 3);
            cairo_set_source_rgb(cr, color ? 0.914 : 1, color ? 0.784 : 1, color ? 0.525 : 1);
            cairo_fill_preserve(cr);
            cairo_set_source_rgb(cr, 0, 0, 0);
            cairo_set_line_width(cr, 1);
            cairo_stroke(cr);
        }
        style_draw(s, theme, cr, types[i], upside, i * cell + 1, 0, cell - 2, h);
    }
    cairo_destroy(cr);
    GdkPixbuf *pb = surface_pixbuf(surf);
    cairo_surface_destroy(surf);
    return pb;
}

GdkPixbuf *pieces_pixbuf(PieceStyle *s, const int *types, int n, gboolean upside, int cell)
{
    return pieces_pixbuf_as(s, types, n, upside, cell, color_screen());
}

/* One of the sets offered for `variant`, as it draws there (NULL: not offered, or nothing for it). */
static PieceStyle *set_load(const char *file, int variant)
{
    gboolean listed = FALSE;
    for (guint i = 0; i < app.style_files->len && !listed; i++)
        listed = !strcmp(file, g_ptr_array_index(app.style_files, i));
    char *path = listed ? style_path(file) : NULL;
    PieceStyle *s = path ? style_load(path, app.fonts) : NULL;
    g_free(path);
    if (s) {
        style_set_shape(s, piece_shape(variant));
        style_set_colored(s, colored_pieces(variant));
    }
    if (s && !style_serves(s, variant)) {
        style_free(s);
        s = NULL;
    }
    return s;
}

/* A set's sample: a few of its pieces (the animals for a set of their own). */
static GdkPixbuf *set_picture(PieceStyle *s, int variant)
{
    gboolean own = style_variant(s) != NULL;
    int n = own ? PREVIEW_ANIMALS_COUNT : PREVIEW_COUNT;
    const int *kinds = preview_kinds(variant, &n);
    if (!kinds)
        kinds = own ? PREVIEW_ANIMALS : PREVIEW_PIECES;
    return pieces_pixbuf_as(s, kinds, n, FALSE, app.kindle ? 64 : (int) (44 * ui_scale()), color_screen());
}

/* The piece sets offered for `variant` (-1: all variants): their files, names and samples. */
typedef struct {
    GPtrArray *files, *names, *pictures;
} SetList;

static SetList set_list(int variant)
{
    SetList l = {g_ptr_array_new_with_free_func(g_free), g_ptr_array_new_with_free_func(g_free),
                 g_ptr_array_new_with_free_func(g_object_unref)};
    for (guint i = 0; i < app.style_files->len; i++) {
        const char *f = g_ptr_array_index(app.style_files, i);
        PieceStyle *s = set_load(f, variant);
        if (!s)
            continue;
        g_ptr_array_add(l.files, g_strdup(f));
        g_ptr_array_add(l.names, g_strdup(style_name(s, i18n_language())));
        g_ptr_array_add(l.pictures, set_picture(s, variant));
        style_free(s);
    }
    return l;
}

static void set_list_free(SetList *l)
{
    g_ptr_array_unref(l->files);
    g_ptr_array_unref(l->names);
    g_ptr_array_unref(l->pictures);
}

/* ---- applying ---- */

gboolean notation_ja(void)
{
    int n = setting_int("Notation", 0);
    return n == 0 ? i18n_japanese() : n == 1;
}

/* The board's switches, which redraw only when they change. */
static void apply_view(void)
{
    view_set_japanese(app.view, i18n_japanese());
    view_set_notation_ja(app.view, notation_ja());
    view_set_coords(app.view, setting_int("ShowCoords", 1));
    view_set_show_moves(app.view, show_moves());
    update_move_label();
    view_set_physical(app.view, lesson_playing() ? 2 : illegal_rule()); /* a lesson explains a wrong move */
    view_set_other_side(app.view, setting_int("ShowUnpromoted", 1));
#ifdef __ANDROID__
    android_notch(setting_int("UseNotch", 0));
#endif
    view_set_auto_promote(app.view, setting_int("AutoPromote", 0) ? 1 + setting_int("AutoPromoteAll", 0) : 0);
}

/* Applies the saved preferences to the board view. */
void apply_settings(void)
{
    apply_view();
    controls_load();
    apply_style();
    apply_text_size();
}

typedef struct SetRow SetRow;
static SetRow *set_row; /* the piece set's row on the page shown, its sample redrawn on changes */
static void set_row_show(SetRow *r);

/* Every change is seen at once, on the Kindle too: the game stays in sight beside Preferences. */
/* Rows that do nothing for the set in use, locked: "Colored pieces" on a color screen only and not
 * with "Chess mode" (each side's color takes over), and both not for the animals without their
 * tiles (they keep their own colors). */
static GtkWidget *colored_row, *chess_row;
static int locks_variant;
static void locks_update(void);

/* What a change redoes: only what its key affects, so a switch does not reload the piece set and
 * redraw the board (a full e-ink refresh on the Kindle). */
enum { FX_VIEW = 1, FX_PIECES = 2, FX_TEXT = 4, FX_SCREEN = 8, FX_CONTROLS = 16, FX_LANGUAGE = 32, FX_ALL = 63 };

static void changed_fx(int fx)
{
    save_settings();
    if (fx & FX_LANGUAGE)
        apply_language();
    if (fx & FX_SCREEN)
        apply_orientation(FALSE);
    if (fx & (FX_VIEW | FX_LANGUAGE))
        apply_view(); /* the move label too */
    if (fx & FX_CONTROLS)
        controls_load();
    if (fx & (FX_PIECES | FX_LANGUAGE)) {
        apply_style();
        if (set_row)
            set_row_show(set_row);
        locks_update();
    }
    if (fx & FX_TEXT)
        apply_text_size();
    if (fx & FX_LANGUAGE)
        relabel();
}

static void changed(void) { changed_fx(FX_ALL); }

/* A key's effects, a variant's own ("Pieces.Minishogi") as its name's; one not listed redoes
 * everything. Settings read where they are used change nothing on screen: only saved. */
static int key_effects(const char *key)
{
    static const struct {
        const char *key;
        int fx;
    } KEYS[] = {
        {"Vibration", 0}, {"FlashDrawers", 0}, {"PageTurn", 0}, {"FlashEveryMove", 0}, {"ClockRefresh", 0}, {"Port", 0}, {"UPnP", 0},
        {"Volume", 0}, {"AnalysisTime", 0}, {"AnalysisThreads", 0}, {"AnalysisMemory", 0}, {"Updates", 0},
        {"Engine", 0}, {"EnginePath", 0}, {"SoundSet", 0},
        {"ShowMoves", FX_VIEW}, {"IllegalLoses", FX_VIEW}, {"AutoPromote", FX_VIEW}, {"AutoPromoteAll", FX_VIEW},
        {"ShowUnpromoted", FX_VIEW}, {"ShowCoords", FX_VIEW}, {"UseNotch", FX_VIEW}, {"Notation", FX_VIEW},
        {"Pieces", FX_PIECES}, {"PieceShape", FX_PIECES}, {"ColoredPieces", FX_PIECES}, {"MoveMarks", FX_PIECES},
        {"ChessColors", FX_PIECES}, {"Color", FX_PIECES}, {"Theme", FX_PIECES}, {"PaletteName", FX_PIECES},
        {"WoodBoard", FX_PIECES}, {"Colorblind", FX_PIECES}, {"TextSize", FX_TEXT}, {"Landscape", FX_SCREEN},
    };
    size_t n = strcspn(key, ".");
    for (size_t i = 0; i < G_N_ELEMENTS(KEYS); i++)
        if (strlen(KEYS[i].key) == n && !strncmp(key, KEYS[i].key, n))
            return KEYS[i].fx;
    return FX_ALL;
}

static void changed_key(const char *key) { changed_fx(key_effects(key)); }

static void set_int(const char *key, int value)
{
    g_key_file_set_integer(app.settings, "Settings", key, value);
    changed_key(key);
}

/* ---- pages ---- */

/* The section of Preferences being shown: its page takes LB/RB to the sections beside it. */
static const char *section_title;

gboolean prefs_section_page(const char *title) { return section_title && !strcmp(title, section_title); }

static GtkWidget *page_new(const char *title, GtkWidget **box)
{
    GtkWidget *d = dialog_new(title);
    drawer_set_back(d);
    drawer_set_beside(d); /* the board in sight as its looks change */
    g_object_set_data(G_OBJECT(d), "title", (gpointer) title); /* tr()'s, which lasts */
    g_object_set_data(G_OBJECT(d), "prefs-page", GINT_TO_POINTER(TRUE)); /* Select closes Preferences from here too */
    if (prefs_section_page(title))
        g_object_set_data(G_OBJECT(d), "page-step", GINT_TO_POINTER(TRUE));
    *box = gtk_vbox_new(FALSE, 2);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), *box, FALSE, FALSE, 0);
    return d;
}

/* Shows a page until it is answered, leaving it for the caller to read and destroy. */
static int page_run_keep(GtkWidget *d)
{
    gtk_widget_show_all(d);
    return gtk_dialog_run(GTK_DIALOG(d));
}

#define RESPONSE_SECTION_RESET (-30)
#define RESPONSE_SECTION_AGAIN (-31) /* the same section built again (its rows' width changed) */

static void section_reset_cb(GtkButton *b, gpointer d)
{
    (void) b;
    if (confirm_titled(g_object_get_data(G_OBJECT(d), "title"), tr("Reset this section's settings to the defaults?"),
                       tr("Reset")))
        gtk_dialog_response(GTK_DIALOG(d), RESPONSE_SECTION_RESET);
}

/* "Reset this section" at the foot of a section's page. */
static void add_section_reset(GtkWidget *box, GtkWidget *d)
{
    GtkWidget *b = row_add(rows_card(box), row_section("⭯", tr("Reset this section…"), ""));
    g_signal_connect(b, "clicked", G_CALLBACK(section_reset_cb), d);
}

static int page_run(GtkWidget *d)
{
    gtk_widget_show_all(d);
    int r = gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
    return r;
}

static void switch_cb(GtkToggleButton *b, gpointer key)
{
    set_int(key, gtk_toggle_button_get_active(b));
}

/* A switch for `key`; `sub`, when given, is offered only while it is on. */
static GtkWidget *add_switch(GtkWidget *card, const char *label, const char *key, int value)
{
    GtkWidget *s = row_add(card, row_switch(tr(label), value));
    g_signal_connect(s, "toggled", G_CALLBACK(switch_cb), (gpointer) key);
    return s;
}

static void sensitive_cb(GtkToggleButton *b, gpointer sub)
{
    gtk_widget_set_sensitive(sub, gtk_toggle_button_get_active(b));
}

/* A choice from a list of (translated) items that opens over the page. */
typedef struct {
    const char *title, *key;
    const char *const *items;
    int n;
    GtkWidget *value;
} Choice;

static void choice_picked(int i, gpointer data)
{
    Choice *c = data;
    row_set_value(c->value, tr(c->items[i]));
    g_key_file_set_integer(app.settings, "Settings", c->key, i);
    changed_key(c->key);
}

static void choice_cb(GtkButton *b, gpointer data)
{
    (void) b;
    Choice *c = data;
    const char *t[16];
    for (int i = 0; i < c->n && i < 16; i++)
        t[i] = tr(c->items[i]);
    rows_pick(tr(c->title), t, NULL, c->n, CLAMP(setting_int(c->key, 0), 0, c->n - 1), choice_picked, c);
}

static GtkWidget *add_choice(GtkWidget *card, const char *label, const char *const *items, int n, const char *key)
{
    Choice *c = g_new0(Choice, 1);
    *c = (Choice) {label, key, items, n, NULL};
    GtkWidget *b = row_add(card, row_choice(tr(label), tr(items[CLAMP(setting_int(key, 0), 0, n - 1)]), &c->value));
    g_signal_connect_data(b, "clicked", G_CALLBACK(choice_cb), c, free_data, 0);
    return b;
}

/* ---- Game ---- */

static void engine_cb(int i, gpointer other)
{
    set_int("Engine", i);
    gtk_widget_set_visible(other, i == 1);
}

static void engine_file_cb(GtkFileChooserButton *b, gpointer data)
{
    (void) data;
    char *file = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(b));
    g_key_file_set_string(app.settings, "Settings", "EnginePath", file ? file : "");
    g_free(file);
    changed_key("EnginePath");
}

static void data_folder_cb(GtkFileChooserButton *b, gpointer data)
{
    (void) data;
    char *dir = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(b));
    data_folder_choose(dir);
    g_free(dir);
}

static void data_folder_default_cb(GtkButton *b, gpointer chooser)
{
    (void) b;
    data_folder_choose(NULL);
    char *dir = g_build_filename(g_get_user_data_dir(), "denboku", NULL);
    gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(chooser), dir);
    g_free(dir);
}

static void custom_folder_cb(GtkFileChooserButton *b, gpointer data)
{
    (void) data;
    char *dir = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(b));
    g_key_file_set_string(app.settings, "Settings", "CustomFolder", dir ? dir : "");
    custom_folder_set(dir);
    load_style_list();
    g_free(dir);
    changed_fx(FX_PIECES); /* its piece sets */
}

static void custom_folder_default_cb(GtkButton *b, gpointer chooser)
{
    (void) b;
    g_key_file_remove_key(app.settings, "Settings", "CustomFolder", NULL);
    custom_folder_set(NULL);
    load_style_list();
    gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(chooser), app.custom);
    changed_fx(FX_PIECES);
}

/* A file chooser that asks for little width, as text_entry() does: the drawer stays as wide as the others. */
static GtkWidget *narrow_chooser(GtkWidget *b)
{
    gtk_file_chooser_button_set_width_chars(GTK_FILE_CHOOSER_BUTTON(b), 6);
    return b;
}

/* Where your own piece and sound sets are (pieces/ and sounds/ in it), at once. */
G_GNUC_UNUSED static void custom_folder_card(GtkWidget *box)
{
    GtkWidget *card = rows_card(box), *col = gtk_vbox_new(FALSE, 4);
    GtkWidget *chooser = narrow_chooser(gtk_file_chooser_button_new(tr("Choose the folder of your pieces and sounds"), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER));
    gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(chooser), app.custom);
    g_signal_connect(chooser, "file-set", G_CALLBACK(custom_folder_cb), NULL);
    GtkWidget *use_default = gtk_button_new_with_label(tr("Default folder"));
    g_signal_connect(use_default, "clicked", G_CALLBACK(custom_folder_default_cb), chooser);
    GtkWidget *note = row_text(tr("Your pieces and sounds: their folder, with pieces/ and sounds/ in it (☰ → Help → How to Play)."),
                               TRUE, FALSE, TRUE);
    gtk_label_set_line_wrap(GTK_LABEL(note), TRUE);
    gtk_widget_set_size_request(note, rows_width() - 36, -1); /* inside the column's 8 px border */
    gtk_container_set_border_width(GTK_CONTAINER(col), 8);
    gtk_box_pack_start(GTK_BOX(col), note, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(col), chooser, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(col), use_default, FALSE, FALSE, 0);
    row_add(card, col);
}

/* Where settings, saved games and your own piece and sound sets live: a folder of the player's
 * choice, used from the next start. */
G_GNUC_UNUSED static void data_folder_card(GtkWidget *box)
{
    GtkWidget *card = rows_card(box), *col = gtk_vbox_new(FALSE, 4);
    GtkWidget *chooser = narrow_chooser(gtk_file_chooser_button_new(tr("Choose the data folder"), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER));
    gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(chooser), app.home);
    g_signal_connect(chooser, "file-set", G_CALLBACK(data_folder_cb), NULL);
    GtkWidget *use_default = gtk_button_new_with_label(tr("Default folder"));
    g_signal_connect(use_default, "clicked", G_CALLBACK(data_folder_default_cb), chooser);
    GtkWidget *note = row_text(tr("Data folder: settings, saved games and your own piece and sound sets. "
                                  "A new folder takes effect after a restart, and the current files are copied to it."),
                               TRUE, FALSE, TRUE);
    gtk_label_set_line_wrap(GTK_LABEL(note), TRUE);
    gtk_widget_set_size_request(note, rows_width() - 36, -1); /* inside the column's 8 px border */
    gtk_container_set_border_width(GTK_CONTAINER(col), 8);
    gtk_box_pack_start(GTK_BOX(col), note, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(col), chooser, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(col), use_default, FALSE, FALSE, 0);
    row_add(card, col);
}

static int game_page(void)
{
    GtkWidget *box, *d = page_new(tr("Game"), &box);
    GtkWidget *card = rows_card(box);
    GtkWidget *legal = add_switch(card, "Show legal moves", "ShowMoves", setting_int("ShowMoves", 1));
    GtkWidget *illegal = add_choice(card, "Illegal moves", ILLEGAL_LABELS, 3, "IllegalLoses");
    if (app.net) { /* both players keep the host's rule until the network game ends */
        gtk_widget_set_sensitive(legal, FALSE);
        gtk_widget_set_sensitive(illegal, FALSE);
        GtkWidget *note = row_add(card, row_text(tr("In a network game the host sets these rules."), TRUE, FALSE, TRUE));
        gtk_misc_set_padding(GTK_MISC(note), 8, 4);
    }
    card = rows_card(box);
    GtkWidget *promote = add_switch(card, "Promote automatically (pawn, bishop, rook)", "AutoPromote", setting_int("AutoPromote", 0));
    GtkWidget *all = add_switch(card, "Also lance, knight and silver", "AutoPromoteAll", setting_int("AutoPromoteAll", 0));
    gtk_widget_set_sensitive(all, setting_int("AutoPromote", 0));
    g_signal_connect(promote, "toggled", G_CALLBACK(sensitive_cb), all);
    if (OTHER_ENGINE) {
        /* Another USI program (YaneuraOu…), for the standard game; no other engine fits the Kindle. */
        card = rows_card(box);
        GtkWidget *other = gtk_vbox_new(FALSE, 4), *file = narrow_chooser(gtk_file_chooser_button_new(tr("Choose the engine"), GTK_FILE_CHOOSER_ACTION_OPEN));
        char *path = setting_string("EnginePath", "");
        if (*path)
            gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(file), path);
        g_free(path);
        g_signal_connect(file, "file-set", G_CALLBACK(engine_file_cb), NULL);
        gtk_container_set_border_width(GTK_CONTAINER(other), 8);
        gtk_box_pack_start(GTK_BOX(other), file, FALSE, FALSE, 0);
        GtkWidget *note = row_text(tr("It plays and analyzes standard games; variants keep the built-in engine. "
                                      "Levels work only if it has a Skill Level option."), TRUE, FALSE, TRUE);
        gtk_label_set_line_wrap(GTK_LABEL(note), TRUE);
        gtk_widget_set_size_request(note, rows_width() - 20, -1);
        gtk_box_pack_start(GTK_BOX(other), note, FALSE, FALSE, 0);
        const char *t[] = {tr("Built-in"), tr("Other program")};
        int engine = CLAMP(setting_int("Engine", 0), 0, 1);
        row_add(card, row_chips(tr("Engine"), t, 2, engine, rows_width(), engine_cb, other));
        row_add(card, other);
        gtk_widget_show_all(other);
        gtk_widget_set_no_show_all(other, TRUE);
        gtk_widget_set_visible(other, engine == 1);
    }
#ifndef __ANDROID__ /* Android's folder is the app's own, and the chooser could only look inside it */
    if (!app.kindle) { /* its folder is the game's own */
        data_folder_card(box);
        custom_folder_card(box);
    }
#endif
    add_section_reset(box, d);
    return page_run(d);
}

/* ---- Pieces ---- */

/* The piece set of all variants (variant -1) or of one, which may be "Same as all variants". */
static char *set_of(int variant)
{
    if (variant < 0)
        return pieces_file(setting_string("Pieces", DEFAULT_PIECES));
    char *key = variant_key("Pieces", variant);
    char *file = pieces_file(g_key_file_get_string(app.settings, "Settings", key, NULL));
    g_free(key);
    if (!file)
        file = g_strdup(variant_pieces_default(variant));
    return file;
}

static void set_variant_key(int variant, const char *name, const char *string, int value, gboolean is_default)
{
    char *key = variant_key(name, variant);
    if (is_default)
        g_key_file_remove_key(app.settings, "Settings", key, NULL);
    else if (string)
        g_key_file_set_string(app.settings, "Settings", key, string);
    else
        g_key_file_set_integer(app.settings, "Settings", key, value);
    g_free(key);
    changed_key(name);
}

struct SetRow {
    int variant;
    GtkWidget *value, *image;
    GPtrArray *files; /* while its list is open */
};

static void set_row_free(gpointer data, GClosure *c)
{
    if (set_row == data)
        set_row = NULL;
    free_data(data, c);
}

/* The set chosen and its sample: that set loaded, not the whole list. */
static void set_row_show(SetRow *r)
{
    char *file = set_of(r->variant);
    if (r->variant >= 0 && (!file || !*file)) {
        /* The set of all variants, or (one that has nothing for this variant) the set it gives way to. */
        char *all = set_of(-1);
        PieceStyle *s = set_load(all, r->variant);
        g_free(all);
        g_free(file);
        if (s) {
            style_free(s);
            row_set_value(r->value, tr("Same as all variants"));
            return;
        }
        file = g_strdup(variant_pieces_default(r->variant) ? variant_pieces_default(r->variant) : DEFAULT_PIECES);
    }
    PieceStyle *s = file ? set_load(file, r->variant) : NULL;
    g_free(file);
    if (s) {
        GdkPixbuf *pb = set_picture(s, r->variant);
        gtk_image_set_from_pixbuf(GTK_IMAGE(r->image), pb);
        g_object_unref(pb);
    } else
        gtk_image_clear(GTK_IMAGE(r->image));
    row_set_value(r->value, s ? style_name(s, i18n_language()) : "");
    style_free(s);
}

static void set_picked(int i, gpointer data)
{
    SetRow *r = data;
    if (r->variant < 0) {
        g_key_file_set_string(app.settings, "Settings", "Pieces", g_ptr_array_index(r->files, i));
        changed_key("Pieces");
    } else { /* set_variant_key() applies it */
        const char *f = i ? g_ptr_array_index(r->files, i - 1) : "";
        set_variant_key(r->variant, "Pieces", f, 0, !g_strcmp0(f, variant_pieces_default(r->variant)));
    }
}

/* The sets in a list with their pieces; a variant's list starts with "Same as all variants". */
static void set_pick_cb(GtkButton *b, gpointer data)
{
    (void) b;
    SetRow *r = data;
    SetList l = set_list(r->variant);
    int same = r->variant >= 0, n = (int) l.files->len + same, active = 0;
    const char **names = g_new0(const char *, n);
    GdkPixbuf **pics = g_new0(GdkPixbuf *, n);
    if (same)
        names[0] = tr("Same as all variants");
    char *file = set_of(r->variant);
    for (guint i = 0; i < l.files->len; i++) {
        names[i + same] = g_ptr_array_index(l.names, i);
        pics[i + same] = g_ptr_array_index(l.pictures, i);
        if (file && !strcmp(file, g_ptr_array_index(l.files, i)))
            active = (int) i + same;
    }
    g_free(file);
    r->files = l.files;
    rows_pick(tr("Pieces"), names, pics, n, active, set_picked, r);
    r->files = NULL;
    g_free(names);
    g_free(pics);
    set_list_free(&l);
}

static void add_set_row(GtkWidget *card, int variant)
{
    SetRow *r = g_new0(SetRow, 1);
    r->variant = variant;
    GtkWidget *b = gtk_button_new(), *row = gtk_vbox_new(FALSE, 4), *top = gtk_hbox_new(FALSE, 8);
    gtk_widget_set_name(b, "denboku-flatrow");
    r->value = row_text("", FALSE, FALSE, FALSE);
    gtk_box_pack_start(GTK_BOX(top), r->value, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(top), row_text("›", TRUE, TRUE, FALSE), FALSE, FALSE, 0);
    r->image = gtk_image_new();
    gtk_misc_set_alignment(GTK_MISC(r->image), 0, 0.5);
    gtk_box_pack_start(GTK_BOX(row), top, TRUE, TRUE, 0); /* in the middle of a row a finger tall (style.c) */
    gtk_box_pack_start(GTK_BOX(row), r->image, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(b), row);
    g_signal_connect_data(b, "clicked", G_CALLBACK(set_pick_cb), r, set_row_free, 0);
    row_add(card, b);
    set_row = r;
    set_row_show(r);
}

/* A variant's switches that may be "Same as all variants": their keys and defaults. */
static const char *const TRI_KEYS[] = {"MoveMarks", "ColoredPieces", "PieceShape", "ChessColors"};

static int tri_default(int which, int variant)
{
    return which == 0 ? variant_marks_default(variant) : which == 1 ? variant_colored_default(variant)
         : which == 2 ? variant_shape_default(variant) : -1;
}

/* "Same as all variants", on or off: the variant's setting, -1 for the same as all. */
static void variant_tri_cb(int i, gpointer data)
{
    int variant = GPOINTER_TO_INT(data) >> 8, which = GPOINTER_TO_INT(data) & 255;
    int value = i == 0 ? -1 : i == 1;
    set_variant_key(variant, TRI_KEYS[which], NULL, value, value == tri_default(which, variant));
}

static GtkWidget *add_tri(GtkWidget *card, int variant, int which, const char *title, const char *on, const char *off)
{
    char *key = variant_key(TRI_KEYS[which], variant);
    int value = setting_int(key, tri_default(which, variant));
    g_free(key);
    const char *c[] = {tr("Same as all variants"), tr(on), tr(off)};
    return row_add(card, row_chips(tr(title), c, 3, value < 0 ? 0 : 2 - value, rows_width(), variant_tri_cb,
                                   GINT_TO_POINTER(variant << 8 | which)));
}

typedef struct {
    int variant;
    GPtrArray *sets;
    const char **names;
    GtkWidget *value;
} SoundPick;

static void variant_sound_picked(int i, gpointer data)
{
    SoundPick *p = data;
    const char *set = i ? g_ptr_array_index(p->sets, i - 1) : "";
    set_variant_key(p->variant, "SoundSet", set, 0, !g_strcmp0(set, variant_sound_default(p->variant)));
    row_set_value(p->value, p->names[i]);
    sound_preview(*set ? set : NULL);
}

/* A variant's sound set: "Same as all variants" or one of the sets. */
static void variant_sound_cb(GtkButton *b, gpointer data)
{
    int variant = GPOINTER_TO_INT(data);
    GPtrArray *sets = sound_sets();
    char *own = variant_sound_set(variant);
    int n = (int) sets->len + 1, active = 0;
    const char **names = g_new0(const char *, n);
    names[0] = tr("Same as all variants");
    for (guint i = 0; i < sets->len; i++) {
        names[i + 1] = sound_set_name(g_ptr_array_index(sets, i));
        if (own && !strcmp(own, g_ptr_array_index(sets, i)))
            active = (int) i + 1;
    }
    SoundPick pick = {variant, sets, names, g_object_get_data(G_OBJECT(b), "value")};
    rows_pick(tr("Sound"), names, NULL, n, active, variant_sound_picked, &pick);
    g_free(names);
    g_free(own);
    g_ptr_array_unref(sets);
}

static gboolean bare_animals(int variant)
{
    char *file = set_of(variant), *path = style_path(file);
    PieceStyle *st = style_load(path, app.fonts);
    gboolean bare = st && style_square(st) && !piece_shape(variant);
    style_free(st);
    g_free(path);
    g_free(file);
    return bare;
}

static void locks_update(void)
{
    gboolean bare = (colored_row || chess_row) && bare_animals(locks_variant);
    if (colored_row)
        gtk_widget_set_sensitive(colored_row, color_screen() && !bare && !chess_mode(locks_variant));
    if (chess_row)
        gtk_widget_set_sensitive(chess_row, !bare);
}

static void locks_watch(GtkWidget *colored, GtkWidget *chess, int variant)
{
    colored_row = colored, chess_row = chess, locks_variant = variant;
    if (colored)
        g_object_add_weak_pointer(G_OBJECT(colored), (gpointer *) &colored_row);
    if (chess)
        g_object_add_weak_pointer(G_OBJECT(chess), (gpointer *) &chess_row);
    locks_update();
}

static int variant_page(int variant)
{
    GtkWidget *box, *d = page_new(tr(VARIANT_LABELS[variant]), &box);
    g_object_set_data(G_OBJECT(d), "page-step", GINT_TO_POINTER(TRUE)); /* LB/RB: the variant beside it */
    rows_group(box, tr("Pieces"));
    add_set_row(rows_card(box), variant);
    GtkWidget *card = rows_card(box);
    add_tri(card, variant, 2, "Draw the piece shape", "With the shape", "Without the shape");
    add_tri(card, variant, 0, "How pieces move", "Show", "Do not show");
    GtkWidget *colored = add_tri(card, variant, 1, "Colored pieces", "Colored", "Not colored");
    locks_watch(colored, add_tri(card, variant, 3, "Chess mode", "On", "Off"), variant);
    card = rows_card(box);
    char *own = variant_sound_set(variant);
    GtkWidget *value, *b = row_add(card, row_choice(tr("Sound"), own ? sound_set_name(own) : tr("Same as all variants"), &value));
    g_free(own);
    g_object_set_data(G_OBJECT(b), "value", value);
    g_signal_connect(b, "clicked", G_CALLBACK(variant_sound_cb), GINT_TO_POINTER(variant));
    board_preview(variant);
    int r = page_run(d);
    board_preview(-1);
    return r;
}

static void variant_row_cb(GtkButton *b, gpointer v)
{
    gtk_dialog_response(GTK_DIALOG(g_object_get_data(G_OBJECT(b), "dialog")), 100 + GPOINTER_TO_INT(v));
}

/* Variants: a row each, opening its own pieces and sound over the list. */
static int variants_page(void)
{
    for (int left = -1;;) { /* the variant just left: the cursor goes back to its row */
        GtkWidget *box, *d = page_new(tr("Variants"), &box), *back_to = NULL;
        GtkWidget *card = rows_card(box);
        for (int v = 0; v < V_COUNT; v++) {
            GtkWidget *b = row_add(card, row_choice(tr(VARIANT_LABELS[v]), "", NULL));
            g_object_set_data(G_OBJECT(b), "dialog", d);
            g_signal_connect(b, "clicked", G_CALLBACK(variant_row_cb), GINT_TO_POINTER(v));
            if (v == left)
                back_to = b;
        }
        add_section_reset(box, d);
        gtk_widget_show_all(d);
        if (back_to)
            gtk_widget_grab_focus(back_to);
        int r = page_run(d);
        if (r < 100)
            return r;
        left = r - 100;
        while ((r = variant_page(left)) == RESPONSE_PAGE_NEXT || r == RESPONSE_PAGE_PREV)
            left = (left + (r == RESPONSE_PAGE_NEXT ? 1 : V_COUNT - 1)) % V_COUNT;
        if (r == GTK_RESPONSE_DELETE_EVENT)
            return GTK_RESPONSE_DELETE_EVENT;
    }
}

static void notation_cb(int i, gpointer data)
{
    (void) data;
    set_int("Notation", i);
}

static int pieces_page(void)
{
    GtkWidget *box, *d = page_new(tr("Pieces"), &box);
    rows_group(box, tr("All variants"));
    GtkWidget *card = rows_card(box);
    add_set_row(card, -1);
    add_switch(card, "Draw the piece shape", "PieceShape", piece_shape(-1));
    GtkWidget *colored = add_switch(card, "Colored pieces", "ColoredPieces", colored_pieces(-1));
    add_switch(card, "Show how pieces move", "MoveMarks", setting_int("MoveMarks", 0));
    add_switch(card, "Show the unpromoted side of a picked-up piece", "ShowUnpromoted", setting_int("ShowUnpromoted", 1));
    locks_watch(colored, add_switch(card, "Chess mode", "ChessColors", setting_int("ChessColors", 0)), -1);
    const char *n[] = {tr("Same as language"), NOTATION_SAMPLES[0], NOTATION_SAMPLES[1]};
    row_add(card, row_chips(tr("Notation"), n, 3, setting_int("Notation", 0), rows_width(), notation_cb, NULL));
    add_section_reset(box, d);
    return page_run(d);
}

/* ---- Appearance ---- */

typedef struct {
    GtkWidget *mono_rows, *color_rows, *wood, *colorblind, *size_value;
} Looks;

static void looks_rows(Looks *l)
{
    gboolean color = color_screen();
    gtk_widget_set_visible(l->mono_rows, !color);
    gtk_widget_set_visible(l->color_rows, color);
    gtk_widget_set_visible(l->wood, color && palette_index() != wood_palette());
    gtk_widget_set_visible(l->colorblind, color);
}

static void colors_cb(int i, gpointer data)
{
    set_int("Color", i);
    looks_rows(data);
}

static void theme_cb(int i, gpointer data)
{
    (void) data;
    set_int("Theme", i);
}

/* A palette as a swatch: its surround and its board, the chosen one ringed. */
static gboolean swatch_expose_cb(GtkWidget *w, GdkEventExpose *e, gpointer data)
{
    (void) e;
    int i = GPOINTER_TO_INT(data);
    GtkAllocation a;
    gtk_widget_get_allocation(w, &a);
    cairo_t *cr = gdk_cairo_create(gtk_widget_get_window(w));
    const BoardColors *c = &PALETTES[i].c;
    double x = a.x + 3, y = a.y + 3, ww = a.width - 6, h = a.height - 6, r = h * 0.25;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + ww - r, y + r, r, -G_PI / 2, 0);
    cairo_arc(cr, x + ww - r, y + h - r, r, 0, G_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, G_PI / 2, G_PI);
    cairo_arc(cr, x + r, y + r, r, G_PI, 3 * G_PI / 2);
    cairo_close_path(cr);
    cairo_set_source_rgb(cr, c->bg[0], c->bg[1], c->bg[2]);
    cairo_fill_preserve(cr);
    const BoardColors *now = view_colors(app.view);
    gboolean chosen = i == palette_index();
    cairo_set_source_rgb(cr, chosen ? now->accent[0] : now->fg[0], chosen ? now->accent[1] : now->fg[1], chosen ? now->accent[2] : now->fg[2]);
    cairo_set_line_width(cr, chosen ? MAX(3, h / 12) : 1);
    cairo_stroke(cr);
    cairo_rectangle(cr, x + ww * 0.2, y + h * 0.25, ww * 0.4, h * 0.5);
    cairo_set_source_rgb(cr, c->board[0], c->board[1], c->board[2]);
    cairo_fill(cr);
    /* the accent beside it: the dark ones differ most there */
    cairo_arc(cr, x + ww * 0.76, y + h / 2, h * 0.17, 0, 2 * G_PI);
    cairo_set_source_rgb(cr, c->accent[0], c->accent[1], c->accent[2]);
    cairo_fill(cr);
    cairo_destroy(cr);
    return TRUE;
}

static void swatch_cb(GtkButton *b, gpointer data)
{
    Looks *l = g_object_get_data(G_OBJECT(b), "looks");
    g_key_file_set_string(app.settings, "Settings", "PaletteName", PALETTES[GPOINTER_TO_INT(data)].name);
    changed_key("PaletteName");
    gtk_widget_queue_draw(gtk_widget_get_parent(gtk_widget_get_parent(GTK_WIDGET(b))));
    looks_rows(l);
}

static void text_size_step(GtkButton *b, gpointer data)
{
    Looks *l = data;
    int step = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "step")), now = 2;
    for (int i = 0; i < NTEXT_SIZES; i++)
        if (TEXT_SIZES[i] == setting_int("TextSize", 100))
            now = i;
    now = CLAMP(now + step, 0, NTEXT_SIZES - 1);
    char t[8];
    snprintf(t, sizeof t, "%d%%", TEXT_SIZES[now]);
    gtk_label_set_text(GTK_LABEL(l->size_value), t);
    set_int("TextSize", TEXT_SIZES[now]);
}

static void light_show(GtkWidget *value, int level)
{
    char t[16];
    snprintf(t, sizeof t, "%d%%", level * 100 / KINDLE_LIGHT_MAX);
    gtk_label_set_text(GTK_LABEL(value), t);
}

/* The Kindle's own front light, a step at a time (its setting, not the game's). */
static void light_step(GtkButton *b, gpointer value)
{
    int level = CLAMP(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(value), "level")) +
                          GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "step")) * 2, 0, KINDLE_LIGHT_MAX);
    g_object_set_data(G_OBJECT(value), "level", GINT_TO_POINTER(level));
    light_show(value, level);
    kindle_set_light(level);
}

/* Between portrait and landscape the rows take another width (the window's): the page is built
 * again once the window has turned, which the Kindle does a moment later. */
static GtkWidget *turning_page;
static int turning_ticks;

static gboolean turned_cb(gpointer data)
{
    (void) data;
    if (!turning_page)
        return FALSE;
    int w, h;
    gtk_window_get_size(GTK_WINDOW(app.window), &w, &h);
    if ((w > h) != (setting_int("Landscape", 0) != 0) && ++turning_ticks < 30)
        return TRUE;
    gtk_dialog_response(GTK_DIALOG(turning_page), RESPONSE_SECTION_AGAIN);
    return FALSE;
}

static void screen_cb(int i, gpointer d)
{
    gboolean turned = (setting_int("Landscape", 0) != 0) != (i != 0);
    set_int("Landscape", i);
    if (turned && !turning_page) {
        turning_page = d;
        g_object_add_weak_pointer(G_OBJECT(d), (gpointer *) &turning_page);
        turning_ticks = 0;
        g_timeout_add(200, turned_cb, NULL);
    }
}

static int looks_page(void)
{
    GtkWidget *box, *d = page_new(tr("Appearance"), &box);
    Looks l;
    GtkWidget *card = rows_card(box);
    static const char *colors[] = {"Automatic", "Black and white", "Color"};
    const char *t[] = {tr(colors[0]), tr(colors[1]), tr(colors[2])};
    row_add(card, row_chips(tr("Colors"), t, 3, color_mode(), rows_width(),
                            colors_cb, &l));
    const char *themes[] = {tr("Light"), tr("Dark"), tr("Same as system")};
    l.mono_rows = row_add(card, row_chips(tr("Theme"), themes, 3, theme_setting(), rows_width(), theme_cb, NULL));
    /* The palettes as swatches, four to a row. */
    l.color_rows = gtk_vbox_new(FALSE, 6);
    gtk_container_set_border_width(GTK_CONTAINER(l.color_rows), 8);
    gtk_box_pack_start(GTK_BOX(l.color_rows), row_text(tr("Palette"), TRUE, FALSE, TRUE), FALSE, FALSE, 0);
    GtkWidget *grid = gtk_table_new((NPALETTES + 3) / 4, 4, TRUE);
    gtk_table_set_row_spacings(GTK_TABLE(grid), 6);
    gtk_table_set_col_spacings(GTK_TABLE(grid), 6);
    for (int i = 0; i < NPALETTES; i++) {
        GtkWidget *b = gtk_button_new(), *area = gtk_drawing_area_new();
        gtk_widget_set_name(b, "denboku-flatrow");
        gtk_widget_set_has_window(area, FALSE);
        /* As large as a finger on a phone's dense screen, and on the Kindle's. */
        double k = app.kindle ? 2 : ui_scale();
        gtk_widget_set_size_request(area, (int) (64 * k), (int) (44 * k));
        gtk_widget_set_tooltip_text(b, tr(PALETTES[i].name));
        g_signal_connect(area, "expose-event", G_CALLBACK(swatch_expose_cb), GINT_TO_POINTER(i));
        gtk_container_add(GTK_CONTAINER(b), area);
        g_object_set_data(G_OBJECT(b), "looks", &l);
        g_signal_connect(b, "clicked", G_CALLBACK(swatch_cb), GINT_TO_POINTER(i));
        gtk_table_attach_defaults(GTK_TABLE(grid), b, i % 4, i % 4 + 1, i / 4, i / 4 + 1);
    }
    gtk_box_pack_start(GTK_BOX(l.color_rows), grid, FALSE, FALSE, 0);
    row_add(card, l.color_rows);
    l.wood = add_switch(card, "Wooden board", "WoodBoard", setting_int("WoodBoard", 1));
    l.colorblind = add_switch(card, "Colors for color blindness", "Colorblind", setting_int("Colorblind", 0));
#ifdef __ANDROID__
    add_switch(card, "Use the space beside the camera", "UseNotch", setting_int("UseNotch", 0));
#endif
    add_switch(card, "Show coordinates", "ShowCoords", setting_int("ShowCoords", 1));
    card = rows_card(box);
    GtkWidget *minus, *plus;
    row_add(card, row_stepper(tr("Text size"), &l.size_value, &minus, &plus));
    char size[8];
    snprintf(size, sizeof size, "%d%%", setting_int("TextSize", 100));
    gtk_label_set_text(GTK_LABEL(l.size_value), size);
    g_object_set_data(G_OBJECT(minus), "step", GINT_TO_POINTER(-1));
    g_object_set_data(G_OBJECT(plus), "step", GINT_TO_POINTER(1));
    g_signal_connect(minus, "clicked", G_CALLBACK(text_size_step), &l);
    g_signal_connect(plus, "clicked", G_CALLBACK(text_size_step), &l);
    if (app.kindle) {
        /* The Kindle's window manager turns to three of the four sides: not portrait upside down. */
        const char *o[] = {tr("Portrait"), tr("Landscape"), tr("Landscape, upside down")};
        row_add(card, row_chips(tr("Screen"), o, 3, CLAMP(setting_int("Landscape", 0), 0, 2), rows_width(), screen_cb, d));
        int level = kindle_light();
        if (level >= 0) {
            GtkWidget *value;
            row_add(card, row_stepper(tr("Front light"), &value, &minus, &plus));
            g_object_set_data(G_OBJECT(value), "level", GINT_TO_POINTER(level));
            light_show(value, level);
            g_object_set_data(G_OBJECT(minus), "step", GINT_TO_POINTER(-1));
            g_object_set_data(G_OBJECT(plus), "step", GINT_TO_POINTER(1));
            g_signal_connect(minus, "clicked", G_CALLBACK(light_step), value);
            g_signal_connect(plus, "clicked", G_CALLBACK(light_step), value);
        }
    }
    if (app.kindle) { /* e-ink only: fewer redraws */
        static const char *refresh[] = {"Every second", "Every 10 seconds", "Only after moves"};
        card = rows_card(box);
        add_choice(card, "Clock updates", refresh, 3, "ClockRefresh");
        add_switch(card, "Refresh the whole screen after each move", "FlashEveryMove", setting_int("FlashEveryMove", 0));
        add_switch(card, "Refresh drawers when they open", "FlashDrawers", setting_int("FlashDrawers", 0));
    }
#ifdef __ANDROID__ /* for e-ink phones and readers too */
    card = rows_card(box);
#else
    if (app.kindle) /* card: the e-ink one above */
#endif
        add_switch(card, "Turn pages in long lists", "PageTurn", setting_int("PageTurn", app.kindle));
    gtk_widget_show_all(d);
    gtk_widget_set_no_show_all(l.mono_rows, TRUE);
    gtk_widget_set_no_show_all(l.color_rows, TRUE);
    gtk_widget_set_no_show_all(l.wood, TRUE);
    looks_rows(&l);
    add_section_reset(box, d);
    return page_run(d);
}

/* ---- Sound, Language, Online, Controls ---- */

static int sound_page(void)
{
    GtkWidget *box, *d = page_new(tr("Sound"), &box);
    GtkWidget *card = rows_card(box), *sounds = sound_chooser();
    gtk_container_set_border_width(GTK_CONTAINER(sounds), 8);
    row_add(card, sounds);
    row_add(card, volume_row(sounds));
    if (!app.kindle) /* no motor, nor controllers */
        add_switch(card, "Vibration", "Vibration", setting_int("Vibration", 1));
    add_section_reset(box, d);
    return page_run(d);
}

static void language_picked(int i, gpointer data)
{
    (void) data;
    g_key_file_set_string(app.settings, "Settings", "Language", i > 0 ? i18n_code((Language) (i - 1)) : "auto");
    changed();
}

/* Automatic, then each language in itself. */
static int language_page(void)
{
    const char *names[LANGUAGE_COUNT + 1];
    names[0] = tr("Automatic");
    for (int l = 0; l < LANGUAGE_COUNT; l++)
        names[l + 1] = LANGUAGES[l].name;
    char *code = setting_string("Language", "auto");
    int now = !strcmp(code, "auto") ? 0 : i18n_of_locale(code) + 1;
    g_free(code);
    int r = rows_pick(tr("Language"), names, NULL, LANGUAGE_COUNT + 1, now, language_picked, NULL);
    return r == RESPONSE_PAGE_NEXT || r == RESPONSE_PAGE_PREV ? r : GTK_RESPONSE_CLOSE;
}

/* The port typed (no − / + arrows: thin and tiny on the Kindle); one out of range is not kept. */
static void port_cb(GtkEditable *e, gpointer data)
{
    (void) data;
    int port = atoi(gtk_entry_get_text(GTK_ENTRY(e)));
    if (port >= 1024 && port <= 65535)
        set_int("Port", port);
}

static void port_digits_cb(GtkEditable *e, char *text, int len, int *pos, gpointer data)
{
    (void) pos, (void) data;
    for (int i = 0; i < len; i++)
        if (!g_ascii_isdigit(text[i])) {
            g_signal_stop_emission_by_name(e, "insert-text");
            return;
        }
}

static int online_page(void)
{
    GtkWidget *box, *d = page_new(tr("Online"), &box);
    GtkWidget *card = rows_card(box), *v = gtk_vbox_new(FALSE, 4);
    gtk_container_set_border_width(GTK_CONTAINER(v), 8);
    GtkWidget *name = text_entry();
    char *own = setting_string("PlayerName", "");
    gtk_entry_set_text(GTK_ENTRY(name), own);
    g_free(own);
    gtk_entry_set_max_length(GTK_ENTRY(name), 40);
    gtk_box_pack_start(GTK_BOX(v), row_text(tr("Your name"), TRUE, FALSE, TRUE), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(v), name, FALSE, FALSE, 0);
    row_add(card, v);
    GtkWidget *p = gtk_hbox_new(FALSE, 8), *port = text_entry();
    gtk_container_set_border_width(GTK_CONTAINER(p), 8);
    char number[8];
    snprintf(number, sizeof number, "%d", net_port());
    gtk_entry_set_text(GTK_ENTRY(port), number);
    gtk_entry_set_max_length(GTK_ENTRY(port), 5);
    gtk_entry_set_width_chars(GTK_ENTRY(port), 6);
    gtk_widget_set_sensitive(port, !app.net && !app.net_suspended);
    g_signal_connect(port, "insert-text", G_CALLBACK(port_digits_cb), NULL);
    g_signal_connect(port, "changed", G_CALLBACK(port_cb), NULL);
    gtk_box_pack_start(GTK_BOX(p), row_text(tr("Port when hosting"), FALSE, FALSE, FALSE), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(p), port, FALSE, FALSE, 0);
    row_add(card, p);
    add_switch(card, "Ask the router to open it (UPnP)", "UPnP", setting_int("UPnP", 1));
    rows_group(box, tr(ONLINE_TOKEN_LABEL));
    card = rows_card(box);
    GtkWidget *t = gtk_vbox_new(FALSE, 6), *token, *field = token_field(&token);
    gtk_container_set_border_width(GTK_CONTAINER(t), 8);
    char *own_token = online_token();
    gtk_entry_set_text(GTK_ENTRY(token), own_token);
    g_free(own_token);
    gtk_box_pack_start(GTK_BOX(t), field, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(t), link_button(ONLINE_URL "/account/oauth/token", ONLINE_TOKEN_URL), FALSE, FALSE, 0);
    row_add(card, t);
    add_section_reset(box, d);
    int r = page_run_keep(d);
    if (r != RESPONSE_SECTION_RESET) { /* the fields count when the page closes */
        char *text = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(name))));
        g_key_file_set_string(app.settings, "Settings", "PlayerName", text);
        g_free(text);
        text = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(token))));
        g_key_file_set_string(app.settings, "Settings", "LishogiToken", text);
        g_free(text);
    }
    gtk_widget_destroy(d);
    for (int c = 0; c < 2; c++)
        if (app.players[c] == PLAYER_HUMAN && app.mode != MODE_TWO_PLAYERS)
            snprintf(app.game.names[c], sizeof app.game.names[c], "%s", player_name(c));
    if (app.net) {
        char line[80];
        snprintf(line, sizeof line, "NAME %s", local_name());
        net_send(app.net, line);
    }
    changed();
    return r;
}

/* Shown again after its button looked for, downloaded or installed a version, with what it did. */
static int updates_page(void)
{
    for (;;) {
        GtkWidget *box, *d = page_new(tr("Updates"), &box);
        row_add(rows_card(box), updater_choice());
        GtkWidget *v = gtk_vbox_new(FALSE, 4);
        gtk_container_set_border_width(GTK_CONTAINER(v), 8);
        char *build = updater_version_text();
        GtkWidget *number = row_text(build, TRUE, FALSE, TRUE);
        g_free(build);
        gtk_misc_set_alignment(GTK_MISC(number), 0.5, 0.5);
        gtk_box_pack_start(GTK_BOX(v), number, FALSE, FALSE, 2);
        updater_about(d, v);
        gtk_box_pack_start(GTK_BOX(v), link_button(REPO_HOST "/releases", REPO_URL "/releases"), FALSE, FALSE, 0);
        row_add(rows_card(box), v);
        int r = page_run(d);
        if (r != RESPONSE_UPDATER)
            return r;
    }
}

static int controls_section(void)
{
    GtkWidget *box, *d = page_new(tr("Controls"), &box);
    gtk_box_pack_start(GTK_BOX(box), controls_page(), TRUE, TRUE, 0);
    int r = page_run_keep(d);
    controls_save(); /* before the page goes: its fields are read */
    gtk_widget_destroy(d);
    changed_fx(FX_CONTROLS);
    return r;
}

/* ---- the sections ---- */

typedef struct {
    const char *icon, *title;
    int (*open)(void);
    gboolean pc;
} Section;

static const Section SECTIONS[] = {
    {"♟", "Game", game_page, FALSE},      {"王", "Pieces", pieces_page, FALSE},   {"◇", "Variants", variants_page, FALSE},
    {"◐", "Appearance", looks_page, FALSE},
    {"♪", "Sound", sound_page, FALSE},    {"文", "Language", language_page, FALSE}, {"⇄", "Online", online_page, FALSE},
    {"⇩", "Updates", updates_page, FALSE},    {"⌨", "Controls", controls_section, TRUE},
};

/* The keys each section sets ("Pieces." and the like: a variant's own); what its reset removes. */
static const char *const *section_keys(int (*open)(void))
{
    static const char *const GAME[] = {"ShowMoves", "AutoPromote", "AutoPromoteAll", "IllegalLoses", "Engine", "EnginePath", NULL};
    static const char *const PIECES[] = {"Pieces", "PieceShape", "ColoredPieces", "MoveMarks", "ShowUnpromoted", "ChessColors",
                                         "Notation", NULL};
    static const char *const VARIANTS[] = {"Pieces.", "PieceShape.", "MoveMarks.", "ColoredPieces.", "ChessColors.", "SoundSet.", NULL};
    static const char *const LOOKS[] = {"Color", "Theme", "PaletteName", "WoodBoard", "Colorblind",
                                        "ShowCoords", "TextSize", "Landscape", "UseNotch", "ClockRefresh",
                                        "FlashEveryMove", "FlashDrawers", "PageTurn", NULL};
    static const char *const SOUND[] = {"SoundSet", "Sounds", "Volume", "Vibration", NULL};
    static const char *const ONLINE[] = {"PlayerName", "Port", "UPnP", "LishogiToken", NULL};
    return open == game_page ? GAME : open == pieces_page ? PIECES : open == variants_page ? VARIANTS
         : open == looks_page ? LOOKS : open == sound_page ? SOUND : open == online_page ? ONLINE : NULL;
}

static void section_reset(int (*open)(void))
{
    const char *const *keys = section_keys(open);
    char **all = g_key_file_get_keys(app.settings, "Settings", NULL, NULL);
    for (int i = 0; all && all[i]; i++)
        for (int k = 0; keys && keys[k]; k++) {
            size_t n = strlen(keys[k]);
            if (keys[k][n - 1] == '.' ? !strncmp(all[i], keys[k], n) : !strcmp(all[i], keys[k]))
                g_key_file_remove_key(app.settings, "Settings", all[i], NULL);
        }
    g_strfreev(all);
    changed();
}

/* What is chosen in a section, in a few words. */
static char *summary(int i)
{
    if (SECTIONS[i].open == game_page)
        return g_strdup_printf("%s: %s", tr("Illegal moves"), tr(ILLEGAL_LABELS[CLAMP(setting_int("IllegalLoses", 0), 0, 2)]));
    if (SECTIONS[i].open == variants_page) {
        GString *t = g_string_new(NULL);
        for (int v = 1; v < V_COUNT; v++)
            g_string_append_printf(t, "%s%s", v > 1 ? " · " : "", tr(VARIANT_LABELS[v]));
        return g_string_free(t, FALSE);
    }
    if (SECTIONS[i].open == pieces_page) {
        char *file = set_of(-1), *path = style_path(file);
        PieceStyle *s = style_load(path, app.fonts);
        char *t = g_strdup(s ? style_name(s, i18n_language()) : file);
        style_free(s);
        g_free(path);
        g_free(file);
        return t;
    }
    if (SECTIONS[i].open == looks_page)
        return g_strdup_printf("%s · %d%%", color_screen() ? tr(PALETTES[palette_index()].name) : tr("Black and white"),
                               setting_int("TextSize", 100));
    if (SECTIONS[i].open == sound_page) {
        char *label = sound_label(), *t = g_strdup_printf("%s · %d%%", label, sound_volume());
        g_free(label);
        return t;
    }
    if (SECTIONS[i].open == language_page)
        return g_strdup(LANGUAGES[i18n_language()].name);
    if (SECTIONS[i].open == online_page) {
        char *name = setting_string("PlayerName", "");
        char *t = g_strdup_printf("%s%s%s %d", name, *name ? " · " : "", tr("Port"), net_port());
        g_free(name);
        return t;
    }
    if (SECTIONS[i].open == updates_page)
    {
        char *build = *DENBOKU_VERSION ? g_strdup_printf(tr("Version %s"), DENBOKU_VERSION) : g_strdup_printf(tr("Build %s"), DENBOKU_BUILD);
        char *t = g_strdup_printf("%s · %s", updater_choice_label(), build);
        g_free(build);
        return t;
    }
    return g_strdup_printf("%s · %s", tr("Keyboard"), tr("Controller"));
}

/* Controls only where there are keys to bind; Updates only where the game looks for them. */
static gboolean section_hidden(int i)
{
    return (SECTIONS[i].pc && app.kindle) || (SECTIONS[i].open == updates_page && !updater_offered());
}

static void section_cb(GtkButton *b, gpointer data)
{
    GtkWidget *d = g_object_get_data(G_OBJECT(b), "dialog");
    g_object_set_data(G_OBJECT(d), "section", data);
    gtk_dialog_response(GTK_DIALOG(d), 100);
}

void preferences_dialog(void)
{
    int left = -1; /* the section just left: the cursor goes back to its row */
    for (;;) {
        GtkWidget *back_to = NULL;
        GtkWidget *d = dialog_new(tr("Preferences"));
        drawer_set_beside(d);
        g_object_set_data(G_OBJECT(d), "prefs-ok", GINT_TO_POINTER(TRUE)); /* the controller's Preferences button closes it */
        GtkWidget *card = rows_card(GTK_DIALOG(d)->vbox);
        for (int i = 0; i < (int) G_N_ELEMENTS(SECTIONS); i++) {
            if (section_hidden(i))
                continue;
            char *sum = summary(i);
            GtkWidget *b = row_add(card, row_section(SECTIONS[i].icon, tr(SECTIONS[i].title), sum));
            g_free(sum);
            g_object_set_data(G_OBJECT(b), "dialog", d);
            g_signal_connect(b, "clicked", G_CALLBACK(section_cb), GINT_TO_POINTER(i));
            if (i == left)
                back_to = b;
        }
        GtkWidget *reset = row_add(rows_card(GTK_DIALOG(d)->vbox), row_section("⭯", tr("Reset everything…"), ""));
        g_object_set_data(G_OBJECT(reset), "dialog", d);
        g_signal_connect(reset, "clicked", G_CALLBACK(section_cb), GINT_TO_POINTER(-1));
        gtk_widget_show_all(d);
        if (back_to)
            gtk_widget_grab_focus(back_to);
        int r = gtk_dialog_run(GTK_DIALOG(d));
        int section = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(d), "section"));
        gtk_widget_destroy(d);
        if (r != 100)
            return;
        if (section < 0) {
            if (reset_everything())
                return;
            continue;
        }
        /* The section, then the list again with what changed (in the new language, if that). */
        for (;;) {
            section_title = tr(SECTIONS[section].title);
            r = SECTIONS[section].open();
            section_title = NULL;
            if (r == RESPONSE_SECTION_RESET) { /* the same section again, with its defaults */
                section_reset(SECTIONS[section].open);
                continue;
            }
            if (r == RESPONSE_SECTION_AGAIN)
                continue; /* turning_page went with the page (its weak pointer) */
            if (r != RESPONSE_PAGE_NEXT && r != RESPONSE_PAGE_PREV)
                break;
            int n = (int) G_N_ELEMENTS(SECTIONS);
            do
                section = (section + (r == RESPONSE_PAGE_NEXT ? 1 : n - 1)) % n;
            while (section_hidden(section));
        }
        if (r == GTK_RESPONSE_DELETE_EVENT)
            return;
        left = section;
    }
}
