#include "net.h"
#include "mdns.h"
#include "netcrypt.h"

#include <gio/gio.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#ifdef G_OS_WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#define close_socket closesocket
#define socket_channel g_io_channel_win32_new_socket
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define close_socket close
#define socket_channel g_io_channel_unix_new
#endif

const char NET_TOO_OLD[] = "the other side has an older version";

/* Joiner: the host's KEY is awaited, then (a room with a password) the player's answer. */
typedef enum { STAGE_KEY, STAGE_PASSWORD, STAGE_KEYED } Stage;

struct Net {
    NetCallbacks cb;
    void *data;
    GSocketService *service;
    GSocketClient *client;
    GSocketConnection *conn;
    NetCrypt crypt; /* the current connection's */
    Stage stage;
    gboolean plain; /* a CSA server: lines in clear */
    char *password; /* host: the room's, "" for none */
    GString *buf;  /* what was read and is not yet a whole line */
    GCancellable *cancel;
    gboolean closed;
    int busy;      /* inside the line callbacks, which may free the connection */
    gboolean freed;
    GPtrArray *waiters; /* host: newcomers kept apart from the player (Waiter), the oldest first */
};

/* The longest line taken: MOVES with a whole game is some 8 KiB, sealed some 11. A longer one
 * closes the connection, so that a stranger cannot fill the memory by never ending a line. */
#define LINE_MAX_BYTES 65536

/* A newcomer at the door: it takes the player's place once its first sealed line shows that it
 * has the session key, and so the room password. */
typedef struct {
    Net *n; /* NULL once let go with a read still to come back, which then frees it */
    GSocketConnection *conn;
    NetCrypt crypt;
    char *address;
    GString *buf;
    GCancellable *cancel;
    guint timer;
    gboolean reading;
} Waiter;

typedef struct {
    Net *n;
    Waiter *w;
    GCancellable *cancel; /* the connection's read: a read done just before it was cancelled is dropped */
    char data[4096];
} Chunk;

/* What a newcomer may send before it is let in, and how long it has (a password typed by hand).
 * One waits per address, so that a stranger who keeps connecting does not take the place at the
 * door of the player coming back from another one; WAIT_SLOTS of them at most. */
#define WAIT_MAX_BYTES 4096
#define WAIT_SECONDS 90
#define WAIT_SLOTS 4

/* The joiner's first sealed line, which the host's door takes as the proof of the key. */
#define PROOF "OK"

void net_drop(Net *n);

static void closed(Net *n, const char *why)
{
    if (n->closed)
        return;
    n->closed = TRUE;
    if (n->cb.closed)
        n->cb.closed(why, n->data);
}

static void tell(GSocketConnection *conn, const char *line)
{
    char *text = g_strconcat(line, "\n", NULL);
    g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(conn)), text, strlen(text), NULL, NULL, NULL);
    g_free(text);
}

/* What the host may say in clear, for a joiner it does not let in. */
static gboolean refusal(const char *line) { return !strcmp(line, "BANNED") || !strcmp(line, "DENIED"); }

/* Joiner: the key is made; the host is shown it, and the game may talk. */
static void keyed(Net *n, const char *password)
{
    if (!netcrypt_key(&n->crypt, password)) {
        closed(n, "a bad key");
        return;
    }
    n->stage = STAGE_KEYED;
    char *proof = netcrypt_seal(&n->crypt, PROOF);
    tell(n->conn, proof);
    g_free(proof);
    if (n->cb.connected)
        n->cb.connected(n->data);
}

void net_password(Net *n, const char *password)
{
    if (n && n->conn && !n->closed && n->stage == STAGE_PASSWORD)
        keyed(n, password);
}

