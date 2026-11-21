/* Updates: at each start the latest release's signed manifest (update.c checks it); a later build for
 * this platform is downloaded, checked and swapped in where the game can replace its own files (the
 * Kindle, the Linux folder, the AppImage; the Windows folder, unpacked here and swapped by Denboku.exe at
 * the next start), or handed to the system's installer (Android, which asks the player first). The MSIX
 * only tells; the Flatpak leaves updates to Flatpak. Where from and who signs: src/<game>/release.h. */

#include "app.h"
#include "http.h"
#include "proc.h"
#include "release.h"
#include "update.h"
#include "zip.h"

#include <glib/gstdio.h>
#ifndef G_OS_WIN32
#include <unistd.h>
#endif

enum { UPDATES_UNASKED, UPDATES_AUTO, UPDATES_ASK, UPDATES_OFF };
typedef enum { WAY_NONE, WAY_FOLDER, WAY_APPIMAGE, WAY_ZIP, WAY_APK } Way;
typedef enum { IDLE, CHECKING, LATEST, AVAILABLE, DOWNLOADING, READY, FAILED } State;

static const uint8_t KEYS[][32] = RELEASE_KEYS;
/* An update not tried yet when this run started: this run is its try. Not one installed during this run. */
static gboolean pending_at_start, old_image_at_start;
static State state;
static UpdateOffer offer;
static gboolean by_hand; /* asked from About: say how it went */
static char *package;    /* the package being downloaded */
static Proc *tar;        /* unpacking it */

/* Whether a file can be made in `dir` (access() says little on Windows). */
G_GNUC_UNUSED static gboolean writable(const char *dir)
{
    char *probe = g_build_filename(dir, ".denboku-write-test", NULL);
    gboolean ok = g_file_set_contents(probe, "", 0, NULL);
    g_remove(probe);
    g_free(probe);
    return ok;
}

#ifdef G_OS_WIN32
/* Installed from the MSIX, which only Windows can update (GetCurrentPackageFullName is Windows 8's). */
static gboolean packaged(void)
{
    LONG (WINAPI *full_name)(UINT32 *, PWSTR) = (void *) GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetCurrentPackageFullName");
    UINT32 n = 0;
    return full_name && full_name(&n, NULL) != 15700L; /* APPMODEL_ERROR_NO_PACKAGE */
}
#endif

/* The manifest's name for this build, and how it can update itself; NULL where something else updates it. */
static const char *platform(Way *way)
{
    *way = WAY_NONE;
    if (g_getenv("FLATPAK_ID"))
        return NULL;
#ifdef G_OS_WIN32
    /* The portable folder, if it can be written: Denboku.exe swaps the files before it starts the game.
     * The MSIX only tells: the player installs the new one. */
    char *launcher = g_build_filename(app.base, "Denboku.exe", NULL);
    if (!packaged() && g_file_test(launcher, G_FILE_TEST_EXISTS) && writable(app.base))
        *way = WAY_ZIP;
    g_free(launcher);
    return "windows-x86_64";
#elif defined(__ANDROID__) /* the system installs it, once the player says so */
    *way = WAY_APK;
#if defined(__x86_64__)
    return "android-x86_64";
#else
    return "android";
#endif
#else
    if (app.kindle) {
        *way = WAY_FOLDER;
        return "kindle";
    }
    const char *image = g_getenv("APPIMAGE");
    if (image) {
        char *dir = g_path_get_dirname(image);
        *way = writable(dir) ? WAY_APPIMAGE : WAY_NONE;
        g_free(dir);
        return "appimage-x86_64";
    }
    char *script = g_build_filename(app.base, "denboku.sh", NULL), *parent = g_path_get_dirname(app.base);
    if (g_file_test(script, G_FILE_TEST_EXISTS) && writable(app.base) && writable(parent))
        *way = WAY_FOLDER;
    g_free(script), g_free(parent);
#if defined(__aarch64__)
    return "linux-arm64";
#else
    return "linux-x86_64";
#endif
#endif
}

/* The release's address; DENBOKU_UPDATE_URL stands in for it in tests (with DENBOKU_DEBUG). */
static const char *base_url(gboolean *test)
{
    const char *url = g_getenv("DENBOKU_DEBUG") ? g_getenv("DENBOKU_UPDATE_URL") : NULL;
    *test = url != NULL;
    return url ? url : RELEASE_URL;
}

