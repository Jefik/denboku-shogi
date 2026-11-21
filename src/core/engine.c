#include "engine.h"

#include <glib.h>
#include <string.h>

#include "proc.h"
#include "rules.h"

typedef struct {
    char *name, *key, *value;   /* key: normalized name; value: last one sent */
} Option;

struct Engine {
    EngineCallbacks cb;
    void *data;
    Proc *proc;
    GPtrArray *options;   /* Option: those announced by the engine */
    GPtrArray *pending;   /* "name\nvalue" set before usiok */
    GPtrArray *queue;     /* commands waiting for readyok */
    char *name;
    bool usiok, ready, searching;
    int drop;             /* bestmoves to ignore after a cancel */
};

static void send_line(Engine *e, const char *line)
{
    char *text = g_strconcat(line, "\n", NULL);
    proc_write(e->proc, text);
    g_free(text);
}

static void send_or_queue(Engine *e, const char *line)
{
    if (e->ready)
        send_line(e, line);
    else
        g_ptr_array_add(e->queue, g_strdup(line));
}

/* "USI_Variant" and "UCI_Variant" are the same option. */
static char *normalize(const char *name)
{
    GString *s = g_string_new(NULL);
    for (const char *p = name; *p; p++)
        if (g_ascii_isalnum(*p))
            g_string_append_c(s, g_ascii_tolower(*p));
    if (g_str_has_prefix(s->str, "usi") || g_str_has_prefix(s->str, "uci"))
        g_string_erase(s, 0, 3);
    return g_string_free(s, FALSE);
}

static void option_free(gpointer p)
{
    Option *o = p;
    g_free(o->name);
    g_free(o->key);
    g_free(o->value);
    g_free(o);
}

static void apply_option(Engine *e, const char *name, const char *value)
{
    char *want = normalize(name);
    for (guint i = 0; i < e->options->len; i++) {
        Option *o = g_ptr_array_index(e->options, i);
        if (strcmp(o->key, want))
            continue;
        if (g_strcmp0(o->value, value)) {
            char *line = g_strdup_printf("setoption name %s value %s", o->name, value);
            send_line(e, line);
            g_free(line);
            g_free(o->value);
            o->value = g_strdup(value);
        }
        break;
    }
    g_free(want);
}

static void handle(Engine *e, const char *line)
{
    if (g_str_has_prefix(line, "id name ")) {
        g_free(e->name);
        e->name = g_strdup(line + 8);
    } else if (g_str_has_prefix(line, "option name ")) {
        const char *start = line + 12, *end = strstr(start, " type ");
        if (end) {
            Option *o = g_new0(Option, 1);
            o->name = g_strndup(start, end - start);
            o->key = normalize(o->name);
            g_ptr_array_add(e->options, o);
        }
    } else if (!strcmp(line, "usiok") || !strcmp(line, "uciok")) {
        e->usiok = true;
        for (guint i = 0; i < e->pending->len; i++) {
            char **kv = g_strsplit(g_ptr_array_index(e->pending, i), "\n", 2);
            apply_option(e, kv[0], kv[1]);
            g_strfreev(kv);
        }
        g_ptr_array_set_size(e->pending, 0);
        send_line(e, "isready");
    } else if (!strcmp(line, "readyok")) {
        if (!e->ready) {
            e->ready = true;
            for (guint i = 0; i < e->queue->len; i++)
                send_line(e, g_ptr_array_index(e->queue, i));
            g_ptr_array_set_size(e->queue, 0);
            if (e->cb.ready)
                e->cb.ready(e->data);
        }
    } else if (g_str_has_prefix(line, "info ")) {
        if (e->cb.info && e->searching && !e->drop)
            e->cb.info(line, e->data);
    } else if (g_str_has_prefix(line, "bestmove ")) {
        e->searching = false;
        if (e->drop > 0) {
            e->drop--;
            return;
        }
        char **tok = g_strsplit(line + 9, " ", 2);
        if (e->cb.bestmove)
            e->cb.bestmove(tok[0], e->data);
        g_strfreev(tok);
    }
}

static void line_cb(const char *raw, void *data)
{
    char *line = g_strchomp(g_strdup(raw));
    handle(data, line);
    g_free(line);
}

static void exit_cb(void *data)
{
    Engine *e = data;
    e->ready = false;
    if (e->cb.died)
        e->cb.died(e->data);
}

Engine *engine_new(const char *path, const EngineCallbacks *cb, void *data)
{
    /* At the lowest priority: on one core (the Kindle) a thinking engine otherwise holds up the
     * game's redraw, and the move just made shows a second late. */
    char *argv[] = {"nice", "-n", "19", (char *) path, NULL}, **run = argv;
#ifdef G_OS_WIN32
    run = argv + 3;
#else
    char *nice = g_find_program_in_path("nice");
    if (!nice)
        run = argv + 3;
    g_free(nice);
#endif
    Engine *e = g_new0(Engine, 1);
    e->cb = *cb;
    e->data = data;
    /* In its own directory: YaneuraOu, for one, looks for its eval/ folder there. */
    char *dir = g_path_is_absolute(path) ? g_path_get_dirname(path) : NULL;
    e->proc = proc_spawn(run, dir, line_cb, exit_cb, e);
    g_free(dir);
    if (!e->proc) {
        g_free(e);
        return NULL;
    }
    e->options = g_ptr_array_new_with_free_func(option_free);
    e->pending = g_ptr_array_new_with_free_func(g_free);
    e->queue = g_ptr_array_new_with_free_func(g_free);
    send_line(e, ENGINE_PROTOCOL);
    return e;
}

void engine_free(Engine *e)
{
    if (!e)
        return;
    send_line(e, "quit");
    proc_free(e->proc);
    g_ptr_array_free(e->options, TRUE);
    g_ptr_array_free(e->pending, TRUE);
    g_ptr_array_free(e->queue, TRUE);
    g_free(e->name);
    g_free(e);
}

const char *engine_name(const Engine *e) { return e && e->name ? e->name : "Engine"; }

void engine_set_option(Engine *e, const char *name, const char *value)
{
    if (e->usiok)
        apply_option(e, name, value);
    else
        g_ptr_array_add(e->pending, g_strdup_printf("%s\n%s", name, value));
}

void engine_new_game(Engine *e)
{
    send_or_queue(e, ENGINE_PROTOCOL "newgame");
}

void engine_go(Engine *e, const char *position, const char *go)
{
    char *cmd = g_strdup_printf("go %s", go);
    send_or_queue(e, position);
    send_or_queue(e, cmd);
    g_free(cmd);
    e->searching = true;
}

void engine_cancel(Engine *e)
{
    if (!e)
        return;
    if (!e->ready) {
        /* Not sent yet: forget the queued position/go. */
        for (guint i = e->queue->len; i-- > 0;) {
            const char *c = g_ptr_array_index(e->queue, i);
            if (g_str_has_prefix(c, "go ") || g_str_has_prefix(c, "position "))
                g_ptr_array_remove_index(e->queue, i);
        }
        e->searching = false;
        return;
    }
    if (e->searching) {
        e->drop++;
        send_line(e, "stop");
        e->searching = false;
    }
}
