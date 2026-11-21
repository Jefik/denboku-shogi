#ifndef GAME_H
#define GAME_H

#include <stdbool.h>
#include <stdio.h>

#include "rules.h"

#define MAX_PLY 1024

/* ABORTED: ended without a result (a network game given up, 中断). */
typedef enum { RESULT_NONE, RESULT_FIRST_WIN, RESULT_SECOND_WIN, RESULT_DRAW, RESULT_ABORTED } Result;

typedef struct Game Game;
struct Game {
    Handicap handicap;
    Position start;
    Position pos;              /* position after all moves */
    Move moves[MAX_PLY];
    int times[MAX_PLY];        /* milliseconds spent on each move */
    bool gave_check[MAX_PLY];
    uint64_t hashes[MAX_PLY + 1];
    int count;
    Result result;
    Reason reason;
    char names[2][64];
    long started;              /* unix time */
};

void game_new(Game *g, Handicap h);
/* Plays a legal move (filled by pos_is_legal); game_judge() then checks for the end of the game. */
void game_play(Game *g, const Move *m, int time_ms);
/* An illegal move (one of pos_any_moves): it goes on the board and its player loses. */
void game_play_illegal(Game *g, const Move *m, int time_ms);
void game_undo(Game *g);
void game_end(Game *g, Result r, Reason why);
Result loss_for(int color);
/* Position after `ply` moves (0 = start). */
void game_position_at(const Game *g, int ply, Position *out);
/* Plays the game's move `m` on *p, as game_position_at does: a move by the side not to move means
 * the other passed (a lesson's step). */
void game_replay(Position *p, Move *m);

#endif
