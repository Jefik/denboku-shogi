/* The host's door (net.c): every line sealed, and a newcomer takes the player's place only once
 * it shows the session key, which holds the room password. Sockets on the loopback, no GTK. */
#include "net.h"
#include "netcrypt.h"

#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 47081

static int connected, joiner_connected, host_closed, joiner_closed, denied;
static const char *joiner_why, *joiner_password;
static GString *host_lines, *joiner_lines;
static Net *joiner;

static void host_connected(void *data) { (void) data; connected++; }
static void host_line(const char *line, void *data) { (void) data; g_string_append_printf(host_lines, "%s|", line); }
static void host_closed_cb(const char *why, void *data) { (void) why, (void) data; host_closed++; }
static void host_denied(const char *address, void *data) { (void) address, (void) data; denied++; }
static gboolean banning; /* the host's ban list has 127.0.0.1 */
static gboolean host_accept(const char *address, void *data) { (void) data; return !banning || strcmp(address, "127.0.0.1"); }
static void joiner_connected_cb(void *data) { (void) data; joiner_connected++; }
static void joiner_line(const char *line, void *data) { (void) data; g_string_append_printf(joiner_lines, "%s|", line); }
static void joiner_closed_cb(const char *why, void *data) { (void) data; joiner_closed++; joiner_why = why; }
static void joiner_asked(void *data) { (void) data; net_password(joiner, joiner_password); }

static void spin(int ms)
{
    gint64 end = g_get_monotonic_time() + ms * 1000;
    while (g_get_monotonic_time() < end) {
        while (g_main_context_iteration(NULL, FALSE))
            ;
        g_usleep(1000);
    }
}

/* Someone at the door by hand: a raw socket from 127.0.0.<from>, and the keys it makes. */
typedef struct {
    int fd;
    NetCrypt crypt;
} Peer;

static void say_raw(int fd, const char *text)
{
    if (send(fd, text, strlen(text), MSG_NOSIGNAL) >= 0) /* the host may have closed it already */
        spin(100);
}

/* What `fd` was told; *gone when the other side closed. */
static const char *heard_raw(int fd, gboolean *gone)
{
    static char buf[8192];
    size_t len = 0;
    ssize_t got;
    *gone = FALSE;
    while ((got = read(fd, buf + len, sizeof buf - 1 - len)) > 0)
        len += (size_t) got;
    if (!got)
        *gone = TRUE;
    buf[len] = 0;
    return buf;
}

static int stranger_fd(int from)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in to = {.sin_family = AF_INET, .sin_port = htons(PORT)}, own = {.sin_family = AF_INET};
    inet_pton(AF_INET, "127.0.0.1", &to.sin_addr);
    own.sin_addr.s_addr = htonl(0x7f000000u + (unsigned) from);
    assert(!bind(fd, (struct sockaddr *) &own, sizeof own));
    assert(!connect(fd, (struct sockaddr *) &to, sizeof to));
    fcntl(fd, F_SETFL, O_NONBLOCK);
    spin(100);
    return fd;
}

/* Connects and trades KEY lines; the host's says the room has a password. */
static Peer peer(int from)
{
    Peer p = {stranger_fd(from), {0}};
    gboolean gone, password;
    char *line = g_strdup(heard_raw(p.fd, &gone));
    g_strchomp(line);
    assert(!gone && netcrypt_init(&p.crypt, FALSE) && netcrypt_peer(&p.crypt, line, &password) && password);
    g_free(line);
    char *hello = netcrypt_hello(&p.crypt, FALSE), *text = g_strconcat(hello, "\n", NULL);
    say_raw(p.fd, text);
    g_free(hello);
    g_free(text);
    return p;
}

/* Lines sealed, each followed by a line end. */
static void say(Peer *p, const char *const *lines, int count)
{
    GString *s = g_string_new(NULL);
    for (int i = 0; i < count; i++) {
        char *sealed = netcrypt_seal(&p->crypt, lines[i]);
        g_string_append_printf(s, "%s\n", sealed);
        g_free(sealed);
    }
    say_raw(p->fd, s->str);
    g_string_free(s, TRUE);
}

/* The key from `password`, shown to the host: the proof, then `lines`. */
static void enter(Peer *p, const char *password, const char *const *lines, int count)
{
    assert(netcrypt_key(&p->crypt, password));
    const char *proof = "OK";
    say(p, &proof, 1);
    if (count)
        say(p, lines, count);
}

