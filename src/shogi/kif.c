/* KIF (with BOD) and CSA records. */

#include "game.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "notation.h"

/* ---- KIF ---- */

/* 千日手 is a draw, but a loss for sente in Minishogi and for the side that repeated (it moved last)
 * in Tori shogi (game_rules.c). */
static Result repetition_result(const Game *g)
{
    if (rules_variant == V_MINISHOGI)
        return RESULT_SECOND_WIN;
    if (rules_variant != V_TORI)
        return RESULT_DRAW;
    return g->pos.side == FIRST ? RESULT_FIRST_WIN : RESULT_SECOND_WIN;
}

static void kif_move(Position *p, const Move *m, int prev_to, char *buf, int size)
{
    char square[16];
    if (m->to == prev_to)
        strcpy(square, "同　");
    else
        snprintf(square, sizeof square, "%s%s", zenkaku_digit(FILE_OF(m->to)), kanji_digit(RANK_OF(m->to)));
    if (IS_DROP(*m)) {
        snprintf(buf, size, "%s%s打", square, piece_kanji(drop_type(m)));
        return;
    }
    int type = TYPE(p->board[m->from]);
    const char *promo = "";
    if (can_promote(type, p->side, m->from, m->to))
        promo = m->promote ? "成" : "不成";
    snprintf(buf, size, "%s%s%s(%d%d)", square, piece_kanji(type), promo, FILE_OF(m->from), RANK_OF(m->from));
}

static const char *result_word(const Game *g)
{
    switch (g->reason) {
    case REASON_CHECKMATE: return "詰み";
    case REASON_RESIGN: return "投了";
    case REASON_TIME: return "切れ負け";
    case REASON_REPETITION: return "千日手";
    case REASON_PERPETUAL_CHECK: return "反則負け";
    case REASON_IMPASSE: return g->result == RESULT_DRAW ? "持将棋" : "入玉勝ち";
    case REASON_ILLEGAL: return "反則負け";
    case REASON_ABORT: return "中断";
    default: return NULL;
    }
}

/* A start position that is not one of the handicaps (made in the position editor). */
static int utf8_find(const char *s, const char *const *list, int n, int *len);
static int kif_piece(const char *s, int *len);

static void set_start(Game *g, const Position *p)
{
    char sfen[160];
    pos_to_text(p, sfen, sizeof sfen);
    pos_from_text(&g->start, sfen); /* fills in the hash */
    g->pos = g->start;
    g->hashes[0] = g->pos.hash;
}

static bool custom_start(const Game *g)
{
    Position p;
    pos_from_text(&p, handicap_sfen(g->handicap));
    return memcmp(p.board, g->start.board, sizeof p.board) || memcmp(p.hand, g->start.hand, sizeof p.hand) ||
           p.side != g->start.side;
}

static const char *BOD_PIECES[16] = {[FU] = "歩", [KY] = "香", [KE] = "桂", [GI] = "銀", [KI] = "金", [KA] = "角",
                                     [HI] = "飛", [OU] = "玉", [TO] = "と", [NY] = "杏", [NK] = "圭", [NG] = "全",
                                     [UM] = "馬", [RY] = "龍"};
static const int HAND_KIF_ORDER[HAND_TYPES] = {HI, KA, KI, GI, KE, KY, FU};

static void bod_hand(const Position *p, int c, FILE *f)
{
    bool any = false;
    fputs(c == FIRST ? "先手の持駒：" : "後手の持駒：", f);
    for (int i = 0; i < HAND_TYPES; i++) {
        int t = HAND_KIF_ORDER[i], n = p->hand[c][t];
        if (!n)
            continue;
        /* 二十五 or 百二: the sandbox may have more than a set's 18 pawns. */
        int tens = n / 10 % 10;
        fprintf(f, "%s%s%s%s%s%s", any ? "　" : "", BOD_PIECES[t], n >= 100 ? "百" : "",
                tens >= 2 ? KANJI_DIGITS[tens] : "", tens ? "十" : "", n == 1 ? "" : KANJI_DIGITS[n % 10]);
        any = true;
    }
    fputs(any ? "\n" : "なし\n", f);
}

