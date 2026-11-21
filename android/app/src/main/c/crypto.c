/* The libcrypto.so.3 Xvfb asks for, which takes only SHA-1 from OpenSSL (the Render extension names glyphs by
 * their hash): the six EVP calls it makes, at the version OPENSSL_3.0.0 (crypto.map), instead of the
 * 5 MB library. Built by tools/build-android.sh; android-stage.py puts it in place of Termux's as libsha1.so,
 * a name that does not shadow the system's libcrypto.so, which toybox loads. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t h[5];
    uint64_t len;
    uint8_t buf[64];
    size_t n;
} Sha1;

static const int SHA1_MD; /* the one digest there is: its address stands for it */

static uint32_t rol(uint32_t x, int k) { return x << k | x >> (32 - k); }

static void block(Sha1 *s, const uint8_t *p)
{
    uint32_t w[80], a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t) p[4 * i] << 24 | (uint32_t) p[4 * i + 1] << 16 | (uint32_t) p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 80; i++)
        w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)
            f = (b & c) | (~b & d), k = 0x5A827999;
        else if (i < 40)
            f = b ^ c ^ d, k = 0x6ED9EBA1;
        else if (i < 60)
            f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDC;
        else
            f = b ^ c ^ d, k = 0xCA62C1D6;
        uint32_t t = rol(a, 5) + f + e + k + w[i];
        e = d, d = c, c = rol(b, 30), b = a, a = t;
    }
    s->h[0] += a, s->h[1] += b, s->h[2] += c, s->h[3] += d, s->h[4] += e;
}

void *EVP_MD_CTX_new(void) { return calloc(1, sizeof(Sha1)); }

void EVP_MD_CTX_free(void *ctx) { free(ctx); }

const void *EVP_MD_fetch(void *libctx, const char *name, const char *props)
{
    (void) libctx, (void) props;
    return name && (!strcmp(name, "SHA1") || !strcmp(name, "SHA-1") || !strcmp(name, "sha1")) ? &SHA1_MD : NULL;
}

int EVP_DigestInit_ex2(void *ctx, const void *md, const void *params)
{
    (void) params;
    if (!ctx || md != &SHA1_MD)
        return 0;
    Sha1 *s = ctx;
    memset(s, 0, sizeof *s);
    s->h[0] = 0x67452301, s->h[1] = 0xEFCDAB89, s->h[2] = 0x98BADCFE, s->h[3] = 0x10325476, s->h[4] = 0xC3D2E1F0;
    return 1;
}

int EVP_DigestUpdate(void *ctx, const void *data, size_t len)
{
    Sha1 *s = ctx;
    const uint8_t *p = data;
    s->len += len;
    while (len--) {
        s->buf[s->n++] = *p++;
        if (s->n == 64)
            block(s, s->buf), s->n = 0;
    }
    return 1;
}

int EVP_DigestFinal_ex(void *ctx, unsigned char *out, unsigned int *size)
{
    Sha1 *s = ctx;
    uint64_t bits = s->len * 8;
    uint8_t pad = 0x80;
    EVP_DigestUpdate(s, &pad, 1);
    pad = 0;
    while (s->n != 56)
        EVP_DigestUpdate(s, &pad, 1);
    for (int i = 7; i >= 0; i--) {
        uint8_t b = (uint8_t) (bits >> (8 * i));
        EVP_DigestUpdate(s, &b, 1);
    }
    for (int i = 0; i < 20; i++)
        out[i] = (unsigned char) (s->h[i / 4] >> (24 - 8 * (i % 4)));
    if (size)
        *size = 20;
    return 1;
}
