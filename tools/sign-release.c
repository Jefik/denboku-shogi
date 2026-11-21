/* The update manifest's keys and signature (Ed25519, Monocypher), and a package's size and hash.
 *   sign-release keygen <secret file>        writes a new secret key, prints the public one (hex)
 *   sign-release sign <secret file> <file>   writes <file>.sig (128 hex digits and a line end)
 *   sign-release hash <file>                 prints "<size> <BLAKE2b-256 hex>"
 * The secret key never goes in the repository. */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "../src/core/update.h"
#include "../src/third_party/monocypher.h"

static void hex(const uint8_t *b, size_t n, char *out)
{
    for (size_t i = 0; i < n; i++)
        sprintf(out + 2 * i, "%02x", b[i]);
}

static int secret(const char *path, uint8_t sk[64], uint8_t pk[32])
{
    char *text = NULL;
    uint8_t seed[32];
    if (!g_file_get_contents(path, &text, NULL, NULL) || !update_unhex(g_strstrip(text), seed, 32)) {
        fprintf(stderr, "%s: not a secret key\n", path);
        g_free(text);
        return 0;
    }
    g_free(text);
    crypto_eddsa_key_pair(sk, pk, seed);
    return 1;
}

int main(int argc, char **argv)
{
    char h[129];
    if (argc == 3 && !strcmp(argv[1], "keygen")) {
        uint8_t seed[32], sk[64], pk[32];
        FILE *r = fopen("/dev/urandom", "rb");
        if (!r || fread(seed, 1, 32, r) != 32 || g_file_test(argv[2], G_FILE_TEST_EXISTS)) {
            fprintf(stderr, "no randomness, or %s exists\n", argv[2]);
            return 1;
        }
        fclose(r);
        hex(seed, 32, h);
        strcat(h, "\n");
        mode_t mask = umask(077); /* the secret key: for this user only */
        gboolean saved = g_file_set_contents(argv[2], h, -1, NULL);
        umask(mask);
        if (!saved)
            return 1;
        crypto_eddsa_key_pair(sk, pk, seed);
        hex(pk, 32, h);
        puts(h);
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "sign")) {
        uint8_t sk[64], pk[32], sig[64];
        char *text;
        gsize len;
        if (!secret(argv[2], sk, pk) || !g_file_get_contents(argv[3], &text, &len, NULL))
            return 1;
        crypto_eddsa_sign(sig, sk, (const uint8_t *) text, len);
        hex(sig, 64, h);
        strcat(h, "\n");
        char *out = g_strconcat(argv[3], ".sig", NULL);
        return g_file_set_contents(out, h, -1, NULL) ? 0 : 1;
    }
    if (argc == 3 && !strcmp(argv[1], "hash")) {
        uint8_t hash[32];
        gint64 size;
        if (!update_hash_file(argv[2], hash, &size))
            return 1;
        hex(hash, 32, h);
        printf("%lld %s\n", (long long) size, h);
        return 0;
    }
    fprintf(stderr, "usage: sign-release keygen <secret> | sign <secret> <file> | hash <file>\n");
    return 2;
}
