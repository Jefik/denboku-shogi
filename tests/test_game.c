#include <stdio.h>
#include <string.h>

#include "game.h"
#include "notation.h"

static int failures;

#define EXPECT_STR(got, want)                                                                                   \
    do {                                                                                                       \
        if (strcmp((got), (want))) {                                                                           \
            printf("FAIL %s:%d: got '%s', want '%s'\n", __FILE__, __LINE__, (got), (want));                    \
            failures++;                                                                                        \
        }                                                                                                      \
    } while (0)

#define EXPECT(cond)                                                                                            \
    do {                                                                                                       \
        if (!(cond)) {                                                                                         \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                             \
            failures++;                                                                                        \
        }                                                                                                      \
    } while (0)

static void play(Game *g, const char *usi, char *ja, char *en)
{
    Move m;
    if (!move_from_usi(&g->pos, usi, &m)) {
        printf("FAIL illegal %s\n", usi);
        failures++;
        return;
    }
    int prev = g->count ? g->moves[g->count - 1].to : -1;
    move_to_japanese(&g->pos, &m, prev, ja, 64);
    move_to_western(&g->pos, &m, en, 64);
    game_play(g, &m, 1000);
}

static void test_notation(void)
{
    static const char *usi[] = {"7g7f", "3c3d", "2g2f", "8c8d", "2f2e", "8d8e", "6i7h", "4a3b",
                                "2e2d", "2c2d", "2h2d", "8e8f", "8g8f", "8b8f", "2d3d"};
    static const char *ja[] = {"▲７六歩", "△３四歩", "▲２六歩", "△８四歩", "▲２五歩", "△８五歩", "▲７八金",
                               "△３二金", "▲２四歩", "△同　歩", "▲同　飛", "△８六歩", "▲同　歩", "△同　飛",
                               "▲３四飛"};
    static const char *en[] = {"☗P-7f", "☖P-3d", "☗P-2f", "☖P-8d", "☗P-2e", "☖P-8e", "☗G-7h", "☖G-3b",
                               "☗P-2d", "☖Px2d", "☗Rx2d", "☖P-8f", "☗Px8f", "☖Rx8f", "☗Rx3d"};
    Game g;
    game_new(&g, HANDICAP_NONE);
    for (int i = 0; i < 15; i++) {
        char a[64], b[64];
        play(&g, usi[i], a, b);
        EXPECT_STR(a, ja[i]);
        EXPECT_STR(b, en[i]);
    }
    EXPECT(g.count == 15 && g.result == RESULT_NONE);

    /* KIF round trip. */
    strcpy(g.names[0], "You");
    strcpy(g.names[1], "Fairy-Stockfish");
    g.started = 1700000000;
    char buf[8192];
    FILE *f = fmemopen(buf, sizeof buf, "w");
    record_write(&g, f);
    fclose(f);
    Game r;
    f = fmemopen(buf, strlen(buf), "r");
    EXPECT(record_read(&r, f));
    fclose(f);
    EXPECT(r.count == 15 && r.pos.hash == g.pos.hash);
    EXPECT_STR(r.names[1], "Fairy-Stockfish");
    EXPECT(r.started == g.started);

    /* Sente resigns on Gote's turn (a resignation out of turn): the record says who won. */
    Game res = g;
    game_end(&res, RESULT_SECOND_WIN, REASON_RESIGN);
    f = fmemopen(buf, sizeof buf, "w");
    record_write(&res, f);
    fclose(f);
    f = fmemopen(buf, strlen(buf), "r");
    EXPECT(record_read(&r, f) && r.result == RESULT_SECOND_WIN && r.reason == REASON_RESIGN);
    fclose(f);
    f = fmemopen(buf, sizeof buf, "w");
    record_write_alt(&res, f);
    fclose(f);
    EXPECT(strstr(buf, "'summary:toryo:You lose:Fairy-Stockfish win") != NULL);
    f = fmemopen(buf, strlen(buf), "r");
    EXPECT(record_read_alt(&r, f) && r.result == RESULT_SECOND_WIN && r.reason == REASON_RESIGN);
    fclose(f);

    /* CSA round trip. */
    f = fmemopen(buf, sizeof buf, "w");
    record_write_alt(&g, f);
    fclose(f);
    EXPECT(strstr(buf, "+7776FU") != NULL && strstr(buf, "-8286HI") != NULL);
    f = fmemopen(buf, strlen(buf), "r");
    EXPECT(record_read_alt(&r, f));
    fclose(f);
    EXPECT(r.count == 15 && r.pos.hash == g.pos.hash);

    char usi_pos[1024];
    game_engine_position(&g, usi_pos, sizeof usi_pos);
    EXPECT(strstr(usi_pos, "moves 7g7f 3c3d") != NULL);
}