/* `name` from the latest release, or from release `tag` when there is one (a test server keeps every file in one folder). */
static void download(const char *name, const char *tag, const char *path, HttpDone done)
{
    gboolean test;
    const char *base = base_url(&test);
    char *url = !test && tag && *tag ? g_strconcat(RELEASE_TAG_URL, tag, "/", name, NULL) : g_strconcat(base, name, NULL);
    g_remove(path);
    if (!http_download(url, path, test, done, NULL))
        done(0, NULL);
    g_free(url);
}

static const char *shown_version(void) { return *offer.version ? offer.version : offer.build; }

char *updater_version_text(void)
{
    char *build = g_strdup_printf(tr("Build %s"), DENBOKU_BUILD);
    if (!*DENBOKU_VERSION)
        return build;
    char *version = g_strdup_printf(tr("Version %s"), DENBOKU_VERSION), *t = g_strdup_printf("%s · %s", version, build);
    g_free(version), g_free(build);
    return t;
}

static void remove_tree(const char *path)
{
    if (g_file_test(path, G_FILE_TEST_IS_DIR) && !g_file_test(path, G_FILE_TEST_IS_SYMLINK)) {
        GDir *d = g_dir_open(path, 0, NULL);
        for (const char *name; d && (name = g_dir_read_name(d));) {
            char *child = g_build_filename(path, name, NULL);
            remove_tree(child);
            g_free(child);
        }
        if (d)
            g_dir_close(d);
        g_rmdir(path);
    } else
        g_remove(path);
}

static void failed(const char *why)
{
    state = FAILED;
    if (by_hand || why)
        toast(why ? why : tr("Could not check for updates."));
    by_hand = FALSE;
}

/* ---- installing ---- */

/* Where the Kindle's or the Linux folder's package is unpacked: beside the game, on the same disk, so the
 * swap is a rename. */
static char *staging(void)
{
    Way way;
    platform(&way);
    if (way == WAY_ZIP) /* Denboku.exe looks for it there */
        return g_build_filename(app.base, "update", NULL);
    char *parent = g_path_get_dirname(app.base), *dir = g_build_filename(parent, "denboku-update", NULL);
    g_free(parent);
    return dir;
}

/* The build being installed, kept so that one the start script had to undo is not installed again. */
static char *build_file(void) { return g_build_filename(app.home, "update-build", NULL); }

static void installed(void)
{
    state = READY;
    by_hand = FALSE;
    char *f = build_file();
    g_file_set_contents(f, offer.build, -1, NULL);
    g_free(f);
    /* Started again at once when nothing is lost by it (the game comes back from its autosave anyway). */
    if (setting_int("Updates", UPDATES_UNASKED) == UPDATES_AUTO && (app.game.count == 0 || app.game.result != RESULT_NONE) &&
        !drawer_open() && !app.net)
        updater_restart();
    else
        toast(tr("The new version is installed and runs the next time the game opens (About → Restart)."));
}

/* Every file of the new folder in place of the game's, the game's own kept in old/ until the new one
 * has started (the start script puts them back if it does not); settings, games and your own sets
 * are not in the package and stay. */
static gboolean swap_folder(const char *fresh)
{
    char *old = g_build_filename(app.base, "old", NULL), *mark = g_build_filename(app.base, ".update-pending", NULL);
    remove_tree(old);
    g_mkdir(old, 0755);
#ifndef G_OS_WIN32
    sync(); /* the unpacked files on the storage before any rename (the Kindle's is FAT, no journal) */
#endif
    /* The mark first: a swap cut short (power, a crash) is put back by the start script like a failed one. */
    gboolean ok = g_file_set_contents(mark, shown_version(), -1, NULL);
    GDir *d = ok ? g_dir_open(fresh, 0, NULL) : NULL;
    for (const char *name; ok && d && (name = g_dir_read_name(d));) {
        char *from = g_build_filename(fresh, name, NULL), *to = g_build_filename(app.base, name, NULL),
             *keep = g_build_filename(old, name, NULL);
        if (g_file_test(to, G_FILE_TEST_EXISTS) && g_rename(to, keep) != 0)
            ok = FALSE;
        else if (g_rename(from, to) != 0) {
            g_rename(keep, to);
            ok = FALSE;
        }
        g_free(from), g_free(to), g_free(keep);
    }
    if (d)
        g_dir_close(d);
    if (!ok) { /* back as it was; what does not go back stays in old/, with the mark, for the start script */
        gboolean back = TRUE;
        GDir *o = g_dir_open(old, 0, NULL);
        for (const char *name; o && (name = g_dir_read_name(o));) {
            char *from = g_build_filename(old, name, NULL), *to = g_build_filename(app.base, name, NULL);
            remove_tree(to);
            if (g_rename(from, to) != 0)
                back = FALSE;
            g_free(from), g_free(to);
        }
        if (o)
            g_dir_close(o);
        if (back) {
            remove_tree(old);
            g_remove(mark);
        }
    }
    g_free(old), g_free(mark);
    return ok;
}

