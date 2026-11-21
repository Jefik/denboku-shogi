#include "pieces.h"

#include <cairo-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <glib.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "i18n.h"
#include "rules.h"
#include "zip.h"

const PieceTheme THEME_MONO = {{1, 1, 1}, {1, 1, 1}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, false, 0, {0, 0, 0}};
/* Black pieces drawn in white, for the dark black-and-white theme. */
const PieceTheme THEME_MONO_DARK = {{0, 0, 0}, {0, 0, 0}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}, false, 0, {0, 0, 0}};
const PieceTheme THEME_COLOR = {{0.965, 0.886, 0.71}, {0.863, 0.698, 0.416}, {0, 0, 0}, {0, 0, 0},
                                {0.77, 0.086, 0.11}, true, 0, {0, 0, 0}};
/* Text pieces over a dark background on color screens. */
const PieceTheme THEME_COLOR_ON_DARK = {{0.965, 0.886, 0.71}, {0.863, 0.698, 0.416}, {0, 0, 0}, {0.94, 0.92, 0.88},
                                        {1, 0.47, 0.42}, true, 0, {0, 0, 0}};

/* Colored areas painted under a glyph ([Layers]): glyphs of the layer font, each in its color; a
 * "top" one over it (a thick black part, such as a mane, colored within a black rim). */
#define MAX_LAYERS 4
#define FALLBACK_FONT "NotoSansJP-Medium-shogi.otf" /* data/styles/kanji1.ini's */
typedef struct {
    int n;
    char text[MAX_LAYERS][8];
    double color[MAX_LAYERS][3];
    bool top[MAX_LAYERS];
} Layers;

typedef struct {
    char *file;
    FT_Face face;
    cairo_font_face_t *cairo;
    double fit; /* font size giving a glyph box of 1 unit for the largest piece glyph */
    bool fit_each; /* scale every glyph to its own box instead (FitEach in the style) */
} Font;

typedef struct {
    const PieceTheme *theme;
    int type, w, h;
    bool upside_down;
    cairo_surface_t *surface;
} CacheEntry;

#define CACHE_SIZE 96

struct PieceStyle {
    char *dir; /* the style file's folder: a set of one's own keeps its fonts there */
    char *name, **names; /* the [xx] names, by language (NULL: none) */
    double glyph_scale; /* GlyphScale: glyphs on the koma larger or smaller than the usual size */
    double scale[PIECE_KINDS]; /* [Scale]: one piece's glyph larger or smaller than the others */
    bool keep_glyph_size; /* KeepGlyphSize: the glyph does not shrink with a smaller koma (as printed) */
    double glyph_y; /* GlyphY: the height of one glyph's center on the koma (3 the tip, 107 the base) */
    int layout; /* 1 or 2 characters per piece */
    char *text[PIECE_KINDS];
    Font *font[PIECE_KINDS];
    Font *side_font[PIECE_KINDS]; /* for piece_side()'s character: the piece's own font, or the kanji set's */
    bool mirror[PIECE_KINDS]; /* [Mirror]: the picture drawn facing the other way (the sides told by it) */
    /* The background per piece on color themes with Colored pieces on: [Fill] in the style
     * file, or else the colors of PIECE_ART. */
    double fill[PIECE_KINDS][3];
    bool has_fill[PIECE_KINDS];
    bool colored;
    GPtrArray *fonts;
    CacheEntry cache[CACHE_SIZE];
    int cache_next;
    bool move_marks;
    bool no_shape; /* the glyph alone, no koma or tile (Preferences → Appearance → Piece shape off) */
    bool pictures; /* the glyphs are pictures (ShogiIntl), not kanji */
    bool square;   /* Shape=square: Dōbutsu tiles, all one size, with dots where they move */
    double mark_color[3]; /* MarkColor: the move marks on color themes without Colored pieces (else the ink) */
    bool has_mark_color;
    char *variant;   /* Variant: offered only for that variant (variant_name()) */
    char **not_for; /* NotFor: not offered for these variants (a set with nothing of its own there) */
    /* Color themes: [Layers] in the Layers font (Layers= in [Style]), under each glyph. */
    Font *layer_font;
    Layers layers[PIECE_KINDS];
    int pixels;    /* Pixels: pixel art, drawn about this many pixels wide and enlarged without smoothing */
    int pixel_em;  /* PixelFont: a bitmap-like font's em in pixels, drawn one font pixel per art pixel */
    double line;   /* the outline width, in the 100-unit box */
    /* An image set (Images=): each piece's picture, koma and all, the first player's and, when
     * the set has one, the second's (else the first's turned around); NULL: drawn as text. */
    cairo_surface_t *image[PIECE_KINDS][2];
};

static FT_Library ft;

static bool parse_color(const char *hex, double *c)
{
    unsigned rgb;
    if (!hex || sscanf(hex, "#%6x", &rgb) != 1)
        return false;
    c[0] = (rgb >> 16 & 255) / 255.0;
    c[1] = (rgb >> 8 & 255) / 255.0;
    c[2] = (rgb & 255) / 255.0;
    return true;
}

static void face_done(void *face) { FT_Done_Face(face); }

static Font *font_get(PieceStyle *s, const char *dir, const char *file)
{
    for (guint i = 0; i < s->fonts->len; i++) {
        Font *f = g_ptr_array_index(s->fonts, i);
        if (!strcmp(f->file, file))
            return f;
    }
    if (!ft && FT_Init_FreeType(&ft))
        return NULL;
    char *path = s->dir ? g_build_filename(s->dir, file, NULL) : NULL;
    if (!path || !g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
        g_free(path);
        path = g_build_filename(dir, file, NULL);
    }
    FT_Face face;
    int err = FT_New_Face(ft, path, 0, &face);
    g_free(path);
    if (err)
        return NULL;
    Font *f = g_new0(Font, 1);
    f->file = g_strdup(file);
    f->face = face;
    f->cairo = cairo_ft_font_face_create_for_ft_face(face, 0);
    /* The face is closed when cairo lets go of it (its caches outlive cairo_font_face_destroy);
     * closed earlier, a later face at the same address would draw with the old one's glyphs. */
    static const cairo_user_data_key_t key;
    cairo_font_face_set_user_data(f->cairo, &key, face, face_done);
    g_ptr_array_add(s->fonts, f);
    return f;
}

/* Whether the font has every character of `text` (a traced set has only shogi's pieces). */
static bool font_has(const Font *f, const char *text)
{
    for (const char *p = text; *p; p = g_utf8_next_char(p))
        if (!FT_Get_Char_Index(f->face, g_utf8_get_char(p)))
            return false;
    return true;
}

static void font_free(gpointer p)
{
    Font *f = p;
    cairo_font_face_destroy(f->cairo);
    g_free(f->file);
    g_free(f);
}

/* Ink box of a string, in units of the font size (measured large to avoid hinting rounding). */
static void text_box(Font *f, const char *text, cairo_text_extents_t *e)
{
    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
    cairo_t *cr = cairo_create(surf);
    cairo_set_font_face(cr, f->cairo);
    cairo_set_font_size(cr, 100);
    cairo_text_extents(cr, text, e);
    e->width /= 100;
    e->height /= 100;
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
}

/* Scale each font by the largest glyph it has to draw, so every font fills the
 * piece the same way whatever its own metrics. */
static void fit_fonts(PieceStyle *s)
{
    for (guint i = 0; i < s->fonts->len; i++) {
        Font *f = g_ptr_array_index(s->fonts, i);
        double max = 0;
        for (int t = 0; t < PIECE_KINDS; t++) {
            const char *p = s->font[t] == f ? s->text[t] : NULL;
            if (!p)
                continue;
            while (*p) {
                char ch[8];
                const char *next = g_utf8_next_char(p);
                g_strlcpy(ch, p, next - p + 1);
                cairo_text_extents_t e;
                text_box(f, ch, &e);
                max = MAX(max, MAX(e.width, e.height));
                p = next;
            }
        }
        f->fit = max > 0 ? 1 / max : 1;
    }
}

