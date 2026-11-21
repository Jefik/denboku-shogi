#include "update.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../third_party/monocypher.h"

gboolean update_unhex(const char *hex, uint8_t *out, gsize n)
{
    if (!hex || strlen(hex) != 2 * n)
        return FALSE;
    for (gsize i = 0; i < n; i++) {
        int hi = g_ascii_xdigit_value(hex[2 * i]), lo = g_ascii_xdigit_value(hex[2 * i + 1]);
        if (hi < 0 || lo < 0)
            return FALSE;
        out[i] = (uint8_t) (hi << 4 | lo);
    }
    return TRUE;
}

static gboolean plain_name(const char *s) { return *s && !strchr(s, '/') && !strchr(s, '\\') && s[0] != '.'; }

static void copy_key(GKeyFile *kf, const char *group, const char *key, char *out, gsize n)
{
    char *v = g_key_file_get_string(kf, group, key, NULL);
    g_strlcpy(out, v ? v : "", n);
    g_free(v);
}

gboolean update_read(const char *text, gsize len, const char *sig_hex, const uint8_t (*keys)[32], int nkeys,
                     const char *platform, UpdateOffer *out)
{
    uint8_t sig[64];
    char *hex = g_strstrip(g_strdup(sig_hex ? sig_hex : ""));
    gboolean ok = update_unhex(hex, sig, sizeof sig), signed_ok = FALSE;
    g_free(hex);
    for (int i = 0; ok && i < nkeys && !signed_ok; i++)
        signed_ok = crypto_eddsa_check(sig, keys[i], (const uint8_t *) text, len) == 0;
    if (!signed_ok)
        return FALSE;
    GKeyFile *kf = g_key_file_new();
    memset(out, 0, sizeof *out);
    char *hash = NULL;
    ok = g_key_file_load_from_data(kf, text, len, G_KEY_FILE_NONE, NULL) && g_key_file_has_group(kf, platform);
    if (ok) {
        copy_key(kf, "Release", "Build", out->build, sizeof out->build);
        copy_key(kf, "Release", "Version", out->version, sizeof out->version);
        copy_key(kf, "Release", "Notes", out->notes, sizeof out->notes);
        copy_key(kf, "Release", "Tag", out->tag, sizeof out->tag);
        copy_key(kf, platform, "File", out->file, sizeof out->file);
        out->size = g_key_file_get_int64(kf, platform, "Size", NULL);
        hash = g_key_file_get_string(kf, platform, "Blake2b", NULL);
        /* Plain names: the package is saved under its file name, beside the game, and the tag is a part of its address. */
        ok = *out->build && plain_name(out->file) && (!*out->tag || plain_name(out->tag)) && out->size > 0 && update_unhex(hash, out->hash, sizeof out->hash);
    }
    g_free(hash);
    g_key_file_free(kf);
    return ok;
}

/* AAAAMMDD.HHMM as a number (a later build is a larger one); -1 when it is not one. */
static double build_number(const char *b)
{
    char *end;
    double v = b ? g_ascii_strtod(b, &end) : -1;
    return b && *b && !*end ? v : -1;
}

gboolean update_newer(const char *have, const char *offered)
{
    double h = build_number(have), o = build_number(offered);
    return o > 0 && o > h;
}

gboolean update_hash_file(const char *path, uint8_t hash[32], gint64 *size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return FALSE;
    crypto_blake2b_ctx ctx;
    crypto_blake2b_init(&ctx, 32);
    uint8_t buf[65536];
    size_t n;
    gint64 total = 0;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
        crypto_blake2b_update(&ctx, buf, n);
        total += (gint64) n;
    }
    gboolean ok = !ferror(f);
    fclose(f);
    crypto_blake2b_final(&ctx, hash);
    if (size)
        *size = total;
    return ok;
}

gboolean update_file_ok(const char *path, const UpdateOffer *o)
{
    uint8_t hash[32];
    gint64 size;
    return update_hash_file(path, hash, &size) && size == o->size && crypto_verify32(hash, o->hash) == 0;
}
