#ifndef NETCRYPT_H
#define NETCRYPT_H

#include <glib.h>
#include <stdint.h>

/* The LAN's encryption (docs/CODE.md, "LAN protocol"): an X25519 key exchange, the room password
 * mixed into the key through Argon2id, then every line sealed with XChaCha20-Poly1305 and a
 * counter per direction for nonce, so that a line changed, said again or out of order is refused.
 * No sockets, no GTK. */

typedef struct {
    gboolean host;
    uint8_t secret[32], own[32], peer[32], key[32];
    guint64 sent, got;
    gboolean keyed;
} NetCrypt;

/* A fresh key pair; FALSE when the system gives no random bytes. */
gboolean netcrypt_init(NetCrypt *c, gboolean host);
/* The first line, in clear: "KEY <own key, hex>", and from the host " 1" when the room has a password. */
char *netcrypt_hello(const NetCrypt *c, gboolean password);
/* Reads the other side's "KEY" line; *password tells whether the host's room has one. */
gboolean netcrypt_peer(NetCrypt *c, const char *line, gboolean *password);
/* The session key, from the exchange and the room password ("" or NULL for none); FALSE for a
 * key that gives no secret (a low-order point). */
gboolean netcrypt_key(NetCrypt *c, const char *password);
/* The sealed line (base64), and the line back from it, NULL when it is not this session's next one. */
char *netcrypt_seal(NetCrypt *c, const char *line);
char *netcrypt_open(NetCrypt *c, const char *sealed);
void netcrypt_wipe(NetCrypt *c);

#endif