/* A whole line read, to the line callback once opened. */
static void line_in(Net *n, const char *raw)
{
    if (n->plain) {
        if (n->cb.line)
            n->cb.line(raw, n->data);
        return;
    }
    if (n->stage == STAGE_KEY) {
        gboolean password;
        if (netcrypt_peer(&n->crypt, raw, &password)) {
            n->stage = STAGE_PASSWORD;
            if (!password)
                keyed(n, NULL);
            else if (n->cb.password)
                n->cb.password(n->data);
        } else if (refusal(raw) && n->cb.line) {
            n->cb.line(raw, n->data);
        } else {
            closed(n, NET_TOO_OLD);
        }
        return;
    }
    char *line = n->stage == STAGE_KEYED ? netcrypt_open(&n->crypt, raw) : NULL;
    if (!line && !n->service && refusal(raw) && n->cb.line)
        n->cb.line(raw, n->data); /* a wrong password, or banned meanwhile */
    else if (!line && n->stage == STAGE_KEYED)
        closed(n, "a damaged line");
    else if (line && n->cb.line)
        n->cb.line(line, n->data);
    g_free(line);
}

/* The whole lines read so far, to the line callback (inside busy++ and idle()). */
static void lines_out(Net *n)
{
    char *nl;
    while (!n->freed && !n->closed && n->conn && (nl = memchr(n->buf->str, '\n', n->buf->len))) {
        char *line = g_strndup(n->buf->str, (gsize) (nl - n->buf->str));
        g_string_erase(n->buf, 0, nl - n->buf->str + 1);
        g_strchomp(line);
        line_in(n, line);
        g_free(line);
    }
}

/* Ends busy++: a callback may have freed the connection meanwhile. */
static void idle(Net *n)
{
    if (!--n->busy && n->freed) {
        g_string_free(n->buf, TRUE);
        g_free(n);
    }
}

static void read_cb(GObject *src, GAsyncResult *res, gpointer user);

static void read_more(Net *n)
{
    Chunk *c = g_new(Chunk, 1);
    c->n = n;
    c->cancel = g_object_ref(n->cancel);
    g_input_stream_read_async(g_io_stream_get_input_stream(G_IO_STREAM(n->conn)), c->data, sizeof c->data,
                              G_PRIORITY_DEFAULT, n->cancel, read_cb, c);
}

