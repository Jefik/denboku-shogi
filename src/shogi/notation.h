#ifndef NOTATION_H
#define NOTATION_H

#include "shogi.h"

/* Move text for the move list. `p` is the position before the move and
 * `prev_to` the destination of the previous move (-1 if none), for 同. */
void move_to_japanese(Position *p, const Move *m, int prev_to, char *buf, int size);
void move_to_western(Position *p, const Move *m, char *buf, int size);
/* The move list's text: Japanese (`native`) or Western. */
void move_notation(Position *p, const Move *m, int prev_to, bool native, char *buf, int size);
/* A move in each notation (native, Western) as Preferences shows them, and the widest a move list
 * cell holds, with its number. */
static const char *const NOTATION_SAMPLES[2] = {"▲７六歩", "☗P-7f"};
static const char *const NOTATION_WIDEST[2] = {"888 ▲８八成銀", "888 ☗+Bx8h"};

/* The board's coordinates, by column and row: files 9…1 and ranks 一…九 (`native`) or a…i. */
void file_label(int col, bool native, char *buf, int size);
void rank_label(int row, bool native, char *buf, int size);

/* Piece names in Japanese, used by the UI and KIF. */
const char *piece_kanji(int type);

/* Full-width digit and kanji numeral for 1..9 ("" at 0). */
extern const char *const ZENKAKU_DIGITS[10];
extern const char *const KANJI_DIGITS[10];
const char *zenkaku_digit(int n);
const char *kanji_digit(int n);

#endif
