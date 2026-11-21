#include "game.h"

#include <string.h>
#include <time.h>

void game_new(Game *g, Handicap h)
{
    memset(g, 0, sizeof *g);
    g->handicap = handicap_start(h, &g->start);
    g->pos = g->start;
    g->hashes[0] = g->pos.hash;
    g->started = (long) time(NULL);
}

void game_end(Game *g, Result r, Reason why)
{
    if (g->result == RESULT_NONE)
        g->result = r, g->reason = why;
}

Result loss_for(int color)
{
    return color == FIRST ? RESULT_SECOND_WIN : RESULT_FIRST_WIN;
}

void game_play(Game *g, const Move *m, int time_ms)
{
    if (g->count >= MAX_PLY || g->result != RESULT_NONE)
        return;
    Move mv = *m;
    pos_make(&g->pos, &mv);
    g->moves[g->count] = mv;
    g->times[g->count] = time_ms;
    g->gave_check[g->count] = pos_in_check(&g->pos);
    g->count++;
    g->hashes[g->count] = g->pos.hash;
    game_judge(g);
}

void game_play_illegal(Game *g, const Move *m, int time_ms)
{
    if (g->count >= MAX_PLY || g->result != RESULT_NONE)
        return;
    int mover = g->pos.side;
    game_play(g, m, time_ms);
    g->result = loss_for(mover);
    g->reason = REASON_ILLEGAL;
}

void game_undo(Game *g)
{
    if (!g->count)
        return;
    g->count--;
    pos_unmake(&g->pos, &g->moves[g->count]);
    g->result = RESULT_NONE;
    g->reason = REASON_NONE;
}

void game_position_at(const Game *g, int ply, Position *out)
{
    *out = g->start;
    for (int i = 0; i < ply && i < g->count; i++) {
        Move m = g->moves[i];
        game_replay(out, &m);
    }
}

void game_replay(Position *p, Move *m)
{
    if (m->piece && COLOR(m->piece) != p->side) /* the other side passed */
        p->side = COLOR(m->piece);
    pos_make(p, m);
}
