/* Loaded into Xvfb (LD_PRELOAD): its framebuffer file (-fbdir .../Xvfb_screen0) becomes a memfd,
 * memory that the kernel never writes to storage, and the app gets it over a local socket that
 * only the same user may use (screen.c). Android refuses /dev/ashmem and System V shared memory
 * to apps; a file in the cache had its changed pages written out again and again (Xvfb calls
 * msync() on all of it each time round its loop). */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <unistd.h>

#define SOCKET_NAME "jp.denboku.fb" /* abstract: no file */

static int shared = -1;

static int is_screen(const char *path)
{
    size_t n = path ? strlen(path) : 0, k = strlen("/Xvfb_screen0");
    return n >= k && !strcmp(path + n - k, "/Xvfb_screen0");
}

static void send_fd(int peer, int fd)
{
    char byte = 'F', control[CMSG_SPACE(sizeof fd)];
    struct iovec io = {&byte, 1};
    struct msghdr m = {.msg_iov = &io, .msg_iovlen = 1, .msg_control = control, .msg_controllen = sizeof control};
    struct cmsghdr *c = CMSG_FIRSTHDR(&m);
    c->cmsg_level = SOL_SOCKET, c->cmsg_type = SCM_RIGHTS, c->cmsg_len = CMSG_LEN(sizeof fd);
    memcpy(CMSG_DATA(c), &fd, sizeof fd);
    sendmsg(peer, &m, MSG_NOSIGNAL);
}

/* Hands the framebuffer to each connection from this user (the app), for as long as Xvfb lives. */
static void *serve(void *data)
{
    int fd = (int) (long) data, s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_un a = {.sun_family = AF_UNIX};
    memcpy(a.sun_path + 1, SOCKET_NAME, strlen(SOCKET_NAME));
    socklen_t len = (socklen_t) (offsetof(struct sockaddr_un, sun_path) + 1 + strlen(SOCKET_NAME));
    /* An Xvfb being stopped may still hold the name a moment. */
    for (int i = 0; i < 100 && bind(s, (struct sockaddr *) &a, len); i++)
        usleep(50000);
    if (listen(s, 4))
        return NULL;
    for (;;) {
        int peer = accept4(s, NULL, NULL, SOCK_CLOEXEC);
        if (peer < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        struct ucred cred;
        socklen_t n = sizeof cred;
        if (!getsockopt(peer, SOL_SOCKET, SO_PEERCRED, &cred, &n) && cred.uid == getuid())
            send_fd(peer, fd);
        close(peer);
    }
    return NULL;
}

static int screen_fd(void)
{
    if (shared >= 0)
        return dup(shared);
    shared = (int) syscall(__NR_memfd_create, "denboku-fb", 1u /* MFD_CLOEXEC */);
    if (shared < 0)
        return -1;
    pthread_t t;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_create(&t, &at, serve, (void *) (long) shared);
    return dup(shared);
}

int open(const char *path, int flags, ...)
{
    static int (*real)(const char *, int, ...);
    if (!real)
        real = (int (*)(const char *, int, ...)) dlsym(RTLD_NEXT, "open");
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t) va_arg(ap, int);
        va_end(ap);
    }
    if (is_screen(path)) {
        int fd = screen_fd();
        if (fd >= 0)
            return fd;
    }
    return real(path, flags, mode);
}

int unlink(const char *path)
{
    static int (*real)(const char *);
    if (!real)
        real = (int (*)(const char *)) dlsym(RTLD_NEXT, "unlink");
    return is_screen(path) && shared >= 0 ? 0 : real(path);
}
