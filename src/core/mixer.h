/* The sounds played straight to the sound server (sound.c): each one turned into mono samples at
 * MIX_RATE, and a new one taking over from the one playing, which fades out. No GTK, no sound server. */
#ifndef MIXER_H
#define MIXER_H

#include <glib.h>

enum { MIX_RATE = 44100, MIX_FADE = MIX_RATE / 25 /* 40 ms */ };

typedef struct {
    gint16 *s;
    gsize n, at;
} Clip;

typedef struct {
    Clip *cur, *old; /* playing, and fading out */
    gsize faded;
} Mixer;

/* `frames` frames of 16-bit little-endian samples, `channels` each, at `rate`: mono at MIX_RATE. */
Clip *clip_new(const guint8 *samples, gsize frames, int channels, guint32 rate);
void clip_free(Clip *c);
/* `c` plays from now on; the one playing fades out. */
void mixer_take(Mixer *m, Clip *c);
gboolean mixer_busy(const Mixer *m);
/* The next `n` samples (silence once nothing plays). */
void mixer_fill(Mixer *m, gint16 *out, int n);
void mixer_clear(Mixer *m);

#endif
