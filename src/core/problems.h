#ifndef PROBLEMS_H
#define PROBLEMS_H

#include <glib.h>

/* Mate problems (tsume shogi): data/problems/problems.txt, a line "<plies> <position>" each, in
 * the order to be solved, the position as the game writes it; the attacker (the side to move)
 * mates in that many plies, 1, 3, 5… (the 手 of 3手詰). Which are solved is kept as the positions'
 * ids, hex and comma separated. No GTK. */

typedef struct {
    int plies, variant; /* the variant's number (V_STANDARD…) */
    char *position;
} Problem;

/* The problems in `text` of `variant` (a GArray of Problem; blank lines, # lines and bad ones left out). */
GArray *problems_parse(const char *text, int variant);
/* The text of `list` problems of another variant added to it (problems_parse's result for `variant`). */
void problems_add(GArray *list, const char *text, int variant);
void problems_free(GArray *list);
/* The moves the attacker makes in a mate of `plies`: 1 for 1 ply, 2 for 3, 3 for 5. */
int problem_moves(int plies);
guint32 problem_id(const Problem *p);
gboolean problem_solved(const char *solved, const Problem *p);
/* `solved` with `p` added (g_free it). */
char *problem_mark_solved(const char *solved, const Problem *p);
/* How many problems have `plies`, and how many of them are solved. */
int problems_count(const GArray *list, int variant, int plies, const char *solved, int *done);
/* The index of the first unsolved problem of `plies` after `after` (-1: from the start), going
 * round; -1 when all are solved or there are none. */
int problems_next_unsolved(const GArray *list, int variant, int plies, const char *solved, int after);
/* The first problem of `plies` after `after`, solved or not, going round (-1: none). */
int problems_next(const GArray *list, int variant, int plies, int after);
/* The list's index of the `n`th problem of `plies` (1 is the first), -1 if there is none; and the
 * other way: which of its length the problem at `index` is. */
int problems_nth(const GArray *list, int variant, int plies, int n);
int problems_rank(const GArray *list, int index);
/* The problem lengths there are, in order, up to `max`: returns how many. */
int problems_lengths(const GArray *list, int variant, int *plies, int max);
/* The variants that have problems, in order: returns how many. */
int problems_variants(const GArray *list, int *variants, int max);

#endif