/* The KIF board diagram (BOD) for a custom start. */
static void write_bod(const Position *p, FILE *f)
{
    bod_hand(p, SECOND, f);
    fputs("  ９ ８ ７ ６ ５ ４ ３ ２ １\n+---------------------------+\n", f);
    for (int r = 0; r < 9; r++) {
        fputc('|', f);
        for (int c = 0; c < 9; c++) {
            uint8_t pc = p->board[SQ(r, c)];
            if (pc)
                fprintf(f, "%c%s", COLOR(pc) == SECOND ? 'v' : ' ', BOD_PIECES[TYPE(pc)]);
            else
                fputs(" ・", f);
        }
        fprintf(f, "|%s\n", KANJI_DIGITS[r + 1]);
    }
    fputs("+---------------------------+\n", f);
    bod_hand(p, FIRST, f);
    if (p->side == SECOND)
        fputs("後手番\n", f);
}

static int kanji_number(const char *s, int *len)
{
    int n = 0, l;
    *len = 0;
    if (!strncmp(s, "百", 3))
        n = 100, *len = 3, s += 3;
    int i = utf8_find(s, KANJI_DIGITS + 1, 9, &l);
    if (i >= 0 && !strncmp(s + l, "十", 3))
        n += (i + 1) * 10, *len += l + 3, s += l + 3;
    else if (!strncmp(s, "十", 3))
        n += 10, *len += 3, s += 3;
    i = utf8_find(s, KANJI_DIGITS + 1, 9, &l);
    if (i >= 0)
        n += i + 1, *len += l;
    return *len ? n : 1;
}

static void read_bod_hand(Position *p, int c, const char *s)
{
    while (*s) {
        int len, t = kif_piece(s, &len);
        if (!t) {
            s++;
            continue;
        }
        s += len;
        int n = kanji_number(s, &len);
        s += len;
        if (t <= HI)
            p->hand[c][t] = (uint8_t) n;
    }
}

/* One "|v香v桂 ・ …|一" row; false if it is not one. */
static bool read_bod_row(Position *p, int row, const char *s)
{
    if (*s != '|' || row > 8)
        return false;
    s++;
    for (int c = 0; c < 9; c++) {
        int color = *s == 'v' ? SECOND : FIRST;
        if (*s == 'v' || *s == ' ')
            s++;
        int len, t = kif_piece(s, &len);
        if (!t) {
            if (strncmp(s, "・", 3))
                return false;
            len = 3;
        }
        p->board[SQ(row, c)] = t ? PIECE(t, color) : EMPTY;
        s += len;
    }
    return true;
}

bool record_write(const Game *g, FILE *f)
{
    char date[32];
    time_t t = (time_t) g->started;
    strftime(date, sizeof date, "%Y/%m/%d %H:%M:%S", localtime(&t));
    fprintf(f, "# KIF written by denboku\n開始日時：%s\n", date);
    if (rules_variant != V_STANDARD) {
        /* The KIF board diagram is 9×9 only: the variant and its start come as comments, and
         * the moves follow in ordinary KIF text on the variant's board. */
        char sfen[192];
        pos_to_text(&g->start, sfen, sizeof sfen);
        fprintf(f, "# DENBOKU VARIANT %d\n# SFEN %s\n", rules_variant, sfen);
    } else if (custom_start(g)) {
        write_bod(&g->start, f);
    } else {
        fprintf(f, "手合割：%s\n", handicap_name_ja(g->handicap));
    }
    bool handicap = rules_variant == V_STANDARD && g->handicap != HANDICAP_NONE;
    fprintf(f, "%s：%s\n%s：%s\n", handicap ? "下手" : "先手", g->names[FIRST], handicap ? "上手" : "後手",
            g->names[SECOND]);
    fprintf(f, "手数----指手---------消費時間--\n");
    Position p = g->start;
    long total[2] = {0, 0};
    int prev_to = -1;
    for (int i = 0; i < g->count; i++) {
        char text[64];
        Move m = g->moves[i];
        kif_move(&p, &m, prev_to, text, sizeof text);
        int secs = g->times[i] / 1000, side = p.side;
        total[side] += secs;
        fprintf(f, "%4d %-14s(%2d:%02d/%02ld:%02ld:%02ld)\n", i + 1, text, secs / 60, secs % 60, total[side] / 3600,
                total[side] / 60 % 60, total[side] % 60);
        prev_to = m.to;
        pos_make(&p, &m);
    }
    const char *word = result_word(g);
    if (word && g->reason != REASON_CHECKMATE)
        fprintf(f, "%4d %s\n", g->count + 1, word);
    if (g->result == RESULT_FIRST_WIN || g->result == RESULT_SECOND_WIN) {
        bool sente = g->result == RESULT_FIRST_WIN;
        fprintf(f, "まで%d手で%sの勝ち\n", g->count, handicap ? (sente ? "下手" : "上手") : (sente ? "先手" : "後手"));
    } else if (g->result == RESULT_DRAW) {
        fprintf(f, "まで%d手で千日手\n", g->count);
    }
    return !ferror(f);
}

