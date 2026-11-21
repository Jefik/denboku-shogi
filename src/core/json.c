#include "json.h"

#include <stdlib.h>
#include <string.h>

static const char *ws(const char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
        s++;
    return s;
}

/* Skips one value; NULL on malformed input, or nested deeper than anything the game reads. */
static const char *skip_at(const char *s, int depth)
{
    s = ws(s);
    if (*s == '"') {
        for (s++; *s && *s != '"'; s++)
            if (*s == '\\' && s[1])
                s++;
        return *s ? s + 1 : NULL;
    }
    if (*s == '{' || *s == '[') {
        if (depth > 64)
            return NULL;
        char close = *s == '{' ? '}' : ']';
        s = ws(s + 1);
        if (*s == close)
            return s + 1;
        for (;;) {
            if (close == '}') {
                s = skip_at(s, depth + 1); /* key */
                if (!s || *(s = ws(s)) != ':')
                    return NULL;
                s++;
            }
            if (!(s = skip_at(s, depth + 1)))
                return NULL;
            s = ws(s);
            if (*s == ',')
                s++;
            else if (*s == close)
                return s + 1;
            else
                return NULL;
        }
    }
    while (*s && !strchr(",}] \t\r\n", *s))
        s++;
    return s;
}

static const char *skip(const char *s) { return skip_at(s, 0); }

static void put_utf8(char **o, char *end, unsigned c)
{
    char b[4];
    int n = c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
    if (n == 1)
        b[0] = (char) c;
    else {
        for (int i = n - 1; i > 0; i--, c >>= 6)
            b[i] = (char) (0x80 | (c & 0x3f));
        b[0] = (char) ((n == 2 ? 0xc0 : n == 3 ? 0xe0 : 0xf0) | c);
    }
    for (int i = 0; i < n && *o < end; i++)
        *(*o)++ = b[i];
}

static void copy_value(const char *s, const char *e, char *out, size_t size)
{
    char *o = out, *end = out + size - 1;
    if (*s != '"') {
        size_t n = (size_t) (e - s) < size - 1 ? (size_t) (e - s) : size - 1;
        memcpy(out, s, n);
        out[n] = 0;
        return;
    }
    for (s++; s < e - 1 && o < end; s++) {
        if (*s != '\\') {
            *o++ = *s;
            continue;
        }
        s++;
        switch (*s) {
        case 'n': *o++ = '\n'; break;
        case 't': *o++ = '\t'; break;
        case 'r': *o++ = '\r'; break;
        case 'b': *o++ = '\b'; break;
        case 'f': *o++ = '\f'; break;
        case 'u': { /* four digits, inside the value (a line cut short ends it) */
            if (s + 5 > e - 1)
                return (void) (*o = 0);
            char hex[5] = {0};
            memcpy(hex, s + 1, 4);
            unsigned c = (unsigned) strtoul(hex, NULL, 16);
            s += 4;
            if (c >= 0xd800 && c < 0xdc00 && s + 7 <= e - 1 && s[1] == '\\' && s[2] == 'u') {
                memcpy(hex, s + 3, 4);
                unsigned low = (unsigned) strtoul(hex, NULL, 16);
                if (low >= 0xdc00 && low < 0xe000) {
                    c = 0x10000 + ((c - 0xd800) << 10) + (low - 0xdc00);
                    s += 6;
                }
            }
            if (c >= 0xd800 && c < 0xe000)
                c = 0xfffd; /* a half of a pair alone */
            put_utf8(&o, end, c);
            break;
        }
        default: *o++ = *s;
        }
    }
    *o = 0;
}

bool json_get(const char *json, const char *path, char *out, size_t size)
{
    const char *s = ws(json);
    while (*path) {
        const char *dot = strchr(path, '.');
        size_t klen = dot ? (size_t) (dot - path) : strlen(path);
        if (*s == '[') { /* a number in the path: that element of a list ("0.text") */
            char *num_end;
            long n = strtol(path, &num_end, 10);
            if (num_end != path + klen || n < 0)
                return false;
            s = ws(s + 1);
            for (; n > 0 && *s && *s != ']'; n--) {
                if (!(s = skip(s)))
                    return false;
                s = ws(s);
                if (*s != ',')
                    return false;
                s = ws(s + 1);
            }
            if (!*s || *s == ']')
                return false;
            path += klen + (dot ? 1 : 0);
            continue;
        }
        if (*s != '{')
            return false;
        s = ws(s + 1);
        bool found = false;
        while (*s == '"') {
            const char *k = s + 1, *kend = skip(s);
            if (!kend)
                return false;
            s = ws(kend);
            if (*s != ':')
                return false;
            s = ws(s + 1);
            if ((size_t) (kend - 1 - k) == klen && !strncmp(k, path, klen)) {
                found = true;
                break;
            }
            if (!(s = skip(s)))
                return false;
            s = ws(s);
            if (*s == ',')
                s = ws(s + 1);
        }
        if (!found)
            return false;
        path += klen + (dot ? 1 : 0);
    }
    const char *e = skip(s);
    if (!e || !strncmp(s, "null", 4))
        return false;
    copy_value(s, e, out, size);
    return true;
}

long json_long(const char *json, const char *path, long fallback)
{
    char v[32];
    return json_get(json, path, v, sizeof v) ? strtol(v, NULL, 10) : fallback;
}
