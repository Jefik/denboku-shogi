/* Settings and paths. */

#include "app.h"

#ifndef G_OS_WIN32
#include <fontconfig/fontconfig.h>
#include <sys/stat.h>
#endif

/* ---- settings and paths ---- */

char *setting_string(const char *key, const char *fallback)
{
    char *v = g_key_file_get_string(app.settings, "Settings", key, NULL);
    return v ? v : g_strdup(fallback);
}

int setting_int(const char *key, int fallback)
{
    GError *err = NULL;
    int v = g_key_file_get_integer(app.settings, "Settings", key, &err);
    if (err) {
        g_error_free(err);
        return fallback;
    }
    return v;
}

void save_settings(void)
{
    char *data = g_key_file_to_data(app.settings, NULL, NULL);
#ifndef G_OS_WIN32
    mode_t mask = umask(077); /* the CSA password and the lishogi token: for this user only */
#endif
    g_file_set_contents(app.settings_path, data, -1, NULL);
#ifndef G_OS_WIN32
    umask(mask);
#endif
    g_free(data);
}

#ifdef G_OS_WIN32

#define EXE_SUFFIX ".exe"

#else

#define EXE_SUFFIX ""

#endif

/* The data folder nobody chose: the system's per-user one. */
static char *default_home(void) { return g_build_filename(g_get_user_data_dir(), "denboku", NULL); }

/* Where the chosen folder is kept: a file in the default one, as settings.ini is in the folder itself. */
static char *chosen_file(void)
{
    char *home = default_home(), *file = g_build_filename(home, "datafolder", NULL);
    g_free(home);
    return file;
}

static char *data_folder_chosen(void)
{
    char *file = chosen_file(), *path = NULL;
    if (g_file_get_contents(file, &path, NULL, NULL))
        g_strstrip(path);
    g_free(file);
    if (path && !*path) {
        g_free(path);
        path = NULL;
    }
    return path;
}

void data_folder_choose(const char *path)
{
    char *home = default_home(), *file = chosen_file();
    g_mkdir_with_parents(home, 0755);
    if (path && *path && strcmp(path, home))
        g_file_set_contents(file, path, -1, NULL);
    else
        g_remove(file);
    g_free(home);
    g_free(file);
}

/* What `from` has and `to` lacks, folders and all. */
static void copy_missing(const char *from, const char *to)
{
    GDir *d = g_dir_open(from, 0, NULL);
    g_mkdir_with_parents(to, 0755);
    for (const char *name; d && (name = g_dir_read_name(d));) {
        char *a = g_build_filename(from, name, NULL), *b = g_build_filename(to, name, NULL);
        if (g_file_test(a, G_FILE_TEST_IS_DIR))
            copy_missing(a, b);
        else if (!g_file_test(b, G_FILE_TEST_EXISTS)) {
            char *data;
            gsize len;
            if (g_file_get_contents(a, &data, &len, NULL)) {
                g_file_set_contents(b, data, (gssize) len, NULL);
                g_free(data);
            }
        }
        g_free(a);
        g_free(b);
    }
    if (d)
        g_dir_close(d);
}

/* The user's own sets, apart from the game's (on the Kindle the game's folder is the data folder, and an
 * update replaces its styles/ and sounds/): `dir`, else custom/ of the data folder; Android's default is
 * where a computer sees it over USB. */
void custom_folder_set(const char *dir)
{
    const char *android = g_getenv("DENBOKU_CUSTOM");
    g_free(app.custom), g_free(app.custom_pieces), g_free(app.custom_sounds);
    app.custom = dir && *dir ? g_strdup(dir) : android ? g_strdup(android) : g_build_filename(app.home, "custom", NULL);
    app.custom_pieces = g_build_filename(app.custom, "pieces", NULL);
    app.custom_sounds = g_build_filename(app.custom, "sounds", NULL);
    g_mkdir_with_parents(app.custom_pieces, 0755);
    g_mkdir_with_parents(app.custom_sounds, 0755);
}