static int utf8_find(const char *s, const char *const *list, int n, int *len)
{
    for (int i = 0; i < n; i++) {
        size_t l = strlen(list[i]);
        if (!strncmp(s, list[i], l)) {
            *len = (int) l;
            return i;
        }
    }
    return -1;
}

static int kif_piece(const char *s, int *len)
{
    if (rules_variant == V_TORI) {
        static const char *birds[] = {"左鶉", "右鶉", "燕", "鴈", "鳫", "鷹", "鵰", "鶴", "雉", "鵬"};
        static const int types[] = {KY, KE, FU, TO, TO, KA, UM, KI, GI, OU};
        int i = utf8_find(s, birds, 10, len);
        return i < 0 ? EMPTY : types[i];
    }
    /* Longest names first so 成香 is not read as 成 + 香. */
    static const char *names[] = {"成香", "成桂", "成銀", "歩", "香", "桂", "銀", "金", "角", "飛", "玉", "王",
                                  "と", "杏", "圭", "全", "馬", "龍", "竜"};
    static const int types[] = {NY, NK, NG, FU, KY, KE, GI, KI, KA, HI, OU, OU, TO, NY, NK, NG, UM, RY, RY};
    int i = utf8_find(s, names, 19, len);
    return i < 0 ? EMPTY : types[i];
}

/* 持将棋 is an impasse that ended in a draw. */
static Reason kif_terminal(const char *s, bool *draw)
{
    static const char *words[] = {"投了", "詰み", "切れ負け", "千日手", "反則負け", "中断", "入玉勝ち", "持将棋"};
    static const Reason reasons[] = {REASON_RESIGN, REASON_CHECKMATE, REASON_TIME, REASON_REPETITION,
                                     REASON_ILLEGAL, REASON_ABORT, REASON_IMPASSE, REASON_IMPASSE};
    int len;
    int i = utf8_find(s, words, 8, &len);
    if (i < 0)
        return REASON_NONE;
    *draw = i == 7;
    return reasons[i];
}

static void copy_name(char *dst, const char *src)
{
    size_t n = strlen(src);
    if (n > 63)
        n = 63;
    memcpy(dst, src, n);
    dst[n] = 0;
}

typedef char *(*LineReader)(char *buf, int size, void *source);

static char *file_line(char *buf, int size, void *source) { return fgets(buf, size, source); }

/* The next line of a text in memory; `source` points to the cursor. */
static char *text_line(char *buf, int size, void *source)
{
    const char **at = source;
    if (!**at)
        return NULL;
    const char *nl = strchr(*at, '\n');
    int n = nl ? (int) (nl - *at) + 1 : (int) strlen(*at);
    snprintf(buf, size, "%.*s", n < size ? n : size - 1, *at);
    *at += n;
    return buf;
}

/* CSA allows several statements on a line, separated by commas ("+7776FU,T3" from lishogi):
 * they are handed out one at a time. Comments, names and $ headers stay whole. */
typedef struct {
    LineReader next;
    void *source;
    char line[512];
    char *rest;
} CsaSplit;

