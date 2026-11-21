#ifndef UPDATE_H
#define UPDATE_H

#include <glib.h>
#include <stdint.h>

/* Updates, the parts without GTK or network: a release's manifest (update.txt, in the settings.ini
 * format) signed with Ed25519, each package with its size and BLAKE2b-256. */
typedef struct {
    char build[32], version[32], notes[256], tag[64], file[128];
    gint64 size;
    uint8_t hash[32];
} UpdateOffer;

/* The manifest `text` (its exact bytes) and its signature (128 hex digits): TRUE, with `out` filled for
 * `platform`, when one of `keys` signed it and it has that platform. */
gboolean update_read(const char *text, gsize len, const char *sig_hex, const uint8_t (*keys)[32], int nkeys,
                     const char *platform, UpdateOffer *out);
/* Whether build `offered` (AAAAMMDD.HHMM) is later than `have`. */
gboolean update_newer(const char *have, const char *offered);
/* Whether the file is the package the offer names: its size and hash. */
gboolean update_file_ok(const char *path, const UpdateOffer *o);
/* BLAKE2b-256 of a file; FALSE when it cannot be read. */
gboolean update_hash_file(const char *path, uint8_t hash[32], gint64 *size);
/* Hex to bytes: FALSE unless `hex` is exactly 2 × n digits. */
gboolean update_unhex(const char *hex, uint8_t *out, gsize n);

#endif
