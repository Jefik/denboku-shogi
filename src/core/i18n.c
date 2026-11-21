/* Languages: English is the text in the code; the others are rows of LANGUAGES, each with the
 * app's translations (lang_<code>.c) and the game's (src/<game>/strings_<code>.c, found by code). */

#include "i18n.h"

#include <stdlib.h>
#include <string.h>

extern const char *const STRINGS_JA[][2], *const STRINGS_PT[][2];

const LanguageInfo LANGUAGES[] = {
    {"en", "English", NULL},
    {"ja", "日本語", STRINGS_JA},
    {"pt", "Português (Brasil)", STRINGS_PT},
};
const int LANGUAGE_COUNT = sizeof LANGUAGES / sizeof LANGUAGES[0];

static Language language;

void i18n_set_language(Language l) { language = l >= 0 && l < LANGUAGE_COUNT ? l : LANG_EN; }
Language i18n_language(void) { return language; }
bool i18n_japanese(void) { return !strcmp(LANGUAGES[language].code, "ja"); }

const char *i18n_code(Language l) { return LANGUAGES[l >= 0 && l < LANGUAGE_COUNT ? l : LANG_EN].code; }

/* "pt_BR.UTF-8" is pt; a language with a regional code ("zh_TW") wants that region. */
int i18n_of_locale(const char *name)
{
    if (!name)
        return -1;
    for (int pass = 0; pass < 2; pass++)
        for (int l = 0; l < LANGUAGE_COUNT; l++) {
            const char *code = LANGUAGES[l].code;
            size_t n = strlen(code);
            if (pass == 0 ? n > 2 && !strncmp(name, code, n) : n == 2 && !strncmp(name, code, 2))
                if (!name[n] || strchr("_.-@", name[n]))
                    return l;
        }
    return -1;
}

static const char *const (*game_strings(Language l))[2]
{
    for (int i = 0; GAME_LANGUAGES[i].code; i++)
        if (!strcmp(GAME_LANGUAGES[i].code, LANGUAGES[l].code))
            return GAME_LANGUAGES[i].strings;
    return NULL;
}

/* Each language's rows sorted by their English, made at its first use: tr() runs many times a
 * redraw, and a scan of some 500 rows is slow on the Kindle. The app's row wins over the game's. */
typedef struct {
    const char *english, *text;
    int order;
} Row;

static int by_english(const void *a, const void *b)
{
    const Row *x = a, *y = b;
    int c = strcmp(x->english, y->english);
    return c ? c : x->order - y->order;
}

static Row *index_of(Language l, int *count)
{
    static Row *rows[16];
    static int counts[16];
    if (l < 0 || l >= 16)
        return NULL;
    if (!rows[l]) {
        const char *const (*tables[2])[2] = {LANGUAGES[l].strings, game_strings(l)};
        int n = 0;
        for (int t = 0; t < 2; t++)
            for (int i = 0; tables[t] && tables[t][i][0]; i++)
                n++;
        Row *r = malloc((n ? n : 1) * sizeof *r);
        if (!r)
            return NULL;
        n = 0;
        for (int t = 0; t < 2; t++)
            for (int i = 0; tables[t] && tables[t][i][0]; i++, n++)
                r[n] = (Row) {tables[t][i][0], tables[t][i][1], n};
        qsort(r, n, sizeof *r, by_english);
        int kept = 0; /* one row per English, the first (the app's) */
        for (int i = 0; i < n; i++)
            if (!kept || strcmp(r[kept - 1].english, r[i].english))
                r[kept++] = r[i];
        rows[l] = r, counts[l] = kept;
    }
    *count = counts[l];
    return rows[l];
}

static int find_english(const void *key, const void *row) { return strcmp(key, ((const Row *) row)->english); }

/* The app's strings, then the game's. */
const char *tr(const char *english)
{
    if (language == LANG_EN || !english)
        return english;
    int n;
    Row *rows = index_of(language, &n);
    const Row *r = rows ? bsearch(english, rows, n, sizeof *rows, find_english) : NULL;
    return r ? r->text : english;
}