static char *csa_statement(char *buf, int size, void *source)
{
    CsaSplit *s = source;
    if (!s->rest) {
        if (!s->next(s->line, sizeof s->line, s->source))
            return NULL;
        s->line[strcspn(s->line, "\r\n")] = 0;
        if (strchr("'$N", s->line[0])) {
            snprintf(buf, size, "%s", s->line);
            return buf;
        }
        s->rest = s->line;
    }
    char *comma = strchr(s->rest, ',');
    if (comma)
        *comma = 0;
    snprintf(buf, size, "%s", s->rest);
    s->rest = comma ? comma + 1 : NULL;
    return buf;
}

static bool read_kif(Game *g, LineReader next, void *source);

bool record_read(Game *g, FILE *f) { return read_kif(g, file_line, f); }

bool record_read_text(Game *g, const char *text) { return read_kif(g, text_line, &text); }

/* A move's time as read from a file: within a day, so its milliseconds fit an int. */
static int clamp_secs(long secs) { return secs < 0 ? 0 : secs > 86400 ? 86400 : (int) secs; }

static bool read_kif(Game *g, LineReader next, void *source)
{
    char line[512];
    Handicap h = HANDICAP_NONE;
    char names[2][64] = {"", ""};
    char file_sfen[192] = "";
    int file_variant = V_STANDARD;
    long started = 0;
    bool moves_started = false, has_illegal = false;
    int prev_to = -1, bod_rows = 0, illegal_ms = 0;
    Move illegal;
    Position bod;
    memset(&bod, 0, sizeof bod);
    rules_set_variant(V_STANDARD); /* no header: a standard game */
    while (next(line, sizeof line, source)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!moves_started) {
            const char *v;
            if (!strncmp(line, "# DENBOKU VARIANT ", 18) && !bod_rows) { /* the board after it is that variant's */
                file_variant = atoi(line + 18);
                if (file_variant < 0)
                    file_variant = 0;
                if (file_variant >= V_COUNT)
                    file_variant = V_COUNT - 1;
                rules_set_variant(file_variant);
            } else if (!strncmp(line, "開始日時：", strlen("開始日時："))) {
                struct tm tm = {0};
                if (sscanf(line + strlen("開始日時："), "%d/%d/%d %d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                           &tm.tm_hour, &tm.tm_min, &tm.tm_sec) >= 3) {
                    tm.tm_year -= 1900, tm.tm_mon--, tm.tm_isdst = -1;
                    started = (long) mktime(&tm);
                }
            } else if (!strncmp(line, "# SFEN ", 7)) {
                snprintf(file_sfen, sizeof file_sfen, "%.*s", (int) sizeof file_sfen - 1, line + 7);
            } else if (read_bod_row(&bod, bod_rows, line)) {
                bod_rows++;
            } else if (!strncmp(line, "先手の持駒：", strlen("先手の持駒：")) ||
                       !strncmp(line, "下手の持駒：", strlen("下手の持駒："))) {
                read_bod_hand(&bod, FIRST, line + strlen("先手の持駒："));
            } else if (!strncmp(line, "後手の持駒：", strlen("後手の持駒：")) ||
                       !strncmp(line, "上手の持駒：", strlen("上手の持駒："))) {
                read_bod_hand(&bod, SECOND, line + strlen("後手の持駒："));
            } else if (!strncmp(line, "後手番", strlen("後手番")) || !strncmp(line, "上手番", strlen("上手番"))) {
                bod.side = SECOND;
            } else if ((v = strstr(line, "手合割："))) {
                v += strlen("手合割：");
                for (int i = 0; i < HANDICAP_COUNT; i++)
                    if (!strncmp(v, handicap_name_ja(i), strlen(handicap_name_ja(i))))
                        h = (Handicap) i;
            } else if (!strncmp(line, "先手：", strlen("先手：")) || !strncmp(line, "下手：", strlen("下手："))) {
                copy_name(names[FIRST], line + strlen("先手："));
            } else if (!strncmp(line, "後手：", strlen("後手：")) || !strncmp(line, "上手：", strlen("上手："))) {
                copy_name(names[SECOND], line + strlen("後手："));
            } else if (strstr(line, "手数----")) {
                moves_started = true;
                game_new(g, h);
                memcpy(g->names, names, sizeof names);
                if (started > 0)
                    g->started = started;
                if (file_sfen[0]) {
                    Position p;
                    if (pos_from_text(&p, file_sfen))
                        set_start(g, &p);
                } else if (bod_rows > 0 && bod_rows == board_h) {
                    set_start(g, &bod);
                }
            }
            continue;
        }
        char *s = line;
        while (*s == ' ')
            s++;
        if (*s < '0' || *s > '9')
            continue;
        while (*s >= '0' && *s <= '9')
            s++;
        while (*s == ' ')
            s++;
        bool draw = false;
        Reason end = kif_terminal(s, &draw);
        if (has_illegal) {
            /* An illegal move is the last one, followed by 反則負け. */
            if (end != REASON_ILLEGAL)
                return false;
            game_play_illegal(g, &illegal, illegal_ms);
            has_illegal = false;
            break;
        }
        if (end != REASON_NONE) {
            if (end == REASON_ABORT)
                game_end(g, RESULT_ABORTED, end);
            else if (end == REASON_REPETITION)
                game_end(g, repetition_result(g), end);
            else if (end == REASON_IMPASSE && draw)
                game_end(g, RESULT_DRAW, end);
            else if (end == REASON_IMPASSE)
                game_end(g, g->pos.side == FIRST ? RESULT_FIRST_WIN : RESULT_SECOND_WIN, end);
            else
                game_end(g, g->pos.side == FIRST ? RESULT_SECOND_WIN : RESULT_FIRST_WIN, end);
            /* "まで…で先手の勝ち" says who won where the side to move does not: a resignation out of turn
             * (during the engine's thinking, or a network opponent's). */
            while (end != REASON_ABORT && end != REASON_REPETITION && !draw && next(line, sizeof line, source)) {
                if (!strstr(line, "まで") || !strstr(line, "の勝ち"))
                    continue;
                bool first = strstr(line, "先手の勝ち") || strstr(line, "下手の勝ち");
                bool second = strstr(line, "後手の勝ち") || strstr(line, "上手の勝ち");
                if (first != second)
                    g->result = first ? RESULT_FIRST_WIN : RESULT_SECOND_WIN;
                break;
            }
            break;
        }
        Move m = {0};
        int to;
        if (!strncmp(s, "同", 3)) {
            to = prev_to;
            s += 3;
            if (!strncmp(s, "　", 3))
                s += 3;
        } else {
            int len, file = utf8_find(s, ZENKAKU_DIGITS + 1, 9, &len);
            if (file < 0 || file >= board_w)
                return false;
            s += len;
            int rank = utf8_find(s, KANJI_DIGITS + 1, 9, &len);
            if (rank < 0 || rank >= board_h)
                return false;
            s += len;
            to = SQ(rank, board_w - (file + 1));
        }
        if (to < 0)
            return false;
        int len, type = kif_piece(s, &len);
        if (!type)
            return false;
        s += len;
        m.to = (uint8_t) to;
        if (!strncmp(s, "不成", strlen("不成"))) {
            s += strlen("不成");
        } else if (!strncmp(s, "成", strlen("成"))) {
            m.promote = 1;
            s += strlen("成");
        }
        char *paren = strchr(s, '(');
        bool drop = !strncmp(s, "打", strlen("打")) || !paren;
        if (drop) {
            m.from = DROP + type;
            drop_from_face(&m);
        } else {
            int file = paren[1] - '0', rank = paren[2] - '0';
            if (file < 1 || file > board_w || rank < 1 || rank > board_h)
                return false;
            m.from = (uint8_t) SQ(rank - 1, board_w - file);
        }
        bool legal = pos_is_legal(&g->pos, &m);
        if (!legal && !pos_is_possible(&g->pos, &m))
            return false;
        int secs = 0;
        char *t = drop ? paren : strchr(paren + 1, '('); /* a drop has only the time in parentheses */
        if (t) {
            int mm = 0, ss = 0;
            if (sscanf(t + 1, " %d:%d", &mm, &ss) == 2)
                secs = clamp_secs(mm * 60L + ss);
        }
        if (legal)
            game_play(g, &m, secs * 1000);
        else
            illegal = m, illegal_ms = secs * 1000, has_illegal = true;
        prev_to = to;
    }
    return moves_started && !has_illegal;
}

