#ifndef ONLINE_SETUP_H
#define ONLINE_SETUP_H

/* The choices of a game asked of the online server, as New Game offers them: the server's own
 * values and rules (lishogi's setup form and API, lishogi_setup.c). No GTK, for the tests. */

#include <stdbool.h>

#include "rules.h"

typedef enum {
    ONLINE_SEEK, ONLINE_AI, ONLINE_USER, ONLINE_LINK, ONLINE_WAIT, ONLINE_ACCEPT, ONLINE_RESUME, ONLINE_COUNT
} OnlineOpponent;
extern const char *const ONLINE_OPPONENTS[ONLINE_COUNT];

typedef enum { ONLINE_REAL_TIME, ONLINE_CORRESPONDENCE, ONLINE_UNLIMITED, ONLINE_TIME_MODE_COUNT } OnlineTimeMode;
extern const char *const ONLINE_TIME_MODES[ONLINE_TIME_MODE_COUNT];

typedef struct {
    OnlineOpponent who;
    int variant;
    int level; /* the server's AI, 1 to ONLINE_LEVELS */
    int side;  /* 0 first, 1 second, 2 random */
    Handicap handicap;
    bool rated;
    OnlineTimeMode time_mode;
    int limit_s, byoyomi_s, increment_s, periods; /* real time */
    int days;                                     /* correspondence: per move */
    int range_below, range_above;                 /* a seek's opponents: rating points from ours */
} OnlineSetup;

#define ONLINE_LEVELS 10
#define ONLINE_PERIODS 5
#define ONLINE_RANGE_MAX 750
#define ONLINE_RANGE_STEP 50
/* The values offered, as on the server's own form. */
extern const int ONLINE_LIMITS[]; /* seconds */
extern const int ONLINE_NLIMITS;
extern const int ONLINE_SECONDS[]; /* byoyomi and increment */
extern const int ONLINE_NSECONDS;
extern const int ONLINE_DAYS[];
extern const int ONLINE_NDAYS;

bool online_has_variant(int variant);
/* The server's name of a variant ("kyotoshogi"), and back (-1 for one we lack). */
int online_variant_from_key(const char *key);
/* The opponent can be asked for a rated game, and with these settings it would stay rated. */
bool online_can_rate(const OnlineSetup *s);
/* Why the server would refuse these settings (English, the key of a translation), or NULL. */
const char *online_setup_problem(const OnlineSetup *s);
/* The server's perf a seek is rated in ("realTime", "minishogi"…), for our rating there. */
const char *online_perf_key(const OnlineSetup *s);
/* The form of the request that starts the game (a seek, the AI, a challenge); `rating` is ours
 * in that perf, 0 if unknown (the seek then has no rating range). Freed by the caller. */
char *online_setup_form(const OnlineSetup *s, int rating);

#endif
