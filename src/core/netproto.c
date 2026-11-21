#include "netproto.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* `text` into out->buf: `max` bytes at most, cut at a whole character and at anything that is
 * not text. */
static void clean_text(NetLine *out, const char *text, size_t max)
{
    const char *end;
    g_strlcpy(out->buf, text, MIN(max, sizeof out->buf - 1) + 1);
    g_utf8_validate(out->buf, -1, &end);
    out->buf[end - out->buf] = 0;
    out->text = out->buf;
}

static long clamp_ms(const char *s)
{
    gint64 v = g_ascii_strtoll(s, NULL, 10);
    return (long) CLAMP(v, -NET_MAX_MS, NET_MAX_MS);
}

char *net_room_text(const NetRoom *r)
{
    return g_strdup_printf("%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d", r->name, r->password, r->variant, r->clock, r->main_s,
                           r->byoyomi_s, r->increment_s, r->setup, r->handicap);
}

int net_room_parse(const char *text, const NetLimits *limits, NetRoom *r)
{
    memset(r, 0, sizeof *r);
    char **f = g_strsplit(text, "\t", 10);
    NetLine name;
    clean_text(&name, f[0] ? f[0] : "", NET_NAME_BYTES);
    g_strlcpy(r->name, name.buf, sizeof r->name);
    int whole = g_strv_length(f) == 9;
    if (whole) {
        int n[8];
        for (int i = 0; i < 8; i++)
            n[i] = (int) CLAMP(g_ascii_strtoll(f[i + 1], NULL, 10), 0, NET_MAX_MS / 1000);
        r->password = !!n[0], r->variant = MIN(n[1], limits->variant), r->clock = MIN(n[2], limits->clock);
        r->main_s = n[3], r->byoyomi_s = n[4], r->increment_s = n[5], r->setup = !!n[6];
        r->handicap = MIN(n[7], limits->handicap);
    }
    g_strfreev(f);
    return whole;
}

int net_moves_fields(const char *fields, long ms[3])
{
    int count = 0;
    ms[0] = ms[1] = ms[2] = 0;
    for (const char *p = fields; *p; count++) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        if (count < 3)
            ms[count] = clamp_ms(p);
        while (*p && *p != ' ')
            p++;
    }
    ms[2] = MAX(0, ms[2]);
    return count;
}

void net_parse(const char *line, const NetLimits *limits, NetLine *out)
{
    static const struct { const char *word; NetLineKind kind; } WORDS[] = {
        {"DENIED", NL_DENIED}, {"KICKED", NL_KICKED}, {"BANNED", NL_BANNED},
        {"BYE", NL_BYE}, {"PING", NL_PING}, {"TAKEBACK", NL_TAKEBACK}, {"REMATCH", NL_REMATCH}, {"%TORYO", NL_RESIGN},
        {"%TIME_UP", NL_TIME_UP}};
    int a, b, c, d = 0;
    out->kind = NL_MOVE;
    out->text = line;
    out->buf[0] = 0;
    memset(out->n, 0, sizeof out->n);
    memset(out->ms, 0, sizeof out->ms);
    for (size_t i = 0; i < G_N_ELEMENTS(WORDS); i++)
        if (!strcmp(line, WORDS[i].word)) {
            out->kind = WORDS[i].kind;
            return;
        }
    if (sscanf(line, "DENBOKU %d", &a) == 1) {
        out->kind = NL_VERSION;
        out->n[0] = CLAMP(a, 1, 99);
    } else if (g_str_has_prefix(line, "NAME ")) {
        out->kind = NL_NAME;
        clean_text(out, line + 5, NET_NAME_BYTES);
    } else if (sscanf(line, "GAME %d %d %d %d", &a, &b, &c, &d) == 4) {
        out->kind = NL_GAME;
        out->n[0] = CLAMP(a, 0, limits->handicap);
        out->n[1] = CLAMP(b, 0, limits->clock);
        out->n[2] = c != 0;
        out->n[3] = CLAMP(d, 0, 5);
    } else if (sscanf(line, "CLOCK %d %d %d", &a, &b, &c) == 3) {
        /* Within what New Game offers. */
        out->kind = NL_CLOCK;
        out->n[0] = CLAMP(a, 0, 180 * 60);
        out->n[1] = CLAMP(b, 0, 300);
        out->n[2] = CLAMP(c, 0, 60);
    } else if (sscanf(line, "VARIANT %d", &a) == 1) {
        out->kind = NL_VARIANT;
        out->n[0] = CLAMP(a, 0, limits->variant);
    } else if (!strncmp(line, "SFEN", 4) && (!line[4] || line[4] == ' ')) {
        out->kind = NL_SFEN;
        out->text = line[4] && line[5] ? line + 5 : NULL;
    } else if (g_str_has_prefix(line, "READY ")) {
        out->kind = NL_READY;
        out->n[0] = atoi(line + 6) != 0;
    } else if (g_str_has_prefix(line, "TAKEBACK ")) {
        out->kind = NL_TAKEBACK_ANSWER;
        out->n[0] = atoi(line + 9) != 0;
    } else if (g_str_has_prefix(line, "CHAT ")) {
        out->kind = NL_CHAT;
        clean_text(out, line + 5, NET_CHAT_BYTES);
    } else if (g_str_has_prefix(line, "MOVES ")) {
        out->kind = NL_MOVES;
        out->text = line + 6;
        out->n[0] = net_moves_fields(out->text, out->ms);
    } else if (g_str_has_prefix(line, "%ILLEGAL")) {
        out->kind = NL_ILLEGAL;
        out->text = line[8] == ' ' ? line + 9 : NULL;
    }
}