/* ---- CSA ---- */

static const char *CSA_NAMES[16] = {[FU] = "FU", [KY] = "KY", [KE] = "KE", [GI] = "GI", [KI] = "KI", [KA] = "KA",
                                    [HI] = "HI", [OU] = "OU", [TO] = "TO", [NY] = "NY", [NK] = "NK", [NG] = "NG",
                                    [UM] = "UM", [RY] = "RY"};
/* Gote's pieces removed in each handicap, as CSA "PI" squares. */
static const char *CSA_HANDICAP[HANDICAP_COUNT] = {
    "", "11KY", "22KA", "82HI", "82HI11KY", "82HI22KA", "82HI22KA11KY91KY", "82HI22KA11KY91KY21KE81KE",
    "82HI22KA11KY91KY21KE81KE31GI71GI", "82HI22KA11KY91KY21KE81KE31GI71GI41KI61KI",
};

void csa_move(const Position *p, const Move *m, char *buf)
{
    int from_f = 0, from_r = 0, type;
    if (IS_DROP(*m)) {
        type = drop_type(m);
    } else {
        from_f = FILE_OF(m->from), from_r = RANK_OF(m->from);
        int from_type = TYPE(p->board[m->from]);
        type = m->promote ? promote_type(from_type) : from_type;
    }
    sprintf(buf, "%c%d%d%d%d%s", p->side == FIRST ? '+' : '-', from_f, from_r, FILE_OF(m->to), RANK_OF(m->to),
            CSA_NAMES[type]);
}

