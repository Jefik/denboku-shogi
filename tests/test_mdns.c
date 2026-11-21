/* The mDNS announcement (mdns.c): made, read back, and read from damaged bytes. No sockets. */
#include "mdns.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    unsigned char pkt[512];
    char name[MDNS_NAME_BYTES + 1];
    int port;

    int n = mdns_announce(pkt, sizeof pkt, 0xbeef, 4081, "Jefik (password) — Shogi · 10 min");
    assert(n > 0 && mdns_parse(pkt, n, &port, name) == 1 && port == 4081);
    assert(!strcmp(name, "Jefik (password) — Shogi · 10 min"));

    /* A name that is too long is cut between characters. */
    char big[600];
    memset(big, 'a', sizeof big), big[599] = 0;
    memcpy(big + 199, "é", 2); /* straddles the limit */
    n = mdns_announce(pkt, sizeof pkt, 1, 5000, big);
    assert(n > 0 && mdns_parse(pkt, n, &port, name) == 1 && port == 5000);
    assert(strlen(name) == 199 && name[198] == 'a');

    assert(mdns_announce(pkt, 20, 1, 4081, "x") == 0);

    /* Not an announcement: a query, a short packet, another service. */
    n = mdns_announce(pkt, sizeof pkt, 1, 4081, "x");
    unsigned char copy[512];
    memcpy(copy, pkt, n), copy[2] = 0;
    assert(!mdns_parse(copy, n, &port, name));
    assert(!mdns_parse(pkt, 5, &port, name));
    memcpy(copy, pkt, n);
    for (int i = 0; i + 8 <= n; i++)
        if (!memcmp(copy + i, "_denboku", 8))
            copy[i + 6] = 'X';
    assert(!mdns_parse(copy, n, &port, name));

    /* Damage: every prefix, and random bytes, never read past the end (ASan watches). */
    for (int cut = 0; cut <= n; cut++) {
        unsigned char *exact = malloc(cut ? cut : 1);
        memcpy(exact, pkt, cut);
        mdns_parse(exact, cut, &port, name);
        free(exact);
    }
    srand(7);
    for (int i = 0; i < 20000; i++) {
        memcpy(copy, pkt, n);
        for (int k = 0; k < 1 + rand() % 4; k++)
            copy[rand() % n] = (unsigned char) rand();
        unsigned char *exact = malloc(n);
        memcpy(exact, copy, n);
        mdns_parse(exact, n, &port, name);
        free(exact);
    }
    puts("mdns tests passed");
    return 0;
}
