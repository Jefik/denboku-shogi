#include "problems.h"

#include <stdlib.h>
#include <string.h>

void problems_add(GArray *list, const char *text, int variant)
{
    char **lines = g_strsplit(text, "\n", -1);
    for (char **l = lines; *l; l++) {
        char *line = g_strstrip(*l), *end;
        if (!*line || *line == '#')
            continue;
        long plies = strtol(line, &end, 10);
        while (*end == ' ')
            end++;
        if (end == line || plies < 1 || plies > 99 || !*end)
            continue;
        Problem p = {(int) plies, variant, g_strdup(end)};
        g_array_append_val(list, p);
    }
    g_strfreev(lines);
}

GArray *problems_parse(const char *text, int variant)
{
    GArray *list = g_array_new(FALSE, FALSE, sizeof(Problem));
    problems_add(list, text, variant);
    return list;
}

void problems_free(GArray *list)
{
    if (!list)
        return;
    for (guint i = 0; i < list->len; i++)
        g_free(g_array_index(list, Problem, i).position);
    g_array_free(list, TRUE);
}

int problem_moves(int plies) { return (plies + 1) / 2; }

guint32 problem_id(const Problem *p) { return g_str_hash(p->position); }

gboolean problem_solved(const char *solved, const Problem *p)
{
    char id[16];
    g_snprintf(id, sizeof id, "%x", problem_id(p));
    size_t n = strlen(id); /* a whole item of the list, without splitting it (one call per problem) */
    for (const char *at = solved ? strstr(solved, id) : NULL; at; at = strstr(at + 1, id))
        if ((at == solved || at[-1] == ',') && (at[n] == ',' || !at[n]))
            return TRUE;
    return FALSE;
}

char *problem_mark_solved(const char *solved, const Problem *p)
{
    if (problem_solved(solved, p))
        return g_strdup(solved);
    return g_strdup_printf("%s%s%x", solved ? solved : "", solved && *solved ? "," : "", problem_id(p));
}

int problems_count(const GArray *list, int variant, int plies, const char *solved, int *done)
{
    int n = 0, d = 0;
    for (guint i = 0; i < list->len; i++) {
        const Problem *p = &g_array_index(list, Problem, i);
        if (p->plies != plies || p->variant != variant)
            continue;
        n++;
        d += problem_solved(solved, p);
    }
    if (done)
        *done = d;
    return n;
}

int problems_next_unsolved(const GArray *list, int variant, int plies, const char *solved, int after)
{
    for (guint k = 1; k <= list->len; k++) {
        int i = after < 0 ? (int) (k - 1) : (int) ((after + k) % list->len);
        const Problem *p = &g_array_index(list, Problem, i);
        if (p->plies == plies && p->variant == variant && !problem_solved(solved, p))
            return i;
    }
    return -1;
}

int problems_next(const GArray *list, int variant, int plies, int after)
{
    for (guint k = 1; k <= list->len; k++) {
        int i = after < 0 ? (int) (k - 1) : (int) ((after + k) % list->len);
        if (g_array_index(list, Problem, i).plies == plies && g_array_index(list, Problem, i).variant == variant)
            return i;
    }
    return -1;
}

int problems_nth(const GArray *list, int variant, int plies, int n)
{
    for (guint i = 0; i < list->len; i++)
        if (g_array_index(list, Problem, i).plies == plies && g_array_index(list, Problem, i).variant == variant && --n == 0)
            return (int) i;
    return -1;
}

int problems_rank(const GArray *list, int index)
{
    int n = 0;
    for (int i = 0; i <= index && i < (int) list->len; i++)
        n += g_array_index(list, Problem, i).plies == g_array_index(list, Problem, index).plies &&
             g_array_index(list, Problem, i).variant == g_array_index(list, Problem, index).variant;
    return n;
}

int problems_lengths(const GArray *list, int variant, int *plies, int max)
{
    int n = 0;
    for (int p = 1; p < 100 && n < max; p += 2)
        for (guint i = 0; i < list->len; i++)
            if (g_array_index(list, Problem, i).plies == p && g_array_index(list, Problem, i).variant == variant) {
                plies[n++] = p;
                break;
            }
    return n;
}

int problems_variants(const GArray *list, int *variants, int max)
{
    int n = 0;
    for (int v = 0; v < 64 && n < max; v++)
        for (guint i = 0; i < list->len; i++)
            if (g_array_index(list, Problem, i).variant == v) {
                variants[n++] = v;
                break;
            }
    return n;
}