/* "U+E000 #rrggbb, U+E001 #rrggbb top": the layers of one piece. */
static void parse_layers(const char *value, Layers *l)
{
    char **parts = g_strsplit(value, ",", MAX_LAYERS);
    for (int i = 0; parts[i] && l->n < MAX_LAYERS; i++) {
        unsigned point;
        char hex[8], where[8] = "";
        if (sscanf(g_strstrip(parts[i]), "U+%x %7s %7s", &point, hex, where) >= 2 && parse_color(hex, l->color[l->n])) {
            l->text[l->n][g_unichar_to_utf8(point, l->text[l->n])] = 0;
            l->top[l->n] = !strcmp(where, "top");
            l->n++;
        }
    }
    g_strfreev(parts);
}

/* Pictures are kept at most this tall: enough for any square, and small in memory. */
#define IMAGE_MAX 512

/* A surface `k` times the size of `src` (k < 1), halving first so a large picture does not alias. */
static cairo_surface_t *shrink(cairo_surface_t *src, double k)
{
    cairo_surface_reference(src);
    while (k < 1) {
        double step = MAX(k, 0.5);
        int w = MAX(1, (int) (cairo_image_surface_get_width(src) * step + 0.5));
        int h = MAX(1, (int) (cairo_image_surface_get_height(src) * step + 0.5));
        cairo_surface_t *dst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        cairo_t *cr = cairo_create(dst);
        cairo_scale(cr, (double) w / cairo_image_surface_get_width(src), (double) h / cairo_image_surface_get_height(src));
        cairo_set_source_surface(cr, src, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_paint(cr);
        cairo_destroy(cr);
        cairo_surface_destroy(src);
        src = dst;
        k /= step;
    }
    return src;
}

typedef struct {
    const guint8 *p;
    gsize left;
} Reader;

static cairo_status_t read_png(void *closure, unsigned char *data, unsigned int len)
{
    Reader *r = closure;
    if (len > r->left)
        return CAIRO_STATUS_READ_ERROR;
    memcpy(data, r->p, len);
    r->p += len, r->left -= len;
    return CAIRO_STATUS_SUCCESS;
}

/* A PNG from its bytes, read by cairo itself (no image loader needed, so it is the same everywhere).
 * NULL when it cannot be read. */
static cairo_surface_t *image_load(const GByteArray *bytes)
{
    Reader r = {bytes->data, bytes->len};
    cairo_surface_t *img = cairo_image_surface_create_from_png_stream(read_png, &r);
    if (img && cairo_surface_status(img) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(img);
        return NULL;
    }
    if (img && cairo_image_surface_get_height(img) > IMAGE_MAX) {
        cairo_surface_t *small = shrink(img, (double) IMAGE_MAX / cairo_image_surface_get_height(img));
        cairo_surface_destroy(img);
        img = small;
    }
    return img;
}

/* Where a picture set's files are: a folder, or a zip (its files found by name, whatever folders they are in). */
typedef struct {
    char *dir;
    Zip *zip;
} Source;

static GByteArray *source_get(const Source *src, const char *name)
{
    if (src->zip)
        return zip_get(src->zip, name);
    char *path = g_build_filename(src->dir, name, NULL);
    gchar *data;
    gsize len;
    GByteArray *out = NULL;
    if (g_file_get_contents(path, &data, &len, NULL)) {
        out = g_byte_array_sized_new(len);
        g_byte_array_append(out, (const guint8 *) data, len);
        g_free(data);
    }
    g_free(path);
    return out;
}

static bool source_open(Source *src, const char *path)
{
    src->dir = NULL, src->zip = NULL;
    if (g_file_test(path, G_FILE_TEST_IS_DIR))
        src->dir = g_strdup(path);
    else if (g_str_has_suffix(path, ".zip"))
        src->zip = zip_open(path);
    return src->dir || src->zip;
}

static void source_close(Source *src)
{
    g_free(src->dir);
    zip_close(src->zip);
}

/* A piece's picture: "<side><CODE>.png" (pychess's 0FU.png, 1FU.png), or "<CODE>.png" for the
 * first player alone. The two kings stand in for each other. */
static cairo_surface_t *image_find(const Source *src, const char *code, int side)
{
    const char *alias = !strcmp(code, "GY") ? "OU" : !strcmp(code, "OU") ? "GY" : NULL;
    for (int a = 0; a < 2; a++) {
        const char *c = a ? alias : code;
        for (int plain = 0; c && plain < (side ? 1 : 2); plain++) {
            char *name = g_strdup_printf("%s%s.png", plain ? "" : side ? "1" : "0", c);
            GByteArray *bytes = source_get(src, name);
            cairo_surface_t *img = bytes ? image_load(bytes) : NULL;
            if (bytes)
                g_byte_array_unref(bytes);
            g_free(name);
            if (img)
                return img;
        }
    }
    return NULL;
}

/* A folder or zip of pictures: its style.ini if it has one, else named after it (without .zip). */
static GKeyFile *picture_keys(const Source *src, const char *path)
{
    GKeyFile *kf = g_key_file_new();
    GByteArray *ini = source_get(src, "style.ini");
    if (!ini || !g_key_file_load_from_data(kf, (const char *) ini->data, ini->len, G_KEY_FILE_KEEP_TRANSLATIONS, NULL)) {
        char *name = g_path_get_basename(path);
        if (g_str_has_suffix(name, ".zip"))
            name[strlen(name) - 4] = 0;
        g_key_file_set_string(kf, "Style", "Name", name);
        g_free(name);
    }
    if (ini)
        g_byte_array_unref(ini);
    g_key_file_set_string(kf, "Style", "Images", ".");
    return kf;
}

bool style_is_pictures(const char *path)
{
    Source src;
    if (!source_open(&src, path))
        return false;
    static const char *const ANY[] = {"0FU.png", "FU.png", "0OU.png", "OU.png"};
    bool ok = false;
    for (size_t i = 0; i < G_N_ELEMENTS(ANY) && !ok; i++) {
        GByteArray *b = source_get(&src, ANY[i]);
        ok = b != NULL;
        if (b)
            g_byte_array_unref(b);
    }
    source_close(&src);
    return ok;
}

PieceStyle *style_load(const char *path, const char *font_dir)
{
    Source pics = {0};
    bool folder = !g_str_has_suffix(path, ".ini") && source_open(&pics, path);
    GKeyFile *kf = folder ? picture_keys(&pics, path) : g_key_file_new();
    /* Without KEEP_TRANSLATIONS GLib drops Name[ja] unless the system locale is Japanese. */
    if (!folder && !g_key_file_load_from_file(kf, path, G_KEY_FILE_KEEP_TRANSLATIONS, NULL)) {
        g_key_file_free(kf);
        return NULL;
    }
    PieceStyle *s = g_new0(PieceStyle, 1);
    s->dir = folder && pics.dir ? g_strdup(path) : g_path_get_dirname(path);
    if (!folder && g_key_file_has_key(kf, "Style", "Images", NULL)) { /* a style file's pictures, in a folder or zip beside it */
        char *where = g_key_file_get_string(kf, "Style", "Images", NULL), *full = g_build_filename(s->dir, where, NULL);
        source_open(&pics, full);
        g_free(where);
        g_free(full);
    }
    s->fonts = g_ptr_array_new_with_free_func(font_free);
    s->name = g_key_file_get_string(kf, "Style", "Name", NULL);
    s->names = g_new0(char *, LANGUAGE_COUNT);
    for (int l = 1; l < LANGUAGE_COUNT; l++) {
        /* Only a name written for that language, not GKeyFile's fallback to the plain one. */
        char key[32];
        snprintf(key, sizeof key, "Name[%s]", LANGUAGES[l].code);
        s->names[l] = g_key_file_get_string(kf, "Style", key, NULL);
    }
    s->layout = g_key_file_get_integer(kf, "Style", "Layout", NULL) == 2 ? 2 : 1;
    s->glyph_scale = g_key_file_has_key(kf, "Style", "GlyphScale", NULL) ? g_key_file_get_double(kf, "Style", "GlyphScale", NULL) : 1;
    s->keep_glyph_size = g_key_file_get_boolean(kf, "Style", "KeepGlyphSize", NULL);
    s->glyph_y = g_key_file_has_key(kf, "Style", "GlyphY", NULL) ? g_key_file_get_double(kf, "Style", "GlyphY", NULL) : 63;
    char *font = g_key_file_get_string(kf, "Style", "Font", NULL), *images = g_key_file_get_string(kf, "Style", "Images", NULL);
    if (!font && images) /* the pieces a picture set lacks, in the plain kanji set's font */
        font = g_strdup(FALLBACK_FONT);
    Font *base = font ? font_get(s, font_dir, font) : NULL;
    s->pictures = g_key_file_get_boolean(kf, "Style", "Pictures", NULL);
    char *shape = g_key_file_get_string(kf, "Style", "Shape", NULL), *mark = g_key_file_get_string(kf, "Style", "MarkColor", NULL);
    s->square = shape && !strcmp(shape, "square");
    s->has_mark_color = parse_color(mark, s->mark_color);
    s->variant = g_key_file_get_string(kf, "Style", "Variant", NULL);
    s->not_for = g_key_file_get_string_list(kf, "Style", "NotFor", NULL, NULL);
    s->pixels = CLAMP(g_key_file_get_integer(kf, "Style", "Pixels", NULL), 0, 64);
    s->pixel_em = s->pixels ? CLAMP(g_key_file_get_integer(kf, "Style", "PixelFont", NULL), 0, 64) : 0;
    s->line = 3;
    g_free(shape);
    g_free(mark);
    char *layer_font = g_key_file_get_string(kf, "Style", "Layers", NULL);
    s->layer_font = layer_font ? font_get(s, font_dir, layer_font) : NULL;
    g_free(layer_font);
    for (int t = 0; t < PIECE_KINDS; t++) {
        if (!PIECE_ART[t].code)
            continue;
        char *text = g_key_file_get_string(kf, "Text", PIECE_ART[t].code, NULL);
        s->text[t] = text ? text : g_strdup(s->layout == 2 ? PIECE_ART[t].text2 : PIECE_ART[t].text1);
        char *hex = g_key_file_get_string(kf, "Fill", PIECE_ART[t].code, NULL);
        s->has_fill[t] = parse_color(hex, s->fill[t]);
        g_free(hex);
        if (!g_key_file_has_group(kf, "Fill")) {
            unsigned c = PIECE_ART[t].color;
            s->fill[t][0] = (c >> 16) / 255.0, s->fill[t][1] = (c >> 8 & 255) / 255.0, s->fill[t][2] = (c & 255) / 255.0;
            s->has_fill[t] = true;
        }
        char *layers = g_key_file_get_string(kf, "Layers", PIECE_ART[t].code, NULL);
        if (layers) {
            parse_layers(layers, &s->layers[t]);
            g_free(layers);
        }
        s->scale[t] = g_key_file_has_key(kf, "Scale", PIECE_ART[t].code, NULL)
                          ? g_key_file_get_double(kf, "Scale", PIECE_ART[t].code, NULL)
                          : 1;
        char *file = g_key_file_get_string(kf, "Fonts", PIECE_ART[t].code, NULL);
        s->font[t] = file ? font_get(s, font_dir, file) : base;
        if (!s->font[t])
            s->font[t] = base;
        g_free(file);
        /* Pieces a set has no glyphs for (Tori shogi's birds) in the plain kanji set's font. */
        if (s->font[t] && !font_has(s->font[t], s->text[t])) {
            Font *kanji = font_get(s, font_dir, FALLBACK_FONT);
            if (kanji && font_has(kanji, s->text[t]))
                s->font[t] = kanji;
        }
        s->mirror[t] = g_key_file_get_boolean(kf, "Mirror", PIECE_ART[t].code, NULL);
        for (int side = 0; images && side < 2 && (pics.dir || pics.zip); side++)
            s->image[t][side] = image_find(&pics, PIECE_ART[t].code, side);
        /* Pictures tell the sides by which way they face; kanji sets by the character, in one font for all. */
        int at;
        if (piece_side(t, &at) && !s->pictures && !*g_utf8_next_char(s->text[t]))
            s->side_font[t] = font_get(s, font_dir, FALLBACK_FONT);
    }
    char **each = g_key_file_get_string_list(kf, "Style", "FitEach", NULL, NULL);
    for (int i = 0; each && each[i]; i++)
        for (guint k = 0; k < s->fonts->len; k++) {
            Font *one = g_ptr_array_index(s->fonts, k);
            if (!strcmp(one->file, g_strstrip(each[i])))
                one->fit_each = true;
        }
    g_strfreev(each);
    g_free(font);
    g_free(images);
    source_close(&pics);
    g_key_file_free(kf);
    if (!base) {
        style_free(s);
        return NULL;
    }
    fit_fonts(s);
    return s;
}

void style_free(PieceStyle *s)
{
    if (!s)
        return;
    for (int i = 0; i < CACHE_SIZE; i++)
        if (s->cache[i].surface)
            cairo_surface_destroy(s->cache[i].surface);
    for (int t = 0; t < PIECE_KINDS; t++) {
        g_free(s->text[t]);
        for (int side = 0; side < 2; side++)
            if (s->image[t][side])
                cairo_surface_destroy(s->image[t][side]);
    }
    g_ptr_array_free(s->fonts, TRUE);
    g_free(s->dir);
    g_free(s->name);
    for (int l = 0; s->names && l < LANGUAGE_COUNT; l++)
        g_free(s->names[l]);
    g_free(s->names);
    g_free(s->variant);
    g_strfreev(s->not_for);
    g_free(s);
}

static double luminance(const double *c) { return 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]; }

