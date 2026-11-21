/* The problem list (problems.c): reading, what is solved, which comes next. No GTK. */
#include "problems.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    GArray *l = problems_parse("# comment\n\n1 a/b b G 1\n3  c/d b P 1\nx bad\n5\n3 e/f b - 1\n  1 g/h b S 1  \n9999 big\n", 0);
    assert(l->len == 4);
    assert(g_array_index(l, Problem, 0).plies == 1 && !strcmp(g_array_index(l, Problem, 0).position, "a/b b G 1"));
    assert(!strcmp(g_array_index(l, Problem, 1).position, "c/d b P 1"));
    assert(!strcmp(g_array_index(l, Problem, 3).position, "g/h b S 1"));

    assert(problem_moves(1) == 1 && problem_moves(3) == 2 && problem_moves(9) == 5);
    int plies[8];
    assert(problems_lengths(l, 0, plies, 8) == 2 && plies[0] == 1 && plies[1] == 3);

    int done;
    assert(problems_count(l, 0, 1, "", &done) == 2 && done == 0);
    const Problem *p0 = &g_array_index(l, Problem, 0), *p3 = &g_array_index(l, Problem, 3);
    char *s = problem_mark_solved(NULL, p0), *s2 = problem_mark_solved(s, p3), *s3 = problem_mark_solved(s2, p0);
    assert(problem_solved(s, p0) && !problem_solved(s, p3) && problem_solved(s2, p3) && !strcmp(s2, s3));
    assert(problems_count(l, 0, 1, s2, &done) == 2 && done == 2);
    assert(problems_next_unsolved(l, 0, 1, s, -1) == 3);
    assert(problems_next_unsolved(l, 0, 1, s, 3) == 3);
    assert(problems_next_unsolved(l, 0, 1, s2, -1) == -1);
    assert(problems_next_unsolved(l, 0, 3, s2, 1) == 2);
    assert(problems_next(l, 0, 1, 0) == 3 && problems_next(l, 0, 1, 3) == 0 && problems_next(l, 0, 5, -1) == -1);
    assert(problems_nth(l, 0, 1, 1) == 0 && problems_nth(l, 0, 1, 2) == 3 && problems_nth(l, 0, 1, 3) == -1 && problems_nth(l, 0, 3, 2) == 2);
    assert(problems_rank(l, 0) == 1 && problems_rank(l, 3) == 2 && problems_rank(l, 2) == 2);
    assert(!problem_solved("zz,1", p0));
    g_free(s), g_free(s2), g_free(s3);
    problems_add(l, "1 m/n b G 1\n3 o/p b S 1\n", 1);
    int vs[4];
    assert(problems_variants(l, vs, 4) == 2 && vs[0] == 0 && vs[1] == 1);
    assert(problems_count(l, 1, 1, NULL, NULL) == 1 && problems_count(l, 0, 1, NULL, NULL) == 2);
    assert(problems_nth(l, 1, 3, 1) == 5 && problems_rank(l, 4) == 1 && problems_next(l, 1, 1, -1) == 4);
    problems_free(l);
    puts("problems tests passed");
    return 0;
}
