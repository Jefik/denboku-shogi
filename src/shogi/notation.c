#include "notation.h"

#include <stdio.h>
#include <string.h>

const char *const ZENKAKU_DIGITS[10] = {"", "１", "２", "３", "４", "５", "６", "７", "８", "９"};
const char *const KANJI_DIGITS[10] = {"", "一", "二", "三", "四", "五", "六", "七", "八", "九"};

const char *zenkaku_digit(int n) { return n >= 1 && n <= 9 ? ZENKAKU_DIGITS[n] : ""; }
const char *kanji_digit(int n) { return n >= 1 && n <= 9 ? KANJI_DIGITS[n] : ""; }

const char *piece_kanji(int type)
{
    if (rules_variant == V_TORI) {
        switch (type) {
        case FU: return "燕";
        case TO: return "鴈";
        case KA: return "鷹";
        case UM: return "鵰";
        case KI: return "鶴";
        case GI: return "雉";
        case KY: return "左鶉";
        case KE: return "右鶉";
        case OU: return "鵬";
        }
        return "";
    }
    switch (type) {
    case FU: return "歩";
    case KY: return "香";
    case KE: return "桂";
    case GI: return "銀";
    case KI: return "金";
    case KA: return "角";
    case HI: return "飛";
    case OU: return "玉";
    case TO: return "と";
    case NY: return "成香";
    case NK: return "成桂";
    case NG: return "成銀";
    case UM: return "馬";
    case RY: return "龍";
    }
    return "";
}

/* Board pieces of the mover's type that could also reach m->to (m->from included). */
static int candidates(Position *p, const Move *m, int *out)
{
    Move moves[MAX_MOVES];
    int n = pos_legal_moves(p, moves), k = 0;
    uint8_t pc = IS_DROP(*m) ? PIECE(drop_type(m), p->side) : p->board[m->from];
    for (int i = 0; i < n; i++) {
        if (IS_DROP(moves[i]) || moves[i].to != m->to || p->board[moves[i].from] != pc)
            continue;
        bool seen = false;
        for (int j = 0; j < k; j++)
            seen |= out[j] == moves[i].from;
        if (!seen)
            out[k++] = moves[i].from;
    }
    return k;
}

/* 1 forward, -1 backward, 0 sideways, from the mover's point of view. */
static int direction(int color, int from, int to)
{
    int d = ROW(from) - ROW(to);
    if (color == SECOND)
        d = -d;
    return d > 0 ? 1 : d < 0 ? -1 : 0;
}

/* Column as the mover sees it: smaller is further left. */
static int view_col(int color, int sq)
{
    return color == FIRST ? COL(sq) : board_w - 1 - COL(sq);
}

static void disambiguate(Position *p, const Move *m, char *out, size_t size)
{
    out[0] = 0;
    int cands[20];
    int n = candidates(p, m, cands);
    if (IS_DROP(*m)) {
        if (n)
            snprintf(out, size, "打");
        return;
    }
    if (n <= 1)
        return;
    int us = p->side, type = TYPE(p->board[m->from]);
    int dir = direction(us, m->from, m->to);
    int same_dir = 0;
    for (int i = 0; i < n; i++)
        same_dir += direction(us, cands[i], m->to) == dir;
    static const char *DIRS[] = {"引", "寄", "上"};
    if (same_dir == 1) {
        snprintf(out, size, "%s", DIRS[dir + 1]);
        return;
    }
    bool goldlike = rules_variant != V_TORI &&
                    (type == KI || type == GI || type == TO || type == NY || type == NK || type == NG);
    if (goldlike && dir == 1 && COL(m->from) == COL(m->to)) {
        snprintf(out, size, "直");
        return;
    }
    int mine = view_col(us, m->from), left = 0, right = 0;
    for (int i = 0; i < n; i++) {
        if (cands[i] == m->from || direction(us, cands[i], m->to) != dir)
            continue;
        int c = view_col(us, cands[i]);
        left += c < mine;
        right += c > mine;
    }
    snprintf(out, size, "%s%s", left == 0 ? "左" : right == 0 ? "右" : "", same_dir < n ? DIRS[dir + 1] : "");
}

