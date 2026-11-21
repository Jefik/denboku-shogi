#include "shogi.h"

#include <stdio.h>
#include <string.h>

/* A step (slide 0), a jump (|dr| or |dc| 2), a slide any distance (1) or up to `slide` squares. */
typedef struct { int dr, dc, slide; } Step;

int board_w = 9, board_h = 9, rules_variant = V_STANDARD;
static const char STANDARD_SFEN[] = "lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1";
const VariantRules VARIANTS[V_COUNT] = {
    [V_STANDARD] = {9, 9, true, true, false, false, false, false, false, 3, 1, STANDARD_SFEN},
    [V_MINISHOGI] = {5, 5, true, true, false, false, false, false, false, 1, 1, "rbsgk/4p/5/P4/KGSBR b - 1"},
    [V_KYOTOSHOGI] = {5, 5, true, false, false, false, false, true, true, 5, 0, "pgkst/5/5/5/TSKGP b - 1"},
    [V_CHECKSHOGI] = {9, 9, true, true, true, false, false, false, false, 3, 1, STANDARD_SFEN},
    [V_DOBUTSU] = {3, 4, true, false, false, true, true, false, false, 1, 0, "rkb/1p1/1P1/BKR b - 1"},
    [V_GOROGORO] = {5, 6, true, true, false, false, false, false, false, 2, 1, "sgkgs/5/1ppp1/1PPP1/5/SGKGS b - 1"},
    /* Fairy-Stockfish's torishogi: two swallows a file, promotion forced. */
    [V_TORI] = {7, 7, true, true, false, false, false, false, true, 2, 2, "rpckcpl/3f3/sssssss/2s1S2/SSSSSSS/3F3/LPCKCPR b - 1"},
    [V_JUDKINS] = {6, 6, true, true, false, false, false, false, false, 2, 1, "rbnsgk/5p/6/6/P5/KGSNBR b - 1"},
    /* Fairy-Stockfish's euroshogi: three ranks to promote in, promotion forced. */
    [V_EURO] = {8, 8, true, true, false, false, false, false, true, 3, 1, "1nbgkgn1/1r4b1/pppppppp/8/8/PPPPPPPP/1B4R1/1NGKGBN1 b - 1"},
};

void rules_set_variant(int variant)
{
    rules_variant = variant >= 0 && variant < V_COUNT ? variant : V_STANDARD;
    board_w = VARIANTS[rules_variant].w;
    board_h = VARIANTS[rules_variant].h;
}

int promote_type(int type)
{
    if (rules_variant == V_KYOTOSHOGI) {
        switch (type) {
        case FU: return HI;
        case HI: return FU;
        case GI: return KA;
        case KA: return GI;
        case KI: return KE;
        case KE: return KI;
        case TO: return KY;
        case KY: return TO;
        default: return type;
        }
    }
    return type | PROMOTED;
}

int unpromote_type(int type)
{
    /* The Kyotoshogi flip is its own inverse. */
    return rules_variant == V_KYOTOSHOGI ? promote_type(type) : BASE(type);
}

int hand_type(int type)
{
    if (rules_variant == V_KYOTOSHOGI) {
        switch (type) {
        case FU: return FU;
        case KI: return KI;
        case GI: return GI;
        case TO: return TO;
        case KE: return KI; /* knight -> gold */
        case KY: return TO; /* lance -> tokin */
        case KA: return GI; /* bishop -> silver */
        case HI: return FU; /* rook -> pawn */
        default: return EMPTY;
        }
    }
    return BASE(type);
}

int hand_slot(int type) { return type == TO ? HAND_TO_SLOT : type; }

/* The pieces in hand, in the order they are shown. */
static const int HAND_ORDER_STD[] = {HI, KA, KI, GI, KE, KY, FU};
static const int HAND_ORDER_MINI[] = {HI, KA, KI, GI, FU};
static const int HAND_ORDER_KYOTO[] = {TO, KI, GI, FU};
static const int HAND_ORDER_DOBUTSU[] = {HI, KA, FU};
static const int HAND_ORDER_GORO[] = {KI, GI, FU};
static const int HAND_ORDER_TORI[] = {KA, KI, GI, KY, KE, FU};
static const int HAND_ORDER_JUDKINS[] = {HI, KA, KI, GI, KE, FU};
static const int HAND_ORDER_EURO[] = {HI, KA, KI, KE, FU};

