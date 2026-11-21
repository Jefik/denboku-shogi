#ifndef HISTINDEX_H
#define HISTINDEX_H

#include <glib.h>

/* history/index.txt: what the history list shows of each game, so that opening it reads only the
 * games new or changed since (docs/CODE.md, Files). No GTK. */

typedef struct {
    char *file;
    gint64 mtime, size; /* the file's, to tell when it changed */
    gint64 started;
    int variant, result, reason, count;
    char *names[2];
} HistEntry;

/* A line of the index (no newline) into *e; FALSE, with *e untouched, when it is not one. */
gboolean histindex_parse(const char *line, HistEntry *e);
/* The line for e, without its newline; tabs and line breaks in the names become spaces. */
char *histindex_format(const HistEntry *e);
void histindex_free(HistEntry *e);
/* File name -> HistEntry; empty when the index is missing or not one, without its damaged lines. */
GHashTable *histindex_load(const char *path);
gboolean histindex_save(const char *path, GHashTable *entries);

#endif