/* The package unpacked into the staging folder (or not, when `unpacked` is FALSE): its folder in place. */
static void folder_unpacked(gboolean unpacked)
{
    char *dir = staging(), *fresh = NULL;
    GDir *d = g_dir_open(dir, 0, NULL);
    for (const char *name; d && (name = g_dir_read_name(d));) { /* the package's one folder: denboku/, denboku-linux/ */
        char *p = g_build_filename(dir, name, NULL), *bin = g_build_filename(p, "bin", NULL);
        if (!fresh && g_file_test(bin, G_FILE_TEST_IS_DIR))
            fresh = g_strdup(p);
        g_free(p), g_free(bin);
    }
    if (d)
        g_dir_close(d);
    if (unpacked && fresh && swap_folder(fresh))
        installed();
    else
        failed(tr("The update could not be installed; nothing was changed."));
    remove_tree(dir);
    g_free(dir), g_free(fresh);
}

static void unpacked_cb(void *data)
{
    (void) data;
    gboolean unpacked = proc_succeeded(tar); /* tar stopped short (a full disk) leaves a part of the package */
    proc_free(tar);
    tar = NULL;
    folder_unpacked(unpacked);
}

static void package_cb(int status, void *data)
{
    (void) data;
    Way way;
    platform(&way);
    if (status != 200 || !update_file_ok(package, &offer)) {
        g_remove(package);
        failed(tr("The update could not be downloaded."));
        return;
    }
    if (way == WAY_APPIMAGE) {
        const char *image = g_getenv("APPIMAGE");
        char *old = g_strconcat(image, ".old", NULL);
        g_chmod(package, 0755);
        gboolean ok = g_rename(image, old) == 0;
        if (ok && g_rename(package, image) != 0) {
            g_rename(old, image);
            ok = FALSE;
        }
        g_free(old);
        if (ok)
            installed();
        else
            failed(tr("The update could not be installed; nothing was changed."));
        return;
    }
#ifdef __ANDROID__
    if (way == WAY_APK) { /* the system asks, installs and closes the game; the game comes back from its autosave */
        autosave();
        state = AVAILABLE;
        by_hand = FALSE;
        android_install(package);
        return;
    }
#endif
    if (way == WAY_ZIP) { /* the zip holds the folder's files at its root: unpacked into update\new\ */
        char *dir = staging(), *into = g_build_filename(dir, "new", NULL), *fresh = NULL;
        Zip *z = zip_open(package);
        gboolean ok = z && zip_extract(z, into);
        zip_close(z);
        g_remove(package);
        char *exe = g_build_filename(into, "bin", "denboku.exe", NULL);
        if (ok && g_file_test(exe, G_FILE_TEST_EXISTS))
            fresh = g_strdup("new");
        g_free(exe), g_free(into);
        char *ready = g_build_filename(dir, "ready", NULL);
        if (fresh && g_file_set_contents(ready, fresh, -1, NULL))
            installed();
        else {
            remove_tree(dir);
            failed(tr("The update could not be installed; nothing was changed."));
        }
        g_free(ready), g_free(dir), g_free(fresh);
        return;
    }
    char *dir = staging();
    if (g_str_has_suffix(package, ".zip")) { /* the Kindle's: its denboku/ folder, unpacked here */
        Zip *z = zip_open(package);
        gboolean ok = z && zip_extract(z, dir);
        zip_close(z);
        g_remove(package);
        g_free(dir);
        folder_unpacked(ok);
        return;
    }
    /* o: no owners (root on the Kindle's FAT); J: the Linux folder's comes as .tar.xz. */
    char *argv[] = {"tar", g_str_has_suffix(package, ".xz") ? "xJof" : "xzof", package, "-C", dir, NULL};
    tar = proc_spawn(argv, NULL, NULL, unpacked_cb, NULL);
    if (!tar)
        failed(tr("The update could not be installed; nothing was changed."));
    g_free(dir);
}

