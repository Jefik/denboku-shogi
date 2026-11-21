#ifndef NET_H
#define NET_H

#include <glib.h>

/* A line-based TCP connection to one opponent (LAN play), every line sealed once the two sides
 * made a key (netcrypt.h): the room password goes into the key and never over the network. */
typedef struct Net Net;

typedef struct {
    /* The key is made: lines may be sent. */
    void (*connected)(void *data);
    /* A line from the other side; "BANNED" and "DENIED" from a host that does not let this side in. */
    void (*line)(const char *line, void *data);
    /* Connection lost or refused; `why` is a short message, NET_TOO_OLD for an older version. */
    void (*closed)(const char *why, void *data);
    /* Host: decides whether to accept a new connection from `address`. A refused connection is
     * told "BANNED" and closed before it can replace the current player. NULL accepts all. */
    gboolean (*accept)(const char *address, void *data);
    /* Host: a newcomer from `address` gave a wrong room password; it was told "DENIED". */
    void (*denied)(const char *address, void *data);
    /* Joiner: the room has a password; net_password() goes on. */
    void (*password)(void *data);
} NetCallbacks;

extern const char NET_TOO_OLD[];

/* The default game port, and always the UDP port of LAN discovery. */
#define NET_PORT 4081

/* Listens on `port` for a room with `password` ("" or NULL for none). A later connection replaces
 * the current one (the opponent coming back) once it shows that it has the key. */
Net *net_host(int port, const char *password, const NetCallbacks *cb, void *data, GError **error);
/* Host: the password asked from the next ones to come. */
void net_set_password(Net *n, const char *password);
/* Connects to "host" or "host:port" (default `port`). */
Net *net_join(const char *host, int port, const NetCallbacks *cb, void *data);
/* The same, lines in clear: a CSA server. */
Net *net_join_plain(const char *host, int port, const NetCallbacks *cb, void *data);
/* Joiner: the room password the player gave, after the password callback. */
void net_password(Net *n, const char *password);
void net_send(Net *n, const char *line);
/* The current connection's remote address ("192.168.0.5"), or NULL when there is none. */
char *net_peer_address(Net *n);
/* Host: closes the current connection, without the closed callback, and goes on listening. */
void net_drop(Net *n);
void net_free(Net *n);
/* This machine's IPv4 addresses, space separated (for the host to tell the other player). */
char *net_local_addresses(void);
/* True when there is a usable non-loopback IPv4 interface (Wi-Fi/Ethernet, not airplane mode). */
gboolean net_available(void);
/* A VPN is up, which can keep the devices of a network game from finding each other. */
gboolean net_vpn_on(void);

/* LAN discovery: a waiting host broadcasts "DENBOKU-HOST 2 <game port> <name>" on UDP NET_PORT
 * every two seconds; a browser reports every host it hears, as "address" or "address:port". */
typedef struct NetAnnounce NetAnnounce;
NetAnnounce *net_announce(const char *name, int port);
void net_announce_free(NetAnnounce *a);

typedef struct NetBrowser NetBrowser;
typedef void (*NetFoundFunc)(const char *address, const char *name, void *data);
NetBrowser *net_browse(NetFoundFunc found, void *data);
void net_browser_free(NetBrowser *b);

#endif