bool style_no_shape(const PieceStyle *s) { return s && s->no_shape; }

static void cache_clear(PieceStyle *s)
{
    for (int i = 0; i < CACHE_SIZE; i++)
        if (s->cache[i].surface) {
            cairo_surface_destroy(s->cache[i].surface);
            s->cache[i].surface = NULL;
        }
}

void style_set_shape(PieceStyle *s, bool shape)
{
    if (s->no_shape != !shape)
        s->no_shape = !shape, cache_clear(s);
}

void style_set_colored(PieceStyle *s, bool colored)
{
    if (s->colored != colored)
        s->colored = colored, cache_clear(s);
}

/* Chess mode: the first player (sente) white, as in chess, its symbols white with a black outline,
 * and gote's black, on the usual koma or alone; promoted, white's outlined in red and black's red.
 * Black and white screens: white koma, the same symbols. CHESS[screen][0] is black, [1] white. */
#define WOOD {0.965, 0.886, 0.71}, {0.863, 0.698, 0.416}, {0, 0, 0}
static const PieceTheme CHESS[2][2] = {
    /* color screens: black, white */
    {{WOOD, {0, 0, 0}, {0.77, 0.086, 0.11}, true, 1, {0, 0, 0}},
     {WOOD, {1, 1, 1}, {1, 1, 1}, true, 2, {0, 0, 0}}},
    /* black and white */
    {{{1, 1, 1}, {1, 1, 1}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, false, 1, {0, 0, 0}},
     {{1, 1, 1}, {1, 1, 1}, {0, 0, 0}, {1, 1, 1}, {1, 1, 1}, false, 2, {0, 0, 0}}},
};
static const double CHESS_RED[3] = {0.77, 0.086, 0.11};
/* The animals keep their pictures on tiles light for sente and dark for gote, as chess's white and
 * black (chess 3 and 4: no symbol outlining; the kind's color of Colored pieces gives way, or the
 * sides look alike, all facing the same way). Black and white screens: white tiles and black ones,
 * the animals then white. [0] color screens, [1] black and white; [.][0] sente, [.][1] gote. */
