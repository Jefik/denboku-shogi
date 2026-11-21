#include "proc.h"

#include <string.h>

static void split_lines(GString *buf, ProcLine line, void *data, const gboolean *dead)
{
    char *nl;
    while (!*dead && (nl = memchr(buf->str, '\n', buf->len))) {
        *nl = 0;
        if (nl > buf->str && nl[-1] == '\r')
            nl[-1] = 0;
        char *copy = g_strdup(buf->str);
        g_string_erase(buf, 0, nl - buf->str + 1);
        if (line)
            line(copy, data);
        g_free(copy);
    }
}

#ifdef G_OS_WIN32

#include <windows.h>

/* A thread reads the pipe (blocking is fine there) and hands each chunk to the main loop. */
struct Proc {
    ProcLine line;
    ProcExit exit;
    void *data;
    HANDLE process, in, out;
    GString *buf;
    gboolean dead;
    gint refs;
};

typedef struct {
    Proc *p;
    char *chunk; /* NULL: end of output */
    gsize len;
} Delivery;

static void unref(Proc *p)
{
    if (!g_atomic_int_dec_and_test(&p->refs))
        return;
    if (p->process)
        CloseHandle(p->process);
    if (p->in)
        CloseHandle(p->in);
    if (p->out)
        CloseHandle(p->out);
    g_string_free(p->buf, TRUE);
    g_free(p);
}

static gboolean deliver(gpointer user)
{
    Delivery *d = user;
    Proc *p = d->p;
    if (!p->dead) {
        if (d->chunk) {
            g_string_append_len(p->buf, d->chunk, d->len);
            split_lines(p->buf, p->line, p->data, &p->dead);
        } else {
            p->dead = TRUE;
            if (p->exit)
                p->exit(p->data);
        }
    }
    g_free(d->chunk);
    g_free(d);
    unref(p);
    return FALSE;
}

static void post(Proc *p, char *chunk, gsize len)
{
    Delivery *d = g_new(Delivery, 1);
    d->p = p, d->chunk = chunk, d->len = len;
    g_atomic_int_inc(&p->refs);
    g_idle_add(deliver, d);
}

static gpointer reader(gpointer user)
{
    Proc *p = user;
    char buf[4096];
    DWORD n;
    while (!p->dead && ReadFile(p->out, buf, sizeof buf, &n, NULL) && n)
        post(p, g_memdup(buf, n), n);
    post(p, NULL, 0);
    unref(p);
    return NULL;
}

/* Windows command-line quoting, as the C runtime parses it. */
static void append_arg(GString *cmd, const char *arg)
{
    if (cmd->len)
        g_string_append_c(cmd, ' ');
    if (*arg && !strpbrk(arg, " \t\"")) {
        g_string_append(cmd, arg);
        return;
    }
    g_string_append_c(cmd, '"');
    for (const char *c = arg;; c++) {
        int slashes = 0;
        while (*c == '\\')
            slashes++, c++;
        if (!*c) {
            for (int i = 0; i < slashes * 2; i++)
                g_string_append_c(cmd, '\\');
            break;
        }
        for (int i = 0; i < (*c == '"' ? slashes * 2 + 1 : slashes); i++)
            g_string_append_c(cmd, '\\');
        g_string_append_c(cmd, *c);
    }
    g_string_append_c(cmd, '"');
}

Proc *proc_spawn(char **argv, const char *dir, ProcLine line, ProcExit exit, void *data)
{
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    HANDLE in_r, in_w, out_r, out_w;
    if (!CreatePipe(&in_r, &in_w, &sa, 0))
        return NULL;
    if (!CreatePipe(&out_r, &out_w, &sa, 0)) {
        CloseHandle(in_r), CloseHandle(in_w);
        return NULL;
    }
    SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    GString *cmd = g_string_new(NULL);
    for (char **a = argv; *a; a++)
        append_arg(cmd, *a);
    gunichar2 *wcmd = g_utf8_to_utf16(cmd->str, -1, NULL, NULL, NULL);
    g_string_free(cmd, TRUE);
    STARTUPINFOW si = {.cb = sizeof si, .dwFlags = STARTF_USESTDHANDLES, .hStdInput = in_r, .hStdOutput = out_w,
                       .hStdError = nul};
    PROCESS_INFORMATION pi;
    /* A bare program name ("curl") is looked up in PATH by CreateProcess itself. */
    gunichar2 *wdir = dir ? g_utf8_to_utf16(dir, -1, NULL, NULL, NULL) : NULL;
    BOOL ok = wcmd && CreateProcessW(NULL, (wchar_t *) wcmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, (wchar_t *) wdir, &si, &pi);
    g_free(wcmd);
    g_free(wdir);
    CloseHandle(in_r), CloseHandle(out_w);
    if (nul != INVALID_HANDLE_VALUE)
        CloseHandle(nul);
    if (!ok) {
        CloseHandle(in_w), CloseHandle(out_r);
        return NULL;
    }
    CloseHandle(pi.hThread);
    Proc *p = g_new0(Proc, 1);
    p->line = line, p->exit = exit, p->data = data;
    p->process = pi.hProcess, p->in = in_w, p->out = out_r;
    p->buf = g_string_new(NULL);
    p->refs = 2; /* the caller and the reader thread */
    /* g_thread_create: the Kindle's GLib predates g_thread_new, and the code builds for both. */
    g_thread_create(reader, p, FALSE, NULL);
    return p;
}

