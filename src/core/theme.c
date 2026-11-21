/* Piece styles, colors, palettes and the widget theme. */

#include "app.h"

/* ---- styles ---- */

ColorMode color_mode(void) { return (ColorMode) CLAMP(setting_int("Color", COLOR_AUTO), COLOR_AUTO, COLOR_COLOR); }

gboolean color_screen(void)
{
    ColorMode m = color_mode();
    if (m != COLOR_AUTO)
        return m == COLOR_COLOR;
    return auto_color_screen();
}

/* Kindles: color on the Colorsoft models, known by the device code in the serial number (as FBInk
 * reads it: new serials, starting with G, carry it base-32 in characters 3-5); computers always. */
gboolean auto_color_screen(void)
{
    if (!app.kindle)
        return TRUE;
    if (g_getenv("DENBOKU_KINDLE_LOOK")) /* a Paperwhite's, on a computer whose screen has color */
        return FALSE;
    static const unsigned COLOR_KINDLES[] = {0xE29, 0xE24, 0xE2B, 0xE26, 0xE22, 0xC9F, 0xE27, 0xE5B, 0xE46,
                                             0x10A6, 0x10A5, 0x11D7, 0x13BF, 0x12EF, 0x12F1, 0x11E9, 0x11EB, 0x10D7};
    char *usid = NULL;
    gboolean color = FALSE;
    if (g_file_get_contents("/proc/usid", &usid, NULL, NULL) && strlen(usid) >= 6 && usid[0] != 'B' && usid[0] != '9') {
        static const char digits[] = "0123456789ABCDEFGHJKLMNPQRSTUVWX";
        unsigned code = 0;
        for (int i = 3; i < 6; i++) {
            const char *d = strchr(digits, usid[i]);
            code = code * 32 + (d && *d ? (unsigned) (d - digits) : 0);
        }
        for (size_t i = 0; i < G_N_ELEMENTS(COLOR_KINDLES); i++)
            color |= code == COLOR_KINDLES[i];
    }
    g_free(usid);
    if (!color) {
        char *bpp = NULL;
        if (g_file_get_contents("/sys/class/graphics/fb0/bits_per_pixel", &bpp, NULL, NULL))
            color = atoi(bpp) > 8;
        g_free(bpp);
    }
    return color;
}

#define HEX(x) {((x) >> 16 & 255) / 255.0, ((x) >> 8 & 255) / 255.0, ((x) & 255) / 255.0}

#define PALETTE(name, board, hand, last, dot, fg, bg, lines) \
    {name, {HEX(board), HEX(hand), HEX(last), HEX(dot), FALSE, HEX(fg), HEX(bg), HEX(lines), HEX(dot), HEX(0x1a8c33)}}

/* Color screens: a surround with a matching board, its accent the dot's; in rows of four, the cool
 * dark ones, the warm dark ones, then Green felt and the light ones. Wood's board, last move, dot and lines are
 * also the wooden board under any other. */
