/* The sound mixer: rates and channels to mono at MIX_RATE, and a new sound taking over at once. */
#include "mixer.h"

#include <stdio.h>
#include <stdlib.h>

static int failures;
#define CHECK(c) ((c) ? (void) 0 : (void) (printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c), failures++))

/* `frames` frames of a constant value. */
static Clip *constant(gint16 v, gsize frames, int channels, guint32 rate)
{
    guint8 *b = g_malloc(frames * channels * 2);
    for (gsize i = 0; i < frames * channels; i++)
        b[2 * i] = (guint8) (v & 0xff), b[2 * i + 1] = (guint8) ((v >> 8) & 0xff);
    Clip *c = clip_new(b, frames, channels, rate);
    g_free(b);
    return c;
}

int main(void)
{
    Clip *half = constant(1000, 22050, 1, 22050), *stereo = constant(-2000, 4410, 2, 44100);
    CHECK(half->n == MIX_RATE && half->s[0] == 1000 && half->s[half->n - 1] == 1000);
    CHECK(stereo->n == 4410 && stereo->s[100] == -2000);
    clip_free(stereo);
    Clip *empty = clip_new(NULL, 0, 1, 44100);
    CHECK(empty->n == 0);
    clip_free(empty);

    /* One second of 1000 playing; after 10 ms a sound of 100 takes over: the first fades out in MIX_FADE. */
    Mixer m = {0};
    gint16 out[MIX_RATE];
    mixer_take(&m, half);
    mixer_fill(&m, out, 441);
    CHECK(out[0] == 1000 && out[440] == 1000);
    mixer_take(&m, constant(100, 4410, 1, 44100));
    mixer_fill(&m, out, 4410);
    CHECK(out[0] == 1100);                          /* both, the old at full level */
    CHECK(out[MIX_FADE / 2] > 500 && out[MIX_FADE / 2] < 700);
    CHECK(out[MIX_FADE + 10] == 100);               /* the old one gone */
    CHECK(out[4409] == 100 && !mixer_busy(&m));     /* and the new one ended: no backlog */
    mixer_fill(&m, out, 10);
    CHECK(out[5] == 0);

    /* Many sounds in quick succession: what plays is always the latest, never a queue of them. */
    for (int i = 0; i < 20; i++) {
        mixer_take(&m, constant((gint16) (i * 10), 44100, 1, 44100));
        mixer_fill(&m, out, 100);
    }
    mixer_fill(&m, out, MIX_FADE + 1);
    CHECK(out[MIX_FADE] == 190);
    mixer_clear(&m);
    CHECK(!mixer_busy(&m));

    if (failures)
        return 1;
    printf("mixer tests passed\n");
    return 0;
}