static void read_cb(GObject *src, GAsyncResult *res, gpointer user)
{
    Chunk *c = user;
    Net *n = c->n;
    GError *err = NULL;
    gssize got = g_input_stream_read_finish(G_INPUT_STREAM(src), res, &err);
    /* Cancelled: the connection was dropped or freed, and `n` may be gone. The Kindle's GLib can
     * still hand over a read that finished just before, without the error. */
    gboolean cancelled = g_cancellable_is_cancelled(c->cancel);
    g_object_unref(c->cancel);
    if (cancelled || got <= 0) {
        if (!cancelled && !g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            closed(n, err ? err->message : "connection closed");
        g_clear_error(&err);
        g_free(c);
        return;
    }
    g_string_append_len(n->buf, c->data, got);
    g_free(c);
    if (n->buf->len > LINE_MAX_BYTES && !memchr(n->buf->str, '\n', n->buf->len)) {
        g_string_truncate(n->buf, 0);
        closed(n, "line too long");
        return;
    }
    /* Ask for more first: a line's callback may run a dialog for a long time. */
    read_more(n);
    n->busy++;
    lines_out(n);
    idle(n);
}

/* "192.168.0.5" for an IPv4 connection; the same text the ban list keeps. */
static char *connection_address(GSocketConnection *conn)
{
    GSocketAddress *sa = g_socket_connection_get_remote_address(conn, NULL);
    char *out = NULL;
    if (sa && G_IS_INET_SOCKET_ADDRESS(sa)) {
        GInetAddress *ia = g_inet_socket_address_get_address(G_INET_SOCKET_ADDRESS(sa));
        out = g_inet_address_to_string(ia);
    }
    if (sa)
        g_object_unref(sa);
    return out;
}

char *net_peer_address(Net *n)
{
    if (!n || !n->conn)
        return NULL;
    return connection_address(n->conn);
}

static void waiter_free(Waiter *w)
{
    netcrypt_wipe(&w->crypt);
    g_object_unref(w->cancel);
    g_object_unref(w->conn);
    g_free(w->address);
    g_string_free(w->buf, TRUE);
    g_free(w);
}

/* The newcomer goes away, or (`keep`) stays open to become the connection. */
static void wait_end(Waiter *w, gboolean keep)
{
    g_ptr_array_remove(w->n->waiters, w);
    w->n = NULL;
    if (w->timer)
        g_source_remove(w->timer);
    w->timer = 0;
    if (!keep) {
        g_cancellable_cancel(w->cancel);
        g_io_stream_close(G_IO_STREAM(w->conn), NULL, NULL);
    }
    if (!w->reading)
        waiter_free(w);
}

static gboolean wait_timeout_cb(gpointer user)
{
    Waiter *w = user;
    w->timer = 0;
    wait_end(w, FALSE);
    return FALSE;
}

/* The newcomer `w`, who showed the key, takes the place of the current connection, which is told
 * to stop coming back (it may still be running). */
static void admit(Net *n, Waiter *w)
{
    GSocketConnection *conn = g_object_ref(w->conn);
    NetCrypt crypt = w->crypt;
    GString *rest = g_string_new_len(w->buf->str, w->buf->len); /* its first lines, sealed */
    wait_end(w, TRUE);
    n->busy++;
    if (n->conn) {
        net_send(n, "BYE");
        net_drop(n);
    }
    n->conn = conn;
    n->crypt = crypt;
    n->stage = STAGE_KEYED;
    g_string_assign(n->buf, rest->str);
    g_string_free(rest, TRUE);
    if (n->cb.connected)
        n->cb.connected(n->data);
    if (!n->freed && n->conn) { /* not dropped by the callback */
        read_more(n);
        lines_out(n);
    }
    netcrypt_wipe(&crypt);
    idle(n);
}

static void wait_read_cb(GObject *src, GAsyncResult *res, gpointer user);

static void wait_read(Waiter *w)
{
    Chunk *c = g_new0(Chunk, 1);
    c->w = w;
    w->reading = TRUE;
    g_input_stream_read_async(g_io_stream_get_input_stream(G_IO_STREAM(w->conn)), c->data, sizeof c->data,
                              G_PRIORITY_DEFAULT, w->cancel, wait_read_cb, c);
}

static void wait_read_cb(GObject *src, GAsyncResult *res, gpointer user)
{
    Chunk *c = user;
    Waiter *w = c->w;
    Net *n = w->n;
    gssize got = g_input_stream_read_finish(G_INPUT_STREAM(src), res, NULL);
    w->reading = FALSE;
    if (!n || got <= 0) {
        if (n)
            wait_end(w, FALSE);
        else
            waiter_free(w); /* let go meanwhile, and `n` may be gone */
        g_free(c);
        return;
    }
    g_string_append_len(w->buf, c->data, got);
    g_free(c);
    char *nl;
    while ((nl = memchr(w->buf->str, '\n', w->buf->len))) {
        char *line = g_strndup(w->buf->str, (gsize) (nl - w->buf->str));
        g_string_erase(w->buf, 0, nl - w->buf->str + 1);
        g_strchomp(line);
        if (!w->crypt.keyed) {
            gboolean unused;
            gboolean ok = netcrypt_peer(&w->crypt, line, &unused) && netcrypt_key(&w->crypt, n->password);
            g_free(line);
            if (!ok) { /* an older version, or a bad key */
                wait_end(w, FALSE);
                return;
            }
            continue;
        }
        char *proof = netcrypt_open(&w->crypt, line);
        gboolean ok = proof && !strcmp(proof, PROOF);
        g_free(proof);
        g_free(line);
        if (ok) {
            admit(n, w);
            return;
        }
        /* Not the session's key: with a password, the wrong one. */
        if (*n->password) {
            tell(w->conn, "DENIED");
            if (n->cb.denied)
                n->cb.denied(w->address, n->data);
        }
        wait_end(w, FALSE);
        return;
    }
    if (w->buf->len > WAIT_MAX_BYTES)
        wait_end(w, FALSE);
    else
        wait_read(w);
}

/* The host keeps listening: a new connection replaces the current one, since it is the other
 * player coming back (after the app restarted or the network changed) before this side noticed
 * the old connection was gone; it is kept apart until it shows the key. */
static gboolean incoming_cb(GSocketService *service, GSocketConnection *conn, GObject *source, gpointer user)
{
    Net *n = user;
    (void) service, (void) source;
    char *address = connection_address(conn);
    if (!address)
        address = g_strdup("");
    if (n->cb.accept && !n->cb.accept(address, n->data)) {
        /* Do not let a banned player push out the one who is playing. */
        tell(conn, "BANNED");
        g_io_stream_close(G_IO_STREAM(conn), NULL, NULL);
        g_free(address);
        return TRUE;
    }
    Waiter *w = g_new0(Waiter, 1);
    if (!netcrypt_init(&w->crypt, TRUE)) {
        g_io_stream_close(G_IO_STREAM(conn), NULL, NULL);
        g_free(address);
        g_free(w);
        return TRUE;
    }
    /* It takes the place of the one waiting from its own address (the same player trying again)
     * or, the door full, of the oldest. */
    for (guint i = n->waiters->len; i-- > 0;) {
        Waiter *old = n->waiters->pdata[i];
        if (!strcmp(old->address, address))
            wait_end(old, FALSE);
    }
    if (n->waiters->len >= WAIT_SLOTS)
        wait_end(n->waiters->pdata[0], FALSE);
    w->n = n;
    w->conn = g_object_ref(conn);
    w->address = address;
    w->buf = g_string_new(NULL);
    w->cancel = g_cancellable_new();
    w->timer = g_timeout_add_seconds(WAIT_SECONDS, wait_timeout_cb, w);
    g_ptr_array_add(n->waiters, w);
    char *hello = netcrypt_hello(&w->crypt, *n->password);
    tell(conn, hello);
    g_free(hello);
    wait_read(w);
    return TRUE;
}

void net_drop(Net *n)
{
    if (!n || !n->conn)
        return;
    g_cancellable_cancel(n->cancel);
    g_object_unref(n->cancel);
    n->cancel = g_cancellable_new();
    g_string_truncate(n->buf, 0);
    g_io_stream_close(G_IO_STREAM(n->conn), NULL, NULL);
    g_object_unref(n->conn);
    n->conn = NULL;
    netcrypt_wipe(&n->crypt);
    n->closed = FALSE;
}

void net_set_password(Net *n, const char *password)
{
    g_free(n->password);
    n->password = g_strdup(password ? password : "");
}

Net *net_host(int port, const char *password, const NetCallbacks *cb, void *data, GError **error)
{
    Net *n = g_new0(Net, 1);
    n->cb = *cb;
    n->data = data;
    n->cancel = g_cancellable_new();
    n->buf = g_string_new(NULL);
    n->waiters = g_ptr_array_new();
    net_set_password(n, password);
    n->service = g_socket_service_new();
    if (!g_socket_listener_add_inet_port(G_SOCKET_LISTENER(n->service), port, NULL, error)) {
        net_free(n);
        return NULL;
    }
    g_signal_connect(n->service, "incoming", G_CALLBACK(incoming_cb), n);
    g_socket_service_start(n->service);
    return n;
}

static void connect_cb(GObject *src, GAsyncResult *res, gpointer user)
{
    Net *n = user;
    GError *err = NULL;
    GSocketConnection *conn = g_socket_client_connect_to_host_finish(G_SOCKET_CLIENT(src), res, &err);
    if (!conn) {
        if (!g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            closed(n, err->message);
        g_error_free(err);
        return;
    }
    n->conn = conn;
    if (n->plain) {
        n->stage = STAGE_KEYED;
        if (n->cb.connected)
            n->cb.connected(n->data);
        if (n->conn)
            read_more(n);
        return;
    }
    if (!netcrypt_init(&n->crypt, FALSE)) {
        closed(n, "no random numbers");
        return;
    }
    n->stage = STAGE_KEY;
    char *hello = netcrypt_hello(&n->crypt, FALSE);
    tell(conn, hello);
    g_free(hello);
    read_more(n);
}

static Net *join(const char *host, int port, const NetCallbacks *cb, void *data, gboolean plain)
{
    Net *n = g_new0(Net, 1);
    n->cb = *cb;
    n->data = data;
    n->plain = plain;
    n->cancel = g_cancellable_new();
    n->buf = g_string_new(NULL);
    n->client = g_socket_client_new();
    g_socket_client_connect_to_host_async(n->client, host, port, n->cancel, connect_cb, n);
    return n;
}

Net *net_join(const char *host, int port, const NetCallbacks *cb, void *data) { return join(host, port, cb, data, FALSE); }


Net *net_join_plain(const char *host, int port, const NetCallbacks *cb, void *data) { return join(host, port, cb, data, TRUE); }

void net_send(Net *n, const char *line)
{
    if (!n || !n->conn || n->closed || n->stage != STAGE_KEYED)
        return;
    GOutputStream *out = g_io_stream_get_output_stream(G_IO_STREAM(n->conn));
    char *sealed = n->plain ? g_strdup(line) : netcrypt_seal(&n->crypt, line), *text = g_strconcat(sealed, "\n", NULL);
    GError *err = NULL;
    if (!g_output_stream_write_all(out, text, strlen(text), NULL, NULL, &err)) {
        closed(n, err->message);
        g_error_free(err);
    }
    g_free(sealed);
    g_free(text);
}

void net_free(Net *n)
{
    if (!n)
        return;
    n->closed = TRUE;
    g_cancellable_cancel(n->cancel);
    if (n->waiters) {
        while (n->waiters->len)
            wait_end(n->waiters->pdata[0], FALSE);
        g_ptr_array_free(n->waiters, TRUE);
    }
    if (n->service) {
        g_socket_service_stop(n->service);
        g_socket_listener_close(G_SOCKET_LISTENER(n->service));
        g_object_unref(n->service);
    }
    if (n->conn) {
        g_io_stream_close(G_IO_STREAM(n->conn), NULL, NULL);
        g_object_unref(n->conn);
        n->conn = NULL;
    }
    if (n->client)
        g_object_unref(n->client);
    g_object_unref(n->cancel);
    netcrypt_wipe(&n->crypt);
    g_free(n->password);
    n->password = NULL;
    if (n->busy) { /* read_cb frees it once its line's callback returns */
        n->freed = TRUE;
        return;
    }
    g_string_free(n->buf, TRUE);
    g_free(n);
}

#ifndef G_OS_WIN32
/* Bridges of containers and virtual machines: the other player cannot reach those. */
static gboolean virtual_interface(const char *name)
{
    static const char *prefixes[] = {"docker", "br-", "veth", "virbr", "vmnet", "vboxnet", "lxc", "lxd", "podman", "cni"};
    for (size_t i = 0; i < G_N_ELEMENTS(prefixes); i++)
        if (g_str_has_prefix(name, prefixes[i]))
            return TRUE;
    return FALSE;
}
#endif

#ifdef G_OS_WIN32
/* The IPv4 adapters, NULL if Windows cannot list them; g_free() it. The size grows to what Windows
 * asks for (many virtual adapters do not fit a fixed buffer). */
static IP_ADAPTER_ADDRESSES *adapters(void)
{
    ULONG size = 16384;
    for (int i = 0; i < 4; i++) {
        IP_ADAPTER_ADDRESSES *list = g_malloc(size);
        ULONG r = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                       NULL, list, &size);
        if (r == NO_ERROR)
            return list;
        g_free(list);
        if (r != ERROR_BUFFER_OVERFLOW)
            break;
    }
    return NULL;
}
#endif

char *net_local_addresses(void)
{
    GString *s = g_string_new(NULL);
#ifdef G_OS_WIN32
    IP_ADAPTER_ADDRESSES *list = adapters();
    if (list) {
        for (IP_ADAPTER_ADDRESSES *a = list; a; a = a->Next) {
            char *name = g_utf16_to_utf8((gunichar2 *) a->FriendlyName, -1, NULL, NULL, NULL);
            gboolean skip = a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK ||
                            (name && (strstr(name, "vEthernet") || strstr(name, "VirtualBox") || strstr(name, "VMware")));
            g_free(name);
            for (IP_ADAPTER_UNICAST_ADDRESS *u = a->FirstUnicastAddress; u && !skip; u = u->Next) {
                char buf[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &((struct sockaddr_in *) u->Address.lpSockaddr)->sin_addr, buf, sizeof buf);
                g_string_append_printf(s, "%s%s", s->len ? " " : "", buf);
            }
        }
    }
    g_free(list);
#else
    struct ifaddrs *list, *a;
    if (getifaddrs(&list) == 0) {
        for (a = list; a; a = a->ifa_next) {
            if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET || !(a->ifa_flags & IFF_UP) ||
                (a->ifa_flags & IFF_LOOPBACK) || virtual_interface(a->ifa_name))
                continue;
            char buf[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &((struct sockaddr_in *) a->ifa_addr)->sin_addr, buf, sizeof buf);
            g_string_append_printf(s, "%s%s", s->len ? " " : "", buf);
        }
        freeifaddrs(list);
    }
#endif
    return g_string_free(s, FALSE);
}

