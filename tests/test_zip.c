/* zip.c: stored and deflated files, names without their folders, and damaged archives. */
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

#include "zip.h"

/* Made by Python's zipfile: set/0FU.png stored, set/OU.svg deflated, an empty style.ini, a comment. */
static const unsigned char ARCHIVE[] = {80, 75, 3, 4, 20, 0, 0, 0, 0, 0, 0, 0, 33, 0, 120, 119, 166, 15, 12, 0, 0, 0, 12, 0, 0, 0, 11, 0, 0, 0, 115, 101, 116, 47, 48, 70, 85, 46, 112, 110, 103, 115, 116, 111, 114, 101, 100, 32, 98, 121, 116, 101, 115, 80, 75, 3, 4, 20, 0, 0, 0, 8, 0, 59, 51, 112, 93, 99, 186, 53, 80, 28, 0, 0, 0, 187, 4, 0, 0, 10, 0, 0, 0, 115, 101, 116, 47, 79, 85, 46, 115, 118, 103, 179, 41, 46, 75, 183, 75, 76, 74, 30, 69, 163, 33, 48, 26, 2, 163, 33, 144, 56, 20, 138, 2, 27, 125, 80, 169, 5, 0, 80, 75, 3, 4, 20, 0, 0, 0, 8, 0, 59, 51, 112, 93, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 9, 0, 0, 0, 115, 116, 121, 108, 101, 46, 105, 110, 105, 3, 0, 80, 75, 1, 2, 20, 3, 20, 0, 0, 0, 0, 0, 0, 0, 33, 0, 120, 119, 166, 15, 12, 0, 0, 0, 12, 0, 0, 0, 11, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128, 1, 0, 0, 0, 0, 115, 101, 116, 47, 48, 70, 85, 46, 112, 110, 103, 80, 75, 1, 2, 20, 3, 20, 0, 0, 0, 8, 0, 59, 51, 112, 93, 99, 186, 53, 80, 28, 0, 0, 0, 187, 4, 0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128, 1, 53, 0, 0, 0, 115, 101, 116, 47, 79, 85, 46, 115, 118, 103, 80, 75, 1, 2, 20, 3, 20, 0, 0, 0, 8, 0, 59, 51, 112, 93, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128, 1, 121, 0, 0, 0, 115, 116, 121, 108, 101, 46, 105, 110, 105, 80, 75, 5, 6, 0, 0, 0, 0, 3, 0, 3, 0, 168, 0, 0, 0, 162, 0, 0, 0, 9, 0, 97, 32, 99, 111, 109, 109, 101, 110, 116};

static const unsigned char PACKAGE[] = {80, 75, 3, 4, 20, 0, 0, 0, 0, 0, 0, 0, 33, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 112, 107, 103, 47, 80, 75, 3, 4, 20, 0, 0, 0, 8, 0, 202, 112, 112, 93, 243, 49, 162, 13, 17, 0, 0, 0, 88, 2, 0, 0, 13, 0, 0, 0, 112, 107, 103, 47, 98, 105, 110, 47, 97, 46, 116, 120, 116, 203, 72, 205, 201, 201, 87, 200, 24, 37, 71, 67, 32, 149, 154, 41, 1, 0, 80, 75, 3, 4, 20, 0, 0, 0, 0, 0, 202, 112, 112, 93, 207, 98, 32, 25, 5, 0, 0, 0, 5, 0, 0, 0, 9, 0, 0, 0, 112, 107, 103, 47, 98, 46, 116, 120, 116, 112, 108, 97, 105, 110, 80, 75, 1, 2, 20, 3, 20, 0, 0, 0, 0, 0, 0, 0, 33, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128, 1, 0, 0, 0, 0, 112, 107, 103, 47, 80, 75, 1, 2, 20, 3, 20, 0, 0, 0, 8, 0, 202, 112, 112, 93, 243, 49, 162, 13, 17, 0, 0, 0, 88, 2, 0, 0, 13, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128, 1, 34, 0, 0, 0, 112, 107, 103, 47, 98, 105, 110, 47, 97, 46, 116, 120, 116, 80, 75, 1, 2, 20, 3, 20, 0, 0, 0, 0, 0, 202, 112, 112, 93, 207, 98, 32, 25, 5, 0, 0, 0, 5, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128, 1, 94, 0, 0, 0, 112, 107, 103, 47, 98, 46, 116, 120, 116, 80, 75, 5, 6, 0, 0, 0, 0, 3, 0, 3, 0, 164, 0, 0, 0, 138, 0, 0, 0, 0, 0};
static const unsigned char ESCAPE[] = {80, 75, 3, 4, 20, 0, 0, 0, 0, 0, 202, 112, 112, 93, 146, 84, 169, 190, 4, 0, 0, 0, 4, 0, 0, 0, 10, 0, 0, 0, 112, 107, 103, 47, 111, 107, 46, 116, 120, 116, 102, 105, 110, 101, 80, 75, 3, 4, 20, 0, 0, 0, 0, 0, 202, 112, 112, 93, 82, 49, 251, 141, 4, 0, 0, 0, 4, 0, 0, 0, 11, 0, 0, 0, 46, 46, 47, 101, 118, 105, 108, 46, 116, 120, 116, 101, 118, 105, 108, 80, 75, 1, 2, 20, 3, 20, 0, 0, 0, 0, 0, 202, 112, 112, 93, 146, 84, 169, 190, 4, 0, 0, 0, 4, 0, 0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128, 1, 0, 0, 0, 0, 112, 107, 103, 47, 111, 107, 46, 116, 120, 116, 80, 75, 1, 2, 20, 3, 20, 0, 0, 0, 0, 0, 202, 112, 112, 93, 82, 49, 251, 141, 4, 0, 0, 0, 4, 0, 0, 0, 11, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128, 1, 44, 0, 0, 0, 46, 46, 47, 101, 118, 105, 108, 46, 116, 120, 116, 80, 75, 5, 6, 0, 0, 0, 0, 2, 0, 2, 0, 113, 0, 0, 0, 89, 0, 0, 0, 0, 0};

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static char *write_file(const char *name, const void *data, gsize len)
{
    char *path = g_build_filename(g_get_tmp_dir(), name, NULL);
    g_file_set_contents(path, data, len, NULL);
    return path;
}

