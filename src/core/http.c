#include "http.h"

#include <stdlib.h>
#include <string.h>

#include "proc.h"

/* curl prints this after the body, so the status arrives in the same stream. */
#define STATUS_MARK "DENBOKU-HTTP "

struct Http {
    HttpLine line;
    HttpDone done;
    void *data;
    Proc *proc;
    int status;
    gint64 heard; /* when it last sent a line, the empty keep-alive ones too */
};

static void line_cb(const char *line, void *data)
{
    Http *h = data;
    h->heard = g_get_monotonic_time();
    if (g_getenv("DENBOKU_DEBUG"))
        g_printerr("%.3f http %p: %.300s\n", g_get_monotonic_time() / 1e6, (void *) h, line);
    if (g_str_has_prefix(line, STATUS_MARK))
        h->status = atoi(line + strlen(STATUS_MARK));
    else if (*line && h->line)
        h->line(line, h->data);
}

/* curl is done: report, then the request frees itself. */
static void exit_cb(void *data)
{
    Http *h = data;
    if (h->status >= 200 && h->status <= 299 && !proc_succeeded(h->proc))
        h->status = 0; /* cut off on the way: a dropped connection, not an answer that ended */
    if (h->done)
        h->done(h->status, h->data);
    proc_free(h->proc);
    g_free(h);
}

/* A line of curl's config, `name = "prefix value"`: within the quotes it takes \\ and \" for those
 * two, and a line end would start another option. */
static void config_value(GString *config, const char *name, const char *prefix, const char *value)
{
    g_string_append_printf(config, "%s = \"%s", name, prefix);
    for (const char *p = value; *p; p++) {
        if (*p == '"' || *p == '\\')
            g_string_append_c(config, '\\');
        if (*p != '\n' && *p != '\r')
            g_string_append_c(config, *p);
    }
    g_string_append(config, "\"\n");
}

Http *http_start(const char *method, const char *url, const char *token, const char *form, HttpLine line,
                 HttpDone done, void *data)
{
    /* The token goes in a config read from stdin, not on the command line other users can see. */
    char *argv[] = {"curl", "-sS", "-N", "-K", "-", "--proto", "=https", "--connect-timeout", "20", "-X", (char *) method,
                    "-A", "denboku",
                    "-w", "\n" STATUS_MARK "%{http_code}\n", (char *) url, NULL};
    Http *h = g_new0(Http, 1);
    if (g_getenv("DENBOKU_DEBUG"))
        g_printerr("http %p: %s %s\n", (void *) h, method, url);
    h->line = line;
    h->done = done;
    h->data = data;
    h->heard = g_get_monotonic_time();
    h->proc = proc_spawn(argv, NULL, line_cb, exit_cb, h);
    if (!h->proc) {
        g_free(h);
        return NULL;
    }
    GString *config = g_string_new(NULL);
    if (token && *token)
        config_value(config, "header", "Authorization: Bearer ", token);
    if (form)
        config_value(config, "data", "", form);
    proc_write(h->proc, config->str);
    proc_close_input(h->proc);
    g_string_free(config, TRUE);
    return h;
}

Http *http_download(const char *url, const char *path, gboolean insecure_ok, HttpDone done, void *data)
{
    const char *proto = insecure_ok ? "=https,http" : "=https";
    /* Given up when it stalls a minute, not after a fixed time: a package takes long on a slow line. */
    char *argv[] = {"curl", "-sS", "-f", "-L", "--proto", (char *) proto, "--proto-redir", (char *) proto, "--connect-timeout", "20",
                    "--speed-limit", "1000", "--speed-time", "60", "-A", "denboku", "-o", (char *) path,
                    "-w", STATUS_MARK "%{http_code}\n", (char *) url, NULL};
    Http *h = g_new0(Http, 1);
    if (g_getenv("DENBOKU_DEBUG"))
        g_printerr("http %p: download %s to %s\n", (void *) h, url, path);
    h->done = done;
    h->data = data;
    h->proc = proc_spawn(argv, NULL, line_cb, exit_cb, h);
    if (!h->proc) {
        g_free(h);
        return NULL;
    }
    proc_close_input(h->proc);
    return h;
}

gint64 http_quiet_us(const Http *h) { return h ? g_get_monotonic_time() - h->heard : 0; }

void http_cancel(Http *h)
{
    if (!h)
        return;
    proc_free(h->proc);
    g_free(h);
}

char *http_escape(const char *s)
{
    GString *out = g_string_new(NULL);
    for (const unsigned char *p = (const unsigned char *) s; *p; p++) {
        if (g_ascii_isalnum(*p) || strchr("-_.~", *p))
            g_string_append_c(out, *p);
        else
            g_string_append_printf(out, "%%%02X", *p);
    }
    return g_string_free(out, FALSE);
}