/* A VPN is up: discovery and the other player's connection may go through it rather than the
 * local network (tun, WireGuard, PPP and the like; on Windows, adapters that say so). */
gboolean net_vpn_on(void)
{
    gboolean on = FALSE;
#ifdef G_OS_WIN32
    static const wchar_t *const NAMES[] = {L"VPN", L"TAP", L"WireGuard", L"Wintun", L"Tailscale", L"NordLynx", L"OpenVPN"};
    IP_ADAPTER_ADDRESSES *list = adapters();
    if (list)
        for (IP_ADAPTER_ADDRESSES *a = list; a && !on; a = a->Next) {
            if (a->OperStatus != IfOperStatusUp || !a->FirstUnicastAddress)
                continue;
            on = a->IfType == IF_TYPE_PPP;
            for (size_t i = 0; i < G_N_ELEMENTS(NAMES) && !on; i++)
                on = (a->FriendlyName && wcsstr(a->FriendlyName, NAMES[i])) || (a->Description && wcsstr(a->Description, NAMES[i]));
        }
    g_free(list);
#else
    static const char *prefixes[] = {"tun", "tap", "wg", "ppp", "utun", "ipsec", "nordlynx", "tailscale", "proton"};
    struct ifaddrs *list, *a;
    if (getifaddrs(&list) == 0) {
        for (a = list; a && !on; a = a->ifa_next) {
            if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET || !(a->ifa_flags & IFF_UP))
                continue;
            for (size_t i = 0; i < G_N_ELEMENTS(prefixes) && !on; i++)
                on = g_str_has_prefix(a->ifa_name, prefixes[i]);
        }
        freeifaddrs(list);
    }