static void install(void)
{
    Way way;
    platform(&way);
    if (way == WAY_NONE || state == DOWNLOADING)
        return;
    char *xz = g_str_has_suffix(offer.file, ".xz") ? g_find_program_in_path("xz") : g_strdup("");
    g_free(xz);
    if (!xz) { /* tar unpacks .tar.xz through it: every desktop system has it, a bare one may not */
        failed(tr("Updating needs the xz program; install it, or download the new version from GitHub."));
        return;
    }
    state = DOWNLOADING;
    g_free(package);
    if (way == WAY_APPIMAGE)
        package = g_strconcat(g_getenv("APPIMAGE"), ".new", NULL);
    else if (way == WAY_APK)
        package = g_build_filename(app.home, offer.file, NULL);
    else {
        char *dir = staging();
        remove_tree(dir);
        g_mkdir_with_parents(dir, 0755);
        package = g_build_filename(dir, offer.file, NULL);
        g_free(dir);
    }
    download(offer.file, offer.tag, package, package_cb);
}

/* ---- checking ---- */

static char *manifest_path(const char *suffix)
{
    char *name = g_strconcat("update.txt", suffix, NULL), *path = g_build_filename(app.home, name, NULL);
    g_free(name);
    return path;
}

static void signature_cb(int status, void *data)
{
    (void) data;
    char *text_path = manifest_path(""), *sig_path = manifest_path(".sig"), *text = NULL, *sig = NULL;
    gsize len = 0;
    Way way;
    const char *name = platform(&way);
    gboolean ok = status == 200 && name && g_file_get_contents(text_path, &text, &len, NULL) && g_file_get_contents(sig_path, &sig, NULL, NULL) &&
                  update_read(text, len, sig, KEYS, G_N_ELEMENTS(KEYS), name, &offer);
    g_remove(text_path), g_remove(sig_path);
    g_free(text_path), g_free(sig_path), g_free(text), g_free(sig);
    if (!ok) {
        failed(NULL);
        return;
    }
    char *skip = setting_string("UpdateSkip", "");
    gboolean skipped = !strcmp(skip, offer.build);
    g_free(skip);
    if (!update_newer(DENBOKU_BUILD, offer.build) || skipped) {
        state = LATEST;
        if (by_hand)
            toast(tr("Denboku Shōgi is up to date."));
        by_hand = FALSE;
        return;
    }
    state = AVAILABLE;
    if (setting_int("Updates", UPDATES_UNASKED) == UPDATES_AUTO && way != WAY_NONE && !by_hand)
        install();
    else {
        char *text = g_strdup_printf(tr("Version %s is available: ☰ → Help → About."), shown_version());
        toast(text);
        g_free(text);
    }
    by_hand = FALSE;
}

static void manifest_cb(int status, void *data)
{
    (void) data;
    if (status != 200) {
        failed(NULL);
        return;
    }
    char *path = manifest_path(".sig");
    download("update.txt.sig", NULL, path, signature_cb);
    g_free(path);
}

static void look(void)
{
    Way way;
    if (!platform(&way) || state == CHECKING || state == DOWNLOADING || state == READY)
        return;
    state = CHECKING;
    char *path = manifest_path("");
    download("update.txt", NULL, path, manifest_cb);
    g_free(path);
}

/* The choice saved; on Android, updates turned on ask now to allow installs from the game, not at the first update. */
static void choose(int choice)
{
    g_key_file_set_integer(app.settings, "Settings", "Updates", choice);
    save_settings();
#ifdef __ANDROID__
    if (choice == UPDATES_AUTO || choice == UPDATES_ASK)
        android_allow_install();
#endif
}

/* When the game starts, if the player wants it; never in a game over the network (its connection comes first). */
static void look_if_wanted(void)
{
    int choice = setting_int("Updates", UPDATES_UNASKED);
    /* Not while this run is trying an update just installed: it would find that one again. */
    if ((choice == UPDATES_AUTO || choice == UPDATES_ASK) && !pending_at_start && !app.net && !server_game())
        look();
}

static gboolean first_cb(gpointer data)
{
    (void) data;
    look_if_wanted();
    return FALSE;
}

/* ---- asking, starting and About ---- */