const int *hand_order(int *count)
{
    switch (rules_variant) {
    case V_MINISHOGI: *count = 5; return HAND_ORDER_MINI;
    case V_KYOTOSHOGI: *count = 4; return HAND_ORDER_KYOTO;
    case V_DOBUTSU: *count = 3; return HAND_ORDER_DOBUTSU;
    case V_GOROGORO: *count = 3; return HAND_ORDER_GORO;
    case V_TORI: *count = 6; return HAND_ORDER_TORI;
    case V_JUDKINS: *count = 6; return HAND_ORDER_JUDKINS;
    case V_EURO: *count = 5; return HAND_ORDER_EURO;
    default: *count = 7; return HAND_ORDER_STD;
    }
}

/* The face a drop puts down: DROP_TYPE is the piece in hand, and in Kyotoshogi promote drops its other
 * side (lishogi and Fairy-Stockfish allow either). */
int drop_type(const Move *m) { return m->promote ? promote_type(DROP_TYPE(*m)) : DROP_TYPE(*m); }

/* A Kyotoshogi drop written with the face it puts down (角打, B*) as the piece in hand and promote. */
void drop_from_face(Move *m)
{
    if (!IS_DROP(*m) || rules_variant != V_KYOTOSHOGI)
        return;
    int t = DROP_TYPE(*m), h = hand_type(t);
    if (h != EMPTY && h != t)
        m->from = DROP + h, m->promote = 1;
}

#define GOLD_STEPS {-1, -1, 0}, {-1, 0, 0}, {-1, 1, 0}, {0, -1, 0}, {0, 1, 0}, {1, 0, 0}

static const Step STEPS[16][9] = {
    [FU] = {{-1, 0, 0}},
    [KY] = {{-1, 0, 1}},
    [KE] = {{-2, -1, 0}, {-2, 1, 0}},
    [GI] = {{-1, -1, 0}, {-1, 0, 0}, {-1, 1, 0}, {1, -1, 0}, {1, 1, 0}},
    [KI] = {GOLD_STEPS}, [TO] = {GOLD_STEPS}, [NY] = {GOLD_STEPS}, [NK] = {GOLD_STEPS}, [NG] = {GOLD_STEPS},
    [KA] = {{-1, -1, 1}, {-1, 1, 1}, {1, -1, 1}, {1, 1, 1}},
    [HI] = {{-1, 0, 1}, {1, 0, 1}, {0, -1, 1}, {0, 1, 1}},
    [OU] = {{-1, -1, 0}, {-1, 0, 0}, {-1, 1, 0}, {0, -1, 0}, {0, 1, 0}, {1, -1, 0}, {1, 0, 0}, {1, 1, 0}},
    [UM] = {{-1, -1, 1}, {-1, 1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}},
    [RY] = {{-1, 0, 1}, {1, 0, 1}, {0, -1, 1}, {0, 1, 1}, {-1, -1, 0}, {-1, 1, 0}, {1, -1, 0}, {1, 1, 0}},
};
static const int NSTEPS[16] = {[FU] = 1, [KY] = 1, [KE] = 2, [GI] = 5, [KI] = 6, [TO] = 6, [NY] = 6, [NK] = 6,
                               [NG] = 6, [KA] = 4, [HI] = 4, [OU] = 8, [UM] = 8, [RY] = 8};