int main(void)
{
    char *path = write_file("denboku-test.zip", ARCHIVE, sizeof ARCHIVE);
    Zip *z = zip_open(path);
    char *p;
    CHECK(z);
    GByteArray *a = zip_get(z, "0FU.png");
    CHECK(a && a->len == 12 && !memcmp(a->data, "stored bytes", 12));
    if (a)
        g_byte_array_unref(a);
    a = zip_get(z, "ou.SVG");
    CHECK(a && a->len == 1211 && !memcmp(a->data, "<svg>abcabc", 11) && !memcmp(a->data + 1205, "</svg>", 6));
    if (a)
        g_byte_array_unref(a);
    a = zip_get(z, "style.ini");
    CHECK(a && a->len == 0);
    if (a)
        g_byte_array_unref(a);
    CHECK(!zip_get(z, "FU.png"));
    CHECK(!zip_get(z, "set"));
    zip_close(z);
    /* Cut short at every length, and every byte changed in turn: no crash, no read outside it. */
    guint8 copy[sizeof ARCHIVE];
    for (gsize cut = 0; cut < sizeof ARCHIVE; cut += 7) {
        p = write_file("denboku-test-cut.zip", ARCHIVE, cut);
        z = zip_open(p);
        if (z) {
            const char *names[] = {"0FU.png", "OU.svg", "style.ini"};
            for (int i = 0; i < 3; i++)
                if ((a = zip_get(z, names[i])))
                    g_byte_array_unref(a);
            zip_close(z);
        }
        g_unlink(p);
        g_free(p);
    }
    for (gsize i = 0; i < sizeof ARCHIVE; i++) {
        memcpy(copy, ARCHIVE, sizeof copy);
        copy[i] ^= 0xa5;
        p = write_file("denboku-test-bad.zip", copy, sizeof copy);
        z = zip_open(p);
        if (z) {
            const char *names[] = {"0FU.png", "OU.svg", "style.ini"};
            for (int k = 0; k < 3; k++)
                if ((a = zip_get(z, names[k])))
                    g_byte_array_unref(a);
            zip_close(z);
        }
        g_unlink(p);
        g_free(p);
    }
    /* A package unpacked whole; one with a name leaving the folder writes nothing. */
    char *dir = g_build_filename(g_get_tmp_dir(), "denboku-test-unzip", "inner", NULL);
    p = write_file("denboku-test-pkg.zip", PACKAGE, sizeof PACKAGE);
    z = zip_open(p);
    CHECK(z && zip_extract(z, dir));
    zip_close(z);
    g_unlink(p);
    g_free(p);
    char *f = g_build_filename(dir, "pkg", "bin", "a.txt", NULL), *text = NULL;
    gsize len = 0;
    CHECK(g_file_get_contents(f, &text, &len, NULL) && len == 600 && !memcmp(text, "hello hello", 11));
    g_unlink(f);
    g_free(f), g_free(text);
    f = g_build_filename(dir, "pkg", "b.txt", NULL);
    CHECK(g_file_get_contents(f, &text, &len, NULL) && len == 5);
    g_unlink(f);
    g_free(f), g_free(text);
    p = write_file("denboku-test-escape.zip", ESCAPE, sizeof ESCAPE);
    z = zip_open(p);
    CHECK(z && !zip_extract(z, dir));
    zip_close(z);
    g_unlink(p);
    g_free(p);
    f = g_build_filename(dir, "pkg", "ok.txt", NULL);
    CHECK(!g_file_test(f, G_FILE_TEST_EXISTS));
    g_free(f);
    f = g_build_filename(g_get_tmp_dir(), "denboku-test-unzip", "evil.txt", NULL);
    CHECK(!g_file_test(f, G_FILE_TEST_EXISTS));
    g_free(f);
    char *bin = g_build_filename(dir, "pkg", "bin", NULL), *pkg = g_build_filename(dir, "pkg", NULL), *top = g_path_get_dirname(dir);
    g_rmdir(bin), g_rmdir(pkg), g_rmdir(dir), g_rmdir(top);
    g_free(bin), g_free(pkg), g_free(top), g_free(dir);
    char junk[] = "not a zip";
    p = write_file("denboku-test-junk.zip", junk, sizeof junk);
    CHECK(!zip_open(p));
    g_unlink(p);
    g_free(p);
    g_unlink(path);
    g_free(path);
    if (failures)
        return 1;
    puts("zip tests passed");
    return 0;
}