static const PieceTheme CHESS_TILE[2][2] = {
    {{{0.99, 0.98, 0.95}, {0.9, 0.88, 0.84}, {0, 0, 0}, {0, 0, 0}, {0.77, 0.086, 0.11}, true, 3, {0, 0, 0}},
     {{0.2, 0.2, 0.22}, {0.08, 0.08, 0.09}, {0, 0, 0}, {1, 1, 1}, {1, 0.45, 0.45}, true, 4, {0, 0, 0}}},
    {{{1, 1, 1}, {1, 1, 1}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, false, 3, {0, 0, 0}},
     {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {1, 1, 1}, {1, 1, 1}, false, 4, {0, 0, 0}}},
};

const PieceTheme *style_chess_theme(const PieceStyle *s, const PieceTheme *th, bool first, const double *bg)
{
    (void) bg; /* sente white and gote black, as chess has the first player white */
    if (s && s->square)
        return &CHESS_TILE[th->gradient ? 0 : 1][!first];
    return &CHESS[th->gradient ? 0 : 1][first];
}

bool style_square(const PieceStyle *s) { return s && s->square; }

const PieceTheme *style_theme_on(const PieceStyle *s, const PieceTheme *th, const double *bg)
{
    if (!s || !s->no_shape || !bg)
        return th;
    bool dark = luminance(bg) < 0.5;
    if (th->gradient)
        return dark ? &THEME_COLOR_ON_DARK : &THEME_COLOR;
    return dark ? &THEME_MONO_DARK : &THEME_MONO;
}

const char *style_variant(const PieceStyle *s) { return s ? s->variant : NULL; }

bool style_serves(const PieceStyle *s, int variant)
{
    if (variant < 0)
        return !s->variant;
    const char *name = variant_name(variant);
    if (s->variant && strcmp(s->variant, name))
        return false;
    for (char **v = s->not_for; v && *v; v++)
        if (!strcmp(*v, name))
            return false;
    return true;
}

const char *style_name(const PieceStyle *s, int lang)
{
    const char *n = lang > 0 && lang < LANGUAGE_COUNT && s->names[lang] ? s->names[lang] : s->name;
    return n ? n : "";
}

static void set_rgb(cairo_t *cr, const double *c)
{
    cairo_set_source_rgb(cr, c[0], c[1], c[2]);
}

/* The piece being rendered in chess mode (else NULL): its theme, whether it is promoted (white's
 * then outlined in red) and whether its glyphs are pictures (a line drawing among them is filled, not outlined). */
static const PieceTheme *chess_th;
static bool chess_promoted, chess_pictures;

static const double *chess_outline(void)
{
    static const double black[3] = {0, 0, 0};
    if (chess_th->chess == 2 && chess_promoted)
        return chess_th->gradient ? CHESS_RED : black;
    return chess_th->outline;
}

/* An A8 mask the size of `target` with `alpha` where `on`. */
static cairo_surface_t *mask_of(int w, int h, const guint8 *alpha)
{
    cairo_surface_t *m = cairo_image_surface_create(CAIRO_FORMAT_A8, w, h);
    cairo_surface_flush(m);
    guint8 *d = cairo_image_surface_get_data(m);
    int stride = cairo_image_surface_get_stride(m);
    for (int y = 0; y < h; y++)
        memcpy(d + y * stride, alpha + y * w, (size_t) w);
    cairo_surface_mark_dirty(m);
    return m;
}

static void paint_mask(cairo_t *cr, const double *c, cairo_surface_t *m)
{
    cairo_save(cr);
    cairo_identity_matrix(cr);
    cairo_set_source_rgb(cr, c[0], c[1], c[2]);
    cairo_mask_surface(cr, m, 0, 0);
    cairo_restore(cr);
}

static guint8 *dup_bytes(const guint8 *a, gsize n) { return memcpy(g_malloc(n), a, n); }

/* Grows (r > 0) or shrinks (r < 0) a mask by |r| pixels, as a disc would. */
static void morph(guint8 *a, int w, int h, int r)
{
    if (!r)
        return;
    guint8 *src = dup_bytes(a, (gsize) w * h);
    int rr = r * r, R = abs(r);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int v = r > 0 ? 0 : 255;
            for (int dy = -R; dy <= R; dy++)
                for (int dx = -R; dx <= R; dx++) {
                    if (dx * dx + dy * dy > rr)
                        continue;
                    int sx = x + dx, sy = y + dy;
                    int s = sx < 0 || sy < 0 || sx >= w || sy >= h ? 0 : src[sy * w + sx];
                    v = r > 0 ? MAX(v, s) : MIN(v, s);
                }
            a[y * w + x] = (guint8) v;
        }
    g_free(src);
}

/* A picture in chess colors, from its pixels: the glyph drawn as usual (G), the area it encloses (S:
 * all but what the outside reaches). A solid picture (most of S inked: a knight, a helmet) is the
 * black piece as it is, and the white one is G white within a black rim, as chess sets draw them; a
 * line drawing (a crown) is the white piece as it is, filled white, and the black one is S black
 * with its inner lines white. */
