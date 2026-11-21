/* Shogi's side of a game: handicaps, how games end, impasse, and positions for the engine. */

#include "game.h"
#include "i18n.h"

#include <stdio.h>
#include <string.h>

/* In handicap games the stronger player (上手, gote) removes pieces and moves first. */
static const struct { const char *name, *sfen; } HANDICAPS[HANDICAP_COUNT] = {
    {"平手", "lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1"},
    {"香落ち", "lnsgkgsn1/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL w - 1"},
    {"角落ち", "lnsgkgsnl/1r7/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL w - 1"},
    {"飛車落ち", "lnsgkgsnl/7b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL w - 1"},
    {"飛香落ち", "lnsgkgsn1/7b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL w - 1"},
    {"二枚落ち", "lnsgkgsnl/9/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL w - 1"},
    {"四枚落ち", "1nsgkgsn1/9/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL w - 1"},
    {"六枚落ち", "2sgkgs2/9/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL w - 1"},
    {"八枚落ち", "3gkg3/9/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL w - 1"},
    {"十枚落ち", "4k4/9/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL w - 1"},
};

const char *handicap_name_ja(Handicap h) { return HANDICAPS[h].name; }
const char *handicap_sfen(Handicap h) { return HANDICAPS[h].sfen; }

const char *handicap_label(Handicap h)
{
    static const char *const EN[HANDICAP_COUNT] = {"Even", "Lance", "Bishop", "Rook", "Rook and lance",
                                                      "2 pieces", "4 pieces", "6 pieces", "8 pieces", "10 pieces"};
    return i18n_japanese() ? HANDICAPS[h].name : tr(EN[h]);
}

Handicap handicap_start(Handicap h, Position *start)
{
    if (rules_variant != V_STANDARD) {
        pos_start(start);
        return HANDICAP_NONE;
    }
    pos_from_text(start, HANDICAPS[h].sfen);
    return h;
}

/* 千日手, as lishogi plays it (scalashogi's variants): the fourth time a position comes back, a draw,
 * or a loss for the side that gave check all along; in Minishogi a loss for sente (Fairy-Stockfish
 * too); in Kyotoshogi and Dōbutsu the third time, a draw. Tori shogi, which lishogi lacks: the third
 * time is a loss for the side that brought it back (Wikipedia, PyChess; Fairy-Stockfish waits for
 * the fourth). Euroshogi: the third time a draw, perpetual check a loss (Wikipedia; Fairy-Stockfish
 * waits for the fourth). */
static void check_repetition(Game *g)
{
    uint64_t h = g->hashes[g->count];
    int seen = 0, first = -1;
    for (int i = 0; i <= g->count; i++)
        if (g->hashes[i] == h) {
            if (first < 0)
                first = i;
            seen++;
        }
    int v = rules_variant;
    bool three = v == V_KYOTOSHOGI || v == V_DOBUTSU || v == V_TORI || v == V_EURO;
    if (seen < (three ? 3 : 4))
        return;
    if (v == V_TORI) {
        game_end(g, loss_for(!g->pos.side), REASON_REPETITION);
        return;
    }
    if (three && v != V_EURO) {
        game_end(g, RESULT_DRAW, REASON_REPETITION);
        return;
    }
    /* A side that gave check on every move of the cycle loses (連続王手の千日手). */
    for (int color = 0; color < 2; color++) {
        bool all = true, any = false;
        for (int j = first; j < g->count; j++) {
            if ((g->start.side + j) % 2 != color)
                continue;
            any = true;
            all &= g->gave_check[j];
        }
        if (any && all) {
            game_end(g, loss_for(color), REASON_PERPETUAL_CHECK);
            return;
        }
    }
    game_end(g, v == V_MINISHOGI ? RESULT_SECOND_WIN : RESULT_DRAW, REASON_REPETITION);
}