void proc_write(Proc *p, const char *text)
{
    DWORD n;
    if (p && p->in && !p->dead)
        WriteFile(p->in, text, (DWORD) strlen(text), &n, NULL);
}

void proc_close_input(Proc *p)
{
    if (p && p->in) {
        CloseHandle(p->in);
        p->in = NULL;
    }
}

void proc_free(Proc *p)
{
    if (!p)
        return;
    p->dead = TRUE;
    proc_close_input(p);
    TerminateProcess(p->process, 1);
    unref(p);
}

gboolean proc_succeeded(Proc *p)
{
    DWORD code = 1;
    return p && p->process && WaitForSingleObject(p->process, 5000) == WAIT_OBJECT_0 && GetExitCodeProcess(p->process, &code) && code == 0;
}

#else

#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

struct Proc {
    ProcLine line;
    ProcExit exit;
    void *data;
    GPid pid;
    GIOChannel *in, *out;
    guint watch, child;
    GString *buf;
    gboolean eof, exited, dead;
    int status;
};

static void finish(Proc *p)
{
    if (!p->eof || !p->exited || p->dead)
        return;
    p->dead = TRUE;
    if (p->exit)
        p->exit(p->data);
}

static gboolean read_cb(GIOChannel *ch, GIOCondition cond, gpointer user)
{
    Proc *p = user;
    char buf[4096];
    gsize n = 0;
    GIOStatus st = G_IO_STATUS_NORMAL;
    if (cond & G_IO_IN) {
        st = g_io_channel_read_chars(ch, buf, sizeof buf, &n, NULL);
        if (n) {
            g_string_append_len(p->buf, buf, n);
            split_lines(p->buf, p->line, p->data, &p->dead);
        }
    }
    if (p->dead)
        return TRUE; /* freed later by proc_free, which removes this watch */
    if (st == G_IO_STATUS_EOF || st == G_IO_STATUS_ERROR || (!n && (cond & (G_IO_HUP | G_IO_ERR)))) {
        p->watch = 0;
        p->eof = TRUE;
        finish(p);
        return FALSE;
    }
    return TRUE;
}

static void child_cb(GPid pid, gint status, gpointer user)
{
    Proc *p = user;
    p->status = status;
    g_spawn_close_pid(pid);
    p->pid = 0;
    p->child = 0;
    p->exited = TRUE;
    finish(p);
}

Proc *proc_spawn(char **argv, const char *dir, ProcLine line, ProcExit exit, void *data)
{
    int in_fd, out_fd;
    GPid pid;
    if (!g_spawn_async_with_pipes(dir, argv, NULL,
                                  G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDERR_TO_DEV_NULL, NULL,
                                  NULL, &pid, &in_fd, &out_fd, NULL, NULL))
        return NULL;
    Proc *p = g_new0(Proc, 1);
    p->line = line, p->exit = exit, p->data = data;
    p->pid = pid;
    p->buf = g_string_new(NULL);
    p->in = g_io_channel_unix_new(in_fd);
    p->out = g_io_channel_unix_new(out_fd);
    for (int i = 0; i < 2; i++) {
        GIOChannel *ch = i ? p->out : p->in;
        g_io_channel_set_encoding(ch, NULL, NULL);
        g_io_channel_set_close_on_unref(ch, TRUE);
    }
    g_io_channel_set_buffered(p->in, FALSE);
    /* A blocking read would wait for a full buffer and freeze the UI. */
    g_io_channel_set_flags(p->out, G_IO_FLAG_NONBLOCK, NULL);
    p->watch = g_io_add_watch(p->out, G_IO_IN | G_IO_HUP | G_IO_ERR, read_cb, p);
    p->child = g_child_watch_add(pid, child_cb, p);
    return p;
}

void proc_write(Proc *p, const char *text)
{
    if (p && p->in && !p->dead)
        g_io_channel_write_chars(p->in, text, -1, NULL, NULL);
}

void proc_close_input(Proc *p)
{
    if (p && p->in) {
        g_io_channel_shutdown(p->in, FALSE, NULL);
        g_io_channel_unref(p->in);
        p->in = NULL;
    }
}

static gboolean free_idle(gpointer user)
{
    Proc *p = user;
    g_io_channel_unref(p->out);
    g_string_free(p->buf, TRUE);
    g_free(p);
    return FALSE;
}

/* A program stopped by proc_free: waited for when it ends, so it does not stay a zombie. */
static void reap_cb(GPid pid, gint status, gpointer user)
{
    (void) status, (void) user;
    g_spawn_close_pid(pid);
}

gboolean proc_succeeded(Proc *p) { return p && p->exited && WIFEXITED(p->status) && WEXITSTATUS(p->status) == 0; }

void proc_free(Proc *p)
{
    if (!p)
        return;
    p->dead = TRUE;
    proc_close_input(p);
    if (p->watch)
        g_source_remove(p->watch);
    if (p->child)
        g_source_remove(p->child);
    p->watch = p->child = 0;
    if (p->pid) {
        kill(p->pid, SIGTERM);
        g_child_watch_add(p->pid, reap_cb, NULL);
        p->pid = 0;
    }
    /* Later: this may run inside one of its own callbacks. */
    g_idle_add(free_idle, p);
}

#endif
