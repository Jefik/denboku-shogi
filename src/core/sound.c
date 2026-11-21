/* Sound effects and the sound chooser. */

#include "app.h"

static void play_file(const char *path);

/* Sounds (data/sounds, and the user's own in custom/sounds/ of the data folder): every sound comes from
 * the chosen set, sounds/<set>/; one the set lacks, as one a later version adds, from the first
 * built-in set. On by default on computers, off (Silent) on the Kindle, which has no speaker but
 * can play to Bluetooth headphones. */
static gboolean sounds_on(void) { return setting_int("Sounds", !app.kindle); }

static char *sets_dir(gboolean user) { return user ? g_strdup(app.custom_sounds) : g_build_filename(app.base, "sounds", NULL); }

/* By the number before the name ("11-Clappers" after "9-Birds"), then by name. */
static gint compare_sets(gconstpointer a, gconstpointer b)
{
    const char *x = *(char *const *) a, *y = *(char *const *) b;
    long nx = strtol(x, NULL, 10), ny = strtol(y, NULL, 10);
    return nx != ny ? (nx < ny ? -1 : 1) : strcmp(x, y);
}

/* The sets in a sounds/ folder, in their numbers' order. */
static GPtrArray *sets_in(gboolean user)
{
    char *dir = sets_dir(user);
    GDir *d = g_dir_open(dir, 0, NULL);
    GPtrArray *sets = g_ptr_array_new_with_free_func(g_free);
    for (const char *name; d && (name = g_dir_read_name(d));) {
        char *full = g_build_filename(dir, name, NULL);
        if (g_file_test(full, G_FILE_TEST_IS_DIR))
            g_ptr_array_add(sets, g_strdup(name));
        g_free(full);
    }
    if (d)
        g_dir_close(d);
    g_free(dir);
    g_ptr_array_sort(sets, compare_sets);
    return sets;
}

static gboolean set_exists(const char *set)
{
    if (!set || !*set)
        return FALSE;
    for (int user = 0; user < 2; user++) {
        char *sets = sets_dir(user), *dir = g_build_filename(sets, set, NULL);
        g_free(sets);
        gboolean ok = g_file_test(dir, G_FILE_TEST_IS_DIR);
        g_free(dir);
        if (ok)
            return TRUE;
    }
    return FALSE;
}

/* The sets of the game and then the user's, each in name order ("2-Pachi" shows as "Pachi"). */
GPtrArray *sound_sets(void)
{
    GPtrArray *sets = sets_in(FALSE), *own = sets_in(TRUE);
    for (guint i = 0; i < own->len; i++) {
        gboolean have = FALSE;
        for (guint k = 0; k < sets->len; k++)
            have |= !strcmp(g_ptr_array_index(own, i), g_ptr_array_index(sets, k));
        if (!have)
            g_ptr_array_add(sets, g_strdup(g_ptr_array_index(own, i)));
    }
    g_ptr_array_unref(own);
    return sets;
}

/* The set's name without its order prefix ("2-Pachi": Pachi). */
const char *sound_set_name(const char *set)
{
    const char *digits = set;
    while (g_ascii_isdigit(*digits))
        digits++;
    return tr(digits > set && *digits == '-' ? digits + 1 : set);
}

/* The file of a sound in `set`: the user's set, the game's set of that name, else the same sound
 * of the first set of the game. */
static char *sound_path(const char *set, const char *file)
{
    char *path = NULL;
    for (int user = 1; user >= 0 && set && *set; user--) {
        char *sets = sets_dir(user);
        path = g_build_filename(sets, set, file, NULL);
        g_free(sets);
        if (g_file_test(path, G_FILE_TEST_IS_REGULAR))
            return path;
        g_free(path);
    }
    GPtrArray *builtin = sets_in(FALSE);
    path = g_build_filename(app.base, "sounds", builtin->len ? (char *) g_ptr_array_index(builtin, 0) : "", file, NULL);
    g_ptr_array_unref(builtin);
    return path;
}

/* The set chosen for all variants; with none chosen (or a set that is gone), the first one listed. */
static char *all_sound_set(void)
{
    char *set = setting_string("SoundSet", "");
    if (set_exists(set))
        return set;
    g_free(set);
    GPtrArray *sets = sound_sets();
    set = g_strdup(sets->len ? g_ptr_array_index(sets, 0) : "");
    g_ptr_array_unref(sets);
    return set;
}