/* The first time, before the tutorial's offer (app.c calls both as soon as the board is drawn). */
void updater_ask(void)
{
    Way way;
    if (!platform(&way) || setting_int("Updates", UPDATES_UNASKED) != UPDATES_UNASKED || app.net || server_game())
        return;
    GtkWidget *d = dialog_new(tr("Updates"));
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox),
                       row_para(tr(way != WAY_NONE ? "Check GitHub for a new version of Denboku Shōgi each time the game starts? It can update "
                                                     "itself, or tell you first. Preferences → Online changes this."
                                                   : "Check GitHub for a new version of Denboku Shōgi each time the game starts, and tell "
                                                     "you when there is one? Preferences → Online changes this.")),
                       TRUE, TRUE, 12);
    gtk_dialog_add_button(GTK_DIALOG(d), tr("Don't check"), UPDATES_OFF);
    gtk_dialog_add_button(GTK_DIALOG(d), tr("Tell me"), UPDATES_ASK);
    if (way != WAY_NONE)
        gtk_dialog_add_button(GTK_DIALOG(d), tr("Auto-update"), UPDATES_AUTO);
    int r = run_dialog(d);
    if (r == UPDATES_OFF || r == UPDATES_ASK || r == UPDATES_AUTO) { /* closed unanswered: asked again next time */
        choose(r);
        look_if_wanted();
    }
}


static gboolean started_cb(gpointer data)
{
    (void) data;
    /* The new version opened and ran a while: the one before it is no longer needed. */
    char *mark = g_build_filename(app.base, ".update-pending", NULL), *old = g_build_filename(app.base, "old", NULL);
    if (pending_at_start && state != READY && g_file_test(mark, G_FILE_TEST_EXISTS)) {
        remove_tree(old);
        g_remove(mark);
        char *f = build_file();
        g_remove(f);
        g_free(f);
    }
    g_free(mark), g_free(old);
#ifdef __ANDROID__
    GDir *d = g_dir_open(app.home, 0, NULL); /* the APK the system installed */
    for (const char *name; d && (name = g_dir_read_name(d));)
        if (g_str_has_suffix(name, ".apk")) {
            char *apk = g_build_filename(app.home, name, NULL);
            g_remove(apk);
            g_free(apk);
        }
    if (d)
        g_dir_close(d);
#endif
    if (old_image_at_start && state != READY) {
        char *prev = g_strconcat(g_getenv("APPIMAGE"), ".old", NULL);
        g_remove(prev);
        g_free(prev);
    }
    return FALSE;
}

void updater_start(void)
{
    Way way;
    if (!platform(&way))
        return;
    char *mark = g_build_filename(app.base, ".update-pending", NULL);
    pending_at_start = g_file_test(mark, G_FILE_TEST_EXISTS);
    g_free(mark);
    if (g_getenv("APPIMAGE")) {
        char *prev = g_strconcat(g_getenv("APPIMAGE"), ".old", NULL);
        old_image_at_start = g_file_test(prev, G_FILE_TEST_EXISTS);
        g_free(prev);
    }
    g_timeout_add_seconds(30, started_cb, NULL); /* after Denboku.exe has stopped watching (15 s) */
    /* The start script put the version before back: that build is not offered again. */
    char *failed_mark = g_build_filename(app.base, ".update-failed", NULL), *f = build_file(), *build = NULL;
    if (g_file_test(failed_mark, G_FILE_TEST_EXISTS)) {
        if (g_file_get_contents(f, &build, NULL, NULL)) {
            g_key_file_set_string(app.settings, "Settings", "UpdateSkip", g_strstrip(build));
            save_settings();
        }
        g_remove(failed_mark), g_remove(f);
        toast(tr("The new version did not start, so the previous one was restored."));
    }
    g_free(failed_mark), g_free(f), g_free(build);
    if (setting_int("Updates", UPDATES_UNASKED) != UPDATES_UNASKED)
        g_timeout_add_seconds(10, first_cb, NULL);
}