/* After each move: the ways a shogi game ends (checkmate, 千日手, a variant's own wins). */
void game_judge(Game *g)
{
    Move mv = g->moves[g->count - 1];
    int mover = !g->pos.side;
    if (mv.captured && TYPE(mv.captured) == OU) {
        game_end(g, loss_for(g->pos.side), REASON_KING);
        return;
    }
    if (rules_variant == V_CHECKSHOGI && pos_in_check(&g->pos)) {
        game_end(g, loss_for(g->pos.side), REASON_CHECK);
        return;
    }
    if (rules_variant == V_DOBUTSU) {
        int mk = pos_king(&g->pos, mover), ok = pos_king(&g->pos, g->pos.side);
        bool mt = mk >= 0 && in_zone(mover, ROW(mk)) && !pos_attacked(&g->pos, mk, g->pos.side);
        bool ot = ok >= 0 && in_zone(g->pos.side, ROW(ok)) && !pos_attacked(&g->pos, ok, mover);
        if (mt || ot) {
            if (mt && ot)
                game_end(g, RESULT_DRAW, REASON_TRY);
            else if (mt)
                game_end(g, loss_for(g->pos.side), REASON_TRY);
            else
                game_end(g, loss_for(mover), REASON_TRY);
            return;
        }
    }
    if (!pos_has_legal_move(&g->pos))
        game_end(g, loss_for(g->pos.side), REASON_CHECKMATE);
    else
        check_repetition(g);
}

/* Board/hand letters Fairy-Stockfish uses for the variants whose SFEN differs from lishogi's. */
static char engine_board_letter(int type)
{
    if (rules_variant == V_DOBUTSU) {
        switch (type) {
        case FU: case TO: return 'C';
        case HI: return 'G';
        case KA: return 'E';
        case OU: return 'L';
        default: return ' ';
        }
    }
    switch (type) { /* Kyotoshogi */
    case FU: case HI: return 'P';
    case GI: case KA: return 'S';
    case KI: case KE: return 'N';
    case TO: case KY: return 'L';
    case OU: return 'K';
    default: return ' ';
    }
}

static char engine_hand_letter(int type)
{
    if (rules_variant == V_DOBUTSU) {
        switch (type) {
        case FU: return 'C';
        case HI: return 'G';
        case KA: return 'E';
        default: return ' ';
        }
    }
    switch (type) { /* Kyotoshogi */
    case FU: return 'P';
    case KI: return 'N';
    case GI: return 'S';
    case TO: return 'L';
    default: return ' ';
    }
}

static void engine_sfen(const Position *p, char *buf, int size)
{
    if (rules_variant != V_DOBUTSU && rules_variant != V_KYOTOSHOGI) {
        pos_to_text(p, buf, size);
        return;
    }
    char out[256];
    int n = 0;
    for (int row = 0; row < board_h; row++) {
        int empty = 0;
        for (int col = 0; col < board_w; col++) {
            uint8_t pc = p->board[SQ(row, col)];
            if (!pc) {
                empty++;
                continue;
            }
            if (empty)
                out[n++] = '0' + empty, empty = 0;
            int t = TYPE(pc);
            if (rules_variant == V_DOBUTSU ? t == TO
                                           : (t == HI || t == KA || t == KI || t == TO))
                out[n++] = '+';
            char c = engine_board_letter(t);
            out[n++] = COLOR(pc) == SECOND ? c | 32 : c;
        }
        if (empty)
            out[n++] = '0' + empty;
        if (row < board_h - 1)
            out[n++] = '/';
    }
    out[n++] = ' ';
    out[n++] = p->side == FIRST ? 'b' : 'w';
    out[n++] = ' ';
    int count, start = n;
    const int *order = hand_order(&count);
    for (int c = 0; c < 2; c++)
        for (int i = 0; i < count; i++) {
            int t = order[i], k = p->hand[c][hand_slot(t)];
            if (!k)
                continue;
            if (k > 1)
                n += sprintf(out + n, "%d", k);
            char l = engine_hand_letter(t);
            out[n++] = c == SECOND ? l | 32 : l;
        }
    if (n == start)
        out[n++] = '-';
    n += sprintf(out + n, " %d", p->ply + 1);
    snprintf(buf, size, "%.*s", n, out);
}