/* Tori shogi's birds, as Fairy-Stockfish moves them (Betza in its variant.cpp). */
static const Step TORI_STEPS[16][9] = {
    [FU] = {{-1, 0, 0}},                                                             /* swallow: fW */
    [TO] = {{-2, -2, 0}, {-2, 2, 0}, {2, 0, 0}},                                      /* goose: fAbD */
    [KA] = {{-1, -1, 0}, {-1, 0, 0}, {-1, 1, 0}, {0, -1, 0}, {0, 1, 0}, {1, -1, 0}, {1, 1, 0}}, /* falcon: FsfW */
    [UM] = {{-1, -1, 1}, {-1, 1, 1}, {1, 0, 1}, {1, -1, 2}, {1, 1, 2},                 /* eagle: KbRfBbF2 */
            {-1, 0, 0}, {0, -1, 0}, {0, 1, 0}},
    [KI] = {{-1, -1, 0}, {-1, 0, 0}, {-1, 1, 0}, {1, -1, 0}, {1, 0, 0}, {1, 1, 0}},     /* crane: FvW */
    [GI] = {{-2, 0, 0}, {1, -1, 0}, {1, 1, 0}},                                       /* pheasant: bFfD */
    [KY] = {{-1, 0, 1}, {1, 1, 1}, {1, -1, 0}},                                       /* left quail: fRrbBlbF */
    [KE] = {{-1, 0, 1}, {1, -1, 1}, {1, 1, 0}},                                       /* right quail: fRlbBrbF */
    [OU] = {{-1, -1, 0}, {-1, 0, 0}, {-1, 1, 0}, {0, -1, 0}, {0, 1, 0}, {1, -1, 0}, {1, 0, 0}, {1, 1, 0}}, /* phoenix */
};
static const int TORI_NSTEPS[16] = {[FU] = 1, [TO] = 3, [KA] = 7, [UM] = 8, [KI] = 6, [GI] = 3, [KY] = 3, [KE] = 3,
                                    [OU] = 8};

/* Euroshogi's knight: the shogi knight's jumps and a step to either side (fNsW). */
static const Step EURO_KE[] = {{-2, -1, 0}, {-2, 1, 0}, {0, -1, 0}, {0, 1, 0}};

static inline const Step *steps_of(int type, int *n)
{
    if (rules_variant == V_EURO && type == KE) {
        *n = 4;
        return EURO_KE;
    }
    bool tori = rules_variant == V_TORI;
    *n = (tori ? TORI_NSTEPS : NSTEPS)[type];
    return (tori ? TORI_STEPS : STEPS)[type];
}

/* Dōbutsu's giraffe and elephant step where the rook and bishop slide. */
static inline int slides(int type, const Step *st)
{
    return rules_variant == V_DOBUTSU && (type == HI || type == KA) ? 0 : st->slide;
}

static uint64_t zobrist_board[81][32];
static uint64_t zobrist_hand[2][HAND_SLOTS][256];
static uint64_t zobrist_side;

static uint64_t next_random(uint64_t *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 7;
    *s ^= *s << 17;
    return *s;
}

void rules_init(void)
{
    uint64_t s = 0x9e3779b97f4a7c15ULL;
    for (int sq = 0; sq < BOARD_MAX; sq++)
        for (int p = 0; p < 32; p++)
            zobrist_board[sq][p] = next_random(&s);
    for (int c = 0; c < 2; c++)
        for (int t = 0; t < HAND_SLOTS; t++)
            for (int n = 0; n < 256; n++)
                zobrist_hand[c][t][n] = next_random(&s);
    zobrist_side = next_random(&s);
    rules_set_variant(V_STANDARD);
}

static uint64_t compute_hash(const Position *p)
{
    uint64_t h = p->side ? zobrist_side : 0;
    for (int sq = 0; sq < SQUARES; sq++)
        if (p->board[sq])
            h ^= zobrist_board[sq][p->board[sq]];
    for (int c = 0; c < 2; c++)
        for (int t = 0; t < HAND_SLOTS; t++)
            h ^= zobrist_hand[c][t][p->hand[c][t]];
    return h;
}

void pos_start(Position *p)
{
    pos_from_text(p, VARIANTS[rules_variant].start_sfen);
}

static int type_from_letter(char c)
{
    c |= 32;
    if (rules_variant == V_KYOTOSHOGI) {
        switch (c) {
        case 'p': return FU;
        case 'r': return HI;
        case 's': return GI;
        case 'b': return KA;
        case 'g': return KI;
        case 'n': return KE;
        case 't': return TO;
        case 'l': return KY;
        case 'k': return OU;
        default: return EMPTY;
        }
    }
    if (rules_variant == V_TORI) {
        switch (c) {
        case 's': return FU; /* swallow */
        case 'f': return KA; /* falcon */
        case 'c': return KI; /* crane */
        case 'p': return GI; /* pheasant */
        case 'l': return KY; /* left quail */
        case 'r': return KE; /* right quail */
        case 'k': return OU; /* phoenix */
        default: return EMPTY;
        }
    }
    if (rules_variant == V_DOBUTSU) {
        if (c == 'l') return OU; /* lion */
        if (c == 'c') return FU; /* chick */
        if (c == 'g') return HI; /* giraffe */
        if (c == 'e') return KA; /* elephant */
    }
    static const char *letters = "plnsgbrk";
    static const int types[] = {FU, KY, KE, GI, KI, KA, HI, OU};
    for (int i = 0; letters[i]; i++)
        if (letters[i] == c)
            return types[i];
    return EMPTY;
}