static int csa_type(const char *s);

static bool csa_parse(const Position *p, const char *line, Move *m)
{
    memset(m, 0, sizeof *m);
    if ((line[0] != '+' && line[0] != '-') || strlen(line) < 7 || (line[0] == '-') != (p->side == SECOND))
        return false;
    int ff = line[1] - '0', fr = line[2] - '0', tf = line[3] - '0', tr = line[4] - '0';
    int type = csa_type(line + 5);
    if (tf < 1 || tf > board_w || tr < 1 || tr > board_h || !type)
        return false;
    m->to = (uint8_t) SQ(tr - 1, board_w - tf);
    if (ff == 0) {
        m->from = DROP + type;
        drop_from_face(m);
    } else {
        if (ff < 1 || ff > board_w || fr < 1 || fr > board_h)
            return false;
        m->from = (uint8_t) SQ(fr - 1, board_w - ff);
        int from_type = TYPE(p->board[m->from]);
        int promoted = promote_type(from_type);
        m->promote = type == promoted && promoted != from_type;
    }
    return true;
}

bool csa_parse_move(Position *p, const char *line, Move *m) { return csa_parse(p, line, m) && pos_is_legal(p, m); }

bool csa_parse_any(const Position *p, const char *line, Move *m) { return csa_parse(p, line, m) && pos_is_possible(p, m); }

