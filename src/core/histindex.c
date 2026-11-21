/* The history's index: a header line, then a game per line, its fields joined by tabs. */

#include "histindex.h"

#include <string.h>

#define HEADER "denboku-history 1"

static gboolean number(const char *s, gint64 *out)
{
    char *end;
    *out = g_ascii_strtoll(s, &end, 10);
    return *s && !*end;
}

gboolean histindex_parse(const char *line, HistEntry *e)
{
    char **f = g_strsplit(line, "\t", 0);
    gint64 n[7];
    gboolean ok = g_strv_length(f) == 10 && *f[0];
    for (int i = 0; ok && i < 7; i++) /* the last four are ints */
        ok = number(f[i + 1], &n[i]) && (i < 3 || (n[i] >= G_MININT && n[i] <= G_MAXINT));
    if (ok)
        *e = (HistEntry){g_strdup(f[0]), n[0], n[1], n[2], n[3], n[4], n[5], n[6], {g_strdup(f[8]), g_strdup(f[9])}};
    g_strfreev(f);
    return ok;
}

static char *one_line(const char *s)
{
    return g_strdelimit(g_strdup(s), "\t\r\n", ' ');
}

char *histindex_format(const HistEntry *e)
{
    char *a = one_line(e->names[0]), *b = one_line(e->names[1]);
    char *line = g_strdup_printf("%s\t%" G_GINT64_FORMAT "\t%" G_GINT64_FORMAT "\t%" G_GINT64_FORMAT "\t%d\t%d\t%d\t%d\t%s\t%s",
                                 e->file, e->mtime, e->size, e->started, e->variant, e->result, e->reason, e->count, a, b);
    g_free(a);
    g_free(b);
    return line;
}

void histindex_free(HistEntry *e)
{
    if (!e)
        return;
    g_free(e->file);
    g_free(e->names[0]);
    g_free(e->names[1]);
    g_free(e);
}

GHashTable *histindex_load(const char *path)
{
    GHashTable *t = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, (GDestroyNotify) histindex_free);
    char *text = NULL;
    if (!g_file_get_contents(path, &text, NULL, NULL))
        return t;
    char **lines = g_strsplit(text, "\n", 0);
    if (lines[0] && !strcmp(lines[0], HEADER))
        for (int i = 1; lines[i]; i++) {
            HistEntry e;
            if (histindex_parse(lines[i], &e))
                g_hash_table_replace(t, e.file, g_memdup(&e, sizeof e));
        }
    g_strfreev(lines);
    g_free(text);
    return t;
}

/* The lines in file order; g_file_set_contents() renames a new file over the old one. */
gboolean histindex_save(const char *path, GHashTable *entries)
{
    GString *s = g_string_new(HEADER "\n");
    GList *keys = g_list_sort(g_hash_table_get_keys(entries), (GCompareFunc) strcmp);
    for (GList *k = keys; k; k = k->next) {
        char *line = histindex_format(g_hash_table_lookup(entries, k->data));
        g_string_append_printf(s, "%s\n", line);
        g_free(line);
    }
    g_list_free(keys);
    gboolean ok = g_file_set_contents(path, s->str, s->len, NULL);
    g_string_free(s, TRUE);
    return ok;
}