const Palette PALETTES[NPALETTES] = {
    PALETTE("Slate", 0x2f3b4c, 0x263140, 0x4f6f8f, 0x9cc8ff, 0xdde6f0, 0x161b22, 0xdde6f0),
    PALETTE("Charcoal", 0x3a3d42, 0x2e3135, 0x55734a, 0x8ab4f8, 0xe6e6e6, 0x1e1f22, 0xe6e6e6),
    PALETTE("Ice", 0x3b4252, 0x2e3440, 0x5e81ac, 0x88c0d0, 0xeceff4, 0x242933, 0xeceff4),
    PALETTE("Navy", 0x1f2a44, 0x18213a, 0x3d5a80, 0xf2c14e, 0xe0e6f5, 0x0e1424, 0xe0e6f5),
    PALETTE("Eggplant", 0x2e2540, 0x251e34, 0x5c4f8a, 0xc9a6ff, 0xece6f5, 0x15111f, 0xece6f5),
    PALETTE("Rose", 0x42263a, 0x33202d, 0x7a3d5e, 0xf48fb8, 0xf6e6ee, 0x1d1118, 0xf6e6ee),
    PALETTE("Wine", 0x3d2228, 0x311b20, 0x6b4a2a, 0xf0c070, 0xf0e2e4, 0x1c0f12, 0xf0e2e4),
    PALETTE("Walnut", 0x3d2b1a, 0x2e2114, 0x6b5530, 0xe0a854, 0xede0c7, 0x17120d, 0xede0c7),
    PALETTE("Green felt", 0x23412f, 0x1b3325, 0x4d7a3a, 0xf0d080, 0xe3eee6, 0x102016, 0xe3eee6),
    PALETTE("Wood", 0xe9c886, 0xeadbbd, 0x9bbf68, 0xb8442e, 0x3a2a1a, 0xf7f0e3, 0x3a2a1a),
    PALETTE("Sakura", 0xf4d6df, 0xf5dbe3, 0xeaa3bb, 0xc2477a, 0x3b2530, 0xfff6f8, 0x3b2530),
    PALETTE("Paper", 0xe4e4dc, 0xe8e8e2, 0xb9c6d8, 0x2f4a78, 0x22262e, 0xf6f6f2, 0x22262e),
};

/* Saved by name ("PaletteName"), Slate unless chosen. */
int palette_index(void)
{
    char *name = setting_string("PaletteName", "Slate");
    int found = 0;
    for (int i = 0; i < NPALETTES; i++)
        if (!strcmp(PALETTES[i].name, name))
            found = i;
    g_free(name);
    return found;
}

int wood_palette(void)
{
    for (int i = 0; i < NPALETTES; i++)
        if (!strcmp(PALETTES[i].name, "Wood"))
            return i;
    return 0;
}

/* The board colors for color screens; `wood` keeps the wooden board under any surround (the
 * accent stays the palette's). */
static BoardColors palette_colors(int i, gboolean wood)
{
    BoardColors c = PALETTES[i].c;
    if (wood && i != wood_palette()) {
        const BoardColors *w = &PALETTES[wood_palette()].c;
        memcpy(c.board, w->board, sizeof c.board);
        memcpy(c.last, w->last, sizeof c.last);
        memcpy(c.dot, w->dot, sizeof c.dot);
        memcpy(c.lines, w->lines, sizeof c.lines);
    }
    /* Colors for color blindness: green against the wood's tan is what red-green color blindness
     * (the common kind) loses; blue and vermilion stay apart for it (Okabe and Ito's palette). */
    if (setting_int("Colorblind", 0)) {
        static const double SKY[3] = HEX(0x56b4e9), LIGHT_SKY[3] = HEX(0x86b8e8), VERMILION[3] = HEX(0xd55e00);
        gboolean light = c.board[0] + c.board[1] + c.board[2] > 1.5;
        for (int k = 0; k < 3; k++)
            c.last[k] = light ? LIGHT_SKY[k] : c.board[k] + (SKY[k] - c.board[k]) * 0.5;
        memcpy(c.hint, VERMILION, sizeof c.hint);
    }
    return c;
}

/* Whether the desktop asks for dark: the XDG portal setting, or a dark GTK theme. */
static gboolean system_dark(void)
{
    if (app.kindle)
        return FALSE;
#ifdef G_OS_WIN32
    DWORD light = 1, size = sizeof light;
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &light, &size);
    return !light;