static void chess_picture(cairo_t *cr, const char *ch, double x, double y, double box)
{
    cairo_surface_t *target = cairo_get_target(cr);
    int w = cairo_image_surface_get_width(target), h = cairo_image_surface_get_height(target);
    cairo_surface_t *gs = cairo_image_surface_create(CAIRO_FORMAT_A8, w, h);
    cairo_t *gc = cairo_create(gs);
    cairo_matrix_t m, fm;
    cairo_get_matrix(cr, &m);
    cairo_get_font_matrix(cr, &fm);
    cairo_set_matrix(gc, &m);
    cairo_set_font_face(gc, cairo_get_font_face(cr));
    cairo_set_font_matrix(gc, &fm);
    cairo_move_to(gc, x, y);
    cairo_show_text(gc, ch);
    cairo_destroy(gc);
    cairo_surface_flush(gs);
    const guint8 *gd = cairo_image_surface_get_data(gs);
    int gstride = cairo_image_surface_get_stride(gs);
    guint8 *g = g_malloc((gsize) w * h), *sil = g_malloc0((gsize) w * h);
    for (int yy = 0; yy < h; yy++)
        memcpy(g + yy * w, gd + yy * gstride, (size_t) w);
    cairo_surface_destroy(gs);
    /* The outside: what the edges reach without crossing ink. */
    int *queue = g_malloc(sizeof(int) * (gsize) w * h), head = 0, tail = 0;
    guint8 *out = g_malloc0((gsize) w * h);
    for (int i = 0; i < w * h; i++) {
        int xx = i % w, yy = i / w;
        if ((xx == 0 || yy == 0 || xx == w - 1 || yy == h - 1) && g[i] < 128)
            out[i] = 1, queue[tail++] = i;
    }
    while (head < tail) {
        int i = queue[head++], xx = i % w, yy = i / w;
        const int n[4] = {xx > 0 ? i - 1 : -1, xx < w - 1 ? i + 1 : -1, yy > 0 ? i - w : -1, yy < h - 1 ? i + w : -1};
        for (int k = 0; k < 4; k++)
            if (n[k] >= 0 && !out[n[k]] && g[n[k]] < 128)
                out[n[k]] = 1, queue[tail++] = n[k];
    }
    long inked = 0, area = 0;
    for (int i = 0; i < w * h; i++) {
        sil[i] = out[i] ? g[i] : 255; /* the ink's own edge keeps its smoothing */
        inked += g[i] >= 128, area += sil[i] >= 128;
    }
    g_free(queue);
    g_free(out);
    double dx = box * 0.05, dy = 0;
    cairo_user_to_device_distance(cr, &dx, &dy);
    int r = MAX(1, (int) (hypot(dx, dy) + 0.5));
    bool solid = !chess_pictures || (area && inked * 2 > area); /* a kanji: its strokes white, outlined */
    static const double white[3] = {1, 1, 1}, black[3] = {0, 0, 0};
    const double *ink = chess_promoted && chess_th->chess == 1 ? (chess_th->gradient ? CHESS_RED : black) : black;
    cairo_surface_t *gm = mask_of(w, h, g);
    if (chess_th->chess == 2) {
        const double *line = chess_outline();
        if (solid) {
            /* A rim around the ink: it fills the thin holes (the details, black as on a chess set's
             * white pieces) and rings the wide ones, which keep the background as in the black piece. */
            guint8 *rim = dup_bytes(g, (gsize) w * h);
            morph(rim, w, h, r);
            cairo_surface_t *rm = mask_of(w, h, rim);
            paint_mask(cr, line, rm);
            paint_mask(cr, white, gm);
            cairo_surface_destroy(rm);
            g_free(rim);
        } else {
            cairo_surface_t *sm = mask_of(w, h, sil);
            paint_mask(cr, white, sm);
            paint_mask(cr, line, gm);
            cairo_surface_destroy(sm);
        }
    } else if (solid) {
        paint_mask(cr, ink, gm);
    } else {
        /* S black, its inner lines white, its outer edge black again. */
        guint8 *inner = dup_bytes(sil, (gsize) w * h);
        morph(inner, w, h, -r);
        for (int i = 0; i < w * h; i++)
            inner[i] = (guint8) (g[i] * inner[i] / 255);
        cairo_surface_t *sm = mask_of(w, h, sil), *im = mask_of(w, h, inner);
        paint_mask(cr, ink, sm);
        paint_mask(cr, white, im);
        cairo_surface_destroy(sm);
        cairo_surface_destroy(im);
        g_free(inner);
    }
    cairo_surface_destroy(gm);
    g_free(g);
    g_free(sil);
}

/* Draws one character centered on (cx, cy) inside a box×box square, squeezed by sx. */
static void draw_glyph_fit(cairo_t *cr, Font *f, const char *ch, double cx, double cy, double box, double sx, bool each,
                           Font *layer_font, const Layers *layers)
{
    cairo_save(cr);
    cairo_set_font_face(cr, f->cairo);
    cairo_text_extents_t e;
    if (each || f->fit_each) {
        text_box(f, ch, &e);
        cairo_set_font_size(cr, box / MAX(MAX(e.width, e.height), 0.01));
    } else {
        cairo_set_font_size(cr, box * f->fit);
    }
    cairo_text_extents(cr, ch, &e);
    cairo_translate(cr, cx, cy);
    cairo_scale(cr, sx, 1);
    double x = -e.x_bearing - e.width / 2, y = -e.y_bearing - e.height / 2;
    if (layer_font && layers && layers->n) {
        /* The layer glyphs share the outline's units and origin: the same size, the same place. */
        cairo_pattern_t *ink = cairo_pattern_reference(cairo_get_source(cr));
        cairo_matrix_t m;
        cairo_get_font_matrix(cr, &m);
        cairo_set_font_face(cr, layer_font->cairo);
        cairo_set_font_matrix(cr, &m);
        for (int pass = 0; pass < 2; pass++) { /* under the outline, then over it */
            for (int i = 0; i < layers->n; i++) {
                if (layers->top[i] != pass)
                    continue;
                cairo_set_font_face(cr, layer_font->cairo);
                cairo_set_font_matrix(cr, &m);
                cairo_set_source_rgb(cr, layers->color[i][0], layers->color[i][1], layers->color[i][2]);
                cairo_move_to(cr, x, y);
                cairo_show_text(cr, layers->text[i]);
            }
            if (!pass) {
                cairo_set_font_face(cr, f->cairo);
                cairo_set_font_matrix(cr, &m);
                cairo_set_source(cr, ink);
                cairo_move_to(cr, x, y);
                cairo_show_text(cr, ch);
            }
        }
        cairo_pattern_destroy(ink);
        cairo_restore(cr);
        return;
    } else if (chess_th && (chess_pictures || chess_th->chess == 2)) {
        chess_picture(cr, ch, x, y, box);
        cairo_restore(cr);
        return;
    }
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, ch);
    cairo_restore(cr);
}

/* A pixel font at its own size, one font pixel per art pixel (s->line, on a piece scaled by k). */
static void draw_pixel_glyph(const PieceStyle *s, cairo_t *cr, Font *f, const char *ch, double cx, double cy, double k)
{
    cairo_save(cr);
    cairo_set_font_face(cr, f->cairo);
    double px = s->line / k;
    cairo_set_font_size(cr, s->pixel_em * px);
    cairo_text_extents_t e;
    cairo_text_extents(cr, ch, &e);
    double x = cx - e.x_bearing - e.width / 2, y = cy - e.y_bearing - e.height / 2;
    cairo_move_to(cr, floor(x / px + 0.5) * px, floor(y / px + 0.5) * px);
    cairo_show_text(cr, ch);
    cairo_restore(cr);
}

static void draw_glyph(cairo_t *cr, Font *f, const char *ch, double cx, double cy, double box, double sx)
{
    draw_glyph_fit(cr, f, ch, cx, cy, box, sx, false, NULL, NULL);
}

/* Where the mark for a direction sits, inside the pentagon (unit box 100×110), every mark 3 units or
 * more clear of the outline (a slide's bar reaches further out than a dot). */
static void mark_position(int dr, int dc, bool slide, double *x, double *y)
{
    if (dr == -2) {          /* knight: next to the tip */
        *x = 50 + dc * 11, *y = 16;
    } else if (dr == -1) {
        *x = 50 + dc * 24, *y = dc ? 26 : 19;
    } else if (dr == 0) {
        *x = 50 + dc * (slide ? 28 : 32), *y = 62;
    } else {
        *x = 50 + dc * (slide ? 30 : 33), *y = slide ? 94 : 97;
    }
}

/* A jump (the knight's, and Tori shogi's pheasant and goose two squares straight or diagonally):
 * its direction as a step's, `*far` 2 for a jump of two squares, 1 for the knight. */
static int jump_of(int *dr, int *dc)
{
    if (abs(*dr) == 2 && abs(*dc) == 1)
        return 1;
    if (abs(*dr) != 2 && abs(*dc) != 2)
        return 0;
    *dr = (*dr > 0) - (*dr < 0), *dc = (*dc > 0) - (*dc < 0);
    return 2;
}

/* A knight's jump: its fork's stem comes down to y 29.5, below the other marks. */
static bool forked(int type)
{
    int dr[8], dc[8], slide[8], n = piece_steps(kind_type(type), dr, dc, slide);
    for (int i = 0; i < n; i++)
        if (jump_of(&dr[i], &dc[i]) == 1)
            return true;
    return false;
}

