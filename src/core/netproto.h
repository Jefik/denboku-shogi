#ifndef NETPROTO_H
#define NETPROTO_H

/* What a line of a network game says (the table in docs/CODE.md), whoever sent it: no GTK, no
 * state. lan.c decides what to do about it. */

typedef enum {
    NL_MOVE, /* anything else: a move, as the game writes it */
    NL_VERSION, NL_NAME, NL_DENIED, NL_KICKED, NL_BANNED, NL_BYE, NL_PING,
    NL_GAME, NL_CLOCK, NL_VARIANT, NL_SFEN, NL_READY, NL_MOVES,
    NL_TAKEBACK, NL_TAKEBACK_ANSWER, NL_REMATCH, NL_CHAT, NL_RESIGN, NL_ILLEGAL, NL_TIME_UP
} NetLineKind;

/* The highest handicap, clock choice and variant the game has. */
typedef struct { int handicap, clock, variant; } NetLimits;

#define NET_NAME_BYTES 40
#define NET_CHAT_BYTES 1000
/* The most a clock may hold or a turn may have taken, in ms. */
#define NET_MAX_MS (48 * 3600 * 1000L)

typedef struct {
    NetLineKind kind;
    /* The line's numbers, within what the game takes whatever the line says. VERSION: the
     * version. GAME: handicap, clock choice, the joiner's side (0 or 1), move rule. CLOCK: main,
     * byoyomi and increment seconds. VARIANT: the variant. READY, TAKEBACK_ANSWER: 0 or 1.
     * MOVES: how many fields follow (three clocks, then the moves). */
    int n[4];
    long ms[3]; /* MOVES: each side's clock and what this turn has used */
    /* NAME and CHAT: the text, whole UTF-8 characters up to the limits above (in `buf`). SFEN: the position, NULL for the usual one. MOVES: its fields. ILLEGAL: the
     * move, NULL for none. MOVE: the line. */
    const char *text;
    char buf[NET_CHAT_BYTES + 1];
} NetLine;

/* A room as the host announces it on the LAN: the host's name and the room's settings as numbers,
 * joined by tabs, so that each joiner writes them in its own language. */
typedef struct {
    char name[NET_NAME_BYTES + 1];
    int password, variant, clock, main_s, byoyomi_s, increment_s, setup, handicap;
} NetRoom;
/* The announcement for `r`; free it. */
char *net_room_text(const NetRoom *r);
/* Reads an announcement, the numbers within `limits`; 0 for one that is only a name (copied, cut
 * as a name is). */
int net_room_parse(const char *text, const NetLimits *limits, NetRoom *r);

/* The fields of MOVES (`text` there): how many, and the three clocks in `ms`, the turn's not below 0. */
int net_moves_fields(const char *fields, long ms[3]);
void net_parse(const char *line, const NetLimits *limits, NetLine *out);

#endif