#endif
    const char *env = g_getenv("GTK_THEME");
    if (env && strstr(env, ":dark"))
        return TRUE;
    gboolean dark = FALSE, known = FALSE;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
    if (bus) {
        GVariant *r = g_dbus_connection_call_sync(bus, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                                                  "org.freedesktop.portal.Settings", "Read",
                                                  g_variant_new("(ss)", "org.freedesktop.appearance", "color-scheme"),
                                                  NULL, G_DBUS_CALL_FLAGS_NONE, 500, NULL, NULL);
        if (r) {
            GVariant *v = NULL;
            g_variant_get(r, "(v)", &v);
            while (v && g_variant_is_of_type(v, G_VARIANT_TYPE_VARIANT)) {
                GVariant *inner = g_variant_get_variant(v);
                g_variant_unref(v);
                v = inner;
            }
            if (v && g_variant_is_of_type(v, G_VARIANT_TYPE_UINT32)) {
                guint32 scheme = g_variant_get_uint32(v);
                known = scheme != 0;
                dark = scheme == 1;
            }
            if (v)
                g_variant_unref(v);
            g_variant_unref(r);
        }
        g_object_unref(bus);
    }
    if (known)
        return dark;
    char *theme = NULL;
    g_object_get(gtk_settings_get_default(), "gtk-theme-name", &theme, NULL);
    char *lower = theme ? g_ascii_strdown(theme, -1) : NULL;
    dark = lower && strstr(lower, "dark");
    g_free(lower);
    g_free(theme);
    return dark;
}

/* Black and white screens: 0 light, 1 dark, 2 follow the system. */
int theme_setting(void) { return setting_int("Theme", 2); }

static gboolean mono_dark(void)
{
    int t = theme_setting();
    return t == 1 || (t == 2 && system_dark());
}

/* The built-in sets in the order of the list (Tori's 1-kanji copies where their 2-kanji sets stand); any
 * other set after them, the animals last. */
static int style_rank(const char *file)
{
    static const char *const ORDER[] = {"printout-kanji1.ini", "kanji1.ini", "kanji1-gata.ini", "printout-kanji2.ini", "printout-kanji1-tori.ini",
                                        "kanji2-antique.ini", "kanji1-antique-tori.ini", "pictorial.ini", "printout-western.ini"};
    for (int i = 0; i < (int) G_N_ELEMENTS(ORDER); i++)
        if (!strcmp(file, ORDER[i]))
            return i;
    return strcmp(file, "dobutsu.ini") ? G_N_ELEMENTS(ORDER) : G_N_ELEMENTS(ORDER) + 1;
}

char *style_path(const char *file)
{
    char *path = g_build_filename(app.custom_pieces, file, NULL);
    if (!g_file_test(path, G_FILE_TEST_IS_REGULAR) && !style_is_pictures(path)) {
        g_free(path);
        path = g_build_filename(app.styles, file, NULL);
    }
    return path;
}

void load_style_list(void)
{
    app.style_files = g_ptr_array_new_with_free_func(g_free);
    const char *const dirs[] = {app.styles, app.custom_pieces};
    for (int i = 0; i < 2; i++) {
        GDir *dir = g_dir_open(dirs[i], 0, NULL);
        const char *name;
        while (dir && (name = g_dir_read_name(dir))) {
            gboolean have = FALSE;
            for (guint k = 0; k < app.style_files->len; k++)
                have |= !strcmp(name, g_ptr_array_index(app.style_files, k));
            char *full = g_build_filename(dirs[i], name, NULL);
            if ((g_str_has_suffix(name, ".ini") || style_is_pictures(full)) && !have)
                g_ptr_array_add(app.style_files, g_strdup(name));
            g_free(full);
        }
        if (dir)
            g_dir_close(dir);
    }
    /* Built-in sets first, then the rest alphabetically by name without .ini (so a style comes
     * before its variants), Dōbutsu's animals last. */
    for (guint i = 0; i < app.style_files->len; i++)
        for (guint j = i + 1; j < app.style_files->len; j++) {
            const char *a = g_ptr_array_index(app.style_files, i), *b = g_ptr_array_index(app.style_files, j);
            int ra = style_rank(a), rb = style_rank(b), stem = MAX((int) MIN(strlen(a), strlen(b)) - 4, 0);
            int c = strncmp(b, a, stem);
            if (rb < ra || (ra == rb && (c < 0 || (c == 0 && strlen(b) < strlen(a))))) {
                gpointer t = app.style_files->pdata[i];
                app.style_files->pdata[i] = app.style_files->pdata[j];
                app.style_files->pdata[j] = t;
            }
        }
}

