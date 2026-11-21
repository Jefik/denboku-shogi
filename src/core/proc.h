#ifndef PROC_H
#define PROC_H

#include <glib.h>

/* A child program whose output arrives line by line in the main loop, without blocking it and,
 * on Windows, without a console window. Its stderr is discarded. */
typedef struct Proc Proc;
typedef void (*ProcLine)(const char *line, void *data); /* without the line end */
typedef void (*ProcExit)(void *data);                   /* output closed and program gone */

/* `dir` is the program's working directory (NULL: the game's own). */
Proc *proc_spawn(char **argv, const char *dir, ProcLine line, ProcExit exit, void *data);
void proc_write(Proc *p, const char *text);
void proc_close_input(Proc *p);
/* In the exit callback: whether the program ended with status 0. */
gboolean proc_succeeded(Proc *p);
/* Stops the program; no callback runs after this. */
void proc_free(Proc *p);

#endif