#endif
    return on;
}

gboolean net_available(void)
{
    char *addresses = net_local_addresses();
    gboolean ok = *addresses != 0;
    g_free(addresses);
    return ok;
}

/* ---- discovery ---- */

struct NetAnnounce {
    int fd;
    guint timer, watch;
    GIOChannel *ch;
    char *message;
    GArray *targets; /* the global broadcast and each interface's subnet broadcast */
    unsigned char mdns[512]; /* the same announcement as mDNS, sent from each interface */
    int mdns_len;
    char *interfaces; /* this machine's addresses, space separated */
};

static void add_target(GArray *targets, guint32 addr)
{
    for (guint i = 0; i < targets->len; i++)
        if (g_array_index(targets, guint32, i) == addr)
            return;
    g_array_append_val(targets, addr);
}

static GArray *broadcast_targets(void)
{
    GArray *targets = g_array_new(FALSE, FALSE, sizeof(guint32));
    add_target(targets, INADDR_BROADCAST);
#ifndef G_OS_WIN32
    struct ifaddrs *ifs = NULL;
    if (getifaddrs(&ifs) == 0) {
        for (struct ifaddrs *i = ifs; i; i = i->ifa_next) {
            if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !(i->ifa_flags & IFF_BROADCAST) ||
                !i->ifa_broadaddr)
                continue;
            add_target(targets, ((struct sockaddr_in *) i->ifa_broadaddr)->sin_addr.s_addr);
        }
        freeifaddrs(ifs);
    }