/* One move in the engine's spelling (Dobutsu drops P/R/B -> C/G/E; Kyotoshogi promotion/roles). */
static void engine_move_usi(const Position *before, const Move *m, char *buf, int size)
{
    move_to_usi(m, buf);
    if (rules_variant == V_DOBUTSU && buf[1] == '*') {
        if (buf[0] == 'P') buf[0] = 'C';
        else if (buf[0] == 'R') buf[0] = 'G';
        else if (buf[0] == 'B') buf[0] = 'E';
    } else if (rules_variant == V_KYOTOSHOGI) {
        if (buf[1] == '*') {
            const char *role = buf[0] == 'G' ? "+N" : buf[0] == 'T' ? "+L" : buf[0] == 'B' ? "+S"
                               : buf[0] == 'R' ? "+P"
                                               : NULL;
            if (role && size > 0) {
                char tmp[32];
                snprintf(tmp, sizeof tmp, "%s%s", role, buf + 1);
                size_t l = strlen(tmp);
                if ((int) l >= size)
                    l = size - 1;
                memcpy(buf, tmp, l);
                buf[l] = 0;
            }
        } else if (m->promote) {
            int t = TYPE(before->board[m->from]);
            size_t l = strlen(buf);
            if ((t == KI || t == TO || t == KA || t == HI) && l && buf[l - 1] == '+')
                buf[l - 1] = '-';
        }
    }
}

void game_engine_position(const Game *g, char *buf, int size)
{
    char sfen[192];
    engine_sfen(&g->start, sfen, sizeof sfen);
    int n = snprintf(buf, size, "position sfen %s", sfen);
    if (g->count && n < size)
        n += snprintf(buf + n, size - n, " moves");
    Position p = g->start;
    for (int i = 0; i < g->count && n < size; i++) {
        char usi[16];
        engine_move_usi(&p, &g->moves[i], usi, sizeof usi);
        n += snprintf(buf + n, size - n, " %s", usi);
        Move m = g->moves[i];
        pos_make(&p, &m);
    }
}

bool game_move_from_engine(Position *p, const char *text, Move *m)
{
    char usi[16];
    snprintf(usi, sizeof usi, "%s", text);
    if (rules_variant == V_DOBUTSU && usi[1] == '*') {
        if (usi[0] == 'C' || usi[0] == 'c') usi[0] = 'P';
        else if (usi[0] == 'G' || usi[0] == 'g') usi[0] = 'R';
        else if (usi[0] == 'E' || usi[0] == 'e') usi[0] = 'B';
    } else if (rules_variant == V_KYOTOSHOGI) {
        if (usi[0] == '+' && usi[2] == '*') {
            char role = usi[1] == 'N' || usi[1] == 'n' ? 'G' : usi[1] == 'L' || usi[1] == 'l' ? 'T'
                        : usi[1] == 'S' || usi[1] == 's' ? 'B'
                        : usi[1] == 'P' || usi[1] == 'p' ? 'R'
                                                         : 0;
            if (role) {
                char tmp[16];
                snprintf(tmp, sizeof tmp, "%c%s", role, usi + 2);
                snprintf(usi, sizeof usi, "%s", tmp);
            }
        } else {
            size_t l = strlen(usi);
            if (l && usi[l - 1] == '-')
                usi[l - 1] = '+';
        }
    }
    return move_from_usi(p, usi, m);
}

const char *move_problem(Position *p, const Move *m)
{
    Move mv = *m;
    if (pos_is_legal(p, &mv))
        return NULL;
    static Move all[MAX_MOVES];
    int n = pos_physical_moves(p, all), i = 0;
    while (i < n && (all[i].from != mv.from || all[i].to != mv.to))
        i++;
    if (i == n || !pos_is_possible(p, &mv))
        return tr("That piece does not move that way.");
    int us = p->side, drop = IS_DROP(mv), type = drop ? drop_type(&mv) : TYPE(p->board[mv.from]);
    if (rules_variant != V_KYOTOSHOGI && rules_variant != V_TORI && !mv.promote && must_promote(type, us, mv.to))
        return drop ? tr("A piece may not be dropped where it could never move again.")
                    : tr("There the piece could never move again: it must promote.");
    if (drop && type == FU && VARIANTS[rules_variant].nifu) {
        int same = 0;
        for (int r = 0; r < board_h; r++)
            same += p->board[SQ(r, COL(mv.to))] == PIECE(FU, us);
        if (same >= VARIANTS[rules_variant].pawns_per_file)
            return tr("Two unpromoted pawns of one side on a file (二歩) are not allowed.");
    }
    pos_make(p, &mv);
    int k = pos_king(p, us);
    bool check = k >= 0 && pos_attacked(p, k, p->side);
    pos_unmake(p, &mv);
    if (check)
        return tr("That would leave your king in check.");
    if (drop && type == FU)
        return tr("A pawn drop may not give checkmate (打ち歩詰め).");
    return tr("That move is not allowed.");
}