static char letter_of(int type)
{
    if (rules_variant == V_KYOTOSHOGI) {
        switch (type) {
        case FU: return 'P';
        case HI: return 'R';
        case GI: return 'S';
        case KA: return 'B';
        case KI: return 'G';
        case KE: return 'N';
        case TO: return 'T';
        case KY: return 'L';
        default: return 'K';
        }
    }
    if (rules_variant == V_TORI) {
        switch (BASE(type)) {
        case FU: return 'S';
        case KA: return 'F';
        case KI: return 'C';
        case GI: return 'P';
        case KY: return 'L';
        case KE: return 'R';
        default: return 'K';
        }
    }
    switch (BASE(type)) {
    case FU: return 'P';
    case KY: return 'L';
    case KE: return 'N';
    case GI: return 'S';
    case KI: return 'G';
    case KA: return 'B';
    case HI: return 'R';
    default: return 'K';
    }
}

/* A '+' before a Kyotoshogi letter is the promoted-side spelling in lishogi's SFEN. */
static int kyoto_promoted_letter(char c)
{
    switch (c | 32) {
    case 'p': return HI;
    case 's': return KA;
    case 'n': return KI;
    case 'l': return TO;
    default: return EMPTY;
    }
}

bool pos_from_text(Position *p, const char *s)
{
    memset(p, 0, sizeof *p);
    int row = 0, col = 0;
    bool promoted = false;
    for (; *s && *s != ' '; s++) {
        if (*s == '/') {
            if (col != board_w)
                return false;
            row++, col = 0;
        } else if (*s >= '1' && *s <= '9') {
            col += *s - '0';
        } else if (*s == '+') {
            promoted = true;
        } else {
            int t = promoted && rules_variant == V_KYOTOSHOGI ? kyoto_promoted_letter(*s) : type_from_letter(*s);
            if (!t || row >= board_h || col >= board_w)
                return false;
            if (promoted && rules_variant != V_KYOTOSHOGI)
                t |= PROMOTED;
            p->board[SQ(row, col)] = PIECE(t, (*s >= 'a') ? SECOND : FIRST);
            promoted = false;
            col++;
        }
    }
    if (row != board_h - 1 || col != board_w || *s++ != ' ')
        return false;
    p->side = (*s == 'w') ? SECOND : FIRST;
    while (*s && *s != ' ')
        s++;
    while (*s == ' ')
        s++;
    if (*s == '-') {
        s++;
    } else {
        while (*s && *s != ' ') {
            int n = 0;
            while (*s >= '0' && *s <= '9')
                n = n > HAND_MAX ? n : n * 10 + *s - '0', s++;
            int t = type_from_letter(*s);
            if (!t || t == OU)
                return false;
            t = hand_type(t);
            if (t == EMPTY)
                return false;
            uint8_t *k = &p->hand[*s >= 'a' ? SECOND : FIRST][hand_slot(t)];
            if (*k + (n ? n : 1) > HAND_MAX)
                return false;
            *k += n ? n : 1;
            s++;
        }
    }
    while (*s == ' ')
        s++;
    int moveno = 0;
    while (*s >= '0' && *s <= '9')
        moveno = moveno > 100000 ? moveno : moveno * 10 + *s - '0', s++;
    p->ply = moveno > 0 ? moveno - 1 : 0;
    p->hash = compute_hash(p);
    return true;
}

