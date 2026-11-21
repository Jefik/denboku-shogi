#ifdef _WIN32
#define _CRT_RAND_S
#endif
#include "netcrypt.h"

#include "../third_party/monocypher.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Argon2id with 1 MiB and three passes: some tens of ms on the Kindle, once per connection. */
#define ARGON2_BLOCKS 1024
#define ARGON2_PASSES 3

static gboolean random_bytes(uint8_t *out, size_t n)
{
#ifdef _WIN32
    for (size_t i = 0; i < n; i++) {
        unsigned int r;
        if (rand_s(&r))
            return FALSE;
        out[i] = (uint8_t) r;
    }
    return TRUE;
#else
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f)
        return FALSE;
    gboolean ok = fread(out, 1, n, f) == n;
    fclose(f);
    return ok;
#endif
}

gboolean netcrypt_init(NetCrypt *c, gboolean host)
{
    memset(c, 0, sizeof *c);
    c->host = host;
    if (!random_bytes(c->secret, sizeof c->secret))
        return FALSE;
    crypto_x25519_public_key(c->own, c->secret);
    return TRUE;
}

char *netcrypt_hello(const NetCrypt *c, gboolean password)
{
    char hex[65];
    for (int i = 0; i < 32; i++)
        snprintf(hex + 2 * i, 3, "%02x", c->own[i]);
    return g_strdup_printf("KEY %s%s", hex, c->host && password ? " 1" : "");
}

gboolean netcrypt_peer(NetCrypt *c, const char *line, gboolean *password)
{
    *password = FALSE;
    if (strncmp(line, "KEY ", 4))
        return FALSE;
    const char *p = line + 4;
    for (int i = 0; i < 32; i++) {
        int hi = g_ascii_xdigit_value(p[2 * i]), lo = hi < 0 ? -1 : g_ascii_xdigit_value(p[2 * i + 1]);
        if (lo < 0)
            return FALSE;
        c->peer[i] = (uint8_t) (hi << 4 | lo);
    }
    p += 64;
    if (*p && strcmp(p, " 0") && strcmp(p, " 1"))
        return FALSE;
    *password = !c->host && !strcmp(p, " 1");
    return TRUE;
}

gboolean netcrypt_key(NetCrypt *c, const char *password)
{
    static const uint8_t zero[32];
    const uint8_t *host = c->host ? c->own : c->peer, *joiner = c->host ? c->peer : c->own;
    uint8_t shared[32], pass[32], salt[16];
    crypto_x25519(shared, c->secret, c->peer);
    if (!crypto_verify32(shared, zero))
        return FALSE;
    crypto_blake2b_ctx h;
    if (password && *password) {
        crypto_blake2b_init(&h, sizeof salt);
        crypto_blake2b_update(&h, host, 32);
        crypto_blake2b_update(&h, joiner, 32);
        crypto_blake2b_final(&h, salt);
        void *work = g_malloc((gsize) ARGON2_BLOCKS * 1024);
        crypto_argon2_config config = {CRYPTO_ARGON2_ID, ARGON2_BLOCKS, ARGON2_PASSES, 1};
        crypto_argon2_inputs inputs = {(const uint8_t *) password, salt, (uint32_t) strlen(password), sizeof salt};
        crypto_argon2(pass, sizeof pass, work, config, inputs, crypto_argon2_no_extras);
        g_free(work);
    }
    static const char label[] = "denboku lan 4";
    crypto_blake2b_init(&h, sizeof c->key);
    crypto_blake2b_update(&h, (const uint8_t *) label, sizeof label);
    crypto_blake2b_update(&h, shared, sizeof shared);
    crypto_blake2b_update(&h, host, 32);
    crypto_blake2b_update(&h, joiner, 32);
    if (password && *password)
        crypto_blake2b_update(&h, pass, sizeof pass);
    crypto_blake2b_final(&h, c->key);
    crypto_wipe(shared, sizeof shared);
    crypto_wipe(pass, sizeof pass);
    crypto_wipe(c->secret, sizeof c->secret);
    c->keyed = TRUE;
    return TRUE;
}

/* Which way the line goes (0 from the host) and its number: a nonce never used twice. */
static void nonce(uint8_t out[24], gboolean from_host, guint64 count)
{
    memset(out, 0, 24);
    out[0] = from_host ? 0 : 1;
    for (int i = 0; i < 8; i++)
        out[16 + i] = (uint8_t) (count >> (8 * i));
}

char *netcrypt_seal(NetCrypt *c, const char *line)
{
    size_t len = strlen(line);
    uint8_t n[24], *box = g_malloc(16 + len);
    nonce(n, c->host, c->sent++);
    crypto_aead_lock(box + 16, box, c->key, n, NULL, 0, (const uint8_t *) line, len);
    char *out = g_base64_encode(box, 16 + len);
    g_free(box);
    return out;
}

char *netcrypt_open(NetCrypt *c, const char *sealed)
{
    if (!c->keyed)
        return NULL;
    for (const char *p = sealed; *p; p++)
        if (!g_ascii_isalnum(*p) && *p != '+' && *p != '/' && *p != '=')
            return NULL;
    gsize len;
    uint8_t *box = g_base64_decode(sealed, &len), n[24];
    if (len < 16) {
        g_free(box);
        return NULL;
    }
    char *line = g_malloc(len - 16 + 1);
    nonce(n, !c->host, c->got);
    if (crypto_aead_unlock((uint8_t *) line, box, c->key, n, NULL, 0, box + 16, len - 16) ||
        memchr(line, 0, len - 16) || memchr(line, '\n', len - 16)) {
        g_free(box);
        g_free(line);
        return NULL;
    }
    line[len - 16] = 0;
    c->got++;
    g_free(box);
    return line;
}

void netcrypt_wipe(NetCrypt *c) { crypto_wipe(c, sizeof *c); }
