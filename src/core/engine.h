#ifndef ENGINE_H
#define ENGINE_H

#include <stdbool.h>

typedef struct Engine Engine;

typedef struct {
    void (*ready)(void *data);
    /* "resign", "win" or a USI move. */
    void (*bestmove)(const char *move, void *data);
    void (*died)(void *data);
    /* An "info ..." line of the current search (optional). */
    void (*info)(const char *line, void *data);
} EngineCallbacks;

Engine *engine_new(const char *path, const EngineCallbacks *cb, void *data);
void engine_free(Engine *e);
const char *engine_name(const Engine *e);
/* Option names are matched loosely ("Skill Level" finds "Skill_Level"); unknown ones are ignored, and so is
 * the value last sent. */
void engine_set_option(Engine *e, const char *name, const char *value);
void engine_new_game(Engine *e);
/* Starts a search: `position` is a full "position ..." command; `go` the rest of the go command. */
void engine_go(Engine *e, const char *position, const char *go);
/* Stops the current search and drops its result. */
void engine_cancel(Engine *e);

#endif
