#ifndef I18N_H
#define I18N_H

#include <stdbool.h>

/* UI strings are written in English; tr() returns them in the language set. A language is a row
 * of LANGUAGES (i18n.c): its code (the setting's value and the [xx] keys of the style files), its
 * name in itself, and the app's translations, English text then translation, ending in
 * {NULL, NULL} (lang_<code>.c). The game's translations are GAME_LANGUAGES, by code. */
typedef int Language; /* a row of LANGUAGES */
enum { LANG_EN = 0 };
typedef struct {
    const char *code, *name;
    const char *const (*strings)[2];
} LanguageInfo;
extern const LanguageInfo LANGUAGES[];
extern const int LANGUAGE_COUNT;
/* The game's own strings in each language (src/<game>/strings.c), looked up after the app's;
 * the list ends with a NULL code. */
typedef struct {
    const char *code;
    const char *const (*strings)[2];
} GameLanguage;
extern const GameLanguage GAME_LANGUAGES[];
void i18n_set_language(Language l);
Language i18n_language(void);
bool i18n_japanese(void);
/* "en", "ja" or "pt": the setting's value and the [ja]/[pt] keys of the style files. */
const char *i18n_code(Language l);
/* The language of a locale name such as "pt_BR.UTF-8", or -1 for one the game does not have. */
int i18n_of_locale(const char *name);
const char *tr(const char *english);

#endif
