/* What lishogi accepts when a game is asked of it, from its own code (../lishogi):
 * modules/setup (Config.scala, Mappings.scala, FormFactory.scala, AiConfig.scala), ui/lobby/src/setup
 * (util.ts and ctrl.ts: the values offered and the rules the page applies before sending) and
 * modules/game (Game.scala: when a game may be rated, a seek's shortest clock). */

#include "online_setup.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const ONLINE_OPPONENTS[ONLINE_COUNT] = {"Anyone", "Lishogi AI", "A Lishogi user", "A friend, by link",
                                                    "Wait for a challenge", "Accept an invitation link",
                                                    "Continue a game in progress"};
const char *const ONLINE_TIME_MODES[ONLINE_TIME_MODE_COUNT] = {"Real time", "Correspondence", "Unlimited"};

/* util.ts: ¼, ½, ¾, 1, 1.5, 2 to 20, then 25 to 45 by 5, 60 to 180 by 15 minutes. */
const int ONLINE_LIMITS[] = {0, 15, 30, 45, 60, 90, 120, 180, 240, 300, 360, 420, 480, 540, 600, 660, 720, 780,
                             840, 900, 960, 1020, 1080, 1140, 1200, 1500, 1800, 2100, 2400, 2700, 3600, 4500,
                             5400, 6300, 7200, 8100, 9000, 9900, 10800};
const int ONLINE_NLIMITS = sizeof ONLINE_LIMITS / sizeof ONLINE_LIMITS[0];
const int ONLINE_SECONDS[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
                              25, 30, 35, 40, 45, 60, 90, 120, 150, 180};
const int ONLINE_NSECONDS = sizeof ONLINE_SECONDS / sizeof ONLINE_SECONDS[0];
const int ONLINE_DAYS[] = {1, 2, 3, 5, 7, 10, 14};
const int ONLINE_NDAYS = sizeof ONLINE_DAYS / sizeof ONLINE_DAYS[0];

/* The variants of lishogi that we have (it also has Chu and Annan shogi; not Goro goro nor Tori). */
bool online_has_variant(int variant)
{
    return variant == V_STANDARD || variant == V_MINISHOGI || variant == V_KYOTOSHOGI || variant == V_CHECKSHOGI ||
           variant == V_DOBUTSU;
}

static const char *online_variant_key(int variant)
{
    static const char *KEYS[V_COUNT] = {[V_STANDARD] = "standard", [V_MINISHOGI] = "minishogi",
                                        [V_KYOTOSHOGI] = "kyotoshogi", [V_CHECKSHOGI] = "checkshogi",
                                        [V_DOBUTSU] = "dobutsu"};
    return variant >= 0 && variant < V_COUNT && KEYS[variant] ? KEYS[variant] : "standard";
}

int online_variant_from_key(const char *key)
{
    for (int v = 0; v < V_COUNT; v++)
        if (online_has_variant(v) && !strcmp(key, online_variant_key(v)))
            return v;
    return *key ? -1 : V_STANDARD;
}

/* Byoyomi periods count only with a byoyomi (the page sends 1 without one). */
static int periods(const OnlineSetup *s) { return s->byoyomi_s ? s->periods : 1; }

/* scalashogi's Clock.estimateTotalSeconds: what the server measures a clock by. */
static int estimate(const OnlineSetup *s)
{
    return s->limit_s + 60 * s->increment_s + 25 * (s->byoyomi_s ? periods(s) : 0) * s->byoyomi_s;
}

bool online_can_rate(const OnlineSetup *s)
{
    if (s->who != ONLINE_SEEK && s->who != ONLINE_USER && s->who != ONLINE_LINK)
        return false;
    if (s->handicap != HANDICAP_NONE || s->time_mode == ONLINE_UNLIMITED)
        return false;
    return s->time_mode != ONLINE_REAL_TIME || (periods(s) <= 1 && !(s->increment_s && s->byoyomi_s));
}

