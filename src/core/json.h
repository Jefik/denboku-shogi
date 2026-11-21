#ifndef JSON_H
#define JSON_H

#include <stdbool.h>
#include <stddef.h>

/* Reads one value from a JSON object by a dotted path ("game.id", "state.moves"; a number takes
 * that element of a list, "0.text"). Strings come
 * back unescaped, numbers and literals as written. False when the path is missing or null. */
bool json_get(const char *json, const char *path, char *out, size_t size);
long json_long(const char *json, const char *path, long fallback);

#endif
