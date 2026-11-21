#include "zip.h"

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>

/* No file of a piece set is larger than this unpacked, nor one of the game's package. */
#define ZIP_MAX_FILE (32 << 20)
#define ZIP_MAX_EXTRACT (256 << 20)

struct Zip {
    guint8 *data;
    gsize len;
    gsize dir, entries; /* the central directory's offset and its number of entries */
};

static guint32 le16(const guint8 *p) { return p[0] | p[1] << 8; }
static guint32 le32(const guint8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (guint32) p[3] << 24; }

Zip *zip_open(const char *path)
{
    gchar *data;
    gsize len;
    if (!g_file_get_contents(path, &data, &len, NULL))
        return NULL;
    const guint8 *d = (const guint8 *) data;
    /* The end record is the last 22 bytes, or before a comment of up to 65535 bytes. */
    for (gsize back = 22; back <= len && back <= 22 + 65535; back++) {
        const guint8 *e = d + len - back;
        if (le32(e) != 0x06054b50)
            continue;
        gsize dir = le32(e + 16), entries = le16(e + 10);
        if (dir > len - back)
            break;
        Zip *z = g_new0(Zip, 1);
        z->data = (guint8 *) data, z->len = len, z->dir = dir, z->entries = entries;
        return z;
    }
    g_free(data);
    return NULL;
}

void zip_close(Zip *z)
{
    if (!z)
        return;
    g_free(z->data);
    g_free(z);
}

static GByteArray *inflate(const guint8 *in, gsize len, gsize size)
{
    if (!size)
        return g_byte_array_new();
    GConverter *c = G_CONVERTER(g_zlib_decompressor_new(G_ZLIB_COMPRESSOR_FORMAT_RAW));
    GByteArray *out = g_byte_array_sized_new(size + 1);
    g_byte_array_set_size(out, size + 1); /* a byte more: a damaged file that runs over is told, not cut */
    gsize read_total = 0, written_total = 0;
    gboolean ok = FALSE;
    for (;;) {
        gsize r = 0, w = 0;
        GError *err = NULL;
        GConverterResult res = g_converter_convert(c, in + read_total, len - read_total, out->data + written_total,
                                                   size + 1 - written_total, G_CONVERTER_INPUT_AT_END, &r, &w, &err);
        read_total += r, written_total += w;
        g_clear_error(&err);
        if (res == G_CONVERTER_ERROR || written_total > size)
            break;
        if (res == G_CONVERTER_FINISHED) {
            ok = written_total == size;
            break;
        }
        if (!r && !w)
            break;
    }
    g_object_unref(c);
    if (!ok) {
        g_byte_array_unref(out);
        return NULL;
    }
    g_byte_array_set_size(out, size);
    return out;
}

/* The central directory's entry at `*at`: its full name, and where it goes next. FALSE past the end or damaged. */
static gboolean entry(Zip *z, gsize *at, const char **full, gsize *nlen, gsize *here)
{
    const guint8 *d = z->data;
    if (*at + 46 > z->len || le32(d + *at) != 0x02014b50)
        return FALSE;
    *here = *at;
    *nlen = le16(d + *at + 28);
    gsize extra = le16(d + *at + 30), comment = le16(d + *at + 32);
    if (*at + 46 + *nlen > z->len)
        return FALSE;
    *full = (const char *) d + *at + 46;
    *at += 46 + *nlen + extra + comment;
    return TRUE;
}

/* The bytes of the entry at `here`, up to `max` unpacked. */
static GByteArray *entry_data(Zip *z, gsize here, gsize max)
{
    const guint8 *d = z->data;
    guint32 method = le16(d + here + 10), csize = le32(d + here + 20), usize = le32(d + here + 24);
    gsize local = le32(d + here + 42); /* gsize: near 4 GiB a 32-bit sum would wrap past the check */
    if (usize > max || local + 30 > z->len || le32(d + local) != 0x04034b50)
        return NULL;
    gsize start = local + 30 + le16(d + local + 26) + le16(d + local + 28);
    if (start > z->len || csize > z->len - start)
        return NULL;
    if (method == 0) {
        if (csize != usize)
            return NULL;
        GByteArray *out = g_byte_array_sized_new(usize);
        g_byte_array_append(out, d + start, usize);
        return out;
    }
    return method == 8 ? inflate(d + start, csize, usize) : NULL;
}

GByteArray *zip_get(Zip *z, const char *name)
{
    gsize at = z->dir, want = strlen(name), nlen, here;
    const char *full;
    for (gsize i = 0; i < z->entries; i++) {
        if (!entry(z, &at, &full, &nlen, &here))
            return NULL;
        const char *base = full;
        for (gsize k = 0; k < nlen; k++)
            if (full[k] == '/' || full[k] == '\\')
                base = full + k + 1;
        if ((gsize) (full + nlen - base) == want && !g_ascii_strncasecmp(base, name, want))
            return entry_data(z, here, ZIP_MAX_FILE);
    }
    return NULL;
}

/* A name that stays inside the folder: relative, no "..", no drive. */
static gboolean safe_name(const char *name)
{
    if (!*name || name[0] == '/' || name[0] == '\\' || strchr(name, ':'))
        return FALSE;
    char **parts = g_strsplit_set(name, "/\\", -1);
    gboolean ok = TRUE;
    for (int i = 0; parts[i]; i++)
        ok &= strcmp(parts[i], "..") != 0;
    g_strfreev(parts);
    return ok;
}

gboolean zip_extract(Zip *z, const char *dir)
{
    gsize at = z->dir, nlen, here;
    const char *full;
    for (int pass = 0; pass < 2; pass++) { /* every name checked before anything is written */
        at = z->dir;
        for (gsize i = 0; i < z->entries; i++) {
            if (!entry(z, &at, &full, &nlen, &here))
                return FALSE;
            char *name = g_strndup(full, nlen);
            gboolean ok = safe_name(name);
            if (ok && pass) {
                char *path = g_build_filename(dir, name, NULL);
                if (g_str_has_suffix(name, "/"))
                    ok = g_mkdir_with_parents(path, 0755) == 0;
                else {
                    char *parent = g_path_get_dirname(path);
                    GByteArray *b = entry_data(z, here, ZIP_MAX_EXTRACT);
                    ok = b && g_mkdir_with_parents(parent, 0755) == 0 && g_file_set_contents(path, (const char *) b->data, b->len, NULL);
                    /* A program stays one: made on Unix (the high byte of "made by") with an x bit in its mode. */
                    const guint8 *d = z->data;
                    if (ok && d[here + 5] == 3 && (le32(d + here + 38) >> 16) & 0111)
                        g_chmod(path, 0755);
                    if (b)
                        g_byte_array_unref(b);
                    g_free(parent);
                }
                g_free(path);
            }
            g_free(name);
            if (!ok)
                return FALSE;
        }
    }
    return TRUE;
}