void find_paths(void)
{
#ifdef G_OS_WIN32
    app.base = g_win32_get_package_installation_directory_of_module(NULL);
#else
    char *exe = g_file_read_link("/proc/self/exe", NULL);
    char *bin = g_path_get_dirname(exe ? exe : ".");
    app.base = g_path_get_dirname(bin);
    g_free(exe);
    g_free(bin);
#endif
    /* Android: the program is in the app's library folder, its data elsewhere. */
    if (g_getenv("DENBOKU_BASE")) {
        g_free(app.base);
        app.base = g_strdup(g_getenv("DENBOKU_BASE"));
    }
#ifdef G_OS_WIN32
    app.kindle = FALSE;
#else
    app.kindle = g_file_test("/mnt/us", G_FILE_TEST_IS_DIR) && g_file_test("/usr/bin/lipc-set-prop", G_FILE_TEST_EXISTS);
#endif
    /* DENBOKU_KINDLE_LOOK: on a computer, the Kindle's look and sizes (screenshots, checks without the
     * device), its files where a computer keeps them. */
    gboolean real_kindle = app.kindle;
    if (g_getenv("DENBOKU_KINDLE_LOOK"))
        app.kindle = TRUE;
    const char *home = g_getenv("DENBOKU_HOME");
    if (home)
        app.home = g_strdup(home);
    else if (real_kindle)
        app.home = g_strdup(app.base);
    else {
        app.home = default_home();
        /* A folder the player chose (Preferences → Game): the default one's files come along the first time. */
        char *chosen = real_kindle ? NULL : data_folder_chosen();
        if (chosen && (g_mkdir_with_parents(chosen, 0755) == 0)) {
            char *settings = g_build_filename(chosen, "settings.ini", NULL);
            if (!g_file_test(settings, G_FILE_TEST_EXISTS))
                copy_missing(app.home, chosen);
            g_free(settings);
            g_free(app.home);
            app.home = chosen;
        } else
            g_free(chosen);
    }
    /* Developer mode: a profile of its own, dev/ in the data folder, with the default settings at
     * every start (DENBOKU_DEV=keep keeps them), so testing leaves the player's alone. */
    const char *dev = g_getenv("DENBOKU_DEV");
    if (dev) {
        char *home_dev = g_build_filename(app.home, "dev", NULL);
        g_free(app.home);
        app.home = home_dev;
    }
    app.saves = real_kindle && !dev ? g_strdup("/mnt/us/documents/shogi") : g_build_filename(app.home, "saves", NULL);
    g_mkdir_with_parents(app.home, 0755);
    g_mkdir_with_parents(app.saves, 0755);
    app.history = g_build_filename(app.home, "history", NULL);
    const char *engine = g_getenv("DENBOKU_ENGINE");
    app.engine_path = engine ? g_strdup(engine) : g_build_filename(app.base, "bin", "fairy-stockfish" EXE_SUFFIX, NULL);
    app.fonts = g_build_filename(app.base, "fonts", NULL);
    app.styles = g_build_filename(app.base, "styles", NULL);
    app.settings_path = g_build_filename(app.home, "settings.ini", NULL);
    if (dev && strcmp(dev, "keep"))
        g_unlink(app.settings_path);
    app.settings = g_key_file_new();
    g_key_file_load_from_file(app.settings, app.settings_path, G_KEY_FILE_NONE, NULL);
    char *custom = setting_string("CustomFolder", "");
    custom_folder_set(custom);
    g_free(custom);
    add_ui_fonts();
}

/* The system's language: on the Kindle from its own locale file (the game may not inherit it),
 * elsewhere from the environment (GLib asks Windows for its own). */
static Language system_language(void)
{
    char *text = NULL;
    if (app.kindle && g_file_get_contents("/var/local/system/locale", &text, NULL, NULL)) {
        char **lines = g_strsplit(text, "\n", -1);
        int found = -1;
        for (int i = 0; lines[i] && found < 0; i++) {
            char *eq = strchr(lines[i], '=');
            if (eq)
                found = i18n_of_locale(g_strstrip(eq + 1));
        }
        g_strfreev(lines);
        g_free(text);
        if (found >= 0)
            return (Language) found;
    }
    for (const char *const *n = g_get_language_names(); *n; n++)
        if (i18n_of_locale(*n) >= 0)
            return (Language) i18n_of_locale(*n);
    return LANG_EN;
}

/* "Language": en, ja, pt, or auto (the default), the system's. */
void apply_language(void)
{
    char *code = setting_string("Language", "auto");
    int l = i18n_of_locale(code);
    g_free(code);
    i18n_set_language(l >= 0 ? (Language) l : system_language());
}

/* Denboku Sans (fonts/DenbokuSans-*.ttf, Noto Sans JP cut to what the game writes), so the board's
 * text looks the same everywhere; anything it lacks, as some names, falls back to the system.
 * Yuji Syuku (cut to 墨, the icon's font) holds only the 墨 behind the move list. */
void add_ui_fonts(void)
{
    static const char *const FILES[] = {"DenbokuSans-Regular.ttf", "DenbokuSans-Bold.ttf", "YujiSyuku-sumi.ttf"};
    for (size_t i = 0; i < G_N_ELEMENTS(FILES); i++) {
        char *path = g_build_filename(app.fonts, FILES[i], NULL);
#ifdef G_OS_WIN32
        /* Pango's DirectWrite font map does not see fonts added through GDI. */
        pango_font_map_add_font_file(pango_cairo_font_map_get_default(), path, NULL);
#else
        FcConfigAppFontAddFile(FcConfigGetCurrent(), (const FcChar8 *) path);
#endif
        g_free(path);
    }
}