/* A variant's own sound set ("SoundSet.Dobutsu", else its default), or NULL for the same as all. */
char *variant_sound_set(int variant)
{
    char *key = variant_key("SoundSet", variant), *set = g_key_file_get_string(app.settings, "Settings", key, NULL);
    g_free(key);
    if (!set)
        set = g_strdup(variant_sound_default(variant));
    if (set && !set_exists(set)) {
        g_free(set);
        set = NULL;
    }
    return set;
}

/* The set of the variant being played. */
static char *sound_set(void)
{
    char *set = variant_sound_set(rules_variant);
    return set ? set : all_sound_set();
}

/* Vibration with the sounds of the game (not the clock's ticks): the controller's when it is in
 * use, else the phone's. */
static void vibrate(const char *name)
{
    static const struct { const char *name; double strength; int ms; } FEEL[] = {
        {"move", 0.25, 15}, {"capture", 0.45, 30}, {"check", 0.8, 70}, {"start", 0.4, 40},
        {"notify", 0.5, 50}, {"wrong", 0.8, 110}, {"win", 0.7, 150}, {"lose", 0.9, 250}, {"draw", 0.5, 120},
    };
    if (!setting_int("Vibration", 1))
        return;
    for (size_t i = 0; i < G_N_ELEMENTS(FEEL); i++)
        if (!strcmp(name, FEEL[i].name)) {
            if (controls_pad_used())
                gamepad_rumble(FEEL[i].strength, FEEL[i].strength, FEEL[i].ms * 2);
#ifdef __ANDROID__
            else
                android_vibrate(FEEL[i].ms, (int) (FEEL[i].strength * 255));
#endif
            return;
        }
}

/* A short tick for a button, a tap or a step of the controller's cursor (with Vibration). */
void feedback_tick(void)
{
    if (!setting_int("Vibration", 1) || app.kindle)
        return;
    if (controls_pad_used())
        gamepad_rumble(0.15, 0.15, 25);
#ifdef __ANDROID__
    else
        android_vibrate(10, 70);
#endif
}

void play_sound(const char *name)
{
    vibrate(name);
    if (g_getenv("DENBOKU_DEBUG"))
        g_printerr("sound %s\n", name);
    if (!sounds_on())
        return;
    char *file = g_strconcat(name, ".wav", NULL), *set = sound_set();
    char *path = sound_path(set, file);
    play_file(path);
    g_free(path);
    g_free(set);
    g_free(file);
}

int sound_volume(void) { return CLAMP(setting_int("Volume", 100), 0, 100); }

char *sound_label(void)
{
    if (!sounds_on())
        return g_strdup(tr("Silent"));
    char *set = all_sound_set(), *label = g_strdup(sound_set_name(set));
    g_free(set);
    return label;
}

/* Where the "fmt " body and the "data" chunk of a RIFF WAV are (0 when absent); each chunk's size
 * is checked against what is left before stepping over it, so a bogus one (a set of your own)
 * cannot wrap the arithmetic on a 32-bit device. */
static void wav_chunks(const char *wav, gsize len, gsize *fmt, gsize *data)
{
    *fmt = *data = 0;
    if (!wav || len < 12 || memcmp(wav, "RIFF", 4))
        return;
    for (gsize at = 12; len - at >= 8;) {
        guint32 size = GUINT32_FROM_LE(*(const guint32 *) (wav + at + 4));
        if (!memcmp(wav + at, "fmt ", 4) && size >= 16 && len - at - 8 >= 16)
            *fmt = at + 8;
        else if (!memcmp(wav + at, "data", 4)) {
            *data = at;
            return;
        }
        gsize step = 8 + (gsize) size + (size & 1);
        if (step > len - at)
            return;
        at += step;
    }
}

/* The WAV file at `path` with its 16-bit samples scaled by `volume` (0-100); NULL if it is not
 * one. The players have no common volume option, so the sound itself is made quieter. */
static char *scaled_wav(const char *path, int volume, gsize *len)
{
    char *data;
    if (!g_file_get_contents(path, &data, len, NULL))
        return NULL;
    gsize fmt, at;
    wav_chunks(data, *len, &fmt, &at);
    if (!at) {
        g_free(data);
        return NULL;
    }
    for (char *p = data + at + 8; p + 1 < data + *len; p += 2) {
        gint16 v = (gint16) GUINT16_FROM_LE(*(guint16 *) p);
        *(guint16 *) p = GUINT16_TO_LE((guint16) (gint16) (v * volume / 100));
    }
    return data;
}

#if !defined(G_OS_WIN32) && !defined(__ANDROID__) && !defined(DENBOKU_KINDLE) /* the Kindle's GLib is older than its headers */
#define PULSE
/* PulseAudio (or PipeWire's) straight from the game, as a player program took about 100 ms to start:
 * libpulse-simple when the system has it, one connection on a thread of its own (a sound then
 * starts within 30 ms; connecting takes over 100), closed after five quiet minutes. */
