#include "mdns.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>

#define SERVICE "_denboku._udp"
enum { T_PTR = 12, T_TXT = 16, T_SRV = 33 };

typedef struct { unsigned char *p; int size, n; } Out;

static void put(Out *o, const void *data, int len)
{
    if (o->n + len <= o->size)
        memcpy(o->p + o->n, data, len);
    o->n += len;
}
static void put16(Out *o, unsigned v) { unsigned char b[2] = {v >> 8, v}; put(o, b, 2); }
static void put32(Out *o, unsigned v) { put16(o, v >> 16); put16(o, v & 0xffff); }
static void label(Out *o, const char *s) { unsigned char l = (unsigned char) strlen(s); put(o, &l, 1); put(o, s, l); }
static void end_name(Out *o) { unsigned char z = 0; put(o, &z, 1); }

/* The names, uncompressed: the instance's label (if any) and "_denboku._udp.local". */
static void name_of(Out *o, const char *inst)
{
    if (inst)
        label(o, inst);
    label(o, "_denboku"), label(o, "_udp"), label(o, "local"), end_name(o);
}

/* A record header; returns where its data length sits, to fix once the data is written. */
static int record(Out *o, const char *inst, unsigned type)
{
    name_of(o, inst);
    put16(o, type);
    put16(o, 1);
    put32(o, 120);
    put16(o, 0);
    return o->n - 2;
}
static void fix_length(Out *o, int at)
{
    int len = o->n - at - 2;
    if (o->n <= o->size) {
        o->p[at] = len >> 8;
        o->p[at + 1] = len;
    }
}
/* At most MDNS_NAME_BYTES of `s`, whole UTF-8 characters. */
static void copy_text(char *to, const char *s, int len)
{
    len = MIN(len, MDNS_NAME_BYTES);
    memcpy(to, s, len);
    to[len] = 0;
    const char *bad;
    if (!g_utf8_validate(to, -1, &bad))
        to[bad - to] = 0;
}

int mdns_announce(unsigned char *out, int size, unsigned id, int port, const char *name)
{
    Out o = {out, size, 0};
    char inst[24];
    snprintf(inst, sizeof inst, "denboku-%04x", id & 0xffff);
    char text[MDNS_NAME_BYTES + 1];
    copy_text(text, name, (int) strlen(name));
    put16(&o, 0);
    put16(&o, 0x8400); /* a response, authoritative */
    put16(&o, 0), put16(&o, 3), put16(&o, 0), put16(&o, 0);

    int at = record(&o, NULL, T_PTR);
    name_of(&o, inst);
    fix_length(&o, at);

    at = record(&o, inst, T_SRV);
    put16(&o, 0), put16(&o, 0), put16(&o, port);
    label(&o, inst), label(&o, "local"), end_name(&o);
    fix_length(&o, at);

    at = record(&o, inst, T_TXT);
    char entry[MDNS_NAME_BYTES + 8];
    snprintf(entry, sizeof entry, "name=%s", text);
    label(&o, entry);
    fix_length(&o, at);
    return o.n <= size ? o.n : 0;
}

/* ---- reading ---- */

typedef struct { const unsigned char *p; int len; } In;

static int get16(const In *in, int at) { return at + 2 <= in->len ? in->p[at] << 8 | in->p[at + 1] : -1; }

/* The name at `at` as dotted text (pointers followed, at most a few); returns where it ends in
 * the packet (after its first pointer or its end), or -1. */
static int read_name(const In *in, int at, char *out, int out_size)
{
    int end = -1, jumps = 0, o = 0;
    for (;;) {
        if (at < 0 || at >= in->len)
            return -1;
        int l = in->p[at];
        if (l == 0) {
            at++;
            break;
        }
        if ((l & 0xc0) == 0xc0) {
            if (at + 2 > in->len || ++jumps > 8)
                return -1;
            if (end < 0)
                end = at + 2;
            at = (l & 0x3f) << 8 | in->p[at + 1];
            continue;
        }
        if (l > 63 || at + 1 + l > in->len || o + l + 2 > out_size)
            return -1;
        if (o)
            out[o++] = '.';
        memcpy(out + o, in->p + at + 1, l);
        o += l;
        at += 1 + l;
    }
    out[o] = 0;
    return end < 0 ? at : end;
}

int mdns_parse(const unsigned char *pkt, int len, int *port, char name[MDNS_NAME_BYTES + 1])
{
    In in = {pkt, len};
    if (len < 12 || !(pkt[2] & 0x80))
        return 0;
    int questions = get16(&in, 4), records = get16(&in, 6) + get16(&in, 8) + get16(&in, 10), at = 12;
    char buf[300];
    for (int i = 0; i < questions; i++) {
        at = read_name(&in, at, buf, sizeof buf);
        if (at < 0)
            return 0;
        at += 4;
    }
    int got_port = 0, got_name = 0;
    for (int i = 0; i < records && at >= 0 && at + 10 <= len; i++) {
        char rname[300];
        at = read_name(&in, at, rname, sizeof rname);
        if (at < 0 || at + 10 > len)
            return 0;
        int type = get16(&in, at), rdlen = get16(&in, at + 8), data = at + 10;
        if (data + rdlen > len)
            return 0;
        at = data + rdlen;
        gboolean ours = g_str_has_suffix(rname, "." SERVICE ".local");
        if (type == T_SRV && ours && rdlen >= 6) {
            *port = get16(&in, data + 4);
            got_port = *port > 0;
        } else if (type == T_TXT && ours && rdlen >= 1 && !got_name) {
            int l = pkt[data];
            if (l + 1 <= rdlen && l >= 5 && !memcmp(pkt + data + 1, "name=", 5)) {
                copy_text(name, (const char *) pkt + data + 6, l - 5);
                got_name = 1;
            }
        }
    }
    if (got_port && !got_name)
        name[0] = 0;
    return got_port;
}
