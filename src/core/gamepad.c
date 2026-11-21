/* Game controllers, read as the inputs Controls binds (PadInput). Linux: evdev devices
 * (/dev/input/event*, which logind lets the seat's user read for controllers), looked for again
 * every two seconds. Windows: XInput (Xbox controllers) and, when none is connected, winmm's
 * joystick API for other (DirectInput) controllers, polled. Android: the app reads them
 * (android/…/Pad.java) and android.c hands their bit set here. Each device's inputs are a bit set;
 * the changes go to controls_pad(). */

#include "app.h"

static char *pad_name;
static PadLayout pad_layout = LAYOUT_XBOX;

const char *gamepad_name(void) { return pad_name; }

/* No controller left: Preferences → Controls no longer names one. */
G_GNUC_UNUSED static void unplugged(void)
{
    g_free(pad_name);
    pad_name = NULL;
}
PadLayout gamepad_layout(void) { return pad_layout; }

/* The layout from the driver (Linux) or else the name. */
static void connected(const char *name, const char *driver)
{
    g_free(pad_name);
    pad_name = g_strdup(name);
    char *n = g_ascii_strdown(name, -1);
    if (!g_strcmp0(driver, "nintendo") || strstr(n, "nintendo") || strstr(n, "pro controller") || strstr(n, "joy-con"))
        pad_layout = LAYOUT_NINTENDO;
    else if (!g_strcmp0(driver, "playstation") || !g_strcmp0(driver, "sony") || strstr(n, "sony") ||
             strstr(n, "playstation") || strstr(n, "dualsense") || strstr(n, "dualshock") ||
             strstr(n, "wireless controller"))
        pad_layout = LAYOUT_PLAYSTATION;
    else
        pad_layout = LAYOUT_XBOX;
    g_free(n);
}

/* The inputs of one device went from `before` to `now`. */
/* Behind another window the controller plays for that one, not here: presses go only to the active
 * window (Android and the Kindle have one window, and Android's X server no window manager to say so);
 * releases always, so nothing stays held. */
static gboolean window_active(void)
{
#ifdef __ANDROID__
    return TRUE;
#else
    return app.kindle || !app.window || gtk_window_is_active(GTK_WINDOW(app.window));
#endif
}

static void report(guint32 before, guint32 now)
{
    gboolean active = window_active();
    for (int i = 0; i < PAD_COUNT; i++)
        if ((before ^ now) & (1u << i) && (active || !((now >> i) & 1)))
            controls_pad((PadInput) i, (now >> i) & 1);
}

#ifndef __ANDROID__ /* the app reads Android's sticks */
/* A stick at -1..1: the direction it is pushed past half way. */
static guint32 stick_bits(double x, double y)
{
    return (y < -0.5 ? 1u << PAD_STICK_UP : 0) | (y > 0.5 ? 1u << PAD_STICK_DOWN : 0) |
           (x < -0.5 ? 1u << PAD_STICK_LEFT : 0) | (x > 0.5 ? 1u << PAD_STICK_RIGHT : 0);
}
#endif

#if defined(__ANDROID__)

static guint32 android_bits;

void gamepad_android_name(const char *driver, const char *name)
{
    report(android_bits, 0);
    android_bits = 0;
    connected(name, driver);
}

void gamepad_android_bits(guint32 bits)
{
    report(android_bits, bits);
    android_bits = bits;
}

void gamepad_start(void) {}

void gamepad_rumble(double strong, double weak, int ms) { android_rumble(ms, (int) (strong * 255), (int) (weak * 255)); }

#elif defined(__linux__)

#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifndef BTN_DPAD_UP
#define BTN_DPAD_UP 0x220
#define BTN_DPAD_DOWN 0x221
#define BTN_DPAD_LEFT 0x222
#define BTN_DPAD_RIGHT 0x223
#endif

typedef struct {
    char *path;
    int fd;
    guint watch;
    guint32 bits;
    struct input_absinfo abs[ABS_CNT];
    double stick[2];
    /* The driver sends the top and left buttons by position (BTN_NORTH, BTN_WEST), as the kernel's
     * gamepad spec says; xpad sends the Xbox X and Y by their labels, with the same codes swapped. */
    gboolean positional;
    int rumble; /* the uploaded rumble effect's id, -1 before; -2: none (no motors, or read only) */
} Pad;