#include "mixer.h"
#include <gmodule.h>

typedef struct {
    int format; /* PA_SAMPLE_S16LE */
    guint32 rate;
    guint8 channels;
} PaSpec;
typedef struct {
    guint32 maxlength, tlength, prebuf, minreq, fragsize;
} PaAttr;
static void *(*pa_new)(const char *, const char *, int, const char *, const char *, const PaSpec *, const void *,
                       const PaAttr *, int *);
static int (*pa_write)(void *, const void *, size_t, int *);
static void (*pa_free)(void *);
static GAsyncQueue *pa_queue;
static gint pa_ok = -1; /* not tried yet */

/* Written 10 ms at a time, so that a new sound takes over at once (moves in quick succession, a replay,
 * engines playing each other) instead of waiting behind the one playing (mixer.c). */
static gpointer pa_thread(gpointer data)
{
    (void) data;
    void *out = NULL;
    Mixer m = {0};
    for (;;) {
        Clip *c = mixer_busy(&m) ? g_async_queue_try_pop(pa_queue)
                  : out          ? g_async_queue_timeout_pop(pa_queue, 300 * G_USEC_PER_SEC)
                                 : g_async_queue_pop(pa_queue);
        if (c)
            mixer_take(&m, c);
        else if (!mixer_busy(&m)) { /* five quiet minutes */
            pa_free(out);
            out = NULL;
            continue;
        }
        if (!out) {
            PaSpec spec = {3, MIX_RATE, 1};
            PaAttr attr = {G_MAXUINT32, MIX_RATE * 2 * 30 / 1000, G_MAXUINT32, G_MAXUINT32, G_MAXUINT32}; /* 30 ms buffered */
            out = pa_new(NULL, "Denboku Shōgi", 1, NULL, "sound", &spec, NULL, &attr, NULL);
            if (!out) {
                g_atomic_int_set(&pa_ok, 0); /* no sound server: the player programs from now on */
                mixer_clear(&m);
                continue;
            }
        }
        gint16 chunk[MIX_RATE / 100];
        mixer_fill(&m, chunk, G_N_ELEMENTS(chunk));
        pa_write(out, chunk, sizeof chunk, NULL);
    }
    return NULL;
}

/* Queued for the sound thread; FALSE when there is no libpulse-simple or no server. */
static gboolean pa_play(const char *path, int volume)
{
    if (g_atomic_int_get(&pa_ok) < 0) {
        GModule *m = g_module_open("libpulse-simple.so.0", G_MODULE_BIND_LAZY);
        gboolean ok = m && g_module_symbol(m, "pa_simple_new", (gpointer *) &pa_new) &&
                      g_module_symbol(m, "pa_simple_write", (gpointer *) &pa_write) &&
                      g_module_symbol(m, "pa_simple_free", (gpointer *) &pa_free);
        if (ok) {
            pa_queue = g_async_queue_new();
            g_thread_unref(g_thread_new("sound", pa_thread, NULL));
        }
        g_atomic_int_set(&pa_ok, ok);
    }
    if (!g_atomic_int_get(&pa_ok))
        return FALSE;
    gsize len;
    char *wav = scaled_wav(path, volume, &len);
    gsize fmt_at, data_at;
    wav_chunks(wav, len, &fmt_at, &data_at);
    const char *fmt = wav + fmt_at, *data = wav + data_at;
    if (!fmt_at || !data_at || GUINT16_FROM_LE(*(const guint16 *) (fmt + 14)) != 16) {
        g_free(wav);
        return TRUE; /* not a 16-bit WAV: nothing to play */
    }
    int channels = MAX(1, GUINT16_FROM_LE(*(const guint16 *) (fmt + 2)));
    gsize at = (gsize) (data + 8 - wav);
    Clip *clip = clip_new((const guint8 *) wav + at, MIN(GUINT32_FROM_LE(*(const guint32 *) (data + 4)), len - at) / 2 / channels,
                          channels, GUINT32_FROM_LE(*(const guint32 *) (fmt + 4)));
    g_free(wav);
    g_async_queue_push(pa_queue, clip);
    return TRUE;
}
#endif

#ifdef G_OS_WIN32
/* The sound device kept open (waveOut): PlaySound opened it again for every sound, which was
 * heard a moment after the move. A new sound stops the one playing, whose copy can then go. */
