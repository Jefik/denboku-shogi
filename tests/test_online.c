/* The choices of a lishogi game (lishogi_setup.c) against the rules of lishogi's own code. */
#include "online_setup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("%s:%d: %s\n", __FILE__, __LINE__, #cond);                                                          \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

static OnlineSetup base(OnlineOpponent who)
{
    return (OnlineSetup) {.who = who, .variant = V_STANDARD, .level = 3, .side = 0, .handicap = HANDICAP_NONE,
                          .time_mode = ONLINE_REAL_TIME, .limit_s = 600, .byoyomi_s = 10, .periods = 1, .days = 2,
                          .range_below = 500, .range_above = 500};
}

static bool has(const char *form, const char *part) { return strstr(form, part) != NULL; }

int main(void)
{
    rules_set_variant(V_STANDARD);
    /* The lists: the API takes a limit of 0, 15, 30, 45, 60, 90 s or whole minutes from 2 to 180. */
    for (int i = 0; i < ONLINE_NLIMITS; i++) {
        int l = ONLINE_LIMITS[i];
        CHECK(l <= 90 ? (l % 15 == 0 && l != 75) : (l % 60 == 0 && l >= 120 && l <= 10800));
        CHECK(!i || l > ONLINE_LIMITS[i - 1]);
    }
    CHECK(ONLINE_SECONDS[ONLINE_NSECONDS - 1] == 180 && ONLINE_DAYS[ONLINE_NDAYS - 1] == 14);

    /* Variants: lishogi's keys, none for those it lacks. */
    CHECK(online_has_variant(V_DOBUTSU) && !online_has_variant(V_TORI) && !online_has_variant(V_GOROGORO));
    CHECK(online_variant_from_key("kyotoshogi") == V_KYOTOSHOGI && online_variant_from_key("chushogi") == -1);
    OnlineSetup s = base(ONLINE_SEEK);
    s.variant = V_TORI;
    CHECK(online_setup_problem(&s) != NULL);

    /* Rated: not with a handicap, two periods, increment and byoyomi together, nor against the AI. */
    s = base(ONLINE_USER);
    CHECK(online_can_rate(&s));
    s.handicap = HANDICAP_ROOK;
    CHECK(!online_can_rate(&s));
    s = base(ONLINE_USER), s.periods = 2;
    CHECK(!online_can_rate(&s));
    s.byoyomi_s = 0; /* periods count only with a byoyomi */
    CHECK(online_can_rate(&s));
    s = base(ONLINE_USER), s.increment_s = 5;
    CHECK(!online_can_rate(&s));
    s = base(ONLINE_AI);
    CHECK(!online_can_rate(&s));
    s = base(ONLINE_USER), s.time_mode = ONLINE_UNLIMITED;
    CHECK(!online_can_rate(&s) && !online_setup_problem(&s));

    /* A clock: some time in real time; a seek's shortest by lishogi's estimate (5 min, 15 rated). */
    s = base(ONLINE_USER), s.limit_s = s.byoyomi_s = 0;
    CHECK(online_setup_problem(&s) != NULL);
    s = base(ONLINE_SEEK), s.limit_s = 180, s.byoyomi_s = 5; /* 180 + 125 */
    CHECK(!online_setup_problem(&s));
    s.rated = true;
    CHECK(online_setup_problem(&s) != NULL);
    s.limit_s = 600, s.byoyomi_s = 30, s.periods = 1; /* 600 + 750 */
    CHECK(!online_setup_problem(&s));
    s = base(ONLINE_SEEK), s.time_mode = ONLINE_CORRESPONDENCE;
    CHECK(online_setup_problem(&s) != NULL);

    /* The AI in a variant: 1 minute, a 10 s byoyomi or a 5 s increment. */
    s = base(ONLINE_AI), s.variant = V_MINISHOGI, s.limit_s = 30, s.byoyomi_s = 5;
    CHECK(online_setup_problem(&s) != NULL);
    s.byoyomi_s = 10;
    CHECK(!online_setup_problem(&s));

    /* The forms. */
    char *f = online_setup_form(&(OnlineSetup) {.who = ONLINE_SEEK, .variant = V_MINISHOGI, .side = 0, .rated = true,
                                                .time_mode = ONLINE_REAL_TIME, .limit_s = 900, .byoyomi_s = 0,
                                                .increment_s = 0, .periods = 3, .range_below = 200, .range_above = 750},
                                1500);
    CHECK(has(f, "variant=minishogi") && has(f, "time=15&") && has(f, "periods=1") && has(f, "rated=true"));
    CHECK(has(f, "color=random")); /* a rated seek is random */
    CHECK(has(f, "ratingRange=1300-2250"));
    free(f);
    s = base(ONLINE_AI), s.handicap = HANDICAP_BISHOP, s.time_mode = ONLINE_CORRESPONDENCE, s.days = 7, s.level = 12;
    f = online_setup_form(&s, 0);
    CHECK(has(f, "days=7") && !has(f, "clock.") && has(f, "level=10") && has(f, "sfen=") && !has(f, "rated"));
    CHECK(!strchr(strstr(f, "sfen="), ' '));
    free(f);
    s = base(ONLINE_LINK), s.time_mode = ONLINE_UNLIMITED;
    f = online_setup_form(&s, 0);
    CHECK(!has(f, "clock.") && !has(f, "days") && has(f, "rated=false"));
    free(f);
    s = base(ONLINE_USER), s.limit_s = 45, s.byoyomi_s = 30, s.periods = 2;
    f = online_setup_form(&s, 0);
    CHECK(has(f, "clock.limit=45&clock.increment=0&clock.byoyomi=30&clock.periods=2"));
    free(f);

    if (failures)
        return 1;
    puts("online tests passed");
    return 0;
}
