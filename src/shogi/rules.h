#ifndef RULES_H
#define RULES_H

/* What the app (src/core) needs from the game it plays. A fork for another game provides its own
 * src/<game>/ with a rules.h that has these names; see "Another game" in docs/CODE.md. */

#include <stdbool.h>
#include <stdio.h>

#include "notation.h"
#include "shogi.h"

typedef struct Game Game;

/* The engine speaks USI ("usi", "usinewgame", "position sfen …"); a chess fork, UCI. */
#define ENGINE_PROTOCOL "usi"

#define KING OU

/* The New Game dialog's side choices and clock presets (byoyomi or Fischer increment). */
static const char *const SIDE_LABELS[] = {"Sente (▲)", "Gote (△)", "Random side"};

/* The sides as the interface names them (English, the keys of the translations): their names, who
 * won, a mark before a player's name (the notation's ▲△, or the ☗☖ of Western texts), the Sandbox's
 * labels, and New Game's note on who gives a handicap (the side chosen: 0, 1, 2 random). */
static const char *const SIDE_NAMES[2] = {"Sente", "Gote"};
static const char *const SIDE_WINS[2] = {"Sente wins", "Gote wins"};
static const char *const SIDE_MARKS[2] = {"▲", "△"};
static const char *const SIDE_SYMBOLS[2] = {"☗", "☖"};
static const char *const SIDE_TO_MOVE[2] = {"▲ to move", "△ to move"};
static const char *const SIDE_PALETTE[2] = {"▲ Sente", "△ Gote"};
static const char *const SIDE_PIECES[2] = {"Sente's pieces", "Gote's pieces"};
static const char *const HANDICAP_NOTES[3] = {
    "You receive the handicap (shitate); Gote (uwate) gives it and moves first.",
    "You give the handicap (uwate) and move first; Sente receives it (shitate).",
    "Gote (uwate) gives the handicap and moves first; the side is drawn.",
};

/* The promotion question's two answers, in English (keys) and in the notation's own words. */
static const char *const PROMOTE_LABELS[2][2] = {{"Promote", "Keep"}, {"成", "不成"}};

/* Squares are this much taller than wide (a real shōgi board's); the text and the pieces in hand
 * are measured against a square of a board BOARD_USUAL wide. The star points (hoshi), as
 * intersections of lines (file, rank, from the top left), in *xy; their count. */
#define SQUARE_RATIO 1.075
#define BOARD_USUAL 9
int board_stars(int xy[][2]);

typedef struct { const char *label; int main_s, byoyomi_s, increment_s; } TimeControl;

static const TimeControl TIMES[] = {
    {"No clock", 0, 0, 0},
    {"5 min + 30 s byoyomi", 300, 30, 0},
    {"10 min + 30 s byoyomi", 600, 30, 0},
    {"15 min + 60 s byoyomi", 900, 60, 0},
    {"30 s byoyomi", 0, 30, 0},
    {"10 s byoyomi", 0, 10, 0},
    {"5 min + 5 s increment", 300, 0, 5},
    {"1 min", 60, 0, 0},
    {"3 min", 180, 0, 0},
    {"1 min + 1 s increment", 60, 0, 1},
    {"3 min + 2 s increment", 180, 0, 2},
    {"10 min + 5 s increment", 600, 0, 5},
    {"25 min + 10 s byoyomi", 1500, 10, 0},
};
#define NTIMES ((int) (sizeof TIMES / sizeof TIMES[0]))

/* Piece art (piece_art.c): kinds are the piece types, plus SECOND_KING for the second player's
 * king where it is drawn differently (王, the first player's being 玉), and Tori shogi's birds. */
#define PIECE_KINDS 26
#define SECOND_KING 16
enum { SWALLOW = 17, GOOSE, FALCON, EAGLE, CRANE, PHEASANT, LEFT_QUAIL, RIGHT_QUAIL, PHOENIX };
typedef struct {
    const char *code, *text1, *text2;
    double size;
    bool promoted;
    unsigned color; /* 0xRRGGBB, the background with Colored pieces on */
} PieceArt;
extern const PieceArt PIECE_ART[PIECE_KINDS];
/* A small character in a lower corner that tells apart two kinds of one name (Tori shogi's
 * quails, 左 and 右, as on real sets), and in *at which corner: -1 the left, 1 the right; NULL for none. */
const char *piece_side(int kind, int *at);
/* The kind a piece (PIECE(type, color)) is drawn as, and the type a kind moves as. */
int piece_kind(int piece);
int kind_type(int kind);
/* Samples for the piece set chooser; the _ANIMALS ones for a variant's own set (style_variant). */
extern const int PREVIEW_PIECES[], PREVIEW_COUNT, PREVIEW_ANIMALS[], PREVIEW_ANIMALS_COUNT;
/* The samples for `variant` (-1: all) when it has pieces of its own (Tori shogi's birds), else NULL; *n their count. */
const int *preview_kinds(int variant, int *n);