/* The game again, from the new files; it comes back from its autosave. */
void updater_restart(void)
{
    autosave();
#ifdef G_OS_WIN32
    char *launcher = g_build_filename(app.base, "Denboku.exe", NULL);
    char *argv[] = {launcher, NULL};
    gboolean ok = g_spawn_async(app.base, argv, NULL, 0, NULL, NULL, NULL, NULL); /* not from bin\\, which it renames */
    g_free(launcher);
    if (!ok)
        return;
#else
    char *script = g_build_filename(app.base, app.kindle ? "launch.sh" : "denboku.sh", NULL);
    const char *image = g_getenv("APPIMAGE");
    char *argv[] = {image ? (char *) image : app.kindle ? "sh" : script, app.kindle ? script : NULL, NULL};
    gboolean ok = g_spawn_async(app.base, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
    g_free(script);
    if (!ok)
        return;
#endif
    engine_free(app.engine);
    exit(0);
}

static void about_cb(GtkButton *b, gpointer dialog)
{
    (void) b;
    if (state == READY)
        updater_restart();
    else if (state == AVAILABLE) {
        Way way;
        platform(&way);
        if (way != WAY_NONE) {
            by_hand = TRUE;
            install();
            toast(tr("Downloading the new version…"));
        } else
            open_url(*offer.notes ? offer.notes : RELEASE_URL);
    } else {
        by_hand = TRUE;
        look();
    }
    gtk_dialog_response(GTK_DIALOG(dialog), RESPONSE_UPDATER);
}

gboolean updater_offered(void)
{
    Way way;
    return platform(&way) != NULL;
}

/* About's and Preferences → Updates' line on updates, with what can be done about it. */
void updater_about(GtkWidget *dialog, GtkWidget *box)
{
    Way way;
    if (!platform(&way))
        return;
    char *text = state == CHECKING      ? g_strdup(tr("Looking for updates…"))
                 : state == LATEST      ? g_strdup(tr("Up to date"))
                 : state == AVAILABLE   ? g_strdup_printf(tr("Version %s is available"), shown_version())
                 : state == DOWNLOADING ? g_strdup_printf(tr("Downloading version %s…"), shown_version())
                 : state == READY       ? g_strdup_printf(tr("Version %s is installed"), shown_version())
                 : state == FAILED      ? g_strdup(tr("Could not check for updates."))
                                        : NULL;
    const char *button = state == READY                            ? tr("Restart")
                         : state == AVAILABLE && way != WAY_NONE   ? tr("Update")
                         : state == AVAILABLE                      ? tr("Download")
                         : state == CHECKING || state == DOWNLOADING ? NULL
                                                                   : tr("Look for updates");
    if (text) {
        GtkWidget *l = row_text(text, TRUE, FALSE, TRUE);
        gtk_misc_set_alignment(GTK_MISC(l), 0.5, 0.5);
        gtk_box_pack_start(GTK_BOX(box), l, FALSE, FALSE, 2);
    }
    if (button) {
        GtkWidget *row = gtk_hbox_new(FALSE, 0), *b = gtk_button_new_with_label(button);
        g_signal_connect(b, "clicked", G_CALLBACK(about_cb), dialog);
        gtk_box_pack_start(GTK_BOX(row), b, TRUE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 4);
    }
    g_free(text);
}

/* Preferences → Updates: Automatic, Tell me, Off (no first where the game cannot update itself). */
static int choice_index(Way way)
{
    int choice = setting_int("Updates", UPDATES_UNASKED);
    if (way == WAY_NONE)
        return choice == UPDATES_AUTO || choice == UPDATES_ASK ? 0 : 1;
    return choice == UPDATES_AUTO ? 0 : choice == UPDATES_ASK ? 1 : 2;
}

static const char *const CHOICES[] = {"Automatic", "Tell me", "Off"};

static void choice_cb(int i, gpointer data)
{
    (void) data;
    Way way;
    platform(&way);
    int choice = way != WAY_NONE ? (int[]){UPDATES_AUTO, UPDATES_ASK, UPDATES_OFF}[i] : (int[]){UPDATES_ASK, UPDATES_OFF}[i];
    choose(choice);
    if (choice != UPDATES_OFF)
        look_if_wanted();
}

GtkWidget *updater_choice(void)
{
    Way way;
    platform(&way);
    const char *labels[3];
    int n = way != WAY_NONE ? 3 : 2;
    for (int i = 0; i < n; i++)
        labels[i] = tr(CHOICES[i + 3 - n]);
    return row_chips(tr("Check for new versions"), labels, n, choice_index(way), rows_width(), choice_cb, NULL);
}

const char *updater_choice_label(void)
{
    Way way;
    platform(&way);
    return tr(CHOICES[choice_index(way) + (way != WAY_NONE ? 0 : 1)]);
}
