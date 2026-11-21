#ifndef TEXTS_H
#define TEXTS_H

#include "rules.h"

/* The game's long texts (English, the keys of the translations in strings.c): How to Play, the
 * About credits and each variant's name and rules. */

/* The "How to Play" pages (Pango markup), keys of the translations. */
#define HOW_TO_RULES_EN \
    "<b>Goal</b>\nCheckmate the other king. Sente (▲) moves first; then the players take turns. No move " \
    "may leave your own king in check.\n\n" \
    "<b>Pieces</b>\n" \
    "玉 King: one square in any direction.\n" \
    "飛 Rook: any distance straight.\n" \
    "角 Bishop: any distance diagonally.\n" \
    "金 Gold: one square, but not diagonally back.\n" \
    "銀 Silver: one square forward or diagonally.\n" \
    "桂 Knight: two forward and one to the side, jumping.\n" \
    "香 Lance: any distance forward.\n" \
    "歩 Pawn: one square forward.\n\n" \
    "<b>Promotion</b>\nA piece that moves into, out of or inside the last three ranks may turn over. Rook " \
    "and bishop gain the king's step; silver, knight, lance and pawn move like gold. Gold and king do not " \
    "promote. A piece that could not move again must promote.\n\n" \
    "<b>Captures and drops</b>\nA captured piece joins your hand. Instead of moving, you can drop a piece " \
    "from your hand on any empty square, unpromoted. Not allowed: two unpromoted pawns of yours on one " \
    "file (二歩), a pawn drop that mates (打ち歩詰め), or a piece where it could never move.\n\n" \
    "<b>The end</b>\nCheckmate wins, and so does the opponent's resignation or running out of time. The " \
    "same position four times is a draw (千日手), unless one side gave check every move: that side loses. " \
    "A player whose king is in the enemy camp, not in check, with at least ten other pieces there, can " \
    "declare a win (入玉宣言) with 28 points as Sente or 27 as Gote, counting rook and bishop 5 and the " \
    "others 1, in the camp or in hand: ☰ → Declare Impasse, which says what is missing if it does not hold yet."
#define HOW_TO_APP_EN \
    "<b>Moving</b>\nTap a piece, then a marked square, or drag it there; pieces in hand too. \"Show how " \
    "pieces move\" in Preferences draws each piece's moves on it, for all variants or one; tap a " \
    "promoted piece to see its unpromoted side. During the opponent's turn you can already pick your " \
    "next move. To plan, drag between squares for an arrow or hold a square for a circle (the right " \
    "button with a mouse).\n\n" \
    "<b>Undo and hints</b>\nUndo takes back the last move; in a network game it asks the other player " \
    "first. Hint shows a good move.\n\n" \
    "<b>Games</b>\n◀ ▶ and the move list show earlier positions, and ☰ → Replay plays the game back. " \
    "☰ → Games saves and loads games as KIF or CSA, copies and pastes them on computers, opens a Lishogi " \
    "game by its link and keeps the history of finished games.\n\n" \
    "<b>Sandbox</b>\n☰ → Sandbox sets up any position: drag pieces, or drag on empty squares to select " \
    "several. Play starts it at once between two players, with any number of pieces; Done makes it the " \
    "start of a new game.\n\n" \
    "<b>Online</b>\nIn New Game: a network game hosts or joins a game on the local network (or over the " \
    "internet, with the port open); Lishogi and CSA play on those sites."

#define HOW_TO_CUSTOM_EN \
    "<b>Pieces</b>\nA folder or a zip in pieces/ with a PNG picture of each piece, named by its code: FU (pawn), KY, KE, " \
    "GI, KI, KA, HI, OU and GY (the kings), TO, NY, NK, NG, UM, RY. Either 0FU.png for Sente and 1FU.png for Gote, as in " \
    "pychess's sets, or only FU.png (Gote's is the same picture, turned around). The set takes the folder's or the zip's name.\n\n" \
    "<b>Sounds</b>\nA folder in sounds/ with 16-bit WAV files: move, capture, check, mate, win, lose, draw, start, notify, " \
    "tick and wrong (.wav). A sound the set lacks comes from a built-in set.\n\n" \
    "Restart the game and choose the set in Preferences."