/* A step's dot; a jump's dot at the end of a stem from (ox, oy)'s side, as shogi_printout draws the
 * knight's fork ("it goes past"); for a move of up to two squares (Tori shogi's eagle diagonally
 * back) two dots, the second `gap` from the first toward (ox, oy). The knight (jump 1) has its fork
 * from (ox, oy), the stem below its tip. */
static void step_mark(cairo_t *cr, double x, double y, double r, int jump, bool two, double ox, double oy, double gap,
                      double stem)
{
    double len = hypot(ox - x, oy - y), ux = (ox - x) / len, uy = (oy - y) / len;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x, y, r, 0, 2 * G_PI);
    cairo_fill(cr);
    if (jump) {
        cairo_set_line_width(cr, r * 0.65);
        if (jump == 1) { /* the stem up to the fork, then a branch rising and opening out to the dot */
            double fork = y + r * 1.9;
            cairo_move_to(cr, ox, oy);
            cairo_line_to(cr, ox, fork);
            cairo_curve_to(cr, ox, fork - r * 1.4, ox + (x - ox) * 0.35, y, x, y);
        } else {
            cairo_move_to(cr, x, y);
            cairo_line_to(cr, x + ux * r * stem, y + uy * r * stem);
        }
        cairo_stroke(cr);
    }
    if (two) {
        cairo_arc(cr, x + ux * gap, y + uy * gap, r, 0, 2 * G_PI);
        cairo_fill(cr);
    }
}

/* Square tiles and pieces without the shape: a dot by the edge or corner toward each square the
 * piece reaches, `reach` from the middle, a bar `half` long each way pointing outwards for any
 * distance (as on the other sets), and the knight's jumps as two dots either side of the top. On a
 * tile (Dōbutsu's: 6…94 × 11…99, corners of 12, a line of 3) every mark is 3 units or more clear of
 * its outline, the diagonal ones pulled in from the round corners. */
static void draw_square_marks(cairo_t *cr, int type, const double *color, double reach, double half, bool tile)
{
    int dr[8], dc[8], slide[8];
    int n = piece_steps(kind_type(type), dr, dc, slide);
    set_rgb(cr, color);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    for (int i = 0; i < n; i++) {
        int jump = jump_of(&dr[i], &dc[i]);
        bool diag = dr[i] && dc[i];
        double k = !tile ? reach : slide[i] == 1 ? (diag ? 23 : 26) : (diag ? 28 : 32);
        double x = 50 + dc[i] * k, y = 55 + (dr[i] < -1 ? (tile ? -33 / k : -1.1) : dr[i]) * k;
        if (dr[i] < -1)
            x = 50 + dc[i] * 15;
        if (slide[i] == 1) {
            double ux = dc[i], uy = dr[i], len = sqrt(ux * ux + uy * uy);
            cairo_set_line_width(cr, 7);
            cairo_move_to(cr, x - ux / len * half, y - uy / len * half);
            cairo_line_to(cr, x + ux / len * half, y + uy / len * half);
            cairo_stroke(cr);
        } else {
            /* On a tile a jump's stem is shorter: the picture fills more of the middle than a kanji. */
            step_mark(cr, x, y, jump == 1 && tile ? 4.5 : 5.5, jump, slide[i] > 1, 50, jump == 1 ? y + (tile ? 12 : 15) : 55, 14,
                      tile ? 2 : 3.2);
        }
    }
}

static void draw_move_marks(cairo_t *cr, int type, const double *ink)
{
    int dr[8], dc[8], slide[8];
    int n = piece_steps(kind_type(type), dr, dc, slide);
    cairo_set_source_rgb(cr, ink[0], ink[1], ink[2]);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    for (int i = 0; i < n; i++) {
        double x, y;
        int jump = jump_of(&dr[i], &dc[i]);
        mark_position(jump == 1 ? -2 : dr[i], dc[i], slide[i] == 1, &x, &y);
        if (jump == 2 && dr[i] < 0) { /* ahead, past where a step's dot goes */
            double len = hypot(x - 50, y - 60);
            x += (x - 50) / len * 5, y += (y - 60) / len * 5;
        }
        if (slide[i] == 1) {
            /* A short bar pointing outwards: moves any distance. */
            double len = sqrt((double) dr[i] * dr[i] + dc[i] * dc[i]), ux = dc[i] / len, uy = dr[i] / len;
            cairo_set_line_width(cr, 4);
            cairo_move_to(cr, x - ux * 5, y - uy * 5);
            cairo_line_to(cr, x + ux * 5, y + uy * 5);
            cairo_stroke(cr);
        } else {
            /* The knight's fork from below the tip, as on shogi_printout; room ahead, the kanji below. */
            step_mark(cr, x, y, 3.5, jump, slide[i] > 1, 50, jump == 1 ? 29.5 : 60, dr[i] < 0 ? -8 : 9, 3.2);
        }
    }
}

static const double PENTAGON[5][2] = {{50, 3}, {85, 15}, {95, 107}, {5, 107}, {15, 15}};

/* The pentagon with every side moved `d` inwards: each corner where two moved sides meet. */
static void pentagon_inset(cairo_t *cr, double d)
{
    double a[5], b[5], c[5]; /* side i: a·x + b·y = c, (a, b) pointing inwards */
    for (int i = 0; i < 5; i++) {
        const double *p = PENTAGON[i], *q = PENTAGON[(i + 1) % 5];
        double nx = p[1] - q[1], ny = q[0] - p[0], len = hypot(nx, ny);
        nx /= len, ny /= len;
        if (nx * (50 - p[0]) + ny * (60 - p[1]) < 0)
            nx = -nx, ny = -ny;
        a[i] = nx, b[i] = ny, c[i] = nx * p[0] + ny * p[1] + d;
    }
    for (int i = 0; i < 5; i++) {
        int j = (i + 4) % 5; /* the side before corner i */
        double det = a[j] * b[i] - a[i] * b[j];
        double x = (c[j] * b[i] - c[i] * b[j]) / det, y = (a[j] * c[i] - a[i] * c[j]) / det;
        i ? cairo_line_to(cr, x, y) : cairo_move_to(cr, x, y);
    }
    cairo_close_path(cr);
}

/* The koma's pentagon, or a Dōbutsu tile. */
static void shape_path(const PieceStyle *s, cairo_t *cr)
{
    if (s->square) {
        double r = 12, x0 = 6, y0 = 11, x1 = 94, y1 = 99;
        cairo_new_sub_path(cr);
        cairo_arc(cr, x1 - r, y0 + r, r, -G_PI / 2, 0);
        cairo_arc(cr, x1 - r, y1 - r, r, 0, G_PI / 2);
        cairo_arc(cr, x0 + r, y1 - r, r, G_PI / 2, G_PI);
        cairo_arc(cr, x0 + r, y0 + r, r, G_PI, 3 * G_PI / 2);
        cairo_close_path(cr);
    } else {
        pentagon_inset(cr, 0);
    }
}

/* piece_side()'s character (Tori shogi's 左 and 右) on a red seal in the lower corner of its side,
 * centered at (50 ± dx, y), `r` its radius. Not with the movement marks: they show the side. */
static void draw_side(PieceStyle *s, const PieceTheme *th, cairo_t *cr, int type, double dx, double y, double r)
{
    static const double white[3] = {1, 1, 1};
    int at;
    const char *side = piece_side(type, &at);
    if (!s->side_font[type] || !side || s->move_marks)
        return;
    cairo_new_sub_path(cr);
    cairo_arc(cr, 50 + at * dx, y, r, 0, 2 * G_PI);
    set_rgb(cr, th->red);
    cairo_fill(cr);
    set_rgb(cr, white);
    draw_glyph_fit(cr, s->side_font[type], side, 50 + at * dx, y, r * 1.25, 1, true, NULL, NULL);
}