void pos_to_text(const Position *p, char *buf, int size)
{
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
            if (rules_variant != V_KYOTOSHOGI && (TYPE(pc) & PROMOTED) && TYPE(pc) != OU)
                out[n++] = '+';
            char c = letter_of(TYPE(pc));
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
    int count;
    const int *order = hand_order(&count);
    int start = n;
    for (int c = 0; c < 2; c++)
        for (int i = 0; i < count; i++) {
            int t = order[i], k = p->hand[c][hand_slot(t)];
            if (!k)
                continue;
            if (k > 1)
                n += sprintf(out + n, "%d", k);
            char l = letter_of(t);
            out[n++] = c == SECOND ? l | 32 : l;
        }
    if (n == start)
        out[n++] = '-';
    n += sprintf(out + n, " %d", p->ply + 1);
    snprintf(buf, size, "%.*s", n, out);
}

static inline bool on_board(int r, int c)
{
    return r >= 0 && r < board_h && c >= 0 && c < board_w;
}

bool in_zone(int color, int row)
{
    if (VARIANTS[rules_variant].promo_anywhere)
        return true;
    int rows = VARIANTS[rules_variant].promo_rows;
    return color == FIRST ? row < rows : row >= board_h - rows;
}

const char *variant_name(int variant)
{
    static const char *NAMES[V_COUNT] = {"Standard", "Minishogi", "Kyotoshogi", "Checkshogi", "Dobutsu", "Gorogoro",
                                         "Torishogi", "Judkins", "Euroshogi"};
    return NAMES[variant >= 0 && variant < V_COUNT ? variant : V_STANDARD];
}

bool can_promote(int type, int color, int from, int to)
{
    if (rules_variant == V_KYOTOSHOGI)
        return type != OU;
    if (type & PROMOTED || type == KI || type == OU)
        return false;
    bool zone = in_zone(color, ROW(to)) || (from < DROP && in_zone(color, ROW(from)));
    if (rules_variant == V_DOBUTSU)
        return type == FU && zone;
    if (rules_variant == V_MINISHOGI || rules_variant == V_GOROGORO)
        return (type == FU || type == GI || type == KA || type == HI) && zone;
    if (rules_variant == V_TORI)
        return (type == FU || type == KA) && zone;
    return zone;
}

/* Rows (counted from the far side) where a piece could never move again. */
bool must_promote(int type, int color, int to)
{
    if (rules_variant == V_KYOTOSHOGI)
        return type != OU;
    int far = color == FIRST ? ROW(to) : board_h - 1 - ROW(to);
    if (rules_variant == V_TORI || rules_variant == V_EURO) /* the quails move back, Euroshogi's knight sideways */
        return type == FU && far == 0;
    if (type == FU || type == KY)
        return far == 0;
    if (type == KE)
        return far <= 1;
    return false;
}

int pos_king(const Position *p, int color)
{
    uint8_t k = PIECE(OU, color);
    for (int sq = 0; sq < SQUARES; sq++)
        if (p->board[sq] == k)
            return sq;
    return -1;
}

bool pos_attacked(const Position *p, int sq, int by)
{
    int r = ROW(sq), c = COL(sq);
    int dir = by == FIRST ? 1 : -1;
    for (int from = 0; from < SQUARES; from++) {
        uint8_t pc = p->board[from];
        if (!pc || COLOR(pc) != by)
            continue;
        int t = TYPE(pc), fr = ROW(from), fc = COL(from), ns;
        const Step *steps = steps_of(t, &ns);
        for (int i = 0; i < ns; i++) {
            const Step *st = &steps[i];
            bool slide = slides(t, st);
            int dr = st->dr * dir, dc = st->dc * dir, left = st->slide > 1 ? st->slide : BOARD_MAX;
            int rr = fr + dr, cc = fc + dc;
            while (on_board(rr, cc)) {
                if (rr == r && cc == c)
                    return true;
                if (!slide || p->board[SQ(rr, cc)] || --left == 0)
                    break;
                rr += dr, cc += dc;
            }
        }
    }
    return false;
}

bool pos_in_check(const Position *p)
{
    int k = pos_king(p, p->side);
    return k >= 0 && pos_attacked(p, k, !p->side);
}

static void set_square(Position *p, int sq, uint8_t pc)
{
    if (p->board[sq])
        p->hash ^= zobrist_board[sq][p->board[sq]];
    p->board[sq] = pc;
    if (pc)
        p->hash ^= zobrist_board[sq][pc];
}