#define ABOUT_CREDITS_EN \
    "GPLv3. Layout and design follow GNOME Chess (glchess).\n" \
    "Engine: Fairy-Stockfish (GPLv3).\n" \
    "Mate problems: Generated Tsume-Shogi by Shinta Nakayama, v4 (CC BY 4.0, creativecommons.org/licenses/by/4.0), " \
    "filtered and given a king for Sente; Minishōgi's are generated with Fairy-Stockfish.\n" \
    "Pictorial pieces: CouchTomato87 (CC BY 4.0), via Lishogi.\n" \
    "Kanji and western pieces: peanatsu's shogi_printout (CC BY-SA 3.0), with AR PL UKai (Arphic Public License), " \
    "Un fonts (GPL-2), Hari Seldon's cursive promoted pieces, and chess pieces by Colin M.L. Burnett and NikNaks and a " \
    "lance by Orror/Zigeuner (Wikimedia Commons).\n" \
    "Fonts: Noto Sans JP, Symbols 2 and Emoji, Yuji Syuku, Zen Antique (SIL OFL 1.1); Gata Sōsho by gata (free to use, " \
    "modify and share).\n" \
    "Libraries: GTK 2, cairo and Pango (LGPL), curl, Monocypher (BSD-2 or CC0); on Android, Termux's packages and mbed TLS (Apache-2.0)."

extern const char *const VARIANT_LABELS[V_COUNT], *const VARIANT_RULES[V_COUNT];

/* Proverbs (格言) told one a day in the tutorial: the proverb and what it says, in our words (English, a key). */
static const char *const PROVERBS[][2] = {
    {"一歩千金", "One pawn in hand can be worth a thousand golds: keep one for the right moment."},
    {"金底の歩は岩より固し", "A pawn dropped on the back rank, just under a gold, makes the defense harder than rock."},
    {"香は下段から打て", "Drop a lance low, from far back, so that it covers the whole file."},
    {"桂馬は控えて打て", "Drop a knight a step back, so that it can jump in later."},
    {"桂馬の高とび歩の餌食", "A knight that jumps forward too soon is food for the pawns."},
    {"桂先の銀定跡なり", "A silver on the square in front of a knight is the book answer to it."},
    {"金は止めに残せ", "Keep a gold in hand for the last, mating drop."},
    {"金は斜めに誘え", "Lure a gold diagonally forward: it cannot step back that way."},
    {"序盤は飛車より角", "In the opening the bishop is the more dangerous of the two."},
    {"五三のと金に負けなし", "A tokin on 5c hardly ever loses."},
    {"と金のおそ早", "A tokin looks slow, but its attack comes sooner than it seems."},
    {"居玉は避けよ", "Do not start fighting with the king still on its starting square."},
    {"玉の守りは金銀三枚、攻めは飛角銀桂", "Three of the golds and silvers guard the king; rook, bishop, silver and knight attack."},
    {"玉は角筋を避けよ", "Keep the king off the bishops' diagonals."},
    {"五五の位は天王山", "A pawn held on 5e takes the key point of the board."},
    {"振り飛車には角交換を狙え", "Against Ranging Rook, aim to trade bishops."},
    {"玉は包むように寄せよ", "Close in on the king from all sides, as if wrapping it."},
    {"玉は下段に落とせ", "Drive the king down to its back rank."},
    {"端玉には端歩", "Against a king on the edge, push the edge pawn."},
    {"玉の腹から銀を打て", "Drop a silver beside the king, at its belly."},
    {"終盤は駒の損得より速度", "In the endgame, speed counts for more than pieces won or lost."},
    {"敵の打ちたいところへ打て", "Drop where your opponent would like to drop."},
    {"大駒は近づけて受けよ", "Defend against a rook or bishop by letting it come close, then chasing it."},
    {"両取り逃げるべからず", "When forked, do not just save one piece: hit back."},
    {"玉の早逃げ八手の得あり", "Moving the king away early is worth eight moves."},
};
#define PROVERB_COUNT ((int) (sizeof PROVERBS / sizeof *PROVERBS))

#endif