/* The side that makes the move: the moving piece's (a lesson's step where the other side passes
 * has two moves of one side in a row), the side to move for a drop. */
static int mover(const Position *p, const Move *m)
{
    return IS_DROP(*m) || !p->board[m->from] ? p->side : COLOR(p->board[m->from]);
}

void move_to_japanese(Position *p, const Move *m, int prev_to, char *buf, int size)
{
    const char *mark = mover(p, m) == FIRST ? "▲" : "△";
    char square[16], extra[16];
    if (m->to == prev_to)
        snprintf(square, sizeof square, "同　");
    else
        snprintf(square, sizeof square, "%s%s", zenkaku_digit(FILE_OF(m->to)), kanji_digit(RANK_OF(m->to)));
    int type = IS_DROP(*m) ? drop_type(m) : TYPE(p->board[m->from]);
    disambiguate(p, m, extra, sizeof extra);
    const char *promo = "";
    if (!IS_DROP(*m) && can_promote(type, p->side, m->from, m->to))
        promo = m->promote ? "成" : "不成";
    snprintf(buf, size, "%s%s%s%s%s", mark, square, piece_kanji(type), extra, promo);
}

static const char *western_piece(int type)
{
    if (rules_variant == V_TORI) { /* Fairy-Stockfish's letters */
        switch (type) {
        case FU: return "S";
        case TO: return "+S";
        case KA: return "F";
        case UM: return "+F";
        case KI: return "C";
        case GI: return "P";
        case KY: return "L";
        case KE: return "R";
        case OU: return "K";
        }
        return "";
    }
    switch (type) {
    case FU: return "P";
    case KY: return "L";
    case KE: return "N";
    case GI: return "S";
    case KI: return "G";
    case KA: return "B";
    case HI: return "R";
    case OU: return "K";
    case TO: return "+P";
    case NY: return "+L";
    case NK: return "+N";
    case NG: return "+S";
    case UM: return "+B";
    case RY: return "+R";
    }
    return "";
}

void move_to_western(Position *p, const Move *m, char *buf, int size)
{
    const char *mark = mover(p, m) == FIRST ? "☗" : "☖";
    char to[4];
    snprintf(to, sizeof to, "%d%c", FILE_OF(m->to), 'a' + ROW(m->to));
    if (IS_DROP(*m)) {
        snprintf(buf, size, "%s%s*%s", mark, western_piece(drop_type(m)), to);
        return;
    }
    int type = TYPE(p->board[m->from]);
    int cands[20];
    char origin[4] = "";
    if (candidates(p, m, cands) > 1)
        snprintf(origin, sizeof origin, "%d%c", FILE_OF(m->from), 'a' + ROW(m->from));
    const char *promo = "";
    if (can_promote(type, p->side, m->from, m->to) && !must_promote(type, p->side, m->to))
        promo = m->promote ? "+" : "=";
    else if (m->promote)
        promo = "+";
    snprintf(buf, size, "%s%s%s%s%s%s", mark, western_piece(type), origin, p->board[m->to] ? "x" : "-", to, promo);
}

void move_notation(Position *p, const Move *m, int prev_to, bool native, char *buf, int size)
{
    if (native)
        move_to_japanese(p, m, prev_to, buf, size);
    else
        move_to_western(p, m, buf, size);
}

void file_label(int col, bool native, char *buf, int size)
{
    (void) native;
    snprintf(buf, size, "%d", board_w - col);
}

void rank_label(int row, bool native, char *buf, int size)
{
    if (native)
        snprintf(buf, size, "%s", kanji_digit(row + 1));
    else
        snprintf(buf, size, "%c", 'a' + row);
}