/* "#rrggbb" of a mixed with b by t. */
static void hex_mix(char out[8], const double *a, const double *b, double t)
{
    snprintf(out, 8, "#%02x%02x%02x", (int) ((a[0] + (b[0] - a[0]) * t) * 255), (int) ((a[1] + (b[1] - a[1]) * t) * 255),
             (int) ((a[2] + (b[2] - a[2]) * t) * 255));
}

/* Windows grow with larger text by themselves but keep their size when it gets smaller: open
 * dialogs (Preferences while previewing) go back to the size their contents ask for. */
static void fit_open_dialogs(void)
{
    GList *windows = gtk_window_list_toplevels();
    for (GList *l = windows; l; l = l->next)
        if (GTK_IS_DIALOG(l->data) && gtk_widget_get_visible(l->data))
            gtk_window_resize(l->data, 1, 1);
    g_list_free(windows);
}

/* Color palettes, and black and white on a computer, restyle the widgets; a white surround would
 * keep the system theme (the Kindle's is black and white already). Once switched to dark, going back
 * installs a light style, since rc styles cannot be removed. */
static void apply_widget_theme(const BoardColors *c, gboolean mono)
{
    static char *was;
    style_set_framed(mono);
    style_set_accent(mono ? c->fg : c->accent, c->bg);
    gboolean dark = c->bg[0] + c->bg[1] + c->bg[2] < 1.5;
    gboolean tinted = dark || (!mono && c->bg[0] + c->bg[1] + c->bg[2] < 3);
    char fg[8] = "#000000", bg[8] = "#ffffff", mid[8] = "#dddddd", dim[8] = "#888888", tab[8] = "#eeeeee";
    if (tinted) {
        /* Pressed buttons and unselected tabs a little toward the text; disabled widgets dim it. */
        hex_mix(fg, c->bg, c->fg, 1);
        hex_mix(bg, c->bg, c->fg, 0);
        hex_mix(mid, c->bg, c->fg, dark ? 0.25 : 0.15);
        hex_mix(dim, c->bg, c->fg, 0.5);
        hex_mix(tab, c->bg, c->fg, dark ? 0.1 : 0.06);
    }
    /* A chosen row (the rooms found, a list) in the accent, as the chosen chip; black and white inverts. */
    char sel[8];
    if (mono)
        memcpy(sel, fg, sizeof sel);
    else
        hex_mix(sel, c->accent, c->accent, 0);
    char *key = g_strconcat(fg, bg, sel, NULL);
    if ((!was && !tinted && !(mono && !app.kindle)) || (was && !strcmp(was, key))) {
        g_free(key);
        return;
    }
    g_free(was);
    was = key;
    char *rc = g_strdup_printf("style \"denboku-theme\" {\n"
                               "  fg[NORMAL] = \"%s\" fg[PRELIGHT] = \"%s\" fg[ACTIVE] = \"%s\" fg[SELECTED] = \"%s\"\n"
                               "  fg[INSENSITIVE] = \"%s\" text[INSENSITIVE] = \"%s\"\n"
                               "  text[NORMAL] = \"%s\" text[SELECTED] = \"%s\"\n"
                               "  base[NORMAL] = \"%s\" base[SELECTED] = \"%s\" base[INSENSITIVE] = \"%s\"\n"
                               "  bg[NORMAL] = \"%s\" bg[PRELIGHT] = \"%s\" bg[ACTIVE] = \"%s\" bg[SELECTED] = \"%s\"\n"
                               "  bg[INSENSITIVE] = \"%s\"\n"
                               "}\nwidget_class \"*\" style \"denboku-theme\"\n",
                               fg, fg, fg, bg, dim, dim, fg, bg, bg, sel, bg, bg, mid, tab, sel, bg);
    gtk_rc_parse_string(rc);
    g_free(rc);
    gtk_rc_reset_styles(gtk_settings_get_default());
    fit_open_dialogs();
}

/* Preferences → Appearance → Text size: the widgets' font (top bar, menus, dialogs, chat) at a
 * percentage of the one the game started with; the board scales with the window already. */
