/* How players name a standard game's strategies: each side's rook (Static Rook or which Ranging
 * Rook), the castle its king sits in, and the Bishop Exchange. The castles are the lessons' shapes
 * (data/lessons/lessons.txt). */

#include "game.h"
#include "i18n.h"

#include <stdio.h>
#include <string.h>

/* Sente's square at file f, rank r; Gote's is the board turned around. */
static int at(int color, int f, int r)
{
    int sq = SQ(r - 1, 9 - f);
    return color == FIRST ? sq : 80 - sq;
}

typedef struct {
    const char *name;
    struct { int type, f, r; } pieces[5];
} Castle;

/* The more specific first: the High Mino and the Silver crown are Minos grown up. */
static const Castle CASTLES[] = {
    {"Silver crown", {{OU, 2, 8}, {GI, 2, 7}, {KI, 3, 8}, {KI, 4, 7}}},
    {"High Mino", {{OU, 2, 8}, {GI, 3, 8}, {KI, 4, 7}, {KI, 4, 9}}},
    {"Mino castle", {{OU, 2, 8}, {GI, 3, 8}, {KI, 4, 9}, {KI, 5, 8}}},
    {"Ranging Rook Anaguma", {{OU, 1, 9}, {KY, 1, 8}, {GI, 2, 8}, {KI, 3, 9}}},
    {"Bear in the hole", {{OU, 9, 9}, {KY, 9, 8}, {GI, 8, 8}, {KI, 7, 9}}},
    {"Yagura castle", {{OU, 8, 8}, {KI, 7, 8}, {GI, 7, 7}, {KI, 6, 7}}},
    {"Left Mino", {{OU, 8, 8}, {GI, 7, 8}, {KI, 5, 8}, {KI, 6, 9}}},
    {"Boat castle", {{OU, 7, 8}, {KI, 5, 8}, {GI, 7, 9}, {KI, 6, 9}}},
};

static const char *castle(const Position *p, int color)
{
    for (size_t i = 0; i < sizeof CASTLES / sizeof *CASTLES; i++) {
        bool all = true;
        for (int j = 0; j < 5 && CASTLES[i].pieces[j].type && all; j++)
            all = p->board[at(color, CASTLES[i].pieces[j].f, CASTLES[i].pieces[j].r)] ==
                  PIECE(CASTLES[i].pieces[j].type, color);
        if (all)
            return CASTLES[i].name;
    }
    return NULL;
}

/* The file of `color`'s rook in its own camp (Sente's files), 0 if it is not there. */
static int rook_file(const Position *p, int color)
{
    for (int f = 1; f <= 9; f++)
        for (int r = 6; r <= 9; r++)
            if (p->board[at(color, f, r)] == PIECE(HI, color))
                return f;
    return 0;
}

/* The rook on files 4 to 9 (as its side counts them) names the opening. */
static const char *const RANGING[10] = {
    [4] = "Right Fourth-file Rook", [5] = "Central Rook", [6] = "Fourth-file Rook",
    [7] = "Third-file Rook", [8] = "Opposing Rook", [9] = "Ranging Rook",
};

void game_style(const Game *g, int ply, char *buf, int size)
{
    *buf = 0;
    if (rules_variant != V_STANDARD || g->handicap != HANDICAP_NONE)
        return;
    Position p = g->start;
    const char *rook[2] = {NULL, NULL};
    bool exchange = false;
    for (int i = 0;; i++) {
        for (int c = FIRST; c <= SECOND; c++) {
            int f = rook_file(&p, c);
            if (f && RANGING[f])
                rook[c] = RANGING[f];
        }
        if (i <= 40 && p.hand[FIRST][hand_slot(KA)] && p.hand[SECOND][hand_slot(KA)])
            exchange = true;
        if (i >= ply || i >= g->count)
            break;
        Move m = g->moves[i];
        pos_make(&p, &m);
    }
    int n = 0;
    for (int c = FIRST; c <= SECOND && n < size; c++) {
        const char *r = rook[c] ? rook[c] : ply >= 16 && rook_file(&p, c) == 2 ? "Static Rook" : NULL, *k = castle(&p, c);
        if (r || k)
            n += snprintf(buf + n, size - n, "%s%s %s%s%s", n ? "\n" : "", SIDE_SYMBOLS[c], r ? tr(r) : "",
                          r && k ? tr(", ") : "", k ? tr(k) : "");
    }
    if (exchange && n < size)
        snprintf(buf + n, size - n, "%s%s", n ? "\n" : "", tr("Bishop Exchange"));
}
