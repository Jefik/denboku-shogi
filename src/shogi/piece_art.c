/* What the piece sets know of each kind of piece: its code in the style files (data/styles), its one- and
 * two-character names, its size (as on a real set: king largest, pawn smallest), whether it
 * is promoted (drawn in red) and its color with Colored pieces on (peanatsu's shogi_printout,
 * CC BY-SA 3.0: a piece and its promotion share one). */

#include "rules.h"

const PieceArt PIECE_ART[PIECE_KINDS] = {
    [FU] = {"FU", "歩", "歩兵", .84, false, 0xb3b3b3}, [KY] = {"KY", "香", "香車", .87, false, 0xb4ffb4},
    [KE] = {"KE", "桂", "桂馬", .9, false, 0xff80e5}, [GI] = {"GI", "銀", "銀将", .93, false, 0xdcdcdc},
    [KI] = {"KI", "金", "金将", .93, false, 0xffd700}, [KA] = {"KA", "角", "角行", .96, false, 0x5599ff},
    [HI] = {"HI", "飛", "飛車", .96, false, 0x64ff64}, [OU] = {"GY", "玉", "玉将", 1, false, 0xff3232},
    [SECOND_KING] = {"OU", "王", "王将", 1, false, 0xff3232},
    /* As on real koma: minor promoted pieces carry one cursive character. */
    [TO] = {"TO", "と", "と", .84, true, 0xb3b3b3}, [NY] = {"NY", "杏", "杏", .87, true, 0xb4ffb4},
    [NK] = {"NK", "圭", "圭", .9, true, 0xff80e5}, [NG] = {"NG", "全", "全", .93, true, 0xdcdcdc},
    [UM] = {"UM", "馬", "龍馬", .96, true, 0x5599ff}, [RY] = {"RY", "龍", "龍王", .96, true, 0x64ff64},
    /* Tori shogi (禽将棋): one bird a koma; the quails, both 鶉, told apart by a small 左 or 右 in
     * the lower corner of that side, as on Kanare Abstract's set (2022), in every set offered; the 2-kanji sets are not offered, the birds having no two-character names. */
    [SWALLOW] = {"SW", "燕", "燕", .84, false, 0xb3b3b3}, [GOOSE] = {"GO", "鴈", "鴈", .84, true, 0xb3b3b3},
    [FALCON] = {"FA", "鷹", "鷹", .96, false, 0x5599ff}, [EAGLE] = {"EA", "鵰", "鵰", .96, true, 0x5599ff},
    [CRANE] = {"CR", "鶴", "鶴", .93, false, 0xffd700}, [PHEASANT] = {"PH", "雉", "雉", .93, false, 0xff80e5},
    [LEFT_QUAIL] = {"LQ", "鶉", "鶉", .9, false, 0xb4ffb4}, [RIGHT_QUAIL] = {"RQ", "鶉", "鶉", .9, false, 0x64ff64},
    [PHOENIX] = {"PX", "鵬", "鵬", 1, false, 0xff3232},
};

static const int TORI_KINDS[16] = {[FU] = SWALLOW, [TO] = GOOSE, [KA] = FALCON, [UM] = EAGLE, [KI] = CRANE,
                                   [GI] = PHEASANT, [KY] = LEFT_QUAIL, [KE] = RIGHT_QUAIL, [OU] = PHOENIX};

const char *piece_side(int kind, int *at)
{
    *at = kind == LEFT_QUAIL ? -1 : 1;
    return kind == LEFT_QUAIL ? "左" : kind == RIGHT_QUAIL ? "右" : NULL;
}

int piece_kind(int piece)
{
    if (rules_variant == V_TORI)
        return TORI_KINDS[TYPE(piece)] ? TORI_KINDS[TYPE(piece)] : TYPE(piece);
    return TYPE(piece) == OU && COLOR(piece) == SECOND ? SECOND_KING : TYPE(piece);
}

int kind_type(int kind)
{
    if (kind == SECOND_KING)
        return OU;
    for (int t = 0; t < 16; t++)
        if (kind >= SWALLOW && TORI_KINDS[t] == kind)
            return t;
    return kind < 16 ? kind : EMPTY;
}

/* The style chooser's samples. */
const int PREVIEW_PIECES[] = {SECOND_KING, HI, KA, KI, FU, TO};
const int PREVIEW_COUNT = sizeof PREVIEW_PIECES / sizeof PREVIEW_PIECES[0];
const int PREVIEW_ANIMALS[] = {SECOND_KING, HI, KA, FU, TO};
const int PREVIEW_ANIMALS_COUNT = sizeof PREVIEW_ANIMALS / sizeof PREVIEW_ANIMALS[0];

static const int PREVIEW_BIRDS[] = {PHOENIX, FALCON, CRANE, PHEASANT, SWALLOW, GOOSE};

const int *preview_kinds(int variant, int *n)
{
    if (variant != V_TORI)
        return NULL;
    *n = (int) (sizeof PREVIEW_BIRDS / sizeof PREVIEW_BIRDS[0]);
    return PREVIEW_BIRDS;
}