static void test_disambiguation(void)
{
    Position p;
    Move m;
    char ja[64], en[64];
    /* Two golds can reach 5h: from 6i (left, moving up) and 4i (right, moving up). */
    pos_from_text(&p, "4k4/9/9/9/9/9/9/9/3GKG3 b - 1");
    move_from_usi(&p, "6i5h", &m);
    move_to_japanese(&p, &m, -1, ja, 64);
    EXPECT_STR(ja, "▲５八金左");
    move_to_western(&p, &m, en, 64);
    EXPECT_STR(en, "☗G6i-5h");
    /* Drop where a board piece could also go: 打. */
    pos_from_text(&p, "4k4/9/9/9/9/9/9/4G4/4K4 b G 1");
    move_from_usi(&p, "G*5g", &m);
    move_to_japanese(&p, &m, -1, ja, 64);
    EXPECT_STR(ja, "▲５七金打");
    /* Promotion choice. */
    pos_from_text(&p, "4k4/9/9/4S4/9/9/9/9/4K4 b - 1");
    move_from_usi(&p, "5d5c", &m);
    move_to_japanese(&p, &m, -1, ja, 64);
    EXPECT_STR(ja, "▲５三銀不成");
}

static void test_repetition(void)
{
    Game g;
    game_new(&g, HANDICAP_NONE);
    static const char *cycle[] = {"5i4h", "5a4b", "4h5i", "4b5a"};
    char a[64], b[64];
    for (int n = 0; n < 3 && g.result == RESULT_NONE; n++)
        for (int i = 0; i < 4 && g.result == RESULT_NONE; i++)
            play(&g, cycle[i], a, b);
    EXPECT(g.result == RESULT_DRAW && g.reason == REASON_REPETITION && g.count == 12);
}

/* A Fourth-file Rook with the Mino against a Static Rook, then the bishops traded. */
static void test_style(void)
{
    Game g;
    rules_set_variant(V_STANDARD);
    game_new(&g, HANDICAP_NONE);
    static const char *moves[] = {"2h6h", "3c3d", "5i4h", "8c8d", "4h3h", "8d8e", "3h2h", "9c9d", "3i3h", "1c1d",
                                  "6i5h", "7c7d", "9g9f", "6c6d", "1g1f", "4c4d"};
    char a[64], b[64], style[256];
    game_style(&g, 0, style, sizeof style);
    EXPECT_STR(style, "");
    for (int i = 0; i < 16; i++)
        play(&g, moves[i], a, b);
    game_style(&g, g.count, style, sizeof style);
    EXPECT_STR(style, "☗ Fourth-file Rook, Mino castle\n☖ Static Rook");
    game_style(&g, 14, style, sizeof style);
    EXPECT_STR(style, "☗ Fourth-file Rook, Mino castle");
    game_new(&g, HANDICAP_NONE);
    static const char *trade[] = {"7g7f", "3c3d", "8h2b+", "3a2b"};
    for (int i = 0; i < 4; i++)
        play(&g, trade[i], a, b);
    game_style(&g, g.count, style, sizeof style);
    EXPECT_STR(style, "Bishop Exchange");
}

static void test_checkmate(void)
{
    Game g;
    game_new(&g, HANDICAP_NONE);
    pos_from_text(&g.start, "4k4/9/4P4/9/9/9/9/9/4K4 b G 1");
    g.pos = g.start;
    g.hashes[0] = g.pos.hash;
    char a[64], b[64];
    play(&g, "G*5b", a, b);
    EXPECT(g.result == RESULT_FIRST_WIN && g.reason == REASON_CHECKMATE);
}

/* A start set up in the editor survives KIF and CSA: board, hands (11 pawns), gote to move. */
static void custom_start(const char *sfen)
{
    Game g;
    game_new(&g, HANDICAP_NONE);
    pos_from_text(&g.start, sfen);
    g.pos = g.start;
    g.hashes[0] = g.pos.hash;
    char a[64], b[64];
    play(&g, "5a4b", a, b);
    char buf[8192], got[160], want[160];
    pos_to_text(&g.start, want, sizeof want);
    for (int csa = 0; csa < 2; csa++) {
        FILE *f = fmemopen(buf, sizeof buf, "w");
        csa ? record_write_alt(&g, f) : record_write(&g, f);
        fclose(f);
        Game r;
        f = fmemopen(buf, strlen(buf), "r");
        EXPECT(csa ? record_read_alt(&r, f) : record_read(&r, f));
        fclose(f);
        pos_to_text(&r.start, got, sizeof got);
        EXPECT_STR(got, want);
        EXPECT(r.count == 1 && r.pos.hash == g.pos.hash);
    }
}