const char *online_setup_problem(const OnlineSetup *s)
{
    if (s->who == ONLINE_WAIT || s->who == ONLINE_ACCEPT || s->who == ONLINE_RESUME)
        return NULL; /* the challenger chose, or the game is on */
    if (!online_has_variant(s->variant))
        return "Lishogi does not have this variant.";
    if (s->time_mode != ONLINE_REAL_TIME)
        return s->who == ONLINE_SEEK ? "Looking for an opponent on Lishogi is in real time only." : NULL;
    if (!s->limit_s && !s->increment_s && !s->byoyomi_s)
        return "A game in real time on Lishogi needs a clock.";
    if (s->who == ONLINE_SEEK && estimate(s) < (s->rated && online_can_rate(s) ? 900 : 300))
        return "Too short a clock to look for an opponent on Lishogi: at least 5 minutes, or 15 for a rated game "
               "(each second of increment counts as a minute, of byoyomi as 25 seconds per period).";
    if (s->who == ONLINE_AI && s->variant != V_STANDARD && s->limit_s < 60 && s->byoyomi_s < 10 && s->increment_s < 5)
        return "Against the Lishogi AI in this variant the clock needs at least 1 minute, a 10 s byoyomi or a 5 s "
               "increment.";
    return NULL;
}

const char *online_perf_key(const OnlineSetup *s)
{
    if (s->variant != V_STANDARD)
        return online_variant_key(s->variant);
    return s->time_mode == ONLINE_REAL_TIME ? "realTime" : "correspondence";
}

/* A form value, percent-encoded. */
static int put_escaped(char *out, int size, const char *s)
{
    int n = 0;
    for (; *s; s++) {
        unsigned char c = (unsigned char) *s;
        if (isalnum(c) || strchr("-_.~/", c))
            n += snprintf(out + n, n < size ? size - n : 0, "%c", c);
        else
            n += snprintf(out + n, n < size ? size - n : 0, "%%%02X", c);
    }
    return n;
}

char *online_setup_form(const OnlineSetup *s, int rating)
{
    int size = 512;
    char *out = malloc(size);
    int n = 0;
#define PUT(...) (n += snprintf(out + n, n < size ? size - n : 0, __VA_ARGS__))
    bool rated = s->rated && online_can_rate(s);
    /* The page lets a rated seek only be random. */
    int side = rated && s->who == ONLINE_SEEK ? 2 : s->side;
    const char *color = side == 0 ? "sente" : side == 1 ? "gote" : "random";
    PUT("variant=%s&color=%s", online_variant_key(s->variant), color);
    if (s->who == ONLINE_SEEK) {
        PUT("&time=%g&increment=%d&byoyomi=%d&periods=%d&rated=%s", s->limit_s / 60.0, s->increment_s, s->byoyomi_s,
            periods(s), rated ? "true" : "false");
        if (rating > 0) {
            int lo = rating - s->range_below, hi = rating + s->range_above;
            PUT("&ratingRange=%d-%d", lo < 400 ? 400 : lo, hi > 4000 ? 4000 : hi);
        }
    } else {
        if (s->time_mode == ONLINE_REAL_TIME)
            PUT("&clock.limit=%d&clock.increment=%d&clock.byoyomi=%d&clock.periods=%d", s->limit_s, s->increment_s,
                s->byoyomi_s, periods(s));
        else if (s->time_mode == ONLINE_CORRESPONDENCE)
            PUT("&days=%d", s->days);
        if (s->who == ONLINE_AI)
            PUT("&level=%d", s->level < 1 ? 1 : s->level > ONLINE_LEVELS ? ONLINE_LEVELS : s->level);
        else
            PUT("&rated=%s", rated ? "true" : "false");
        if (s->variant == V_STANDARD && s->handicap != HANDICAP_NONE) {
            PUT("&sfen=");
            n += put_escaped(out + n, n < size ? size - n : 0, handicap_sfen(s->handicap));
        }
    }
#undef PUT
    if (n >= size) /* never with these values; cut rather than overflow */
        out[size - 1] = 0;
    return out;
}
