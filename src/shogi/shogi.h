#ifndef SHOGI_H
#define SHOGI_H

#include <stdbool.h>
#include <stdint.h>

/* Squares: index = row * width + col, row 0 is rank 一 (top, gote's side),
 * col 0 is file width (left as sente sees the board). The board may be 9×9, 7×7, 5×5, 5×6 or 3×4. */
extern int board_w, board_h, rules_variant;
#define SQ(row, col) ((row) * board_w + (col))
#define ROW(sq) ((sq) / board_w)
#define COL(sq) ((sq) % board_w)
#define FILE_OF(sq) (board_w - COL(sq))
#define RANK_OF(sq) (ROW(sq) + 1)
#define SQUARES (board_w * board_h)
#define BOARD_MAX 81

enum { FIRST = 0, SECOND = 1 };

/* Variants (the lishogi set, and Tori shogi). The rules table carries the board size and the
 * differences from standard shogi; the pieces are standard types (Dobutsu maps its animals to them,
 * Tori its birds: swallow FU→goose TO, falcon KA→eagle UM, crane KI, pheasant GI, left quail KY,
 * right quail KE, phoenix OU; Judkins shogi is Minishogi on 6×6 with a knight; Euroshogi's knight also steps sideways). Saved games keep the number: new variants go last. */
typedef enum {
    V_STANDARD, V_MINISHOGI, V_KYOTOSHOGI, V_CHECKSHOGI, V_DOBUTSU, V_GOROGORO, V_TORI, V_JUDKINS, V_EURO, V_COUNT
} Variant;

typedef struct {
    int w, h;
    bool drops, nifu, check_win, king_capture, try_rule;
    bool promo_anywhere, forced_promo;
    int promo_rows; /* ranks in the promotion zone (from each side) */
    int pawns_per_file; /* unpromoted pawns of one side a file may hold (nifu: 1) */
    const char *start_sfen;
} VariantRules;

extern const VariantRules VARIANTS[V_COUNT];
void rules_set_variant(int variant);
/* Role after/before a Kyotoshogi flip; for the other variants it is the usual |PROMOTED. */
int promote_type(int type);
int unpromote_type(int type);
/* Role a captured piece goes to in hand, and its slot (TO has its own slot). */
int hand_type(int type);
int hand_slot(int type);
const int *hand_order(int *count);
bool in_zone(int color, int row);
const char *variant_name(int variant);

enum {
    EMPTY = 0,
    FU, KY, KE, GI, KI, KA, HI, OU,
    PROMOTED = 8,
    TO = FU | PROMOTED, NY = KY | PROMOTED, NK = KE | PROMOTED, NG = GI | PROMOTED,
    UM = KA | PROMOTED, RY = HI | PROMOTED,
};

/* A piece is type | (color << 4). */
#define PIECE(type, color) ((uint8_t) ((type) | ((color) << 4)))
#define TYPE(p) ((p) & 15)
#define COLOR(p) ((p) >> 4)
#define BASE(t) ((t) == OU ? OU : (t) & 7)

#define HAND_TYPES 7 /* FU..HI */
#define HAND_TO_SLOT 8 /* Kyotoshogi's tokin in hand (TO is 9, outside FU..HI) */
#define HAND_SLOTS 9
/* Pieces of one kind in hand when set up or read: a set has 18 pawns, the sandbox takes more.
 * Captures may add the board's 81 to it. */
#define HAND_MAX 99

typedef struct {
    uint8_t from;   /* square, or DROP + type for drops */
    uint8_t to;
    uint8_t promote;
    uint8_t piece;  /* moving piece (filled by generation) */
    uint8_t captured;
} Move;

#define DROP 81
#define IS_DROP(m) ((m).from >= DROP)
#define DROP_TYPE(m) ((m).from - DROP)
int drop_type(const Move *m);
void drop_from_face(Move *m);

typedef struct {
    uint8_t board[81];
    uint8_t hand[2][HAND_SLOTS]; /* FU..HI, and TO at HAND_TO_SLOT */
    int side;
    int ply;       /* number of moves played since the start position */
    uint64_t hash;
} Position;

#define MAX_MOVES 8192 /* pos_any_moves can list ~7000 */

void rules_init(void);
void pos_start(Position *p);
/* A position as text: SFEN. */
bool pos_from_text(Position *p, const char *sfen);
void pos_to_text(const Position *p, char *buf, int size);
void pos_make(Position *p, Move *m);
void pos_unmake(Position *p, const Move *m);
int pos_king(const Position *p, int color);
bool pos_attacked(const Position *p, int sq, int by);
bool pos_in_check(const Position *p);
int pos_legal_moves(Position *p, Move *moves);
bool pos_has_legal_move(Position *p);
/* Every move a piece can physically make, legal or not (for the "illegal moves lose" rule):
 * self-check, nifu, uchifuzume, dead pieces and dead drops included. */
int pos_physical_moves(const Position *p, Move *moves);
/* Any piece to any square not holding one of ours, as on a real board (moves a piece cannot
 * make lose too). */
int pos_any_moves(const Position *p, Move *moves);
bool pos_is_legal(Position *p, Move *m);
/* Whether the move is one of pos_any_moves (filling in the rest of *m): an illegal move that can
 * still be shown on the board. */
bool pos_is_possible(const Position *p, Move *m);
bool can_promote(int type, int color, int from, int to);
bool must_promote(int type, int color, int to);
/* Whether the promoted piece still makes every move of the unpromoted one (pawn, bishop, rook;
 * not lance, knight or silver). */
bool promotion_keeps_moves(int type);
uint64_t perft(Position *p, int depth);

/* Movement of a piece type from sente's side (dr < 0 is forward, dc < 0 to its left); returns the
 * count (at most 8). `slide`: 0 a step or a jump, 1 any distance, more: up to that many squares. */
int piece_steps(int type, int *dr, int *dc, int *slide);

/* USI move text ("7g7f", "P*5e", "8h2b+"). */
void move_to_usi(const Move *m, char *buf);
bool move_from_usi(Position *p, const char *s, Move *m);

/* Impasse (jishogi) declaration, 27-point rule (sente needs 28 points, gote 27,
 * king and ten pieces in the zone, not in check). */
bool pos_can_declare(const Position *p);

#endif