static void add_hand(Position *p, int color, int slot, int delta)
{
    p->hash ^= zobrist_hand[color][slot][p->hand[color][slot]];
    p->hand[color][slot] += delta;
    p->hash ^= zobrist_hand[color][slot][p->hand[color][slot]];
}

void pos_make(Position *p, Move *m)
{
    int us = p->side;
    if (IS_DROP(*m)) {
        m->piece = PIECE(drop_type(m), us);
        m->captured = EMPTY;
        add_hand(p, us, hand_slot(DROP_TYPE(*m)), -1);
        set_square(p, m->to, m->piece);
    } else {
        m->piece = p->board[m->from];
        m->captured = p->board[m->to];
        int ct = hand_type(TYPE(m->captured));
        if (m->captured && TYPE(m->captured) != OU && ct != EMPTY)
            add_hand(p, us, hand_slot(ct), 1);
        set_square(p, m->from, EMPTY);
        int nt = m->promote ? promote_type(TYPE(m->piece)) : TYPE(m->piece);
        set_square(p, m->to, PIECE(nt, us));
    }
    p->side = !us;
    p->hash ^= zobrist_side;
    p->ply++;
}

void pos_unmake(Position *p, const Move *m)
{
    p->side = !p->side;
    p->hash ^= zobrist_side;
    p->ply--;
    int us = p->side;
    if (IS_DROP(*m)) {
        set_square(p, m->to, EMPTY);
        add_hand(p, us, hand_slot(DROP_TYPE(*m)), 1);
    } else {
        set_square(p, m->to, m->captured);
        set_square(p, m->from, m->piece);
        int ct = hand_type(TYPE(m->captured));
        if (m->captured && TYPE(m->captured) != OU && ct != EMPTY)
            add_hand(p, us, hand_slot(ct), -1);
    }
}

/* Every move the pieces can make, with `rules` the ones that also respect forced promotion,
 * 二歩 and drops where a piece could never move (legality against check comes after). */
static int generate(const Position *p, Move *out, bool rules)
{
    int n = 0, us = p->side, dir = us == FIRST ? 1 : -1;
    for (int from = 0; from < SQUARES; from++) {
        uint8_t pc = p->board[from];
        if (!pc || COLOR(pc) != us)
            continue;
        int t = TYPE(pc), fr = ROW(from), fc = COL(from), ns;
        const Step *steps = steps_of(t, &ns);
        for (int i = 0; i < ns; i++) {
            const Step *st = &steps[i];
            bool slide = slides(t, st);
            int dr = st->dr * dir, dc = st->dc * dir, left = st->slide > 1 ? st->slide : BOARD_MAX;
            for (int rr = fr + dr, cc = fc + dc; on_board(rr, cc); rr += dr, cc += dc) {
                int to = SQ(rr, cc);
                uint8_t target = p->board[to];
                if (target && COLOR(target) == us)
                    break;
                bool promo = can_promote(t, us, from, to);
                if (rules_variant == V_KYOTOSHOGI) {
                    if (t != OU)
                        out[n++] = (Move) {from, to, 1, pc, target};
                    else
                        out[n++] = (Move) {from, to, 0, pc, target};
                } else {
                    if (promo)
                        out[n++] = (Move) {from, to, 1, pc, target};
                    if (!rules || !(must_promote(t, us, to) || (promo && VARIANTS[rules_variant].forced_promo)))
                        out[n++] = (Move) {from, to, 0, pc, target};
                }
                if (!slide || target || --left == 0)
                    break;
            }
        }
    }
    int pawn_file[9] = {0};
    if (VARIANTS[rules_variant].nifu)
        for (int sq = 0; rules && sq < SQUARES; sq++)
            if (p->board[sq] == PIECE(FU, us))
                pawn_file[COL(sq)]++;
    if (!VARIANTS[rules_variant].drops)
        return n;
    for (int slot = 0; slot < HAND_SLOTS; slot++) {
        int t = slot == HAND_TO_SLOT ? TO : slot;
        if (!p->hand[us][slot])
            continue;
        for (int to = 0; to < SQUARES; to++) {
            if (p->board[to])
                continue;
            if (rules && rules_variant != V_KYOTOSHOGI &&
                (must_promote(t, us, to) ||
                 (t == FU && VARIANTS[rules_variant].nifu &&
                  pawn_file[COL(to)] >= VARIANTS[rules_variant].pawns_per_file)))
                continue;
            out[n++] = (Move) {DROP + t, to, 0, PIECE(t, us), EMPTY};
            if (rules_variant == V_KYOTOSHOGI)
                out[n++] = (Move) {DROP + t, to, 1, PIECE(promote_type(t), us), EMPTY};
        }
    }
    return n;
}

