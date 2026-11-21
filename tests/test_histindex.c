/* histindex.c: the history index's lines, read back, damaged and saved. */
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

#include "histindex.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

int main(void)
{
    HistEntry e = {"20261116-213000.kif", 1795000000, 1234, 1794990000, 2, 1, 3, 87, {"Sente\tname", "Engine 5"}};
    char *line = histindex_format(&e);
    CHECK(!strcmp(line, "20261116-213000.kif\t1795000000\t1234\t1794990000\t2\t1\t3\t87\tSente name\tEngine 5"));
    HistEntry r;
    CHECK(histindex_parse(line, &r));
    CHECK(!strcmp(r.file, e.file) && r.mtime == e.mtime && r.size == e.size && r.started == e.started);
    CHECK(r.variant == 2 && r.result == 1 && r.reason == 3 && r.count == 87);
    CHECK(!strcmp(r.names[0], "Sente name") && !strcmp(r.names[1], "Engine 5"));
    g_free(r.file);
    g_free(r.names[0]);
    g_free(r.names[1]);
    g_free(line);

    CHECK(histindex_parse("a.kif\t1\t2\t3\t0\t0\t0\t0\t\t", &r)); /* empty names */
    CHECK(!*r.names[0] && !*r.names[1]);
    g_free(r.file);
    g_free(r.names[0]);
    g_free(r.names[1]);
    const char *bad[] = {"", "a.kif\t1\t2\t3\t0\t0\t0\t0\tx", "a.kif\t1\t2\t3\t0\t0\t0\t0\tx\ty\tz",
                         "\t1\t2\t3\t0\t0\t0\t0\tx\ty", "a.kif\t1x\t2\t3\t0\t0\t0\t0\tx\ty",
                         "a.kif\t1\t\t3\t0\t0\t0\t0\tx\ty", "a.kif\t1\t2\t3\t0\t0\t0\t99999999999\tx\ty"};
    for (guint i = 0; i < G_N_ELEMENTS(bad); i++)
        CHECK(!histindex_parse(bad[i], &r));

    char *path = g_build_filename(g_get_tmp_dir(), "denboku-test-index.txt", NULL);
    GHashTable *t = histindex_load(path); /* missing */
    CHECK(g_hash_table_size(t) == 0);
    HistEntry *a = g_new(HistEntry, 1), *b = g_new(HistEntry, 1);
    *a = (HistEntry){g_strdup("b.kif"), 1, 2, 3, 0, 0, 0, 5, {g_strdup("x"), g_strdup("y")}};
    *b = (HistEntry){g_strdup("a.kif"), 4, 5, 6, 1, 2, 1, 9, {g_strdup("p"), g_strdup("q")}};
    g_hash_table_replace(t, a->file, a);
    g_hash_table_replace(t, b->file, b);
    CHECK(histindex_save(path, t));
    g_hash_table_destroy(t);
    char *text = NULL;
    CHECK(g_file_get_contents(path, &text, NULL, NULL));
    CHECK(text && !strcmp(text, "denboku-history 1\na.kif\t4\t5\t6\t1\t2\t1\t9\tp\tq\nb.kif\t1\t2\t3\t0\t0\t0\t5\tx\ty\n"));
    t = histindex_load(path);
    CHECK(g_hash_table_size(t) == 2);
    HistEntry *got = g_hash_table_lookup(t, "a.kif");
    CHECK(got && got->count == 9 && !strcmp(got->names[1], "q"));
    g_hash_table_destroy(t);

    /* A damaged line drops alone; another header drops them all. */
    char *damaged = g_strconcat("denboku-history 1\na.kif\t4\t5\n", text + strlen("denboku-history 1\na.kif\t4\t5\t6\t1\t2\t1\t9\tp\tq\n"), NULL);
    g_file_set_contents(path, damaged, -1, NULL);
    t = histindex_load(path);
    CHECK(g_hash_table_size(t) == 1 && g_hash_table_lookup(t, "b.kif"));
    g_hash_table_destroy(t);
    g_file_set_contents(path, text + 1, -1, NULL);
    t = histindex_load(path);
    CHECK(g_hash_table_size(t) == 0);
    g_hash_table_destroy(t);
    g_remove(path);
    g_free(damaged);
    g_free(text);
    g_free(path);

    if (failures)
        return 1;
    puts("history index tests passed");
    return 0;
}