#endif
    return targets;
}

static void mdns_each(const char *addresses, void (*func)(const char *address, void *data), void *data)
{
    char **list = g_strsplit(addresses, " ", -1);
    for (char **i = list; *i; i++)
        if (**i)
            func(*i, data);
    g_strfreev(list);
}

static void mdns_send(const char *address, void *user)
{
    NetAnnounce *a = user;
    struct in_addr from;
    if (inet_pton(AF_INET, address, &from) != 1)
        return;
    setsockopt(a->fd, IPPROTO_IP, IP_MULTICAST_IF, (const char *) &from, sizeof from);
    struct sockaddr_in to = {.sin_family = AF_INET, .sin_port = htons(MDNS_PORT)};
    inet_pton(AF_INET, MDNS_GROUP, &to.sin_addr);
    sendto(a->fd, (const char *) a->mdns, a->mdns_len, 0, (struct sockaddr *) &to, sizeof to);
}

static gboolean announce_cb(gpointer user)
{
    NetAnnounce *a = user;
    g_free(a->interfaces);
    a->interfaces = net_local_addresses();
    if (a->mdns_len)
        mdns_each(a->interfaces, mdns_send, a);
    for (guint i = 0; i < a->targets->len; i++) {
        struct sockaddr_in to = {.sin_family = AF_INET, .sin_port = htons(NET_PORT),
                                 .sin_addr.s_addr = g_array_index(a->targets, guint32, i)};
        sendto(a->fd, a->message, (int) strlen(a->message), 0, (struct sockaddr *) &to, sizeof to);
    }
    return TRUE;
}