static void test_custom_start(void)
{
    /* The second has more pieces than a set, as the sandbox allows. */
    static const char *const SFENS[] = {"4k4/9/4P4/9/9/9/9/9/4K4 w GS2r2b3g3s4n4l11Pp 1",
                                        "4k4/9/4P4/9/9/9/9/9/4K4 w 20GS2r2b3g3s4n4l25P10p 1",
                                        "4k4/9/4P4/9/9/9/9/9/4K4 w 99P2l 1"};
    for (size_t i = 0; i < sizeof SFENS / sizeof *SFENS; i++)
        custom_start(SFENS[i]);
}

/* An illegal move (here a second pawn on file 7, nifu) stays on the board and in KIF and CSA. */
static void test_illegal_move(void)
{
    Game g;
    game_new(&g, HANDICAP_NONE);
    pos_from_text(&g.start, "4k4/9/9/9/9/9/2P6/9/4K4 b P 1");
    g.pos = g.start;
    g.hashes[0] = g.pos.hash;
    Move m = {.from = DROP + FU, .to = SQ(4, 2)};
    EXPECT(!pos_is_legal(&g.pos, &m) && pos_is_possible(&g.pos, &m));
    game_play_illegal(&g, &m, 3000);
    EXPECT(g.count == 1 && g.result == RESULT_SECOND_WIN && g.reason == REASON_ILLEGAL);
    char buf[8192];
    for (int csa = 0; csa < 2; csa++) {
        FILE *f = fmemopen(buf, sizeof buf, "w");
        csa ? record_write_alt(&g, f) : record_write(&g, f);
        fclose(f);
        Game r;
        f = fmemopen(buf, strlen(buf), "r");
        EXPECT(csa ? record_read_alt(&r, f) : record_read(&r, f));
        fclose(f);
        EXPECT(r.count == 1 && r.pos.hash == g.pos.hash && r.reason == REASON_ILLEGAL && r.result == RESULT_SECOND_WIN);
        EXPECT(r.times[0] == 3000);
    }
}

/* Records as lishogi exports them: CSA with several statements on a line, KIF with its headers. */
static void test_lishogi_records(void)
{
    Game g;
    EXPECT(game_read_csa_text(&g, "N+A\nN-B\nPI\n+\n+7776FU,T3\n-3334FU,T5\n+8822UM,T1\n%TORYO\n"));
    EXPECT(g.count == 3 && g.times[0] == 3000 && g.times[1] == 5000 && g.reason == REASON_RESIGN);
    EXPECT(record_read_text(&g, "開始日時：2026/11/08 02:03:02\n場所：https://lishogi.org/plmBVXxV\n"
                                  "持ち時間：30分+10秒\n手合割：平手\n先手：A\n後手：B\n"
                                  "手数----指手---------消費時間--\n   1   ７六歩(77)   (00:02/00:00:02)\n"
                                  "   2   ９二飛(82)   (00:04/00:00:04)\n   3   切れ負け\n"));
    EXPECT(g.count == 2 && g.times[1] == 4000 && g.reason == REASON_TIME && g.result == RESULT_SECOND_WIN);
}

static void from_position(Game *g, const char *sfen)
{
    pos_from_text(&g->start, sfen);
    g->pos = g->start;
    g->hashes[0] = g->pos.hash;
    g->count = 0;
    g->result = RESULT_NONE;
    g->reason = REASON_NONE;
}