const int TEXT_SIZES[NTEXT_SIZES] = {75, 90, 100, 110, 125, 150, 175, 200};

void apply_text_size(void)
{
    static PangoFontDescription *base;
    static int shown = 100;
    if (!base) {
        char *name = NULL;
        g_object_get(gtk_settings_get_default(), "gtk-font-name", &name, NULL);
        base = pango_font_description_from_string(app.kindle ? "Denboku Sans, Sans 9" : name ? name : "Sans 10");
        g_free(name);
    }
    int pct = CLAMP(setting_int("TextSize", 100), TEXT_SIZES[0], TEXT_SIZES[NTEXT_SIZES - 1]);
    if (pct == shown) {
        style_follow_text(); /* the first time, at the size the game started with */
        return;
    }
    shown = pct;
    PangoFontDescription *fd = pango_font_description_copy(base);
    pango_font_description_set_size(fd, pango_font_description_get_size(base) * pct / 100);
    char *font = pango_font_description_to_string(fd);
    pango_font_description_free(fd);
    char *rc = g_strdup_printf("style \"denboku-size\" { font_name = \"%s\" }\nwidget_class \"*\" style \"denboku-size\"\n", font);
    g_free(font);
    gtk_rc_parse_string(rc);
    g_free(rc);
    gtk_rc_reset_styles(gtk_settings_get_default());
    fit_open_dialogs();
    style_follow_text();
}

/* A setting of one variant ("Pieces.Minishogi"), which overrides the one for all variants. */
char *variant_key(const char *key, int variant) { return g_strdup_printf("%s.%s", key, variant_name(variant)); }

/* Sets that went: the text-only one became 1 kanji without the piece shape, the colored ones
 * and the high-contrast one their plain set, the other 2-kanji fonts AR PL UKai, pixel art UnGungseo. */
static const char *const GONE_SETS[][2] = {
    {"text.ini", "kanji1.ini"},
    {"printout-kanji-colored.ini", "kanji1.ini"},
    {"printout-western-colored.ini", "printout-western.ini"},
    {"printout-western-uniform.ini", "printout-western.ini"},
    {"printout-western-contrast.ini", "printout-western.ini"},
    {"kanji2-ukai.ini", "printout-kanji2.ini"},
    {"kanji2.ini", "printout-kanji2.ini"},
    {"brush.ini", "printout-kanji2.ini"},
    {"pixel.ini", "printout-kanji1.ini"},
};

char *pieces_file(char *file)
{
    for (size_t i = 0; file && i < G_N_ELEMENTS(GONE_SETS); i++)
        if (!strcmp(file, GONE_SETS[i][0])) {
            g_free(file);
            return g_strdup(GONE_SETS[i][1]);
        }
    return file;
}

/* Preferences → Appearance → Colored pieces: each piece's own background on color screens. Off by
 * default (a variant's own default first: on for Dōbutsu), on for whoever had one of the colored
 * sets. */
static gboolean colored_all(void)
{
    if (g_key_file_has_key(app.settings, "Settings", "ColoredPieces", NULL))
        return setting_int("ColoredPieces", 0);
    char *file = setting_string("Pieces", "");
    gboolean colored = strstr(file, "-colored") || strstr(file, "-uniform");
    g_free(file);
    return colored;
}

int colored_setting(int variant)
{
    char *key = variant_key("ColoredPieces", variant);
    int colored = setting_int(key, variant_colored_default(variant));
    g_free(key);
    return colored;
}

gboolean colored_pieces(int variant)
{
    int colored = variant >= 0 ? colored_setting(variant) : -1;
    return colored < 0 ? colored_all() : colored;
}

/* Preferences → Pieces → Draw the piece shape: the koma (or tile) behind every piece set. On by
 * default (a variant's own default first). */
int shape_setting(int variant)
{
    char *key = variant_key("PieceShape", variant);
    int shape = setting_int(key, variant_shape_default(variant));
    g_free(key);
    return shape;
}

