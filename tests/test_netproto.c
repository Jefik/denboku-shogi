/* The lines of a network game as read (netproto.c): the numbers within their limits and the
 * texts whole, whatever a hostile peer sends. No GTK. */
#include "netproto.h"

#include <assert.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>

static const NetLimits LIMITS = {12, 6, 5};
static NetLine l;

static NetLineKind kind(const char *line)
{
    net_parse(line, &LIMITS, &l);
    return l.kind;
}

int main(void)
{
    /* A room's announcement, and what a hostile one is read as. */
    NetRoom room = {"Host", 1, 3, 2, 600, 30, 5, 0, 4}, back;
    char *text = net_room_text(&room);
    assert(net_room_parse(text, &LIMITS, &back) && !memcmp(&room, &back, sizeof room));
    g_free(text);
    assert(!net_room_parse("Old host — Standard · No clock", &LIMITS, &back) && !strcmp(back.name, "Old host — Standard · No clock"));
    assert(net_room_parse("H\t7\t99\t-4\t99999999999\t1\t1\t3\t1000", &LIMITS, &back) && back.password == 1 &&
           back.variant == 5 && back.clock == 0 && back.main_s == NET_MAX_MS / 1000 && back.setup == 1 && back.handicap == 12);

    /* The words, alone on the line. */
    assert(kind("PASSWORD") == NL_MOVE && kind("DENIED") == NL_DENIED && kind("KICKED") == NL_KICKED);
    assert(kind("BANNED") == NL_BANNED && kind("BYE") == NL_BYE && kind("PING") == NL_PING);
    assert(kind("REMATCH") == NL_REMATCH && kind("%TORYO") == NL_RESIGN && kind("TAKEBACK") == NL_TAKEBACK);
    assert(kind("%TIME_UP") == NL_TIME_UP);
    assert(kind("BYE now") == NL_MOVE && kind("PASSWORDS") == NL_MOVE && kind("") == NL_MOVE);
    assert(kind("+7776FU") == NL_MOVE && !strcmp(l.text, "+7776FU"));

    assert(kind("DENBOKU 2") == NL_VERSION && l.n[0] == 2);
    assert(kind("DENBOKU -5") == NL_VERSION && l.n[0] == 1);
    assert(kind("DENBOKU 99999999999999999999") == NL_VERSION && l.n[0] >= 1 && l.n[0] <= 99);
    assert(kind("DENBOKU x") == NL_MOVE);

    /* The settings: within what the game has. */
    assert(kind("GAME 3 2 1 4") == NL_GAME && l.n[0] == 3 && l.n[1] == 2 && l.n[2] == 1 && l.n[3] == 4);
    assert(kind("GAME 3 2 0") == NL_MOVE);
    assert(kind("GAME 999 -7 5 2147483647") == NL_GAME && l.n[0] == 12 && l.n[1] == 0 && l.n[2] == 1 && l.n[3] == 5);
    assert(kind("GAME -2147483648 2147483647 -1 -1") == NL_GAME && l.n[0] == 0 && l.n[1] == 6 && l.n[2] == 1 && l.n[3] == 0);
    assert(kind("GAME 1 2") == NL_MOVE);
    assert(kind("CLOCK 600 30 5") == NL_CLOCK && l.n[0] == 600 && l.n[1] == 30 && l.n[2] == 5);
    assert(kind("CLOCK 2147483647 -1 999999") == NL_CLOCK && l.n[0] == 180 * 60 && l.n[1] == 0 && l.n[2] == 60);
    assert(kind("CLOCK -1 99999 -3") == NL_CLOCK && l.n[0] == 0 && l.n[1] == 300 && l.n[2] == 0);
    assert(kind("CLOCK 1 2") == NL_MOVE);
    assert(kind("VARIANT 4") == NL_VARIANT && l.n[0] == 4);
    assert(kind("VARIANT 77") == NL_VARIANT && l.n[0] == 5);
    assert(kind("VARIANT -1") == NL_VARIANT && l.n[0] == 0);
    assert(kind("SFEN") == NL_SFEN && !l.text);
    assert(kind("SFEN ") == NL_SFEN && !l.text);
    assert(kind("SFEN 4k4/9 b - 1") == NL_SFEN && !strcmp(l.text, "4k4/9 b - 1"));
    assert(kind("SFENX") == NL_MOVE);
    assert(kind("READY 1") == NL_READY && l.n[0] == 1);
    assert(kind("READY 0") == NL_READY && l.n[0] == 0);
    assert(kind("READY 77") == NL_READY && l.n[0] == 1);
    assert(kind("TAKEBACK 1") == NL_TAKEBACK_ANSWER && l.n[0] == 1);
    assert(kind("TAKEBACK no") == NL_TAKEBACK_ANSWER && l.n[0] == 0);

    /* The game: the clocks within two days, a turn never begun in the future. */
    assert(kind("MOVES 600000 590000 1200 +7776FU -3334FU") == NL_MOVES && l.n[0] == 5);
    assert(l.ms[0] == 600000 && l.ms[1] == 590000 && l.ms[2] == 1200 && !strcmp(l.text, "600000 590000 1200 +7776FU -3334FU"));
    assert(kind("MOVES 99999999999999999999 -99999999999999999999 -5") == NL_MOVES && l.n[0] == 3);
    assert(l.ms[0] == NET_MAX_MS && l.ms[1] == -NET_MAX_MS && l.ms[2] == 0);
    assert(kind("MOVES   1  2 ") == NL_MOVES && l.n[0] == 2 && l.ms[0] == 1 && l.ms[1] == 2 && l.ms[2] == 0);
    assert(kind("MOVES ") == NL_MOVES && l.n[0] == 0);
    assert(kind("MOVES x y z") == NL_MOVES && l.n[0] == 3 && !l.ms[0] && !l.ms[1] && !l.ms[2]);

    assert(kind("%ILLEGAL +7775FU") == NL_ILLEGAL && !strcmp(l.text, "+7775FU"));
    assert(kind("%ILLEGAL") == NL_ILLEGAL && !l.text);

    /* Texts: no longer than the limit, cut at a whole character and at what is not text. */
    assert(kind("NAME Akira") == NL_NAME && !strcmp(l.text, "Akira"));
    char line[70000] = "NAME ";
    for (int i = 0; i < 30; i++)
        strcat(line, "駒"); /* 3 bytes each: 40 bytes end inside the 14th */
    assert(kind(line) == NL_NAME && strlen(l.text) == 39 && g_utf8_validate(l.text, -1, NULL));
    assert(kind("NAME ok\xff\xfe" "bad") == NL_NAME && !strcmp(l.text, "ok"));
    strcpy(line, "CHAT ");
    memset(line + 5, 'x', sizeof line - 6);
    line[sizeof line - 1] = 0;
    assert(kind(line) == NL_CHAT && strlen(l.text) == NET_CHAT_BYTES);
    memcpy(line + 5 + NET_CHAT_BYTES - 1, "é", 2); /* its second byte is past the limit */
    assert(kind(line) == NL_CHAT && strlen(l.text) == NET_CHAT_BYTES - 1);
    assert(kind("CHAT ") == NL_CHAT && !*l.text);
    assert(kind("CHAT hi\x80there") == NL_CHAT && !strcmp(l.text, "hi"));

    /* Any bytes at all: a kind, and a text that ends. */
    GRand *r = g_rand_new_with_seed(1);
    static const char *const STARTS[] = {"", "GAME ", "CLOCK ", "VARIANT ", "MOVES ", "NAME ", "CHAT ", "PASS ", "SFEN ",
                                         "DENBOKU ", "READY ", "TAKEBACK ", "%ILLEGAL "};
    for (int i = 0; i < 20000; i++) {
        char *p = line + sprintf(line, "%s", STARTS[i % G_N_ELEMENTS(STARTS)]);
        int len = g_rand_int_range(r, 0, 3000);
        for (int j = 0; j < len; j++) {
            int ch = g_rand_int_range(r, 0, 4) ? "0123456789 -+"[g_rand_int_range(r, 0, 13)] : g_rand_int_range(r, 1, 256);
            *p++ = (char) (ch == '\n' ? ' ' : ch);
        }
        *p = 0;
        kind(line);
        assert(l.kind <= NL_ILLEGAL && (!l.text || strlen(l.text) <= sizeof line));
        assert(l.n[1] >= 0 && l.ms[2] >= 0);
    }
    g_rand_free(r);
    puts("netproto tests passed");
    return 0;
}