bool record_write_alt(const Game *g, FILE *f)
{
    fprintf(f, "V2.2\n");
    if (rules_variant != V_STANDARD)
        fprintf(f, "'DENBOKU VARIANT %d\n", rules_variant);
    fprintf(f, "N+%s\nN-%s\n", g->names[FIRST], g->names[SECOND]);
    if (rules_variant != V_STANDARD || custom_start(g)) {
        for (int r = 0; r < board_h; r++) {
            fprintf(f, "P%d", r + 1);
            for (int c = 0; c < board_w; c++) {
                uint8_t pc = g->start.board[SQ(r, c)];
                if (pc)
                    fprintf(f, "%c%s", COLOR(pc) == FIRST ? '+' : '-', CSA_NAMES[TYPE(pc)]);
                else
                    fputs(" * ", f);
            }
            fputc('\n', f);
        }
        /* A line per kind, as a sandbox hand may not fit one line. */
        static const int ORDER[] = {HI, KA, KI, GI, KE, KY, FU, TO};
        for (int c = 0; c < 2; c++)
            for (size_t i = 0; i < sizeof ORDER / sizeof *ORDER; i++) {
                int t = ORDER[i], k = g->start.hand[c][t == TO ? HAND_TO_SLOT : t];
                if (!k)
                    continue;
                fprintf(f, "P%c", c == FIRST ? '+' : '-');
                while (k--)
                    fprintf(f, "00%s", CSA_NAMES[t]);
                fputc('\n', f);
            }
    } else {
        fprintf(f, "PI%s\n", CSA_HANDICAP[g->handicap]);
    }
    fprintf(f, "%c\n", g->start.side == FIRST ? '+' : '-');
    Position p = g->start;
    for (int i = 0; i < g->count; i++) {
        Move m = g->moves[i];
        char text[16];
        csa_move(&p, &m, text);
        fprintf(f, "%s\nT%d\n", text, g->times[i] / 1000);
        pos_make(&p, &m);
    }
    switch (g->reason) {
    case REASON_RESIGN: fputs("%TORYO\n", f); break;
    case REASON_TIME: fputs("%TIME_UP\n", f); break;
    case REASON_REPETITION: fputs("%SENNICHITE\n", f); break;
    case REASON_IMPASSE: fputs(g->result == RESULT_DRAW ? "%JISHOGI\n" : "%KACHI\n", f); break;
    case REASON_ABORT: fputs("%CHUDAN\n", f); break;
    case REASON_CHECKMATE: fputs("%TSUMI\n", f); break;
    case REASON_ILLEGAL: fputs("%ILLEGAL_MOVE\n", f); break;
    default: break;
    }
    /* Who won, as Floodgate's records say it: a resignation out of turn is not the side to move's. */
    if (g->result == RESULT_FIRST_WIN || g->result == RESULT_SECOND_WIN) {
        bool first = g->result == RESULT_FIRST_WIN;
        fprintf(f, "'summary:%s:%s %s:%s %s\n", g->reason == REASON_RESIGN ? "toryo" : "end", g->names[FIRST],
                first ? "win" : "lose", g->names[SECOND], first ? "lose" : "win");
    }
    return !ferror(f);
}

static int csa_type(const char *s)
{
    for (int t = 0; t < 16; t++)
        if (CSA_NAMES[t] && !strncmp(s, CSA_NAMES[t], 2))
            return t;
    return EMPTY;
}

static bool read_csa(Game *g, LineReader next, void *source);

bool record_read_alt(Game *g, FILE *f)
{
    CsaSplit s = {file_line, f, "", NULL};
    return read_csa(g, csa_statement, &s);
}

/* KIF has its move header or a handicap line; anything else is tried as CSA. */
bool record_read_any_text(Game *g, const char *text)
{
    bool kif = strstr(text, "手数") || strstr(text, "手合割") || strstr(text, "先手：");
    return kif ? record_read_text(g, text) : game_read_csa_text(g, text);
}

bool game_read_csa_text(Game *g, const char *text)
{
    CsaSplit s = {text_line, &text, "", NULL};
    return read_csa(g, csa_statement, &s);
}