/* How much the glyph shrinks and rises to leave the seal room. */
static bool sealed(const PieceStyle *s, int type) { return s->side_font[type] && !s->move_marks; }

/* Piece drawn in a 100×110 unit box. */
static void render(PieceStyle *s, const PieceTheme *th, cairo_t *cr, int type)
{
    /* Smaller pieces stand on the same base, as on a real board; text alone shrinks to the
     * middle of the square, or it sat low. */
    double k = s->square || s->no_shape ? 1 : PIECE_ART[type].size, base = 108;
    cairo_translate(cr, 50, base);
    cairo_scale(cr, k, k);
    cairo_translate(cr, -50, -base);
    const char *text = s->text[type];
    Font *f = s->font[type];
    const char *second = g_utf8_next_char(text);
    char first[8];
    g_strlcpy(first, text, second - text + 1);
    const double *ink = PIECE_ART[type].promoted ? th->red : th->ink;
    bool fill = th->gradient && s->colored && s->has_fill[type] && !th->chess;
    bool tile = th->chess >= 3; /* the animals' chess tiles */
    chess_th = th->chess && !tile ? th : NULL;
    chess_promoted = PIECE_ART[type].promoted, chess_pictures = s->pictures;
    const double *mark_ink = th->chess && !tile ? th->border : ink; /* in chess mode, black; on a tile, its ink */
    if (s->no_shape && !s->square) {
        /* Without the koma: the glyph alone, every piece the same size (the pawn's picture is
         * as large as the king's); the movement marks around it as on square tiles (the piece
         * fills its square, so the koma's places for them ran into the glyph), the glyph smaller
         * to leave them room. Colored pieces color the glyph: the piece's color, darker for dark ink. */
        double tint[3];
        if (fill) {
            const double *c = s->fill[type];
            bool dark_ink = luminance(ink) < 0.5;
            for (int i = 0; i < 3; i++)
                tint[i] = dark_ink ? c[i] * 0.55 : c[i];
            if (dark_ink || luminance(c) > 0.35)
                ink = tint;
        }
        /* The marks at the square's edge, the glyph within 31 of the middle, clear of them. */
        if (s->move_marks)
            draw_square_marks(cr, type, mark_ink, 40, 4.5, false);
        set_rgb(cr, ink);
        bool marks = s->move_marks; /* fitted: GlyphScale and [Scale] are for the koma */
        if (s->pixel_em) {
            draw_pixel_glyph(s, cr, f, first, 50, 55, 1);
        } else if (*second) {
            double g = marks ? 0.58 : 1;
            draw_glyph_fit(cr, f, first, 50, 55 - 27 * g, 52 * g, 0.92, true, NULL, NULL);
            draw_glyph_fit(cr, f, second, 50, 55 + 27 * g, 52 * g, 0.92, true, NULL, NULL);
        } else {
            /* The whole square, as lishogi's kanji sets; smaller inside the movement marks. */
            double chess_k = th->chess ? 0.9 : 1; /* the white pieces' rim within the square */
            draw_glyph_fit(cr, f, first, 50, sealed(s, type) ? 50 : 55, (marks ? 62 : sealed(s, type) ? 86 : 104) * chess_k,
                           s->mirror[type] ? -1 : 1, true, NULL, NULL);
        }
        draw_side(s, th, cr, type, 38, 94, 12);
        return;
    }
    static const double white[3] = {1, 1, 1};
    if (!s->no_shape) { /* Dōbutsu tiles without the shape still get their marks and pictures below */
        shape_path(s, cr);
        if (s->pixels) {
            /* No stroke: a one-pixel line breaks up on the slopes. The border color, then the piece
             * one pixel smaller on top, in a flat color. */
            set_rgb(cr, th->border);
            cairo_fill(cr);
            double d = s->line / k; /* k: this piece's size */
            if (s->square) {
                cairo_save(cr);
                cairo_translate(cr, 50, 55);
                cairo_scale(cr, 1 - 2 * d / 88, 1 - 2 * d / 88);
                cairo_translate(cr, -50, -55);
                shape_path(s, cr);
                cairo_restore(cr);
            } else {
                pentagon_inset(cr, d);
            }
            set_rgb(cr, fill ? s->fill[type] : th->fill);
            cairo_fill(cr);
        } else if (fill) {
            const double *c = s->fill[type];
            set_rgb(cr, c);
            cairo_fill_preserve(cr);
            /* Light symbols on dark pieces (the pictorial set's black king). */
            if (0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2] < 0.35) /* not the red king of the colored sets */
                ink = white;
        } else if (th->gradient) {
            cairo_pattern_t *pat = cairo_pattern_create_linear(0, 0, 100, 110);
            cairo_pattern_add_color_stop_rgb(pat, 0, th->fill[0], th->fill[1], th->fill[2]);
            cairo_pattern_add_color_stop_rgb(pat, 1, th->fill2[0], th->fill2[1], th->fill2[2]);
            cairo_set_source(cr, pat);
            cairo_fill_preserve(cr);
            cairo_pattern_destroy(pat);
        } else {
            set_rgb(cr, th->fill);
            cairo_fill_preserve(cr);
        }
        if (!s->pixels) {
            set_rgb(cr, th->border);
            cairo_set_line_width(cr, s->line);
            cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
            cairo_stroke(cr);
        }
    }

    if (s->square) {
        /* Without the tile, the marks and the picture as other sets without the shape have them (the
         * marks at the square's edge, the picture within 31 of the middle). */
        const double *mc = th->gradient && s->has_mark_color && !fill && !th->chess ? s->mark_color : mark_ink;
        if (s->move_marks)
            draw_square_marks(cr, type, mc, s->no_shape ? 40 : 34, s->no_shape ? 4.5 : 7, !s->no_shape);
        double box = (s->move_marks ? (s->no_shape ? 62 : 46) : s->no_shape ? 96 : 70) * s->glyph_scale * s->scale[type]; /* inside the marks, 3 clear */
        bool layered = th->gradient && s->layer_font && s->layers[type].n;
        static const double black[3] = {0, 0, 0};
        set_rgb(cr, layered ? black : ink); /* colored pictures keep a black outline, promoted or not, on any background */
        draw_glyph_fit(cr, f, first, 50, 55, box, s->mirror[type] ? -1 : 1, false, layered ? s->layer_font : NULL, &s->layers[type]);
        return;
    }
    if (s->move_marks)
        draw_move_marks(cr, type, mark_ink);
    set_rgb(cr, ink);
    /* With movement marks the glyphs shrink to leave the edge free (sizes compared in the
     * investigation mockups, 2026-11-05-7-glyph-size), whatever the set's own scale: within the
     * side marks (x 23 to 77) and the bottom ones (y 91). */
    bool one = !*second;
    double g = (s->move_marks && !one ? 0.80 : 1.08) * s->glyph_scale * s->scale[type] / (s->keep_glyph_size ? k : 1);
    if (s->move_marks && one && !s->pixel_em) {
        /* As large as the set has it, but within the marks (x 24 to 76, y 30 to 92; 36 to 92 under a knight's fork), by the
         * glyph's own ink: a set whose pawn is small in its font (the printed sets, enlarged by
         * [Scale]) keeps it as large as the others'. */
        cairo_text_extents_t e;
        text_box(f, first, &e);
        double unit = 62 * f->fit, tall = forked(type) ? 56 : 62; /* a knight's from y 36, clear of its fork */
        if (e.width > 0 && e.height > 0)
            g = MIN(g, MIN(52 / (unit * e.width), tall / (unit * e.height)));
    }
    if (!one) {
        g = MIN(g, s->move_marks ? 0.62 : 0.90 * s->glyph_scale); /* two glyphs reach the narrow top and the sides */
        draw_glyph(cr, f, first, 50, s->move_marks ? 43 : 38, 44 * g, 0.9);
        draw_glyph(cr, f, second, 50, s->move_marks ? 72 : 83, 44 * g, 0.9);
    } else if (s->pixel_em) {
        draw_pixel_glyph(s, cr, f, first, 50, s->glyph_y, k);
    } else {
        bool seal = sealed(s, type);
        draw_glyph(cr, f, first, 50, s->move_marks ? (forked(type) ? 64 : 61) : s->glyph_y - (seal ? 6 : 0), 62 * g * (seal ? 0.84 : 1),
                   s->mirror[type] ? -1 : 1);
    }
    draw_side(s, th, cr, type, 29, 92, 12.5);
}

