/* The LAN's encryption (netcrypt.c): the key from both sides and the password, lines that come
 * back as they went, and lines changed, said again, out of order or under another key refused. */
#include "netcrypt.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* A host and a joiner that exchanged their KEY lines, with a room password each side believes. */
static void pair(NetCrypt *host, NetCrypt *joiner, const char *host_password, const char *joiner_password)
{
    gboolean password;
    assert(netcrypt_init(host, TRUE) && netcrypt_init(joiner, FALSE));
    char *h = netcrypt_hello(host, host_password && *host_password), *j = netcrypt_hello(joiner, TRUE);
    assert(!strncmp(h, "KEY ", 4) && strlen(j) == 4 + 64); /* a joiner never says " 1" */
    assert(netcrypt_peer(joiner, h, &password) && password == (host_password && *host_password));
    assert(netcrypt_peer(host, j, &password) && !password);
    assert(netcrypt_key(host, host_password) && netcrypt_key(joiner, joiner_password));
    g_free(h);
    g_free(j);
}

int main(void)
{
    NetCrypt host, joiner, other;

    /* Back and forth, each way with its own count. */
    pair(&host, &joiner, "secret", "secret");
    const char *lines[] = {"NAME Kindle", "DENBOKU 4", "", "CHAT ünïcödé 将棋", "+7776FU"};
    for (size_t i = 0; i < G_N_ELEMENTS(lines); i++) {
        char *s = netcrypt_seal(&joiner, lines[i]), *o = netcrypt_open(&host, s);
        assert(o && !strcmp(o, lines[i]) && !strchr(s, '\n') && !strstr(s, "NAME") && !strstr(s, "secret"));
        g_free(o);
        g_free(s);
        s = netcrypt_seal(&host, lines[i]);
        o = netcrypt_open(&joiner, s);
        assert(o && !strcmp(o, lines[i]));
        g_free(o);
        g_free(s);
    }

    /* The same line twice is two different lines on the wire. */
    char *a = netcrypt_seal(&host, "PING"), *b = netcrypt_seal(&host, "PING");
    assert(strcmp(a, b));
    /* Out of order, said again, or sent back to its sender: refused. */
    assert(!netcrypt_open(&joiner, b));
    char *o = netcrypt_open(&joiner, a);
    assert(o && !strcmp(o, "PING"));
    g_free(o);
    assert(!netcrypt_open(&joiner, a));
    assert(!netcrypt_open(&host, b));
    o = netcrypt_open(&joiner, b);
    assert(o);
    g_free(o);
    g_free(a);
    g_free(b);

    /* One character changed, cut short, not base64, or a clear line: refused, and the next real
     * line still opens. */
    a = netcrypt_seal(&host, "MOVES 3 600000 600000 0");
    char *bad = g_strdup(a);
    bad[5] = bad[5] == 'A' ? 'B' : 'A';
    assert(!netcrypt_open(&joiner, bad));
    bad[5] = a[5];
    bad[strlen(bad) - 4] = 0;
    assert(!netcrypt_open(&joiner, bad));
    assert(!netcrypt_open(&joiner, "DENIED") && !netcrypt_open(&joiner, "") && !netcrypt_open(&joiner, "a b\tc"));
    o = netcrypt_open(&joiner, a);
    assert(o && !strcmp(o, "MOVES 3 600000 600000 0"));
    g_free(o);
    g_free(bad);
    g_free(a);
    netcrypt_wipe(&host);
    netcrypt_wipe(&joiner);

    /* A wrong password, or none where the room has one: the keys differ. */
    const char *tries[][2] = {{"secret", "secreT"}, {"secret", ""}, {"", "secret"}, {"secret", NULL}};
    for (size_t i = 0; i < G_N_ELEMENTS(tries); i++) {
        pair(&host, &joiner, tries[i][0], tries[i][1]);
        a = netcrypt_seal(&joiner, "OK");
        assert(!netcrypt_open(&host, a));
        g_free(a);
    }
    /* No password on either side ("" and NULL are the same). */
    pair(&host, &joiner, "", NULL);
    a = netcrypt_seal(&joiner, "OK");
    o = netcrypt_open(&host, a);
    assert(o && !strcmp(o, "OK"));
    g_free(o);
    g_free(a);

    /* Someone in the middle with keys of its own: a line sealed for one session does not open in
     * another, even with the password. */
    pair(&host, &joiner, "secret", "secret");
    pair(&other, &joiner, "secret", "secret");
    a = netcrypt_seal(&joiner, "OK");
    assert(!netcrypt_open(&host, a));
    o = netcrypt_open(&other, a);
    assert(o);
    g_free(o);
    g_free(a);

    /* KEY lines that are not one, and a key that gives no secret (the point of order 1). */
    gboolean password;
    netcrypt_init(&joiner, FALSE);
    assert(!netcrypt_peer(&joiner, "NAME host", &password) && !netcrypt_peer(&joiner, "KEY 12", &password));
    assert(!netcrypt_peer(&joiner, "KEY 0000000000000000000000000000000000000000000000000000000000000000 2", &password));
    assert(!netcrypt_peer(&joiner, "KEY 000000000000000000000000000000000000000000000000000000000000000g", &password));
    assert(netcrypt_peer(&joiner, "KEY 0000000000000000000000000000000000000000000000000000000000000000", &password));
    assert(!netcrypt_key(&joiner, NULL) && !joiner.keyed);
    a = netcrypt_seal(&host, "x");
    assert(!netcrypt_open(&joiner, a));
    g_free(a);

    printf("netcrypt tests passed\n");
    return 0;
}