/* Bound to 5353, the socket also gets the network's mDNS traffic: read and dropped, or its buffer fills. */
static gboolean drain_cb(GIOChannel *ch, GIOCondition cond, gpointer user)
{
    (void) ch, (void) cond;
    char buf[1500];
    recv(GPOINTER_TO_INT(user), buf, sizeof buf, 0);
    return TRUE;
}

static int udp_socket(void)
{
#ifdef G_OS_WIN32
    static gboolean started;
    if (!started) {
        WSADATA wsa;
        started = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    }
#endif
    return socket(AF_INET, SOCK_DGRAM, 0);
}

NetAnnounce *net_announce(const char *name, int port)
{
    int fd = udp_socket(), yes = 1;
    if (fd < 0)
        return NULL;
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, (const char *) &yes, sizeof yes);
    /* mDNS answers come from port 5353; shared with avahi and the like, and only if it is free. */
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *) &yes, sizeof yes);
#if defined(SO_REUSEPORT) && !defined(G_OS_WIN32)
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (const char *) &yes, sizeof yes);
#endif
    struct sockaddr_in me = {.sin_family = AF_INET, .sin_port = htons(MDNS_PORT), .sin_addr.s_addr = INADDR_ANY};
    bind(fd, (struct sockaddr *) &me, sizeof me);
    NetAnnounce *a = g_new0(NetAnnounce, 1);
    a->fd = fd;
    a->message = g_strdup_printf("DENBOKU-HOST 2 %d %s", port, name);
    a->targets = broadcast_targets();
    a->mdns_len = mdns_announce(a->mdns, sizeof a->mdns, g_random_int(), port, name);
    a->ch = socket_channel(fd);
    a->watch = g_io_add_watch(a->ch, G_IO_IN, drain_cb, GINT_TO_POINTER(fd));
    announce_cb(a);
    a->timer = g_timeout_add_seconds(2, announce_cb, a);
    return a;
}

void net_announce_free(NetAnnounce *a)
{
    if (!a)
        return;
    g_source_remove(a->timer);
    g_source_remove(a->watch);
    g_io_channel_unref(a->ch);
    close_socket(a->fd);
    g_array_free(a->targets, TRUE);
    g_free(a->interfaces);
    g_free(a->message);
    g_free(a);
}

struct NetBrowser {
    int fd, mdns_fd; /* the broadcast's socket and the mDNS one (-1 when 5353 is not ours to listen on) */
    guint watch, mdns_watch;
    GIOChannel *ch, *mdns_ch;
    NetFoundFunc found;
    void *data;
};

static void report(NetBrowser *b, const struct sockaddr_in *from, int port, const char *name)
{
    char ip[INET_ADDRSTRLEN], address[INET_ADDRSTRLEN + 8];
    inet_ntop(AF_INET, &from->sin_addr, ip, sizeof ip);
    if (port == NET_PORT)
        snprintf(address, sizeof address, "%s", ip);
    else
        snprintf(address, sizeof address, "%s:%d", ip, port);
    b->found(address, name, b->data);
}