static GHashTable *seen; /* path → Pad, or NULL for a device that is not a controller */

static const struct { int code; PadInput input; } BUTTONS[] = {
    {BTN_A, PAD_A}, {BTN_B, PAD_B}, {BTN_X, PAD_X}, {BTN_Y, PAD_Y}, {BTN_TL, PAD_LB},
    {BTN_TR, PAD_RB}, {BTN_TL2, PAD_LT}, {BTN_TR2, PAD_RT}, {BTN_SELECT, PAD_BACK}, {BTN_START, PAD_START},
    {BTN_THUMBL, PAD_LS}, {BTN_THUMBR, PAD_RS}, {BTN_DPAD_UP, PAD_UP}, {BTN_DPAD_DOWN, PAD_DOWN},
    {BTN_DPAD_LEFT, PAD_LEFT}, {BTN_DPAD_RIGHT, PAD_RIGHT},
    /* Plain USB joysticks number their buttons from BTN_TRIGGER. */
    {BTN_TRIGGER, PAD_A}, {BTN_THUMB, PAD_B}, {BTN_THUMB2, PAD_X}, {BTN_TOP, PAD_Y}, {BTN_TOP2, PAD_LB},
    {BTN_PINKIE, PAD_RB}, {BTN_BASE, PAD_LT}, {BTN_BASE2, PAD_RT}, {BTN_BASE3, PAD_BACK}, {BTN_BASE4, PAD_START},
    {BTN_BASE5, PAD_LS}, {BTN_BASE6, PAD_RS},
};

static gboolean has_bit(const unsigned long *bits, int n)
{
    return (bits[n / (8 * sizeof(long))] >> (n % (8 * sizeof(long)))) & 1;
}

/* An axis at -1..1 (0..1 for one that rests at its minimum, like a trigger). */
static double axis(const Pad *p, int code, int value)
{
    const struct input_absinfo *a = &p->abs[code];
    if (a->maximum <= a->minimum)
        return 0;
    return 2.0 * (value - a->minimum) / (a->maximum - a->minimum) - 1;
}

static gboolean is_pad(gpointer key, gpointer value, gpointer data) { return (void) key, (void) data, value != NULL; }

static void forget(Pad *p)
{
    g_source_remove(p->watch);
    close(p->fd);
    report(p->bits, 0);
    g_hash_table_remove(seen, p->path);
    if (!g_hash_table_find(seen, is_pad, NULL))
        unplugged();
}

static void set_bit(guint32 *bits, PadInput in, gboolean on)
{
    *bits = on ? *bits | (1u << in) : *bits & ~(1u << in);
}

static gboolean read_cb(GIOChannel *ch, GIOCondition cond, gpointer data)
{
    (void) ch;
    Pad *p = data;
    struct input_event ev[32];
    ssize_t n = (cond & G_IO_IN) ? read(p->fd, ev, sizeof ev) : -1;
    if (n < 0 && errno == EAGAIN)
        return TRUE;
    if (n <= 0) {
        forget(p);
        return FALSE;
    }
    guint32 bits = p->bits;
    for (int i = 0; i < n / (ssize_t) sizeof ev[0]; i++) {
        struct input_event *e = &ev[i];
        if (e->type == EV_KEY) {
            for (size_t b = 0; b < G_N_ELEMENTS(BUTTONS); b++)
                if (BUTTONS[b].code == e->code) {
                    PadInput in = BUTTONS[b].input;
                    if (p->positional && (in == PAD_X || in == PAD_Y) && (e->code == BTN_X || e->code == BTN_Y))
                        in = in == PAD_X ? PAD_Y : PAD_X;
                    set_bit(&bits, in, e->value != 0);
                }
        } else if (e->type == EV_ABS) {
            switch (e->code) {
            case ABS_X: case ABS_Y:
                p->stick[e->code == ABS_Y] = axis(p, e->code, e->value);
                bits = (bits & ~stick_bits(-1, -1) & ~stick_bits(1, 1)) | stick_bits(p->stick[0], p->stick[1]);
                break;
            case ABS_RY: { /* the right stick, up and down only: it scrolls */
                double ry = axis(p, e->code, e->value);
                set_bit(&bits, PAD_RSTICK_UP, ry < -0.5);
                set_bit(&bits, PAD_RSTICK_DOWN, ry > 0.5);
                break;
            }
            case ABS_HAT0X:
                set_bit(&bits, PAD_LEFT, e->value < 0);
                set_bit(&bits, PAD_RIGHT, e->value > 0);
                break;
            case ABS_HAT0Y:
                set_bit(&bits, PAD_UP, e->value < 0);
                set_bit(&bits, PAD_DOWN, e->value > 0);
                break;
            case ABS_Z: case ABS_RZ: /* triggers (pressed past three quarters) */
                set_bit(&bits, e->code == ABS_Z ? PAD_LT : PAD_RT, axis(p, e->code, e->value) > 0.5);
                break;
            }
        }
    }
    report(p->bits, bits);
    p->bits = bits;
    return TRUE;
}

