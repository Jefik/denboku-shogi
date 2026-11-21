/* Mutation fuzzer for the record, position, move and JSON readers (ASan + UBSan). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "json.h"
#include "notation.h"

static unsigned long long rs = 88172645463325252ULL;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned) (rs >> 11); }

static const char *TOKENS[] = {"+", "*", "同", "打", "成", "不成", "▲", "△", "９", "九", "(", ")", "/", " ", "\n", "99", "-1",
    "0", "手合割：", "先手：", "後手：", "# SFEN ", "# DENBOKU VARIANT ", "'DENBOKU VARIANT ", "PI", "P+", "P-", "P1", "+0000", "%TORYO",
    "{", "}", "\"", "\\u", "\\", ":", ",", "[", "]", "null", "まで", "投了", "手数＝", "後手の持駒：", "| ", "+--", "T99999999", "(00:00/00:00:00)", "00", "AL", "N+", "$START_TIME:", "V2.2", "b", "w", " - ", "18P", "+P", "k", "K"};

static size_t mutate(char *buf, size_t len, size_t cap)
{
    int n = 1 + rnd() % 6;
    while (n--) {
        switch (rnd() % 6) {
        case 0: if (len) buf[rnd() % len] = (char) rnd(); break;
        case 1: if (len) buf[rnd() % len] ^= (char) (1 << (rnd() % 8)); break;
        case 2: if (len > 2) { size_t a = rnd() % len, l = 1 + rnd() % (len - a < 40 ? len - a : 40); memmove(buf + a, buf + a + l, len - a - l); len -= l; } break;
        case 3: { const char *t = TOKENS[rnd() % (sizeof TOKENS / sizeof *TOKENS)]; size_t tl = strlen(t), a = len ? rnd() % len : 0;
                  if (len + tl < cap) { memmove(buf + a + tl, buf + a, len - a); memcpy(buf + a, t, tl); len += tl; } } break;
        case 4: if (len > 4) { size_t a = rnd() % len, b = rnd() % len, l = 1 + rnd() % 200; if (a + l > len) l = len - a;
                  if (len + l < cap) { char tmp[256]; memcpy(tmp, buf + a, l); memmove(buf + b + l, buf + b, len - b); memcpy(buf + b, tmp, l); len += l; } } break;
        case 5: if (len) { size_t a = rnd() % len; int r = 1 + rnd() % 300; while (r-- && len + 1 < cap) { memmove(buf + a + 1, buf + a, len - a); len++; } } break;
        }
    }
    buf[len] = 0;
    return len;
}

static Game g, r;

static void exercise(Game *x)
{
    /* What the app does with a loaded game: replay, notation, write back. */
    char buf[256];
    Position p;
    for (int i = 0; i <= x->count; i += 1 + x->count / 8) {
        game_position_at(x, i, &p);
        if (i < x->count)
            move_notation(&p, &x->moves[i], i ? x->moves[i - 1].to : -1, rnd() & 1, buf, sizeof buf);
    }
    char sfen[512];
    pos_to_text(&x->pos, sfen, sizeof sfen);
    static char cmd[20000];
    game_engine_position(x, cmd, sizeof cmd);
    FILE *f = fopen("/dev/null", "w");
    record_write(x, f);
    record_write_alt(x, f);
    fclose(f);
}

int main(int argc, char **argv)
{
    long iters = argc > 1 ? atol(argv[1]) : 100000;
    if (argc > 2) rs ^= strtoull(argv[2], NULL, 10) * 0x9E3779B97F4A7C15ULL;
    rules_init();
    static char seed[2][200000], work[400000];
    static Move moves[MAX_MOVES];
    for (long it = 0; it < iters; it++) {
        int variant = rnd() % V_COUNT;
        rules_set_variant(variant);
        game_new(&g, variant == V_STANDARD ? rnd() % HANDICAP_COUNT : HANDICAP_NONE);
        snprintf(g.names[0], 64, "A"); snprintf(g.names[1], 64, "B");
        int len = rnd() % 120;
        for (int i = 0; i < len && g.result == RESULT_NONE; i++) {
            int n = pos_legal_moves(&g.pos, moves);
            if (!n) break;
            Move m = moves[rnd() % n];
            game_play(&g, &m, rnd() % 100000);
            game_judge(&g);
        }
        for (int alt = 0; alt < 2; alt++) {
            rules_set_variant(variant);
            FILE *f = fmemopen(seed[alt], sizeof seed[alt] - 1, "w");
            alt ? record_write_alt(&g, f) : record_write(&g, f);
            long n = ftell(f);
            fclose(f);
            seed[alt][n] = 0;
            /* Round trip first. */
            rules_set_variant(variant);
            bool ok = alt ? game_read_csa_text(&r, seed[alt]) : record_read_text(&r, seed[alt]);
            if (!ok || r.count != g.count || memcmp(r.pos.board, g.pos.board, sizeof g.pos.board)) {
                printf("ROUNDTRIP %s variant %d handicap %d: ok %d count %d/%d\n", alt ? "csa" : "kif", variant, g.handicap, ok, r.count, g.count);
                static int shown; if (shown++ < 3) printf("%s\n", seed[alt]);
            }
            for (int k = 0; k < 8; k++) {
                memcpy(work, seed[alt], (size_t) n + 1);
                mutate(work, (size_t) n, sizeof work - 1);
                rules_set_variant(rnd() % 4 ? variant : (int) (rnd() % V_COUNT));
                if (alt ? game_read_csa_text(&r, work) : record_read_text(&r, work))
                    exercise(&r);
            }
        }
        /* SFEN, moves, JSON. */
        rules_set_variant(variant);
        char sfen[512], out[64];
        pos_to_text(&g.pos, sfen, sizeof sfen);
        for (int k = 0; k < 8; k++) {
            strcpy(work, sfen);
            mutate(work, strlen(work), 400);
            Position p;
            if (pos_from_text(&p, work)) {
                char back[512];
                pos_to_text(&p, back, sizeof back);
                int n = pos_legal_moves(&p, moves);
                for (int i = 0; i < n && i < 30; i++) {
                    char nb[256];
                    move_notation(&p, &moves[i], -1, i & 1, nb, sizeof nb);
                    csa_move(&p, &moves[i], nb);
                }
                setup_problem(&p);
                pos_can_declare(&p);
            }
            Move m;
            char mv[64];
            if (g.count) {
                Position q; game_position_at(&g, g.count - 1, &q);
                csa_move(&q, &g.moves[g.count - 1], mv);
                mutate(mv, strlen(mv), 40);
                csa_parse_move(&q, mv, &m); csa_parse_any(&q, mv, &m);
                move_to_usi(&g.moves[g.count - 1], mv);
                mutate(mv, strlen(mv), 40);
                move_from_usi(&q, mv, &m); game_move_from_engine(&q, mv, &m);
            }
            snprintf(work, 4000, "{\"type\":\"gameState\",\"moves\":\"7g7f 3c3d\",\"a\":{\"b\":{\"c\":\"x\\u00e9\\n\"},\"n\":-12.5e3,\"l\":[1,{\"q\":null}]},\"wtime\":1000,\"s\":\"%s\"}", "tail");
            mutate(work, strlen(work), 4000);
            static const char *paths[] = {"type", "moves", "a.b.c", "a.n", "a.l", "wtime", "s", "a.l.q", "", ".", "a..b"};
            for (size_t i = 0; i < sizeof paths / sizeof *paths; i++) {
                json_get(work, paths[i], out, 1 + rnd() % sizeof out);
                json_long(work, paths[i], 0);
            }
        }
    }
    puts("done");
    return 0;
}
