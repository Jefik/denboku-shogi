#ifndef HTTP_H
#define HTTP_H

#include <glib.h>

/* HTTPS requests through the curl program (the Kindle, Linux and Windows 10 all have it), read
 * line by line, so long-lived NDJSON streams work too. */
typedef struct Http Http;
typedef void (*HttpLine)(const char *line, void *data);
/* `status` is the HTTP status, or 0 when curl could not run or connect or the answer broke off. */
typedef void (*HttpDone)(int status, void *data);

/* `form` is an x-www-form-urlencoded body for POST, or NULL. */
Http *http_start(const char *method, const char *url, const char *token, const char *form, HttpLine line,
                 HttpDone done, void *data);
/* How long (µs) since the request last sent anything: a stream that sends keep-alive lines and has
 * been quiet longer is a connection that died without closing. */
gint64 http_quiet_us(const Http *h);
/* Stops a request; its callbacks are not called again. */
void http_cancel(Http *h);
/* Saves `url` (redirects followed, HTTPS only) to `path`; `done` gets the HTTP status (200: saved), 0
 * when curl could not run or connect. `insecure_ok` allows plain HTTP, for a test server. */
Http *http_download(const char *url, const char *path, gboolean insecure_ok, HttpDone done, void *data);
/* Escapes a value for a form or URL. */
char *http_escape(const char *s);

#endif