static void wave_play(char *wav, gsize len)
{
    static HWAVEOUT out;
    static WAVEFORMATEX open_fmt;
    static WAVEHDR hdr;
    static char *playing;
    gsize fmt_at, data_at;
    wav_chunks(wav, len, &fmt_at, &data_at);
    const char *fmt = wav + fmt_at, *data = wav + data_at;
    if (!fmt_at || !data_at || GUINT16_FROM_LE(*(const guint16 *) (fmt + 14)) != 16) {
        g_free(wav);
        return;
    }
    WAVEFORMATEX f = {WAVE_FORMAT_PCM, GUINT16_FROM_LE(*(const guint16 *) (fmt + 2)), GUINT32_FROM_LE(*(const guint32 *) (fmt + 4)), 0, 0, 16, 0};
    f.nBlockAlign = (WORD) (f.nChannels * 2);
    f.nAvgBytesPerSec = f.nSamplesPerSec * f.nBlockAlign;
    if (out) {
        waveOutReset(out);
        if (hdr.dwFlags & WHDR_PREPARED)
            waveOutUnprepareHeader(out, &hdr, sizeof hdr);
    }
    if (out && (open_fmt.nChannels != f.nChannels || open_fmt.nSamplesPerSec != f.nSamplesPerSec)) {
        waveOutClose(out);
        out = NULL;
    }
    if (!out && waveOutOpen(&out, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
        out = NULL;
    open_fmt = f;
    g_free(playing);
    playing = wav;
    if (!out)
        return;
    memset(&hdr, 0, sizeof hdr);
    hdr.lpData = (LPSTR) data + 8;
    hdr.dwBufferLength = (DWORD) MIN(GUINT32_FROM_LE(*(const guint32 *) (data + 4)), (gsize) (wav + len - data - 8));
    if (waveOutPrepareHeader(out, &hdr, sizeof hdr) == MMSYSERR_NOERROR)
        waveOutWrite(out, &hdr, sizeof hdr);
}
#endif

static void play_file(const char *path)
{
    int volume = sound_volume();
    if (!volume)
        return;
#ifdef G_OS_WIN32
    gsize len;
    char *wav = scaled_wav(path, volume, &len);
    if (wav)
        wave_play(wav, len);
#elif defined(__ANDROID__)
    (void) scaled_wav; /* the app plays it at that volume */
    android_play(path, volume);
#else
#ifdef PULSE
    if (pa_play(path, volume))
        return;
#endif
    /* The Kindle may lack ALSA's aplay but has GStreamer 0.10. */
    static char *player;
    static const char *players[] = {"paplay", "aplay", "gst-launch-1.0", "gst-launch-0.10"};
    for (size_t i = 0; !player && i < G_N_ELEMENTS(players); i++)
        player = g_find_program_in_path(players[i]);
    if (!player)
        player = g_strdup("");
    if (!*player)
        return;
    /* Below full volume the player gets a quieter copy, kept in the temporary folder. */
    char *copy = NULL;
    if (volume < 100) {
        char *dir = g_build_filename(g_get_tmp_dir(), "denboku-sounds", NULL), name[48];
        snprintf(name, sizeof name, "%u-%d.wav", g_str_hash(path), volume);
        copy = g_build_filename(dir, name, NULL);
        gsize len;
        char *wav;
        if (!g_file_test(copy, G_FILE_TEST_EXISTS) && (wav = scaled_wav(path, volume, &len))) {
            g_mkdir_with_parents(dir, 0700);
            g_file_set_contents(copy, wav, (gssize) len, NULL);
            g_free(wav);
        }
        g_free(dir);
        if (g_file_test(copy, G_FILE_TEST_EXISTS))
            path = copy;
    }
    char *uri = g_filename_to_uri(path, NULL, NULL), *prop = g_strconcat("uri=", uri, NULL);
    char *argv[] = {player, "-q", (char *) path, NULL, NULL};
    if (g_str_has_suffix(player, "paplay"))
        argv[1] = (char *) path, argv[2] = NULL;
    else if (strstr(player, "gst-launch"))
        argv[2] = strstr(player, "0.10") ? "playbin2" : "playbin", argv[3] = prop;
    g_spawn_async(NULL, argv, NULL, G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL);
    g_free(prop);
    g_free(uri);
    g_free(copy);
#endif
}

/* ---- the sound chooser (Preferences → Sound) ---- */

static gboolean play_capture_later(gpointer set)
{
    char *path = sound_path(set, "capture.wav");
    play_file(path);
    g_free(path);
    g_free(set);
    return FALSE;
}

/* A move, then a capture, of `set`; NULL for the set of all variants (nothing when sounds are off). */
void sound_preview(const char *set)
{
    char *own = set ? g_strdup(set) : sounds_on() ? all_sound_set() : NULL;
    if (!own)
        return;
    char *path = sound_path(own, "move.wav");
    play_file(path);
    g_free(path);
    g_timeout_add(700, play_capture_later, own);
}

/* The set chosen in `combo` (nothing for Silent). */
static void sound_try_cb(GtkWidget *w, gpointer combo)
{
    (void) w;
    GPtrArray *sets = g_object_get_data(G_OBJECT(combo), "sets");
    int i = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
    if (i > 0)
        sound_preview(g_ptr_array_index(sets, i - 1));
}

static void sound_pick_cb(GtkComboBox *combo, gpointer data)
{
    (void) data;
    GPtrArray *sets = g_object_get_data(G_OBJECT(combo), "sets");
    int i = gtk_combo_box_get_active(combo);
    g_key_file_set_integer(app.settings, "Settings", "Sounds", i > 0);
    if (i > 0)
        g_key_file_set_string(app.settings, "Settings", "SoundSet", g_ptr_array_index(sets, i - 1));
    save_settings();
    sound_try_cb(NULL, combo);
}

/* Silent or one of the sets (the game's and the user's, in name order; "2-Pachi" shows as "Pachi"), with ▶ to hear
 * it. A choice is saved at once. */
GtkWidget *sound_chooser(void)
{
    char *current = all_sound_set();
    GPtrArray *sets = sound_sets();
    GtkWidget *combo = gtk_combo_box_new_text();
    close_popup_on_leave(combo);
    gtk_combo_box_append_text(GTK_COMBO_BOX(combo), tr("Silent"));
    int active = 0;
    for (guint i = 0; i < sets->len; i++) {
        const char *set = g_ptr_array_index(sets, i);
        gtk_combo_box_append_text(GTK_COMBO_BOX(combo), sound_set_name(set));
        if (sounds_on() && !strcmp(set, current))
            active = (int) i + 1;
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), active);
    g_object_set_data_full(G_OBJECT(combo), "sets", sets, (GDestroyNotify) g_ptr_array_unref);
    g_signal_connect(combo, "changed", G_CALLBACK(sound_pick_cb), NULL);
    GtkWidget *play = gtk_button_new_with_label("▶");
    g_signal_connect(play, "clicked", G_CALLBACK(sound_try_cb), combo);
    GtkWidget *box = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(box), combo, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), play, FALSE, FALSE, 0);
    g_object_set_data(G_OBJECT(box), "combo", combo);
    g_free(current);
    return box;
}

