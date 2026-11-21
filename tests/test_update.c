/* The update manifest: signature, platform, build order, package check. */
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../src/core/update.h"
#include "../src/third_party/monocypher.h"

#define CHECK(c)                                                       \
    do {                                                               \
        if (!(c)) {                                                    \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c);    \
            return 1;                                                  \
        }                                                              \
    } while (0)

static void sign(const char *text, const uint8_t sk[64], char out[129])
{
    uint8_t sig[64];
    crypto_eddsa_sign(sig, sk, (const uint8_t *) text, strlen(text));
    for (int i = 0; i < 64; i++)
        sprintf(out + 2 * i, "%02x", sig[i]);
}

int main(void)
{
    uint8_t seed[32] = {1}, seed2[32] = {2}, sk[64], pk[32], sk2[64], pk2[32], other[64], other_pk[32], seed3[32] = {3};
    crypto_eddsa_key_pair(sk, pk, seed);
    crypto_eddsa_key_pair(sk2, pk2, seed2);
    crypto_eddsa_key_pair(other, other_pk, seed3);
    uint8_t keys[2][32];
    memcpy(keys[0], pk, 32), memcpy(keys[1], pk2, 32);

    /* The package: a small file, and its hash in the manifest. */
    char *dir = g_strdup_printf("%s/test_update_%d", g_get_tmp_dir(), (int) getpid()), *pkg = g_build_filename(dir, "pkg.tar.gz", NULL);
    g_mkdir_with_parents(dir, 0700);
    CHECK(g_file_set_contents(pkg, "package bytes", -1, NULL));
    uint8_t hash[32];
    gint64 size;
    CHECK(update_hash_file(pkg, hash, &size) && size == 13);
    char hex[65];
    for (int i = 0; i < 32; i++)
        sprintf(hex + 2 * i, "%02x", hash[i]);
    char *manifest = g_strdup_printf("[Release]\nBuild=20261117.1200\nVersion=0.9.0\nTag=v0.9.0\nNotes=https://example.org/notes\n\n"
                                     "[kindle]\nFile=denboku-kindle.zip\nSize=13\nBlake2b=%s\n", hex);
    char sig[129];
    UpdateOffer o;

    sign(manifest, sk, sig);
    CHECK(update_read(manifest, strlen(manifest), sig, (const uint8_t (*)[32]) keys, 2, "kindle", &o));
    CHECK(!strcmp(o.build, "20261117.1200") && !strcmp(o.version, "0.9.0") && !strcmp(o.tag, "v0.9.0") && !strcmp(o.file, "denboku-kindle.zip") && o.size == 13);
    CHECK(update_file_ok(pkg, &o));
    CHECK(!update_read(manifest, strlen(manifest), sig, (const uint8_t (*)[32]) keys, 2, "windows-x86_64", &o)); /* no such platform */

    sign(manifest, sk2, sig); /* the reserve key */
    CHECK(update_read(manifest, strlen(manifest), sig, (const uint8_t (*)[32]) keys, 2, "kindle", &o));
    sign(manifest, other, sig); /* someone else's */
    CHECK(!update_read(manifest, strlen(manifest), sig, (const uint8_t (*)[32]) keys, 2, "kindle", &o));
    sign(manifest, sk, sig);
    CHECK(!update_read(manifest, strlen(manifest) - 1, sig, (const uint8_t (*)[32]) keys, 2, "kindle", &o)); /* truncated */
    char *changed = g_strdup(manifest);
    changed[strlen("[Release]\nBuild=2026111")] = '9';
    CHECK(!update_read(changed, strlen(changed), sig, (const uint8_t (*)[32]) keys, 2, "kindle", &o));
    CHECK(!update_read(manifest, strlen(manifest), "abc", (const uint8_t (*)[32]) keys, 2, "kindle", &o));

    /* A file name with a folder in it is refused, even signed. */
    char *bad = g_strdup_printf("[Release]\nBuild=20261117.1200\n[kindle]\nFile=../denboku/bin/denboku\nSize=13\nBlake2b=%s\n", hex);
    sign(bad, sk, sig);
    CHECK(!update_read(bad, strlen(bad), sig, (const uint8_t (*)[32]) keys, 2, "kindle", &o));
    char *bad_tag = g_strdup_printf("[Release]\nBuild=20261117.1200\nTag=../../evil\n[kindle]\nFile=denboku-kindle.zip\nSize=13\nBlake2b=%s\n", hex);
    sign(bad_tag, sk, sig);
    CHECK(!update_read(bad_tag, strlen(bad_tag), sig, (const uint8_t (*)[32]) keys, 2, "kindle", &o));

    CHECK(update_newer("20261116.1624", "20261117.1200"));
    CHECK(!update_newer("20261117.1200", "20261117.1200"));
    CHECK(!update_newer("20261117.1200", "20261116.1624"));
    CHECK(!update_newer("20261116.1624", "garbage"));
    CHECK(update_newer("dev", "20261116.1624"));

    CHECK(g_file_set_contents(pkg, "package byte", -1, NULL)); /* cut short */
    CHECK(!update_file_ok(pkg, &o));
    CHECK(g_file_set_contents(pkg, "package bytez", -1, NULL)); /* same size, changed */
    CHECK(!update_file_ok(pkg, &o));
    g_remove(pkg);
    CHECK(!update_file_ok(pkg, &o));
    g_rmdir(dir);
    g_free(dir), g_free(pkg), g_free(manifest), g_free(changed), g_free(bad), g_free(bad_tag);
    printf("update tests passed\n");
    return 0;
}