static gboolean mdns_cb(GIOChannel *ch, GIOCondition cond, gpointer user)
{
    NetBrowser *b = user;
    (void) ch, (void) cond;
    unsigned char buf[1500];
    struct sockaddr_in from;
    socklen_t len = sizeof from;
    int n = (int) recvfrom(b->mdns_fd, (char *) buf, sizeof buf, 0, (struct sockaddr *) &from, &len);
    int port;
    char name[MDNS_NAME_BYTES + 1];
    if (n > 0 && mdns_parse(buf, n, &port, name))
        report(b, &from, port, name);
    return TRUE;
}

static gboolean browse_cb(GIOChannel *ch, GIOCondition cond, gpointer user)
{
    NetBrowser *b = user;
    (void) ch, (void) cond;
    char buf[512];
    struct sockaddr_in from;
    socklen_t len = sizeof from;
    int n = (int) recvfrom(b->fd, buf, sizeof buf - 1, 0, (struct sockaddr *) &from, &len);
    if (n <= 0)
        return TRUE;
    buf[n] = 0;
    if (g_getenv("DENBOKU_DEBUG"))
        g_printerr("browse: %.80s\n", buf);
    int port;
    char name[384] = "";
    if (sscanf(buf, "DENBOKU-HOST %*d %d %383[^\n]", &port, name) >= 1)
        report(b, &from, port, name);
    return TRUE;
}

static void mdns_join(const char *address, void *user)
{
    int fd = GPOINTER_TO_INT(user);
    struct ip_mreq mreq;
    if (inet_pton(AF_INET, address, &mreq.imr_interface) == 1 && inet_pton(AF_INET, MDNS_GROUP, &mreq.imr_multiaddr) == 1)
        setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char *) &mreq, sizeof mreq);
}

/* Listens for mDNS announcements on 5353, shared with whatever else listens there (avahi, the
 * system's resolver); without it the browser still hears the broadcast. */
static int mdns_socket(void)
{
    int fd = udp_socket(), yes = 1;
    if (fd < 0)
        return -1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *) &yes, sizeof yes);
#if defined(SO_REUSEPORT) && !defined(G_OS_WIN32)
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (const char *) &yes, sizeof yes);
#endif
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(MDNS_PORT), .sin_addr.s_addr = INADDR_ANY};
    if (bind(fd, (struct sockaddr *) &addr, sizeof addr) < 0) {
        close_socket(fd);
        return -1;
    }
    char *addresses = net_local_addresses();
    mdns_each(addresses, mdns_join, GINT_TO_POINTER(fd));
    g_free(addresses);
    return fd;
}

NetBrowser *net_browse(NetFoundFunc found, void *data)
{
    int fd = udp_socket(), yes = 1;
    if (fd < 0)
        return NULL;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *) &yes, sizeof yes);
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(NET_PORT), .sin_addr.s_addr = INADDR_ANY};
    if (bind(fd, (struct sockaddr *) &addr, sizeof addr) < 0) {
        if (g_getenv("DENBOKU_DEBUG"))
            g_printerr("browse: port %d: %s\n", NET_PORT, g_strerror(errno));
        close_socket(fd);
        return NULL;
    }
    NetBrowser *b = g_new0(NetBrowser, 1);
    b->fd = fd;
    b->found = found;
    b->data = data;
    b->ch = socket_channel(fd);
    b->watch = g_io_add_watch(b->ch, G_IO_IN, browse_cb, b);
    b->mdns_fd = mdns_socket();
    if (b->mdns_fd >= 0) {
        b->mdns_ch = socket_channel(b->mdns_fd);
        b->mdns_watch = g_io_add_watch(b->mdns_ch, G_IO_IN, mdns_cb, b);
    }
    return b;
}

void net_browser_free(NetBrowser *b)
{
    if (!b)
        return;
    g_source_remove(b->watch);
    g_io_channel_unref(b->ch);
    close_socket(b->fd);
    if (b->mdns_fd >= 0) {
        g_source_remove(b->mdns_watch);
        g_io_channel_unref(b->mdns_ch);
        close_socket(b->mdns_fd);
    }
    g_free(b);
}