static gboolean volume_try_cb(gpointer combo)
{
    g_object_set_data(G_OBJECT(combo), "volume-timer", NULL);
    sound_try_cb(NULL, combo);
    return FALSE;
}

/* − and +, ten at a time: saved at once; the sound plays when the value rests a moment. */
static void volume_step_cb(GtkButton *b, gpointer value)
{
    GObject *combo = g_object_get_data(G_OBJECT(value), "combo");
    int v = CLAMP(sound_volume() + GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "step")), 0, 100);
    g_key_file_set_integer(app.settings, "Settings", "Volume", v);
    save_settings();
    char text[16];
    snprintf(text, sizeof text, "%d%%", v);
    gtk_label_set_text(GTK_LABEL(value), text);
    guint timer = GPOINTER_TO_UINT(g_object_get_data(combo, "volume-timer"));
    if (timer)
        g_source_remove(timer);
    timer = g_timeout_add_full(G_PRIORITY_DEFAULT, 300, volume_try_cb, g_object_ref(combo), g_object_unref);
    g_object_set_data(combo, "volume-timer", GUINT_TO_POINTER(timer));
}

/* The volume of every sound, 0-100 %, for the chooser `sounds` (from sound_chooser()): a row
 * with − and +, which fits any screen (a slider made the Kindle's page scroll sideways). */
GtkWidget *volume_row(GtkWidget *sounds)
{
    GtkWidget *value, *minus, *plus, *row = row_stepper(tr("Volume"), &value, &minus, &plus);
    g_object_set_data(G_OBJECT(value), "combo", g_object_get_data(G_OBJECT(sounds), "combo"));
    g_object_set_data(G_OBJECT(minus), "step", GINT_TO_POINTER(-10));
    g_object_set_data(G_OBJECT(plus), "step", GINT_TO_POINTER(10));
    g_signal_connect(minus, "clicked", G_CALLBACK(volume_step_cb), value);
    g_signal_connect(plus, "clicked", G_CALLBACK(volume_step_cb), value);
    char text[16];
    snprintf(text, sizeof text, "%d%%", sound_volume());
    gtk_label_set_text(GTK_LABEL(value), text);
    return row;
}
