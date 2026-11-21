/* Setting up a position (the Sandbox): the pieces offered and what makes a position unplayable. */

#include <string.h>

#include "i18n.h"
#include "rules.h"

#define N(a) ((int) (sizeof(a) / sizeof((a)[0])))


/* The pieces that exist in the current variant, for the placement grid. */
int setup_piece_types(int *types)
{
    static const int STANDARD[] = {OU, HI, KA, KI, GI, KE, KY, FU, RY, UM, NG, NK, NY, TO};
    static const int MINI[] = {OU, HI, KA, KI, GI, FU, RY, UM, NG, TO};
    static const int KYOTO[] = {OU, FU, HI, GI, KA, KI, KE, KY, TO};
    static const int DOBUTSU[] = {OU, FU, TO, HI, KA};
    static const int GORO[] = {OU, KI, GI, FU, NG, TO};
    static const int TORI[] = {OU, KA, KI, GI, KY, KE, FU, UM, TO};
    static const int JUDKINS[] = {OU, HI, KA, KI, GI, KE, FU, RY, UM, NG, NK, TO};
    static const int EURO[] = {OU, HI, KA, KI, KE, FU, RY, UM, NK, TO};
    const int *src = rules_variant == V_MINISHOGI   ? MINI
                     : rules_variant == V_KYOTOSHOGI ? KYOTO
                     : rules_variant == V_DOBUTSU    ? DOBUTSU
                     : rules_variant == V_GOROGORO   ? GORO
                     : rules_variant == V_TORI       ? TORI
                     : rules_variant == V_JUDKINS    ? JUDKINS
                     : rules_variant == V_EURO       ? EURO
                                                     : STANDARD;
    int n = rules_variant == V_MINISHOGI   ? (int) N(MINI)
            : rules_variant == V_KYOTOSHOGI ? (int) N(KYOTO)
            : rules_variant == V_DOBUTSU    ? (int) N(DOBUTSU)
            : rules_variant == V_GOROGORO   ? (int) N(GORO)
            : rules_variant == V_TORI       ? (int) N(TORI)
            : rules_variant == V_JUDKINS    ? (int) N(JUDKINS)
            : rules_variant == V_EURO       ? (int) N(EURO)
                                            : (int) N(STANDARD);
    for (int i = 0; i < n; i++)
        types[i] = src[i];
    return n;
}

/* The hand slots, per variant: the pieces in hand as they are shown. */
int setup_hand_types(int *types)
{
    int n;
    const int *order = hand_order(&n);
    memcpy(types, order, n * sizeof *types);
    return n;
}

/* What makes a set-up position unplayable, or NULL (any number of pieces of a kind is fine). */
const char *setup_problem(const Position *p)
{
    int kings[2] = {0};
    for (int sq = 0; sq < SQUARES; sq++) {
        uint8_t pc = p->board[sq];
        if (!pc)
            continue;
        int t = TYPE(pc), c = COLOR(pc);
        int rank = c == FIRST ? ROW(sq) : board_h - 1 - ROW(sq);
        if (t == OU)
            kings[c]++;
        bool tori = rules_variant == V_TORI;
        if (tori && t == FU && rank == 0)
            return tr("A swallow on the last rank could never move.");
        if (!tori && (t == FU || t == KY) && rank == 0)
            return tr("A pawn or lance on the last rank could never move.");
        if (!tori && rules_variant != V_EURO && t == KE && rank <= 1)
            return tr("A knight on the last two ranks could never move.");
        if (t == FU && VARIANTS[rules_variant].nifu) {
            int same = 0;
            for (int r = 0; r < board_h; r++)
                same += p->board[SQ(r, COL(sq))] == pc;
            if (same > VARIANTS[rules_variant].pawns_per_file)
                return tori ? tr("More than two swallows of one side on a file.") : tr("Two unpromoted pawns of one side on a file (二歩).");
        }
    }
    if (kings[FIRST] != 1 || kings[SECOND] != 1)
        return tr("Each side needs one king.");
    if (rules_variant != V_DOBUTSU &&
        pos_attacked(p, pos_king(p, !p->side), p->side))
        return tr("The side not to move is in check.");
    return NULL;
}