static bool read_csa(Game *g, LineReader next, void *source)
{
    char line[512];
    Handicap h = HANDICAP_NONE;
    char names[2][64] = {"", ""};
    int file_variant = V_STANDARD;
    bool started = false, has_illegal = false;
    int bod_rows = 0, illegal_ms = 0;
    Move illegal;
    Position bod;
    memset(&bod, 0, sizeof bod);
    rules_set_variant(V_STANDARD);
    while (next(line, sizeof line, source)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "'DENBOKU VARIANT ", 17) && !started && !bod_rows) { /* before the board and the moves */
            file_variant = atoi(line + 17);
            if (file_variant < 0)
                file_variant = 0;
            if (file_variant >= V_COUNT)
                file_variant = V_COUNT - 1;
            rules_set_variant(file_variant);
        } else if (!strncmp(line, "N+", 2) || !strncmp(line, "N-", 2)) {
            copy_name(names[line[1] == '-'], line + 2);
        } else if (!strncmp(line, "PI", 2)) {
            for (int i = 0; i < HANDICAP_COUNT; i++)
                if (!strcmp(line + 2, CSA_HANDICAP[i]))
                    h = (Handicap) i;
        } else if (!started && line[0] == 'P' && line[1] >= '1' && line[1] <= '0' + board_h &&
                   strlen(line) >= (size_t) (2 + 3 * board_w)) {
            int r = line[1] - '1';
            for (int c = 0; c < board_w; c++) {
                const char *cell = line + 2 + c * 3;
                int t = csa_type(cell + 1);
                bod.board[SQ(r, c)] = t ? PIECE(t, cell[0] == '-' ? SECOND : FIRST) : EMPTY;
            }
            bod_rows++;
        } else if (!started && (!strncmp(line, "P+", 2) || !strncmp(line, "P-", 2))) {
            for (const char *c = line + 2; strlen(c) >= 4; c += 4) {
                int t = csa_type(c + 2);
                uint8_t *k = t == TO ? &bod.hand[line[1] == '-'][HAND_TO_SLOT]
                             : t >= FU && t <= HI ? &bod.hand[line[1] == '-'][t]
                                                  : NULL;
                if (k && *k < HAND_MAX)
                    ++*k;
            }
        } else if (!started && (!strcmp(line, "+") || !strcmp(line, "-"))) {
            game_new(g, h);
            memcpy(g->names, names, sizeof names);
            if (bod_rows > 0 && bod_rows == board_h) {
                bod.side = line[0] == '-' ? SECOND : FIRST;
                set_start(g, &bod);
            }
            started = true;
        } else if (started && (line[0] == '+' || line[0] == '-') && strlen(line) >= 7) {
            Move m;
            if (has_illegal || !csa_parse_any(&g->pos, line, &m))
                return false;
            if (pos_is_legal(&g->pos, &m))
                game_play(g, &m, 0);
            else
                illegal = m, has_illegal = true;
        } else if (started && line[0] == 'T' && has_illegal) {
            illegal_ms = clamp_secs(atol(line + 1)) * 1000;
        } else if (started && line[0] == 'T' && g->count > 0) {
            g->times[g->count - 1] = clamp_secs(atol(line + 1)) * 1000;
        } else if (started && line[0] == '%') {
            if (has_illegal) {
                if (strcmp(line, "%ILLEGAL_MOVE"))
                    return false;
                game_play_illegal(g, &illegal, illegal_ms);
                has_illegal = false;
                break;
            }
            int side = g->pos.side;
            Result lose = side == FIRST ? RESULT_SECOND_WIN : RESULT_FIRST_WIN;
            if (!strcmp(line, "%TORYO"))
                game_end(g, lose, REASON_RESIGN);
            else if (!strcmp(line, "%TIME_UP"))
                game_end(g, lose, REASON_TIME);
            else if (!strcmp(line, "%SENNICHITE"))
                game_end(g, repetition_result(g), REASON_REPETITION);
            else if (!strcmp(line, "%KACHI"))
                game_end(g, side == FIRST ? RESULT_FIRST_WIN : RESULT_SECOND_WIN, REASON_IMPASSE);
            else if (!strcmp(line, "%JISHOGI"))
                game_end(g, RESULT_DRAW, REASON_IMPASSE);
            /* "'summary:toryo:<sente> win:<gote> lose" (Floodgate) says who won where the side to move does not. */
            while (g->result == RESULT_FIRST_WIN || g->result == RESULT_SECOND_WIN) {
                if (!next(line, sizeof line, source))
                    break;
                line[strcspn(line, "\r\n")] = 0;
                if (strncmp(line, "'summary:", 9))
                    continue;
                char *gote = strrchr(line, ':'), *space = strrchr(line, ' ');
                if (!gote || !space || space < gote)
                    break;
                *gote = 0;
                const char *first = strrchr(line, ' ');
                bool won = first && !strcmp(first, " win"), lost = first && !strcmp(first, " lose");
                if ((won && !strcmp(space, " lose")) || (lost && !strcmp(space, " win")))
                    g->result = won ? RESULT_FIRST_WIN : RESULT_SECOND_WIN;
                break;
            }
            break;
        }
    }
    return started && !has_illegal;
}