/* Why a game ended; the app knows the first few, the rest are the game's own. */
typedef enum {
    REASON_NONE, REASON_CHECKMATE, REASON_RESIGN, REASON_TIME, REASON_REPETITION,
    REASON_PERPETUAL_CHECK, REASON_IMPASSE, REASON_ILLEGAL, REASON_ABORT,
    REASON_CHECK, REASON_KING, REASON_TRY,
} Reason;

typedef enum {
    HANDICAP_NONE, HANDICAP_LANCE, HANDICAP_BISHOP, HANDICAP_ROOK, HANDICAP_ROOK_LANCE,
    HANDICAP_2, HANDICAP_4, HANDICAP_6, HANDICAP_8, HANDICAP_10, HANDICAP_COUNT,
} Handicap;

const char *handicap_name_ja(Handicap h);
/* Its name in the interface language (Japanese names as such). */
const char *handicap_label(Handicap h);
const char *handicap_sfen(Handicap h);
/* The start position of a handicap in the current variant; returns the handicap it got (none
 * outside standard shogi). */
Handicap handicap_start(Handicap h, Position *start);

/* After each move (game->moves[count - 1]): ends the game if that move ended it. */
void game_judge(Game *g);
/* The "position …" command for the engine, and its best move read back. */
void game_engine_position(const Game *g, char *buf, int size);
bool game_move_from_engine(Position *p, const char *text, Move *m);
/* Declares impasse for the side to move; ends the game if allowed. */
bool game_declare(Game *g);
/* Whether the variant in play has a declaration at all (whether it holds now or not). */
bool game_has_declare(void);
/* Why a declaration fails, as the app says it (English, a key). */
#define DECLARE_RULE_TEXT                                                                                          \
    "You cannot declare impasse now: the king and ten pieces must be in the promotion zone, "                      \
    "with 28 points for Sente or 27 for Gote, and the king not in check."
/* The strategies of the game up to `ply` as players name them, a line per side and translated (in
 * shogi each side's rook and castle, and the Bishop Exchange); "" when it has none to name. */
void game_style(const Game *g, int ply, char *buf, int size);
/* Why the game ended (g->reason), translated; "" for none. */
const char *reason_text(const Game *g);

/* Records: the app's own format (KIF here: saved games, the autosave, the history) and a second
 * one it also saves and opens (CSA). */
#define RECORD_EXT "kif"
#define RECORD_NAME "KIF"
#define RECORD_ALT_EXT "csa"
#define RECORD_ALT_NAME "CSA"
bool record_write(const Game *g, FILE *f);
bool record_read(Game *g, FILE *f);
bool record_read_text(Game *g, const char *text);
bool record_write_alt(const Game *g, FILE *f);
bool record_read_alt(Game *g, FILE *f);
/* Text in either format (pasted): told apart by what it holds. */
bool record_read_any_text(Game *g, const char *text);
/* Labels and messages about records (English, keys). */
#define RECORD_SAVE_ALT_LABEL "Save as CSA"
#define RECORD_COPY_LABEL "Copy Game (KIF)"
#define RECORD_NONE_TEXT "The clipboard holds no KIF or CSA game."
/* The chat's quick lines, as Lishogi offers them ("hi/Hello": the button and what it sends): [Japanese
 * interface][after the game], each list ending in NULL. */
extern const char *const CHAT_PRESETS[2][2][6];
/* Mate problems' introduction. */
#define PROBLEM_INTRO_TEXT                                                                                         \
    "You play Sente and have to checkmate, every move giving check. The engine defends as long as it can; Hint "  \
    "shows the next move."

/* One move as text between two copies of the app (LAN games): CSA ("+7776FU"). Reading checks
 * that it is legal, or for _any only that it can be shown (pos_is_possible). */
void csa_move(const Position *p, const Move *m, char *buf);
bool csa_parse_move(Position *p, const char *line, Move *m);
bool csa_parse_any(const Position *p, const char *line, Move *m);
bool game_read_csa_text(Game *g, const char *text);
#define move_to_net csa_move
#define move_from_net csa_parse_move
#define move_from_net_any csa_parse_any

/* Setting up a position (setup.c): the pieces of the current variant for the board and for the
 * hands, and why a position cannot be played (NULL if it can). */
int setup_piece_types(int *types);
int setup_hand_types(int *types);
const char *setup_problem(const Position *p);
/* The pieces in hand, in the order they are shown. */
/* A square as the game writes it (5d; USI), -1 if it is not one of the board's. */
int square_from_text(const char *s);
/* Why a move a piece can make (pos_any_moves) is not allowed (二歩, 打ち歩詰め, a king left in
 * check…), translated; NULL when it is legal. */
const char *move_problem(Position *p, const Move *m);
/* Whether a king in check is marked (not in Dōbutsu, where the lion may be taken). */
bool variant_shows_check(void);

/* The LAN protocol version (NET_VERSION) a peer needs to play a variant. */
/* Fairy-Stockfish's name for a variant. */
const char *variant_engine_name(int variant);
/* A variant's default piece set file (NULL: the usual one) and move marks (-1: the usual). */
const char *variant_pieces_default(int variant);
int variant_marks_default(int variant);
int variant_colored_default(int variant);
int variant_shape_default(int variant);
const char *variant_sound_default(int variant); /* a folder of data/sounds, or NULL */

#endif
