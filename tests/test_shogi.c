#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shogi.h"

static int failures;

static void check_perft(const char *sfen, const uint64_t *expected, int depth)
{
    Position p;
    if (!pos_from_text(&p, sfen)) {
        printf("FAIL parse %s\n", sfen);
        failures++;
        return;
    }
    for (int d = 1; d <= depth; d++) {
        uint64_t n = perft(&p, d);
        if (n != expected[d - 1]) {
            printf("FAIL perft(%d) = %" PRIu64 ", expected %" PRIu64 " for %s\n", d, n, expected[d - 1], sfen);
            failures++;
        }
    }
}

static void check_sfen_roundtrip(const char *sfen)
{
    Position p;
    char out[256];
    pos_from_text(&p, sfen);
    pos_to_text(&p, out, sizeof out);
    if (strcmp(out, sfen)) {
        printf("FAIL sfen %s -> %s\n", sfen, out);
        failures++;
    }
}

static void check_move(const char *sfen, const char *usi, int legal)
{
    Position p;
    Move m;
    pos_from_text(&p, sfen);
    if (move_from_usi(&p, usi, &m) != legal) {
        printf("FAIL %s should be %s in %s\n", usi, legal ? "legal" : "illegal", sfen);
        failures++;
    }
}

int main(int argc, char **argv)
{
    rules_init();
    const char *start = "lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1";
    static const uint64_t start_perft[] = {30, 900, 25470, 719731, 19861490};
    check_perft(start, start_perft, argc > 1 ? atoi(argv[1]) : 4);
    /* Auto-promotion: pawn, bishop and rook keep their moves; lance, knight and silver lose some. */
    static const int keeps[] = {FU, KA, HI}, loses[] = {KY, KE, GI};
    for (int i = 0; i < 3; i++)
        if (!promotion_keeps_moves(keeps[i]) || promotion_keeps_moves(loses[i])) {
            printf("FAIL promotion_keeps_moves %d/%d\n", keeps[i], loses[i]);
            failures++;
        }

    check_sfen_roundtrip(start);
    check_sfen_roundtrip("lnsgkgsnl/1r5b1/ppppppppp/9/9/2P6/PP1PPPPPP/1B5R1/LNSGKGSNL w - 2");
    check_sfen_roundtrip("8l/1l+R2P3/p2pBG1pp/kps1p4/Nn1P2G2/P1P1P2PP/1PS6/1KSG3+r1/LN2+p3L w Sbgn3p 124");

    /* Nifu: a second unpromoted pawn on file 5. */
    check_move("4k4/9/9/9/9/9/4P4/9/4K4 b P 1", "P*5e", 0);
    check_move("4k4/9/9/9/9/9/4P4/9/4K4 b P 1", "P*4e", 1);
    /* Uchifuzume: pawn drop mate is illegal, the same pawn pushed is fine. */
    check_move("kl7/9/1G7/9/9/9/9/9/8K b P 1", "P*9b", 0);
    /* Forced promotion and dead pieces. */
    check_move("4k4/P8/9/9/9/9/9/9/4K4 b - 1", "9b9a", 0);
    check_move("4k4/P8/9/9/9/9/9/9/4K4 b - 1", "9b9a+", 1);
    check_move("4k4/9/9/9/9/9/9/9/4K4 b N 1", "N*5b", 0);
    check_move("4k4/9/9/9/9/9/9/9/4K4 b N 1", "N*5c", 1);

    /* Physical moves include illegal ones: nifu drop and the unpromoted dead pawn push. */
    {
        Position p;
        Move all[MAX_MOVES];
        pos_from_text(&p, "4k4/P8/9/9/9/9/4P4/9/4K4 b P 1");
        int n = pos_physical_moves(&p, all), nifu = 0, dead = 0;
        for (int i = 0; i < n; i++) {
            nifu += IS_DROP(all[i]) && all[i].to == SQ(4, 4);
            dead += !IS_DROP(all[i]) && all[i].from == SQ(1, 0) && !all[i].promote;
        }
        if (!nifu || !dead) {
            printf("FAIL physical moves\n");
            failures++;
        }
    }
    /* Variants (lishogi/shogiops perfts). */
    rules_set_variant(V_MINISHOGI);
    check_perft("rbsgk/4p/5/P4/KGSBR b - 1", (const uint64_t[]) {14, 181, 2512, 35401}, 4);
    check_sfen_roundtrip("rbsgk/4p/5/P4/KGSBR b - 1");
    rules_set_variant(V_KYOTOSHOGI);
    /* Depths 5 and 6 (Fairy-Stockfish) have drops of either face. */
    check_perft("pgkst/5/5/5/TSKGP b - 1", (const uint64_t[]) {12, 137, 1636, 18268, 225903, 2829234}, 6);
    check_sfen_roundtrip("pgkst/5/5/5/TSKGP b - 1");
    check_perft("2k2/5/5/5/2K2 b GSPTgspt 1", (const uint64_t[]) {189, 28889, 3315266}, 3); /* NSPLnspl to the engine */
    /* Checkshogi is standard moves; the perft stops at a check win in shogiops, so the
     * standard depth 4 number is already covered above. */
    rules_set_variant(V_CHECKSHOGI);
    check_perft("lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1",
                (const uint64_t[]) {30, 900, 25470}, 3);
    /* Dobutsu ends on king capture/try rule, so its perft differs from shogiops after depth 2;
     * the end rules are tested in test_game. */
    rules_set_variant(V_DOBUTSU);
    check_perft("rkb/1p1/1P1/BKR b - 1", (const uint64_t[]) {4, 17}, 2);
    check_sfen_roundtrip("rkb/1p1/1P1/BKR b - 1");
    /* Goro Goro: Fairy-Stockfish's gorogoro perft. */
    rules_set_variant(V_GOROGORO);
    check_perft("sgkgs/5/1ppp1/1PPP1/5/SGKGS b - 1", (const uint64_t[]) {16, 250, 4166, 67517, 1186299}, 5);
    check_sfen_roundtrip("sgkgs/5/1ppp1/1PPP1/5/SGKGS b - 1");
    /* Tori shogi: Fairy-Stockfish's torishogi perft (geese, eagles, quails, two swallows a file). */
    rules_set_variant(V_TORI);
    check_perft("rpckcpl/3f3/sssssss/2s1S2/SSSSSSS/3F3/LPCKCPR b - 1", (const uint64_t[]) {17, 288, 5430, 103857}, 4);
    check_sfen_roundtrip("rpckcpl/3f3/sssssss/2s1S2/SSSSSSS/3F3/LPCKCPR b - 1");
    check_perft("3k3/1s1+f3/7/2s1s2/S1+F2r1/1+s2S2/K1S4 b 2SCLPcfs 1", (const uint64_t[]) {161, 19732, 2513662}, 3);
    check_sfen_roundtrip("3k3/1s1+f3/7/2s1s2/S1+F2r1/1+s2S2/K1S4 b CPL2Sfcs 1");
    check_perft("r2k3/2s4/1s5/3+S3/2S1l2/p+f1S3/1S1KC1+s w FPRSs 20", (const uint64_t[]) {56, 7425, 259540}, 3);
    /* Judkins shogi: Fairy-Stockfish's judkins perft (a knight that must promote on the last two ranks). */
    rules_set_variant(V_JUDKINS);
    check_perft("rbnsgk/5p/6/6/P5/KGSNBR b - 1", (const uint64_t[]) {20, 336, 6183, 118345}, 4);
    check_sfen_roundtrip("rbnsgk/5p/6/6/P5/KGSNBR b - 1");
    check_perft("r2sgk/2b2p/1n4/2P1N1/6/KGS1BR b Pp 1", (const uint64_t[]) {6, 203, 4254}, 3);
    check_perft("4sk/6/5G/6/6/6 b Nr 1", (const uint64_t[]) {27, 848, 6425}, 3);
    /* Euroshogi: Fairy-Stockfish's euroshogi perft (the knight's side steps, forced promotion in three ranks). */
    rules_set_variant(V_EURO);
    check_perft("1nbgkgn1/1r4b1/pppppppp/8/8/PPPPPPPP/1B4R1/1NGKGBN1 b - 1", (const uint64_t[]) {25, 625, 15424, 380499}, 4);
    check_sfen_roundtrip("1nbgkgn1/1r4b1/pppppppp/8/8/PPPPPPPP/1B4R1/1NGKGBN1 b - 1");
    check_perft("4k3/8/1N6/8/8/8/8/4K3 b Nn 1", (const uint64_t[]) {70, 4299, 80095, 933715}, 4);
    check_perft("1n1gkg2/1r4b1/p1pp1ppp/1p2p3/3P4/PP2NP1P/1B1GP1R1/2G1KB2 b NPp 1", (const uint64_t[]) {72, 1927, 101216, 2662649}, 4);
    rules_set_variant(V_STANDARD);

    /* The Minishogi mate problems read as Minishogi positions. */
    FILE *f = fopen("data/problems/problems-minishogi.txt", "r");
    if (f) {
        rules_set_variant(V_MINISHOGI);
        char line[256];
        int n = 0;
        while (fgets(line, sizeof line, f)) {
            Position q;
            if (!pos_from_text(&q, strchr(line, ' ') + 1)) {
                printf("bad minishogi problem: %s", line);
                failures++;
            }
            n++;
        }
        fclose(f);
        printf("%d minishogi problems read\n", n);
        rules_set_variant(V_STANDARD);
    }
    printf(failures ? "%d failures\n" : "all tests passed\n", failures);
    return failures != 0;
}