void style_draw_symbol(PieceStyle *s, cairo_t *cr, int type, bool upside_down, const double *bg, double x, double y,
                       double w, double h)
{
    /* The set's own font: the piece's text, or its first kanji for a 2-kanji set. */
    Font *f = s->font[type];
    const char *text = s->layout == 1 && !*g_utf8_next_char(s->text[type]) ? s->text[type] : PIECE_ART[type].text1;
    if (!f || !text)
        return;
    char first[8];
    g_strlcpy(first, text, g_utf8_next_char(text) - text + 1);
    chess_th = NULL;
    double k = MIN(w, h) / 100;
    cairo_save(cr);
    cairo_translate(cr, x + w / 2, y + h / 2);
    cairo_scale(cr, k, k);
    if (upside_down)
        cairo_rotate(cr, G_PI);
    bool dark = 0.3 * bg[0] + 0.59 * bg[1] + 0.11 * bg[2] < 0.5;
    cairo_set_source_rgb(cr, dark ? 0.94 : 0, dark ? 0.92 : 0, dark ? 0.88 : 0);
    draw_glyph(cr, f, first, 0, 0, 92, 1);
    cairo_restore(cr);
}

static CacheEntry *cache_get(PieceStyle *s, const PieceTheme *th, int type, int iw, int ih, bool upside_down)
{
    for (int i = 0; i < CACHE_SIZE; i++) {
        CacheEntry *c = &s->cache[i];
        if (c->surface && c->theme == th && c->type == type && c->w == iw && c->h == ih &&
            c->upside_down == upside_down)
            return c;
    }
    CacheEntry *e = &s->cache[s->cache_next];
    s->cache_next = (s->cache_next + 1) % CACHE_SIZE;
    if (e->surface)
        cairo_surface_destroy(e->surface);
    *e = (CacheEntry) {th, type, iw, ih, upside_down, cairo_image_surface_create(CAIRO_FORMAT_ARGB32, iw, ih)};
    /* Pixel art: a whole number of screen pixels per art pixel, so they all come out the same size. */
    int grid = s->no_shape && !s->square && s->pixel_em ? s->pixel_em + 2 : s->pixels; /* the glyph alone fills the square */
    int k = grid ? MAX(1, (int) floor(MIN(iw, ih / 1.1) / grid + 0.5)) : 1;
    int sw = (iw + k - 1) / k, sh = (ih + k - 1) / k;
    cairo_surface_t *small = k > 1 ? cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh) : e->surface;
    cairo_t *pc = cairo_create(small);
    cairo_surface_t *img = s->image[type][upside_down && s->image[type][1]];
    if (img) {
        /* A picture fills the 100×110 box as it is drawn, whatever its own proportions: centered, the
         * second player's turned around unless the set draws it. */
        int w = cairo_image_surface_get_width(img), h = cairo_image_surface_get_height(img);
        double k = MIN(iw / (double) w, ih / (double) h);
        cairo_surface_t *fit = shrink(img, k);
        double fw = cairo_image_surface_get_width(fit), fh = cairo_image_surface_get_height(fit), f = MIN(iw / fw, ih / fh);
        cairo_translate(pc, iw / 2.0, ih / 2.0);
        if (upside_down && !s->image[type][1])
            cairo_rotate(pc, G_PI);
        cairo_scale(pc, f, f);
        cairo_set_source_surface(pc, fit, -fw / 2, -fh / 2);
        cairo_paint(pc);
        cairo_surface_destroy(fit);
        cairo_destroy(pc);
        return e;
    }
    double scale = MIN(sw / 100.0, sh / 110.0);
    cairo_translate(pc, (sw - 100 * scale) / 2, (sh - 110 * scale) / 2);
    cairo_scale(pc, scale, scale);
    if (upside_down) {
        cairo_translate(pc, 50, 55);
        cairo_rotate(pc, G_PI);
        cairo_translate(pc, -50, -55);
    }
    if (s->pixels) {
        cairo_set_antialias(pc, CAIRO_ANTIALIAS_NONE);
        cairo_font_options_t *fo = cairo_font_options_create();
        cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_NONE);
        cairo_set_font_options(pc, fo);
        cairo_font_options_destroy(fo);
        s->line = 1 / scale; /* one art pixel */
    }
    render(s, th, pc, type);
    cairo_destroy(pc);
    if (small != e->surface) {
        cairo_t *big = cairo_create(e->surface);
        cairo_translate(big, (iw - sw * k) / 2, (ih - sh * k) / 2);
        cairo_scale(big, k, k);
        cairo_set_source_surface(big, small, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(big), CAIRO_FILTER_NEAREST);
        cairo_paint(big);
        cairo_destroy(big);
        cairo_surface_destroy(small);
    }
    return e;
}

void style_cache(PieceStyle *s, const PieceTheme *th, int type, double w, double h, bool upside_down)
{
    int iw = (int) (w + 0.5), ih = (int) (h + 0.5);
    if (iw < 4 || ih < 4 || type < 1 || type >= PIECE_KINDS || !PIECE_ART[type].code)
        return;
    cache_get(s, th, type, iw, ih, upside_down);
}

static bool vector;

void style_set_vector(bool on) { vector = on; }

void style_draw(PieceStyle *s, const PieceTheme *th, cairo_t *cr, int type, bool upside_down, double x, double y,
                double w, double h)
{
    int iw = (int) (w + 0.5), ih = (int) (h + 0.5);
    if (iw < 4 || ih < 4 || type < 1 || type >= PIECE_KINDS || !PIECE_ART[type].code)
        return;
    bool pixels = s->image[type][0] || (th->chess && th->chess < 3 && (s->pictures || th->chess == 2));
    if (vector && !pixels) { /* as cache_get draws it, onto the target */
        double scale = MIN(w / 100.0, h / 110.0);
        cairo_save(cr);
        cairo_translate(cr, x + (w - 100 * scale) / 2, y + (h - 110 * scale) / 2);
        cairo_scale(cr, scale, scale);
        if (upside_down) {
            cairo_translate(cr, 50, 55);
            cairo_rotate(cr, G_PI);
            cairo_translate(cr, -50, -55);
        }
        if (s->pixels)
            s->line = 1 / scale;
        render(s, th, cr, type);
        cairo_restore(cr);
        return;
    }
    CacheEntry *e = cache_get(s, th, type, iw, ih, upside_down);
    cairo_set_source_surface(cr, e->surface, x, y);
    cairo_paint(cr);
}

void style_set_move_marks(PieceStyle *s, bool marks)
{
    if (s->move_marks != marks)
        s->move_marks = marks, cache_clear(s);
}