static void probe(const char *path)
{
    /* Writable when allowed, for the rumble motors. */
    int fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    gboolean writable = fd >= 0;
    if (fd < 0)
        fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    unsigned long keys[KEY_CNT / (8 * sizeof(long)) + 1] = {0};
    if (fd >= 0 && (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys) < 0 ||
                    (!has_bit(keys, BTN_GAMEPAD) && !has_bit(keys, BTN_JOYSTICK)))) {
        close(fd);
        fd = -1;
    }
    if (fd < 0) {
        g_hash_table_insert(seen, g_strdup(path), NULL);
        return;
    }
    Pad *p = g_new0(Pad, 1);
    p->path = g_strdup(path);
    p->fd = fd;
    unsigned long ff[FF_CNT / (8 * sizeof(long)) + 1] = {0};
    p->rumble = writable && ioctl(fd, EVIOCGBIT(EV_FF, sizeof ff), ff) >= 0 && has_bit(ff, FF_RUMBLE) ? -1 : -2;
    for (int a = 0; a < ABS_CNT; a++)
        ioctl(fd, EVIOCGABS(a), &p->abs[a]);
    char name[128] = "";
    ioctl(fd, EVIOCGNAME(sizeof name - 1), name);
    char *link = g_strdup_printf("/sys/class/input/%s/device/device/driver", strrchr(path, '/') + 1);
    char *target = g_file_read_link(link, NULL), *driver = target ? g_path_get_basename(target) : NULL;
    p->positional = driver && (!strcmp(driver, "playstation") || !strcmp(driver, "sony") || !strcmp(driver, "nintendo"));
    connected(*name ? name : "?", driver);
    g_free(link);
    g_free(target);
    g_free(driver);
    GIOChannel *ch = g_io_channel_unix_new(fd);
    p->watch = g_io_add_watch(ch, G_IO_IN | G_IO_HUP | G_IO_ERR, read_cb, p);
    g_io_channel_unref(ch);
    g_hash_table_insert(seen, g_strdup(path), p);
}

static gboolean gone(gpointer key, gpointer value, gpointer data)
{
    (void) data;
    return !value && !g_file_test(key, G_FILE_TEST_EXISTS);
}

static gboolean scan_cb(gpointer data)
{
    (void) data;
    g_hash_table_foreach_remove(seen, gone, NULL);
    GDir *dir = g_dir_open("/dev/input", 0, NULL);
    const char *name;
    while (dir && (name = g_dir_read_name(dir))) {
        if (!g_str_has_prefix(name, "event"))
            continue;
        char *path = g_build_filename("/dev/input", name, NULL);
        if (!g_hash_table_lookup_extended(seen, path, NULL, NULL))
            probe(path);
        g_free(path);
    }
    if (dir)
        g_dir_close(dir);
    return TRUE;
}

static void free_pad(gpointer data)
{
    Pad *p = data;
    if (p) {
        g_free(p->path);
        g_free(p);
    }
}