/* What the host said, opened (or as it came, for a line in clear). */
static char *heard(Peer *p, gboolean *gone)
{
    GString *out = g_string_new(NULL);
    char **lines = g_strsplit(heard_raw(p->fd, gone), "\n", -1);
    for (char **l = lines; *l && **l; l++) {
        char *open = p->crypt.keyed ? netcrypt_open(&p->crypt, *l) : NULL;
        g_string_append_printf(out, "%s|", open ? open : *l);
        g_free(open);
    }
    g_strfreev(lines);
    return g_string_free(out, FALSE);
}

static void gone_now(Peer *p)
{
    gboolean gone;
    heard_raw(p->fd, &gone);
    assert(gone);
}

static void still_there(Peer *p)
{
    gboolean gone;
    heard_raw(p->fd, &gone);
    assert(!gone);
}

int main(void)
{
    host_lines = g_string_new(NULL);
    joiner_lines = g_string_new(NULL);
    const NetCallbacks host_cb = {host_connected, host_line, host_closed_cb, host_accept, host_denied, NULL};
    const NetCallbacks joiner_cb = {joiner_connected_cb, joiner_line, joiner_closed_cb, NULL, NULL, joiner_asked};
    GError *err = NULL;
    Net *host = net_host(PORT, "secret", &host_cb, NULL, &err);
    if (!host) {
        fprintf(stderr, "net tests skipped: %s\n", err->message);
        g_error_free(err);
        g_string_free(host_lines, TRUE);
        g_string_free(joiner_lines, TRUE);
        return 0;
    }
    /* The player comes in with the password, and both ways talk. */
    joiner_password = "secret";
    joiner = net_join("127.0.0.1", PORT, &joiner_cb, NULL);
    spin(300);
    assert(connected == 1 && joiner_connected == 1);
    net_send(joiner, "NAME player");
    net_send(host, "NAME host");
    spin(100);
    assert(!strcmp(host_lines->str, "NAME player|") && !strcmp(joiner_lines->str, "NAME host|"));
    g_string_truncate(host_lines, 0);
    g_string_truncate(joiner_lines, 0);
    gboolean gone;

    /* A wrong password: refused, the player undisturbed. */
    Peer p = peer(1);
    const char *name = "NAME stranger";
    enter(&p, "guess", &name, 1);
    char *said = heard(&p, &gone);
    assert(!strcmp(said, "DENIED|") && gone);
    g_free(said);
    close(p.fd);
    assert(denied == 1 && connected == 1 && !host_closed && !joiner_closed && !joiner_lines->len && !host_lines->len);

    /* An older version (lines in clear), no line end, and lines that are not sealed: dropped. */
    int fd = stranger_fd(1);
    say_raw(fd, "NAME old\nDENBOKU 3\nPASS secret\n");
    heard_raw(fd, &gone);
    assert(gone);
    close(fd);
    fd = stranger_fd(1);
    char flood[6000];
    memset(flood, 'x', sizeof flood - 1);
    flood[sizeof flood - 1] = 0;
    say_raw(fd, flood);
    heard_raw(fd, &gone);
    assert(gone);
    close(fd);
    p = peer(1);
    say_raw(p.fd, "OK\n"); /* to the door, the same as a wrong password */
    said = heard(&p, &gone);
    assert(!strcmp(said, "DENIED|") && gone);
    g_free(said);
    close(p.fd);
    assert(connected == 1 && denied == 2 && !joiner_closed && !joiner_lines->len && !host_lines->len);

    /* A second newcomer from the same address takes the first one's place at the door. */
    Peer first = peer(1), second = peer(1);
    gone_now(&first);
    close(first.fd);
    close(second.fd);
    spin(100);
    assert(connected == 1 && !joiner_closed);

    /* One from another address keeps its place while a stranger connects again and again, until
     * the door is full: then the oldest goes. */
    Peer back = peer(2);
    for (int i = 0; i < 6; i++) {
        Peer s = peer(1);
        close(s.fd);
    }
    Peer others[3];
    for (int i = 0; i < 3; i++)
        others[i] = peer(3 + i);
    still_there(&back);
    Peer last = peer(6);
    gone_now(&back);
    still_there(&others[0]);
    close(back.fd);
    close(last.fd);
    for (int i = 0; i < 3; i++)
        close(others[i].fd);
    spin(100);
    assert(connected == 1 && !joiner_closed && !joiner_lines->len && !host_lines->len);

    /* The player still plays. */
    net_send(joiner, "+7776FU");
    spin(100);
    assert(!strcmp(host_lines->str, "+7776FU|"));
    g_string_truncate(host_lines, 0);

    /* The right password: the newcomer takes the place, with what it said along with the proof,
     * a stranger having come and gone meanwhile. */
    p = peer(2);
    for (int i = 0; i < 3; i++) {
        Peer s = peer(1);
        close(s.fd);
    }
    Peer idle_peer = peer(1);
    const char *lines[] = {"NAME back", "DENBOKU 4", "READY 1"};
    enter(&p, "secret", lines, 3);
    assert(connected == 2 && denied == 2);
    assert(!strcmp(joiner_lines->str, "BYE|"));
    g_string_truncate(joiner_lines, 0);
    const char *ping = "PING";
    say(&p, &ping, 1);
    assert(!strcmp(host_lines->str, "NAME back|DENBOKU 4|READY 1|PING|"));
    net_send(host, "HELLO");
    said = heard(&p, &gone);
    assert(!strcmp(said, "HELLO|") && !gone);
    g_free(said);
    g_string_truncate(host_lines, 0);

    /* A line changed on the way ends the connection. */
    char *sealed = netcrypt_seal(&p.crypt, "+2726FU");
    sealed[3] = sealed[3] == 'A' ? 'B' : 'A';
    char *text = g_strconcat(sealed, "\n", NULL);
    say_raw(p.fd, text);
    g_free(sealed);
    g_free(text);
    assert(host_closed == 1 && !host_lines->len);
    close(p.fd);
    close(idle_peer.fd);
    net_free(joiner);

    /* A joiner with the wrong password is told so; one without the password does not get in. */
    net_drop(host);
    joiner_password = "Secret";
    joiner = net_join("127.0.0.1", PORT, &joiner_cb, NULL);
    spin(300);
    assert(!strcmp(joiner_lines->str, "DENIED|") && denied == 3 && connected == 2);
    net_free(joiner);
    g_string_truncate(joiner_lines, 0);

    /* No password: anyone gets in. */
    net_set_password(host, NULL);
    joiner = net_join("127.0.0.1", PORT, &joiner_cb, NULL);
    spin(300);
    assert(connected == 3 && joiner_connected == 3);
    net_free(joiner);

    /* Banned: coming back, the player is told so and does not get in; let in again once unbanned. */
    banning = TRUE;
    g_string_truncate(joiner_lines, 0);
    joiner = net_join("127.0.0.1", PORT, &joiner_cb, NULL);
    spin(300);
    assert(!strcmp(joiner_lines->str, "BANNED|") && connected == 3 && joiner_connected == 3);
    net_free(joiner);
    banning = FALSE;
    g_string_truncate(joiner_lines, 0);
    joiner = net_join("127.0.0.1", PORT, &joiner_cb, NULL);
    spin(300);
    assert(connected == 4 && joiner_connected == 4);
    net_free(joiner);
    net_free(host);
    spin(100);

    /* A host of an older version, which talks in clear: the joiner gives up and says why. */
    int listener = socket(AF_INET, SOCK_STREAM, 0), yes = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    struct sockaddr_in at = {.sin_family = AF_INET, .sin_port = htons(PORT)};
    inet_pton(AF_INET, "127.0.0.1", &at.sin_addr);
    assert(!bind(listener, (struct sockaddr *) &at, sizeof at) && !listen(listener, 1));
    joiner_closed = 0;
    joiner = net_join("127.0.0.1", PORT, &joiner_cb, NULL);
    spin(100);
    fd = accept(listener, NULL, NULL);
    say_raw(fd, "NAME old host\nDENBOKU 3\n");
    spin(100);
    assert(joiner_closed == 1 && joiner_why == NET_TOO_OLD && joiner_connected == 4);
    net_free(joiner);
    close(fd);

    /* In clear, as for a CSA server. */
    g_string_truncate(joiner_lines, 0);
    joiner = net_join_plain("127.0.0.1", PORT, &joiner_cb, NULL);
    spin(100);
    fd = accept(listener, NULL, NULL);
    fcntl(fd, F_SETFL, O_NONBLOCK);
    assert(joiner_connected == 5);
    net_send(joiner, "LOGIN me pw");
    spin(100);
    assert(!strcmp(heard_raw(fd, &gone), "LOGIN me pw\n"));
    say_raw(fd, "LOGIN:me OK\n");
    assert(!strcmp(joiner_lines->str, "LOGIN:me OK|"));
    net_free(joiner);
    close(fd);
    close(listener);
    puts("net tests passed");
    return 0;
}