static void test_variants(void)
{
    Game g;
    char a[64], b[64];

    /* Checkshogi: giving check wins immediately (lishogi's rule). */
    rules_set_variant(V_CHECKSHOGI);
    game_new(&g, HANDICAP_NONE);
    from_position(&g, "9/3gk4/9/2b6/9/6B2/9/4KG3/9 b - 1");
    play(&g, "3f2e", a, b);
    EXPECT(g.result == RESULT_FIRST_WIN && g.reason == REASON_CHECK);

    /* Dobutsu: capturing the king ends the game. */
    rules_set_variant(V_DOBUTSU);
    game_new(&g, HANDICAP_NONE);
    from_position(&g, "3/1k1/3/1K1 b - 1");
    play(&g, "2d2c", a, b);
    EXPECT(g.result == RESULT_NONE);
    play(&g, "2b2c", a, b);
    EXPECT(g.result == RESULT_SECOND_WIN && g.reason == REASON_KING);

    /* Tori shogi: the third repetition loses for the side that made it; the birds' notation. */
    rules_set_variant(V_TORI);
    game_new(&g, HANDICAP_NONE);
    from_position(&g, "3k3/7/7/7/7/7/3K3 b - 1");
    for (int i = 0; i < 2 && g.result == RESULT_NONE; i++) {
        play(&g, "4g4f", a, b);
        play(&g, "4a4b", a, b);
        play(&g, "4f4g", a, b);
        play(&g, "4b4a", a, b);
    }
    EXPECT(g.result == RESULT_FIRST_WIN && g.reason == REASON_REPETITION && g.count == 8);
    /* lishogi: Dōbutsu draws on the third, Minishogi's fourth is a loss for sente. */
    rules_set_variant(V_DOBUTSU);
    from_position(&g, "1k1/3/3/1K1 b - 1");
    for (int i = 0; i < 2 && g.result == RESULT_NONE; i++) {
        play(&g, "2d2c", a, b);
        play(&g, "2a2b", a, b);
        play(&g, "2c2d", a, b);
        play(&g, "2b2a", a, b);
    }
    EXPECT(g.result == RESULT_DRAW && g.reason == REASON_REPETITION && g.count == 8);
    rules_set_variant(V_MINISHOGI);
    from_position(&g, "2k2/5/5/5/2K2 b - 1");
    for (int i = 0; i < 3 && g.result == RESULT_NONE; i++) {
        play(&g, "3e3d", a, b);
        play(&g, "3a3b", a, b);
        play(&g, "3d3e", a, b);
        play(&g, "3b3a", a, b);
    }
    EXPECT(g.result == RESULT_SECOND_WIN && g.reason == REASON_REPETITION && g.count == 12);
    rules_set_variant(V_TORI);
    from_position(&g, "3k3/7/7/7/s6/7/3K3 w - 1");
    play(&g, "7e7f+", a, b);
    EXPECT(!strcmp(a, "△７六燕成") && !strcmp(b, "☖S-7f+"));
    Move m;
    from_position(&g, "3k3/7/7/7/s6/7/3K3 w - 1");
    EXPECT(!move_from_usi(&g.pos, "7e7f", &m)); /* promotion is forced */

    rules_set_variant(V_STANDARD);
}

static void test_engine_conversion(void)
{
    Game g;
    char buf[512];
    rules_set_variant(V_DOBUTSU);
    game_new(&g, HANDICAP_NONE);
    game_engine_position(&g, buf, sizeof buf);
    EXPECT(strstr(buf, "position sfen gle/1c1/1C1/ELG b - 1") == buf);

    from_position(&g, "3/1k1/3/1K1 b P 1");
    Move m;
    EXPECT(game_move_from_engine(&g.pos, "C*2c", &m) && IS_DROP(m) && DROP_TYPE(m) == FU);

    rules_set_variant(V_KYOTOSHOGI);
    game_new(&g, HANDICAP_NONE);
    game_engine_position(&g, buf, sizeof buf);
    EXPECT(strstr(buf, "position sfen p+nks+l/5/5/5/+LSK+NP b - 1") == buf);
    from_position(&g, "5/5/5/5/k3K b G 1");
    EXPECT(game_move_from_engine(&g.pos, "+N*5b", &m) && IS_DROP(m) && DROP_TYPE(m) == KI);
    EXPECT(game_move_from_engine(&g.pos, "G*5b", &m) && IS_DROP(m) && DROP_TYPE(m) == KI);

    game_new(&g, HANDICAP_NONE);
    EXPECT(game_move_from_engine(&g.pos, "2e2d-", &m) && m.promote);
    game_play(&g, &m, 0);
    game_engine_position(&g, buf, sizeof buf);
    EXPECT(strstr(buf, "moves 2e2d-") != NULL);

    rules_set_variant(V_STANDARD);
}