bool game_has_declare(void) { return rules_variant == V_STANDARD || rules_variant == V_CHECKSHOGI; }

bool game_declare(Game *g)
{
    if (g->result != RESULT_NONE || !pos_can_declare(&g->pos))
        return false;
    game_end(g, g->pos.side == FIRST ? RESULT_FIRST_WIN : RESULT_SECOND_WIN, REASON_IMPASSE);
    return true;
}

/* lishogi hands Fairy-Stockfish every non-standard variant, Checkshogi too. */
const char *variant_engine_name(int variant)
{
    switch (variant) {
    case V_MINISHOGI: return "minishogi";
    case V_KYOTOSHOGI: return "kyotoshogi";
    case V_CHECKSHOGI: return "checkshogi";
    case V_DOBUTSU: return "dobutsu";
    case V_GOROGORO: return "gorogoro";
    case V_TORI: return "torishogi";
    case V_JUDKINS: return "judkins";
    case V_EURO: return "euroshogi";
    default: return "shogi";
    }
}

/* Dōbutsu has its animal set, with the dots of the real pieces. */
/* Dōbutsu, and Goro Goro, which is ごろごろどうぶつしょうぎ (日本将棋連盟, 北尾まどか supervising): the
 * same animal pieces with their moves marked, a dog for the gold and a cat for the silver. */
static bool animals(int variant) { return variant == V_DOBUTSU || variant == V_GOROGORO; }

/* Tori shogi: the plainest of the sets with its birds, whose kanji have many strokes (the usual set, UnGungseo, has none). */
const char *variant_pieces_default(int variant) { return animals(variant) ? "dobutsu.ini" : variant == V_TORI ? "kanji1.ini" : NULL; }

/* Tori shogi's birds move unlike any shogi piece: their marks shown at first. */
int variant_marks_default(int variant) { return animals(variant) || variant == V_TORI ? 1 : -1; }
int variant_colored_default(int variant) { return animals(variant) ? 1 : -1; }
int variant_shape_default(int variant) { return animals(variant) ? 1 : -1; }
const char *variant_sound_default(int variant) { return animals(variant) ? "6-Bubbles" : variant == V_TORI ? "9-Birds" : NULL; }

bool variant_shows_check(void) { return rules_variant != V_DOBUTSU; }

const char *reason_text(const Game *g)
{
    switch (g->reason) {
    case REASON_CHECKMATE: return tr("Checkmate");
    case REASON_RESIGN: return tr("Resignation");
    case REASON_TIME: return tr("Time");
    case REASON_REPETITION: return tr("Repetition (sennichite)");
    case REASON_PERPETUAL_CHECK: return tr("Perpetual check");
    case REASON_IMPASSE: return g->result == RESULT_DRAW ? tr("Impasse (jishōgi)") : tr("Impasse declaration");
    case REASON_ILLEGAL: return tr("Illegal move");
    case REASON_CHECK: return tr("Check");
    case REASON_KING: return tr("King captured");
    case REASON_TRY: return tr("Try rule");
    default: return "";
    }
}

/* The standard board's four, a third of the way in from each side. */
int board_stars(int xy[][2])
{
    if (board_w != 9 || board_h != 9)
        return 0;
    for (int i = 0; i < 4; i++)
        xy[i][0] = i % 2 ? 6 : 3, xy[i][1] = i / 2 ? 6 : 3;
    return 4;
}