static int pseudo_moves(const Position *p, Move *out) { return generate(p, out, true); }

int pos_physical_moves(const Position *p, Move *out) { return generate(p, out, false); }

int pos_any_moves(const Position *p, Move *out)
{
    int n = 0, us = p->side;
    for (int from = 0; from < SQUARES; from++) {
        uint8_t pc = p->board[from];
        if (!pc || COLOR(pc) != us)
            continue;
        for (int to = 0; to < SQUARES; to++) {
            uint8_t target = p->board[to];
            if (to == from || (target && COLOR(target) == us))
                continue;
            if (rules_variant == V_KYOTOSHOGI) {
                out[n++] = (Move) {from, to, TYPE(pc) != OU, pc, target};
            } else {
                if (can_promote(TYPE(pc), us, from, to))
                    out[n++] = (Move) {from, to, 1, pc, target};
                out[n++] = (Move) {from, to, 0, pc, target};
            }
        }
    }
    if (!VARIANTS[rules_variant].drops)
        return n;
    for (int slot = 0; slot < HAND_SLOTS; slot++) {
        int t = slot == HAND_TO_SLOT ? TO : slot;
        if (!p->hand[us][slot])
            continue;
        for (int to = 0; to < SQUARES; to++)
            if (!p->board[to]) {
                out[n++] = (Move) {DROP + t, to, 0, PIECE(t, us), EMPTY};
                if (rules_variant == V_KYOTOSHOGI)
                    out[n++] = (Move) {DROP + t, to, 1, PIECE(promote_type(t), us), EMPTY};
            }
    }
    return n;
}

bool pos_has_legal_move(Position *p);

/* Legal = own king safe, and a pawn drop must not give checkmate (uchifuzume). */
static bool legal_after_make(Position *p, const Move *m)
{
    if (rules_variant == V_DOBUTSU)
        return true; /* no check rule: the king can be captured */
    int us = !p->side;
    int k = pos_king(p, us);
    if (k >= 0 && pos_attacked(p, k, p->side))
        return false;
    /* Checkshogi wins on check, so a pawn-drop mate is not illegal there; Kyotoshogi also
     * allows it (lishogi). */
    if (IS_DROP(*m) && DROP_TYPE(*m) == FU && rules_variant != V_CHECKSHOGI &&
        rules_variant != V_KYOTOSHOGI) {
        int ek = pos_king(p, p->side);
        int ahead = us == FIRST ? m->to - board_w : m->to + board_w;
        if (ek == ahead && !pos_has_legal_move(p))
            return false;
    }
    return true;
}

bool pos_has_legal_move(Position *p)
{
    Move moves[MAX_MOVES];
    int n = pseudo_moves(p, moves);
    for (int i = 0; i < n; i++) {
        pos_make(p, &moves[i]);
        bool ok = legal_after_make(p, &moves[i]);
        pos_unmake(p, &moves[i]);
        if (ok)
            return true;
    }
    return false;
}

int pos_legal_moves(Position *p, Move *moves)
{
    Move tmp[MAX_MOVES];
    int n = pseudo_moves(p, tmp), k = 0;
    for (int i = 0; i < n; i++) {
        pos_make(p, &tmp[i]);
        if (legal_after_make(p, &tmp[i]))
            moves[k++] = tmp[i];
        pos_unmake(p, &tmp[i]);
    }
    return k;
}

/* The one move checked, not every legal move made. */
bool pos_is_legal(Position *p, Move *m)
{
    Move moves[MAX_MOVES];
    int n = pseudo_moves(p, moves);
    for (int i = 0; i < n; i++)
        if (moves[i].from == m->from && moves[i].to == m->to && moves[i].promote == m->promote) {
            pos_make(p, &moves[i]);
            bool ok = legal_after_make(p, &moves[i]);
            pos_unmake(p, &moves[i]);
            if (ok)
                *m = moves[i];
            return ok;
        }
    return false;
}