/* Variants survive KIF and CSA: the variant header, the start position and the moves. */
static void test_variant_records(void)
{
    static const int variants[] = {V_MINISHOGI, V_KYOTOSHOGI, V_CHECKSHOGI, V_DOBUTSU, V_GOROGORO, V_TORI, V_JUDKINS, V_EURO};
    char buf[8192], want[192], got[192];
    for (size_t v = 0; v < sizeof variants / sizeof variants[0]; v++) {
        rules_set_variant(variants[v]);
        Game g;
        game_new(&g, HANDICAP_NONE);
        Move moves[512];
        int n = pos_legal_moves(&g.pos, moves);
        EXPECT(n > 0);
        if (n > 0)
            game_play(&g, &moves[0], 0);
        pos_to_text(&g.start, want, sizeof want);
        for (int csa = 0; csa < 2; csa++) {
            FILE *f = fmemopen(buf, sizeof buf, "w");
            csa ? record_write_alt(&g, f) : record_write(&g, f);
            fclose(f);
            Game r;
            f = fmemopen(buf, strlen(buf), "r");
            bool ok = csa ? record_read_alt(&r, f) : record_read(&r, f);
            fclose(f);
            EXPECT(ok);
            EXPECT(rules_variant == variants[v]);
            pos_to_text(&r.start, got, sizeof got);
            EXPECT_STR(got, want);
            EXPECT(r.count == g.count && r.pos.hash == g.pos.hash);
        }
    }
    rules_set_variant(V_STANDARD);
}

/* Kyotoshogi drops either face of a piece in hand (lishogi, Fairy-Stockfish): R* is the pawn's rook side. */
static void test_kyoto_drops(void)
{
    rules_set_variant(V_KYOTOSHOGI);
    Game g;
    game_new(&g, HANDICAP_NONE);
    pos_from_text(&g.start, "2k2/5/5/5/2K2 b GSPTgspt 1");
    g.pos = g.start;
    g.hashes[0] = g.pos.hash;
    char ja[64], en[64], usi[16];
    play(&g, "R*1c", ja, en);
    EXPECT_STR(ja, "▲１三飛");
    EXPECT(g.pos.board[SQ(2, 4)] == PIECE(HI, FIRST) && g.pos.hand[FIRST][FU] == 0);
    move_to_usi(&g.moves[0], usi);
    EXPECT_STR(usi, "R*1c");
    play(&g, "B*5a", ja, en);
    play(&g, "N*1d", ja, en);
    play(&g, "L*5b", ja, en);
    EXPECT(g.pos.hand[SECOND][HAND_TO_SLOT] == 0 && g.pos.board[SQ(1, 0)] == PIECE(KY, SECOND));
    char buf[8192];
    for (int csa = 0; csa < 2; csa++) {
        FILE *f = fmemopen(buf, sizeof buf, "w");
        csa ? record_write_alt(&g, f) : record_write(&g, f);
        fclose(f);
        Game r;
        f = fmemopen(buf, strlen(buf), "r");
        EXPECT(csa ? record_read_alt(&r, f) : record_read(&r, f));
        fclose(f);
        EXPECT(r.count == 4 && r.pos.hash == g.pos.hash);
    }
    rules_set_variant(V_STANDARD);
}

/* 持将棋 (an impasse draw) is a draw in KIF and CSA, not a repetition. */
static void test_jishogi(void)
{
    Game g;
    EXPECT(record_read_text(&g, "手合割：平手\n手数----指手---------消費時間--\n   1 ７六歩(77)\n   2 持将棋\n"));
    EXPECT(g.result == RESULT_DRAW && g.reason == REASON_IMPASSE);
    char buf[4096];
    FILE *f = fmemopen(buf, sizeof buf, "w");
    record_write_alt(&g, f);
    fclose(f);
    EXPECT(strstr(buf, "%JISHOGI") != NULL);
    EXPECT(game_read_csa_text(&g, buf) && g.result == RESULT_DRAW && g.reason == REASON_IMPASSE);
}

/* A lesson's step where the other side passes: the same side's moves one after the other, and the
 * position at each ply has the captures in that side's hand. */
static void test_passes(void)
{
    Game g;
    game_new(&g, HANDICAP_NONE);
    pos_from_text(&g.start, "4k4/9/4p4/9/9/9/9/9/R3K4 b - 1");
    Position p = g.start;
    const char *moves[] = {"9i9c", "9c5c"};
    for (int i = 0; i < 2; i++) {
        Move m;
        EXPECT(move_from_usi(&p, moves[i], &m));
        pos_make(&p, &m);
        p.side = FIRST; /* the other side passes */
        g.moves[g.count++] = m;
    }
    Position at;
    game_position_at(&g, 2, &at);
    EXPECT(at.hand[FIRST][hand_slot(FU)] == 1 && at.hand[SECOND][hand_slot(FU)] == 0);
}

int main(void)
{
    rules_init();
    test_notation();
    test_disambiguation();
    test_repetition();
    test_style();
    test_checkmate();
    test_custom_start();
    test_illegal_move();
    test_lishogi_records();
    test_variants();
    test_variant_records();
    test_kyoto_drops();
    test_jishogi();
    test_engine_conversion();
    test_passes();
    printf(failures ? "%d failures\n" : "all tests passed\n", failures);
    return failures != 0;
}
