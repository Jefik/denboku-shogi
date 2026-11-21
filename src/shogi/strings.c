/* The game's texts: the variants' names and rules, and its translations by language (tr() looks
 * here after the app's own): strings_<code>.c, one row each in GAME_LANGUAGES. */

#include "i18n.h"
#include "rules.h"
#include "texts.h"

const char *const VARIANT_LABELS[V_COUNT] = {"Standard", "Minishōgi", "Kyotoshōgi", "Checkshōgi", "Dōbutsu",
                                             "Goro Goro", "Tori shōgi", "Judkins shōgi", "Euroshōgi"};

/* Lishogi's (ui/chat/src/preset.ts), sent as they are there, untranslated. */
const char *const CHAT_PRESETS[2][2][6] = {
    {{"hi/Hello", "gl/Good luck", "hf/Have fun!", "u2/You too!", "yoro/Yoroshiku onegaishimasu", NULL},
     {"gg/Good game", "wp/Well played", "ty/Thank you", "bye/Bye!", NULL}},
    {{"こん/こんにちは", "よろしく/よろしくお願いします", NULL},
     {"gg/Good game", "おつ/お疲れ様でした", "あり/ありがとうございました", "また/またお願いします", NULL}},
};

const char *const VARIANT_RULES[V_COUNT] = {
    "Standard shōgi: 9×9, the usual moves, drops, promotion and checkmate. This is the game "
    "described in How to Play.",
    "Minishōgi: a 5×5 board; each side has a king, a rook, a bishop, a gold, a silver and a pawn. The "
    "promotion zone is the last rank. Drops are allowed and the two-pawn rule applies. A position "
    "repeated a fourth time (sennichite) loses for Sente.",
    "Kyotoshōgi: a 5×5 board. Each move turns the piece over to its other side: pawn↔rook, "
    "silver↔bishop, gold↔knight, tokin↔lance; the king never turns. Pieces in hand can be dropped "
    "either side up, and the two-pawn rule does not apply. A position repeated three times is a draw.",
    "Checkshōgi: standard 9×9 shōgi, but the first player to give check wins. Checkmate is not "
    "needed, and a pawn drop that gives check is legal.",
    "Dōbutsu: 3×4. The chick moves one square forward and becomes a hen, which moves one square in any direction but diagonally back. "
    "The giraffe moves one square orthogonally, the elephant one square diagonally, and the lion "
    "one square in any direction. There is no check: capture the opponent's lion, or move your "
    "lion to the far rank while it is not attacked (try rule)."
     " A position repeated three times is a draw, even if one side attacked the lion every time.",
    "Goro Goro: 5×6, the shōgi the Japan Shōgi Association teaches children with. Each side has a king (the "
     "lion), one square in any direction; two golds (dogs), one square in any direction but diagonally "
     "back; two silvers (cats), one square diagonally or straight forward; and three pawns (chicks), one "
     "square forward. The last two ranks are the promotion zone: there pawns and silvers may promote, and "
     "then move as a gold (a pawn on the last rank must promote). Captured pieces can be dropped; two pawns "
     "on one file and mate by a pawn drop are not allowed.",
    "Tori shōgi (禽将棋, 1799): 7×7, sixteen birds a side.\n"
    "鵬 Phoenix, the king: one square in any direction.\n"
    "鶴 Crane: one square in any direction but sideways.\n"
    "雉 Pheasant: jumps two squares straight forward, or one square diagonally back.\n"
    "左鶉 Left quail: any distance straight forward or diagonally back to the right, one square diagonally back to the left. 右鶉 The right quail the other way round.\n"
    "鷹 Falcon: one square in any direction but straight back. Promotes to 鵰 eagle: one square in any direction, any distance diagonally forward or straight back, up to two squares diagonally back.\n"
    "燕 Swallow: one square forward. Promotes to 鴈 goose: jumps two squares diagonally forward or straight back.\n"
    "Promotion: the last two ranks; a swallow or falcon that moves there must promote.\n"
    "Drops: captured birds join your hand. Not allowed: a third swallow on one file, a swallow drop that mates, or a bird where it could never move.\n"
    "The end: checkmate wins, and so does leaving the opponent no legal move. Repeating a position a third time loses for the side that repeats, and so does perpetual check.",
    "Judkins shōgi: a 6×6 board with the standard pieces except the lance: a king, a rook, a bishop, a gold, a silver, a knight and a pawn each. The promotion zone is the last two ranks; everything else is as in standard shōgi: drops, the two-pawn rule, no mate by a pawn drop, and a position repeated a fourth time is a draw (perpetual check loses).",
    "Euroshōgi (1978): an 8×8 board; each side has a king, a rook, two bishops, two golds, two knights and eight pawns. The knight jumps as in shōgi, two squares forward and one to the side, and can also step one square sideways. The promotion zone is the last three ranks, and a piece that moves into, within or out of it must promote (a piece dropped in the zone promotes when it next moves). Captured pieces can be dropped; two pawns on one file and mate by a pawn drop are not allowed. A position repeated a third time is a draw, and perpetual check loses.",
};

extern const char *const GAME_STRINGS_JA[][2], *const GAME_STRINGS_PT[][2];

const GameLanguage GAME_LANGUAGES[] = {
    {"ja", GAME_STRINGS_JA},
    {"pt", GAME_STRINGS_PT},
    {NULL, NULL},
};