static void rumble_one(gpointer key, gpointer value, gpointer data)
{
    (void) key;
    Pad *p = value;
    const double *a = data;
    if (!p || p->fd < 0 || p->rumble == -2)
        return;
    struct ff_effect e = {.type = FF_RUMBLE, .id = (__s16) p->rumble};
    e.u.rumble.strong_magnitude = (__u16) (a[0] * 0xffff);
    e.u.rumble.weak_magnitude = (__u16) (a[1] * 0xffff);
    e.replay.length = (__u16) a[2];
    if (ioctl(p->fd, EVIOCSFF, &e) < 0) {
        p->rumble = -2;
        return;
    }
    p->rumble = e.id;
    struct input_event play = {.type = EV_FF, .code = (__u16) e.id, .value = 1};
    if (write(p->fd, &play, sizeof play) < 0)
        p->rumble = -2;
}

void gamepad_rumble(double strong, double weak, int ms)
{
    double a[3] = {CLAMP(strong, 0, 1), CLAMP(weak, 0, 1), ms};
    if (seen)
        g_hash_table_foreach(seen, rumble_one, a);
}

void gamepad_start(void)
{
    seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, free_pad);
    scan_cb(NULL);
    g_timeout_add_seconds(2, scan_cb, NULL);
}

#elif defined(G_OS_WIN32)

#include <mmsystem.h>

typedef struct {
    WORD buttons;
    BYTE left_trigger, right_trigger;
    SHORT lx, ly, rx, ry;
} XInputPad;
typedef struct {
    DWORD packet;
    XInputPad pad;
} XInputState;
typedef DWORD(WINAPI *XInputGetStateFn)(DWORD, XInputState *);
typedef struct {
    WORD left, right;
} XInputVibration;
typedef DWORD(WINAPI *XInputSetStateFn)(DWORD, XInputVibration *);

#define NJOY 16
static XInputGetStateFn xinput_get;
static XInputSetStateFn xinput_set;
static guint rumble_stop;
static guint32 xbits[4], jbits[NJOY];
static gboolean xon[4], jon[NJOY];
static int ticks;

static guint32 xinput_bits(const XInputPad *p)
{
    static const struct { WORD mask; PadInput input; } MAP[] = {
        {0x0001, PAD_UP}, {0x0002, PAD_DOWN}, {0x0004, PAD_LEFT}, {0x0008, PAD_RIGHT}, {0x0010, PAD_START},
        {0x0020, PAD_BACK}, {0x0040, PAD_LS}, {0x0080, PAD_RS}, {0x0100, PAD_LB}, {0x0200, PAD_RB},
        {0x1000, PAD_A}, {0x2000, PAD_B}, {0x4000, PAD_X}, {0x8000, PAD_Y},
    };
    guint32 bits = stick_bits(p->lx / 32768.0, -p->ly / 32768.0);
    if (p->ry > 16384)
        bits |= 1u << PAD_RSTICK_UP;
    else if (p->ry < -16384)
        bits |= 1u << PAD_RSTICK_DOWN;
    for (size_t i = 0; i < G_N_ELEMENTS(MAP); i++)
        if (p->buttons & MAP[i].mask)
            bits |= 1u << MAP[i].input;
    if (p->left_trigger > 128)
        bits |= 1u << PAD_LT;
    if (p->right_trigger > 128)
        bits |= 1u << PAD_RT;
    return bits;
}

/* Buttons 1–12 as an Xbox controller's A B X Y LB RB LT RT Back Start LS RS, the hat as the d-pad. */
static guint32 joystick_bits(const JOYINFOEX *j)
{
    static const PadInput ORDER[] = {PAD_A, PAD_B, PAD_X, PAD_Y, PAD_LB, PAD_RB,
                                     PAD_LT, PAD_RT, PAD_BACK, PAD_START, PAD_LS, PAD_RS};
    guint32 bits = stick_bits(j->dwXpos / 32767.5 - 1, j->dwYpos / 32767.5 - 1);
    double ry = j->dwRpos / 32767.5 - 1; /* the right stick's vertical axis on most of them */
    bits |= (ry < -0.5 ? 1u << PAD_RSTICK_UP : 0) | (ry > 0.5 ? 1u << PAD_RSTICK_DOWN : 0);
    for (size_t i = 0; i < G_N_ELEMENTS(ORDER); i++)
        if (j->dwButtons & (1u << i))
            bits |= 1u << ORDER[i];
    DWORD pov = j->dwPOV;
    if (pov != JOY_POVCENTERED) {
        if (pov > 27000 || pov < 9000) bits |= 1u << PAD_UP;
        if (pov > 0 && pov < 18000) bits |= 1u << PAD_RIGHT;
        if (pov > 9000 && pov < 27000) bits |= 1u << PAD_DOWN;
        if (pov > 18000) bits |= 1u << PAD_LEFT;
    }
    return bits;
}

