#ifndef ZIP_H
#define ZIP_H

#include <glib.h>

/* Reading a zip archive (stored or deflated files), for a piece set downloaded as one and the Windows
 * package of an update. No GTK. */

typedef struct Zip Zip;

/* NULL when the file is not a zip archive. */
Zip *zip_open(const char *path);
/* The file whose name, without its folders, is `name` (ASCII case ignored); NULL if there is none
 * or it cannot be read. g_byte_array_unref() it. */
GByteArray *zip_get(Zip *z, const char *name);
/* Every file into `dir`, folders kept; FALSE, with nothing written, if a name would leave `dir`
 * (absolute, "..", a drive), else FALSE when a file cannot be read or written. */
gboolean zip_extract(Zip *z, const char *dir);
void zip_close(Zip *z);

#endif