gboolean piece_shape(int variant)
{
    int shape = variant >= 0 ? shape_setting(variant) : -1;
    return shape >= 0 ? shape : setting_int("PieceShape", 1);
}

char *pieces_setting(int variant)
{
    char *key = variant_key("Pieces", variant);
    char *file = g_key_file_get_string(app.settings, "Settings", key, NULL);
    g_free(key);
    if (!file)
        file = g_strdup(variant_pieces_default(variant));
    if (file && *file)
        return pieces_file(file);
    g_free(file);
    return pieces_file(setting_string("Pieces", DEFAULT_PIECES));
}

/* Preferences → Pieces → Chess mode, or a variant's own (-1 there: the same as all variants). */
gboolean chess_mode(int variant)
{
    char *key = variant >= 0 ? variant_key("ChessColors", variant) : NULL;
    int on = key ? setting_int(key, -1) : -1;
    g_free(key);
    return on < 0 ? setting_int("ChessColors", 0) : on;
}

int move_marks_setting(int variant)
{
    char *key = variant_key("MoveMarks", variant);
    int marks = setting_int(key, variant_marks_default(variant));
    g_free(key);
    return marks < 0 ? setting_int("MoveMarks", 0) : marks;
}

static int styled_variant = -1;

/* The pieces of the variant being played, after it changed. */
void apply_variant_style(void)
{
    if (rules_variant != styled_variant)
        apply_style();
}

/* The piece set of `variant` as its settings have it (NULL when none loads). */
static PieceStyle *variant_style(int variant)
{
    char *file = pieces_setting(variant);
    char *path = style_path(file);
    gint64 t0 = g_get_monotonic_time();
    PieceStyle *s = style_load(path, app.fonts);
    if (g_getenv("DENBOKU_DEBUG"))
        g_printerr("style %s: %.1f ms\n", file, (g_get_monotonic_time() - t0) / 1000.0);
    if (!s || !style_serves(s, variant)) { /* the set for all variants may have nothing for this one */
        style_free(s);
        g_free(path);
        const char *fallback = variant_pieces_default(variant);
        path = style_path(fallback ? fallback : DEFAULT_PIECES);
        s = style_load(path, app.fonts);
    }
    g_free(path);
    g_free(file);
    if (s) {
        style_set_move_marks(s, move_marks_setting(variant));
        style_set_shape(s, piece_shape(variant));
        style_set_colored(s, colored_pieces(variant));
    }
    return s;
}

/* board_preview()'s variant (-1: none), its game and pieces. */
static int previewed = -1;
static Game *preview_game;
static PieceStyle *preview_style;

void board_preview(int variant)
{
    view_preview(app.view, NULL, 0, NULL);
    style_free(preview_style);
    preview_style = NULL;
    previewed = variant == rules_variant ? -1 : variant;
    view_set_chess_colors(app.view, chess_mode(previewed >= 0 ? previewed : rules_variant));
    if (previewed < 0 || !(preview_style = variant_style(variant)))
        return;
    if (!preview_game)
        preview_game = g_new0(Game, 1);
    int game_variant = rules_variant;
    rules_set_variant(variant);
    game_new(preview_game, HANDICAP_NONE);
    rules_set_variant(game_variant);
    view_preview(app.view, preview_game, variant, preview_style);
}

void apply_style(void)
{
    styled_variant = rules_variant;
    PieceStyle *s = variant_style(rules_variant);
    if (!s)
        return;
    gboolean color = color_screen();
    static BoardColors colors;
    colors = color ? palette_colors(palette_index(), setting_int("WoodBoard", 1)) : mono_dark() ? COLORS_DARK : COLORS_MONO;
    apply_widget_theme(&colors, !color);
    const PieceTheme *pieces = color ? &THEME_COLOR : &THEME_MONO;
    view_set_chess_colors(app.view, chess_mode(rules_variant));
    view_set_style(app.view, s, pieces, &colors);
    style_free(app.style);
    app.style = s;
    if (previewed >= 0) /* its settings may have changed too */
        board_preview(previewed);
}