bool pos_is_possible(const Position *p, Move *m)
{
    Move moves[MAX_MOVES];
    int n = pos_any_moves(p, moves);
    for (int i = 0; i < n; i++)
        if (moves[i].from == m->from && moves[i].to == m->to && moves[i].promote == m->promote) {
            *m = moves[i];
            return true;
        }
    return false;
}

uint64_t perft(Position *p, int depth)
{
    Move moves[MAX_MOVES];
    int n = pos_legal_moves(p, moves);
    if (depth <= 1)
        return depth == 1 ? (uint64_t) n : 1;
    uint64_t total = 0;
    for (int i = 0; i < n; i++) {
        pos_make(p, &moves[i]);
        total += perft(p, depth - 1);
        pos_unmake(p, &moves[i]);
    }
    return total;
}

void move_to_usi(const Move *m, char *buf)
{
    if (IS_DROP(*m))
        sprintf(buf, "%c*%d%c", letter_of(drop_type(m)), FILE_OF(m->to), 'a' + ROW(m->to));
    else
        sprintf(buf, "%d%c%d%c%s", FILE_OF(m->from), 'a' + ROW(m->from), FILE_OF(m->to), 'a' + ROW(m->to),
                m->promote ? "+" : "");
}

static int parse_square(const char *s)
{
    if (s[0] < '1' || s[0] >= '1' + board_w || s[1] < 'a' || s[1] >= 'a' + board_h)
        return -1;
    return SQ(s[1] - 'a', board_w - (s[0] - '0'));
}

int square_from_text(const char *s) { return s && s[0] ? parse_square(s) : -1; }

bool move_from_usi(Position *p, const char *s, Move *m)
{
    memset(m, 0, sizeof *m);
    if (s[0] && s[1] == '*') {
        int t = type_from_letter(s[0]);
        int to = parse_square(s + 2);
        if (!t || t == OU || to < 0)
            return false;
        m->from = DROP + t, m->to = to;
        drop_from_face(m);
    } else {
        int from = parse_square(s), to = from >= 0 ? parse_square(s + 2) : -1;
        if (to < 0)
            return false;
        m->from = from, m->to = to, m->promote = s[4] == '+';
    }
    return pos_is_legal(p, m);
}

bool pos_can_declare(const Position *p)
{
    if (rules_variant != V_STANDARD && rules_variant != V_CHECKSHOGI)
        return false;
    int us = p->side, k = pos_king(p, us);
    if (k < 0 || !in_zone(us, ROW(k)) || pos_in_check(p))
        return false;
    int count = 0, points = 0;
    for (int sq = 0; sq < SQUARES; sq++) {
        uint8_t pc = p->board[sq];
        if (!pc || COLOR(pc) != us || TYPE(pc) == OU || !in_zone(us, ROW(sq)))
            continue;
        count++;
        points += (BASE(TYPE(pc)) == KA || BASE(TYPE(pc)) == HI) ? 5 : 1;
    }
    for (int t = FU; t <= HI; t++)
        points += p->hand[us][t] * ((t == KA || t == HI) ? 5 : 1);
    return count >= 10 && points >= (us == FIRST ? 28 : 27);
}

bool promotion_keeps_moves(int type)
{
    int dr[8], dc[8], pr[8], pc[8];
    int slide[8], ps[8];
    int n = piece_steps(type, dr, dc, slide);
    int np = piece_steps(promote_type(type), pr, pc, ps);
    for (int i = 0; i < n; i++) {
        bool kept = false;
        for (int j = 0; j < np && !kept; j++)
            kept = pr[j] == dr[i] && pc[j] == dc[i] && (ps[j] == 1 || !slide[i] || (slide[i] > 1 && ps[j] >= slide[i]));
        if (!kept)
            return false;
    }
    return true;
}

int piece_steps(int type, int *dr, int *dc, int *slide)
{
    if (type < 0 || type > 15)
        return 0;
    int n;
    const Step *steps = steps_of(type, &n);
    for (int i = 0; i < n; i++) {
        dr[i] = steps[i].dr;
        dc[i] = steps[i].dc;
        slide[i] = slides(type, &steps[i]);
    }
    return n;
}
