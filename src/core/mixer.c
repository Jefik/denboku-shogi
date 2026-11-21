#include "mixer.h"

Clip *clip_new(const guint8 *samples, gsize frames, int channels, guint32 rate)
{
    Clip *c = g_new0(Clip, 1);
    channels = MAX(1, channels);
    c->n = rate && frames ? (gsize) ((double) frames * MIX_RATE / rate) : 0;
    c->s = g_new(gint16, c->n + 1);
    for (gsize i = 0; i < c->n; i++) { /* linearly between the samples */
        double x = (double) i * rate / MIX_RATE, f = x - (gsize) x;
        gsize k = MIN((gsize) x, frames - 1), k2 = MIN(k + 1, frames - 1);
        double a = 0, b = 0;
        for (int ch = 0; ch < channels; ch++) {
            a += (gint16) GUINT16_FROM_LE(*(const guint16 *) (samples + (k * channels + ch) * 2));
            b += (gint16) GUINT16_FROM_LE(*(const guint16 *) (samples + (k2 * channels + ch) * 2));
        }
        c->s[i] = (gint16) ((a + (b - a) * f) / channels);
    }
    return c;
}

void clip_free(Clip *c)
{
    if (c)
        g_free(c->s), g_free(c);
}

void mixer_take(Mixer *m, Clip *c)
{
    clip_free(m->old);
    m->old = m->cur, m->cur = c, m->faded = 0;
}

gboolean mixer_busy(const Mixer *m) { return m->cur || m->old; }

void mixer_fill(Mixer *m, gint16 *out, int n)
{
    for (int i = 0; i < n; i++) {
        int v = m->cur && m->cur->at < m->cur->n ? m->cur->s[m->cur->at++] : 0;
        if (m->old && m->old->at < m->old->n && m->faded < MIX_FADE)
            v += m->old->s[m->old->at++] * (int) (MIX_FADE - m->faded++) / MIX_FADE;
        out[i] = (gint16) CLAMP(v, -32768, 32767);
    }
    if (m->old && (m->old->at >= m->old->n || m->faded >= MIX_FADE)) {
        clip_free(m->old);
        m->old = NULL;
    }
    if (m->cur && m->cur->at >= m->cur->n) {
        clip_free(m->cur);
        m->cur = NULL;
    }
}

void mixer_clear(Mixer *m)
{
    clip_free(m->cur), clip_free(m->old);
    *m = (Mixer) {0};
}
