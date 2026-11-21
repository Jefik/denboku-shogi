/* Copies the X server's screen (an XWD file that Xvfb keeps mapped, -fbdir) to the app's Surface.
 * Xvfb keeps it in memory (fbshim.c) and hands it over a local socket; the file is the fallback. */

#include <android/native_window_jni.h>
#include <fcntl.h>
#include <jni.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define SOCKET_NAME "jp.denboku.fb" /* fbshim.c's */

static uint8_t *map;
static size_t map_size;
static const uint32_t *pixels;
static int fb_w, fb_h, fb_stride;
static ANativeWindow *window;

static uint32_t be32(const uint8_t *p) { return (uint32_t) p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

/* The framebuffer's memory from Xvfb (fbshim.c), or -1. */
static int receive_fd(void)
{
    int s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0), fd = -1;
    struct sockaddr_un a = {.sun_family = AF_UNIX};
    memcpy(a.sun_path + 1, SOCKET_NAME, strlen(SOCKET_NAME));
    socklen_t len = (socklen_t) (offsetof(struct sockaddr_un, sun_path) + 1 + strlen(SOCKET_NAME));
    if (s < 0)
        return -1;
    if (!connect(s, (struct sockaddr *) &a, len)) {
        char byte, control[CMSG_SPACE(sizeof fd)];
        struct iovec io = {&byte, 1};
        struct msghdr m = {.msg_iov = &io, .msg_iovlen = 1, .msg_control = control, .msg_controllen = sizeof control};
        if (recvmsg(s, &m, MSG_CMSG_CLOEXEC) > 0) {
            struct cmsghdr *c = CMSG_FIRSTHDR(&m);
            if (c && c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS)
                memcpy(&fd, CMSG_DATA(c), sizeof fd);
        }
    }
    close(s);
    return fd;
}

JNIEXPORT jboolean JNICALL Java_jp_denboku_Screen_open(JNIEnv *env, jclass c, jstring jpath)
{
    (void) c;
    if (map)
        munmap(map, map_size);
    map = NULL, pixels = NULL;
    int fd = receive_fd();
    if (fd < 0) {
        const char *path = (*env)->GetStringUTFChars(env, jpath, NULL);
        fd = open(path, O_RDONLY);
        (*env)->ReleaseStringUTFChars(env, jpath, path);
    }
    struct stat st;
    if (fd < 0 || fstat(fd, &st) || st.st_size < 100) {
        if (fd >= 0)
            close(fd);
        return JNI_FALSE;
    }
    map_size = (size_t) st.st_size;
    map = mmap(NULL, map_size, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (map == MAP_FAILED) {
        map = NULL;
        return JNI_FALSE;
    }
    /* XWDFileHeader: big-endian 32-bit fields, then the colormap (12 bytes an entry). */
    /* Xvfb may not have written the header yet: then the caller tries again. */
    fb_w = (int) be32(map + 16), fb_h = (int) be32(map + 20), fb_stride = (int) be32(map + 48) / 4;
    size_t at = be32(map) + (size_t) be32(map + 76) * 12;
    if (be32(map + 44) != 32 || fb_w <= 0 || fb_h <= 0 || at + (size_t) fb_stride * 4 * fb_h > map_size) {
        munmap(map, map_size);
        map = NULL;
        return JNI_FALSE;
    }
    pixels = (const uint32_t *) (map + at);
    return JNI_TRUE;
}

JNIEXPORT void JNICALL Java_jp_denboku_Screen_attach(JNIEnv *env, jclass c, jobject surface, jint w, jint h)
{
    (void) c;
    if (window)
        ANativeWindow_release(window);
    window = surface ? ANativeWindow_fromSurface(env, surface) : NULL;
    if (window)
        ANativeWindow_setBuffersGeometry(window, w, h, WINDOW_FORMAT_RGBX_8888);
}

/* X keeps 0x00RRGGBB; the Surface wants the bytes R, G, B, X. */
JNIEXPORT void JNICALL Java_jp_denboku_Screen_draw(JNIEnv *env, jclass c, jint x, jint y, jint w, jint h)
{
    (void) env, (void) c;
    if (!window || !pixels)
        return;
    ARect r = {x, y, x + w, y + h};
    ANativeWindow_Buffer b;
    if (ANativeWindow_lock(window, &b, &r))
        return;
    int right = r.right < b.width ? r.right : b.width, bottom = r.bottom < b.height ? r.bottom : b.height;
    if (right > fb_w)
        right = fb_w;
    if (bottom > fb_h)
        bottom = fb_h;
    for (int row = r.top < 0 ? 0 : r.top; row < bottom; row++) {
        const uint32_t *s = pixels + (size_t) row * fb_stride;
        uint32_t *d = (uint32_t *) b.bits + (size_t) row * b.stride;
        for (int col = r.left < 0 ? 0 : r.left; col < right; col++) {
            uint32_t p = s[col];
            d[col] = 0xff000000u | (p & 0xff00u) | (p >> 16 & 0xffu) | (p & 0xffu) << 16;
        }
    }
    ANativeWindow_unlockAndPost(window);
}