static gboolean fast; /* polling every 33 ms while a controller is there (500 ms in the background); else every two seconds */

static gboolean poll_cb(gpointer data)
{
    (void) data;
    /* Asking for a controller that is not there is slow: those only every two seconds. */
    gboolean probe = !fast || ticks++ % 60 == 0, any = FALSE, joy = FALSE;
    for (DWORD i = 0; xinput_get && i < 4; i++) {
        if (!xon[i] && !probe)
            continue;
        XInputState s;
        gboolean on = xinput_get(i, &s) == ERROR_SUCCESS;
        guint32 bits = on ? xinput_bits(&s.pad) : 0;
        if (on && !xon[i])
            connected("Xbox controller (XInput)", NULL);
        report(xbits[i], bits);
        xbits[i] = bits, xon[i] = on, any |= on;
    }
    /* XInput controllers show up in winmm too: only the others from there. */
    for (UINT i = 0; i < NJOY; i++) {
        if (any || (!jon[i] && !probe)) {
            report(jbits[i], 0);
            jbits[i] = 0;
            continue;
        }
        JOYINFOEX j = {0};
        j.dwSize = sizeof j, j.dwFlags = JOY_RETURNALL;
        gboolean on = joyGetPosEx(i, &j) == JOYERR_NOERROR;
        guint32 bits = on ? joystick_bits(&j) : 0;
        if (on && !jon[i]) {
            JOYCAPSA caps;
            connected(joyGetDevCapsA(i, &caps, sizeof caps) == JOYERR_NOERROR ? caps.szPname : "?", NULL);
        }
        report(jbits[i], bits);
        jbits[i] = bits, jon[i] = on, joy |= on;
    }
    if ((any || joy) != fast) {
        fast = any || joy;
        if (!fast)
            unplugged();
    }
    /* Behind another window (presses go nowhere), twice a second is enough to see it come back. */
    static guint every = 2000;
    guint want = !fast ? 2000 : window_active() ? 33 : 500;
    if (want != every) {
        every = want;
        g_timeout_add(every, poll_cb, NULL);
        return FALSE;
    }
    return TRUE;
}

static void set_motors(double strong, double weak)
{
    XInputVibration v = {(WORD) (CLAMP(strong, 0, 1) * 65535), (WORD) (CLAMP(weak, 0, 1) * 65535)};
    for (DWORD i = 0; i < 4; i++)
        if (xon[i])
            xinput_set(i, &v);
}

static gboolean rumble_stop_cb(gpointer data)
{
    (void) data;
    rumble_stop = 0;
    set_motors(0, 0);
    return FALSE;
}

/* XInput controllers only: winmm's joystick API has no motors. */
void gamepad_rumble(double strong, double weak, int ms)
{
    if (!xinput_set)
        return;
    set_motors(strong, weak);
    if (rumble_stop)
        g_source_remove(rumble_stop);
    rumble_stop = g_timeout_add(ms, rumble_stop_cb, NULL);
}

void gamepad_start(void)
{
    const char *dlls[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
    for (size_t i = 0; i < G_N_ELEMENTS(dlls) && !xinput_get; i++) {
        HMODULE m = LoadLibraryA(dlls[i]);
        if (m) {
            xinput_get = (XInputGetStateFn) (void (*)(void)) GetProcAddress(m, "XInputGetState");
            xinput_set = (XInputSetStateFn) (void (*)(void)) GetProcAddress(m, "XInputSetState");
        }
    }
    g_timeout_add(2000, poll_cb, NULL);
}

#else

void gamepad_start(void) {}

void gamepad_rumble(double strong, double weak, int ms) { (void) strong, (void) weak, (void) ms; }

#endif
