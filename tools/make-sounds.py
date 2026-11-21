#!/usr/bin/env python3
"""Synthesize every game sound into data/sounds/ (16-bit mono WAV): make-sounds.py

A sound set is a folder with move.wav and capture.wav and its own effects (start, check, mate,
win, lose, draw, notify, tick) in the same timbre (chosen in the game under Sound; the first one listed
plays until another is chosen). "Pachi" sets follow a real one (measured on recordings):
almost all energy above 3 kHz, peaks around 4-7 kHz, down 20 dB within ~20 ms; a capture adds a
short low thud. Every sound of a kind is as loud in every set (LOUDNESS), so changing sets keeps the volume.
Koto is a string built harmonic by harmonic from levels measured on recordings of a real one (tools/strings.json; needs numpy); Retro is an old console's square wave."""
import json, math, os, random, struct, wave

import numpy as np

RATE = 44100
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data", "sounds")


def bandpass(x, f0, q):
    w = 2 * math.pi * f0 / RATE
    alpha = math.sin(w) / (2 * q)
    a0, a1, a2 = 1 + alpha, -2 * math.cos(w), 1 - alpha
    y, x1, x2, y1, y2 = [], 0, 0, 0, 0
    for s in x:
        v = (alpha * s - alpha * x2 - a1 * y1 - a2 * y2) / a0
        x2, x1, y2, y1 = x1, s, y1, v
        y.append(v)
    return y


def strike(modes, bands, noise_decay, thud=None, seconds=0.22, seed=3, ramp=20000):
    """Noise through band-passes (the contact), damped sines (what rings), and maybe a thud."""
    random.seed(seed)
    n = int(RATE * seconds)
    noise = [random.uniform(-1, 1) * math.exp(-i / RATE * noise_decay) for i in range(n)]
    out = [0.0] * n
    for f0, q, g in bands:
        for i, v in enumerate(bandpass(noise, f0, q)):
            out[i] += v * g
    for f, decay, g in modes + ([thud] if thud else []):
        ph = random.uniform(0, 2 * math.pi) if f > 200 else 0
        r = ramp if f > 200 else 3000
        for i in range(n):
            t = i / RATE
            out[i] += g * math.sin(2 * math.pi * f * t + ph) * math.exp(-t * decay) * min(1, t * r)
    return out


def mix(*parts):
    """(offset seconds, samples, gain) parts added together."""
    n = max(int(o * RATE) + len(s) for o, s, g in parts)
    out = [0.0] * n
    for o, s, g in parts:
        k = int(o * RATE)
        for i, v in enumerate(s):
            out[k + i] += v * g
    return out


# How loud each sound is, the same in every set, so that changing sets keeps the volume: the loudest 50 ms
# (above 150 Hz, which a short sound barely gives the ear), in dB. A move is the reference; the end of a game
# is a little louder, the clock's tick and a notice quieter.
LOUDNESS = dict(move=-22, capture=-20, check=-19.5, mate=-18.5, wrong=-20, start=-22, notify=-24, tick=-29,
                win=-18, lose=-19, draw=-19)
PEAK = 0.89  # -1 dBFS at most, room for the player's resampling


def loudness(samples, rate):
    a = 1 / (1 + 2 * math.pi * 150 / rate)
    w, acc, best, px, py, sq = int(rate * 0.05), 0.0, 0.0, 0.0, 0.0, []
    for i, x in enumerate(samples):
        py = a * (py + x - px)
        px = x
        sq.append(py * py)
        acc += sq[-1] - (sq[i - w] if i >= w else 0)
        best = max(best, acc)
    return 10 * math.log10(best / min(w, len(samples)) + 1e-12)


def write(name, samples, half=False):
    """At the loudness of its kind (LOUDNESS), at half the rate when nothing above 10 kHz is heard in it (half the
    size; `half` forces it), and without the tail below -60 dB."""
    path = os.path.join(OUT, name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    x = np.asarray(samples, dtype=float)
    spectrum = np.fft.rfft(x)
    power = np.abs(spectrum) ** 2
    high = power[np.fft.rfftfreq(len(x), 1 / RATE) > 10000].sum() / (power.sum() or 1)
    rate = RATE
    if half or high < 0.002:
        rate = RATE // 2
        x = np.fft.irfft(spectrum[:len(x) // 2 // 2 + 1], len(x) // 2) / 2
    samples = list(x)
    peak = max(abs(s) for s in samples) or 1
    last = max(i for i, s in enumerate(samples) if abs(s) > peak / 1000)
    samples = samples[:last + 1 + rate // 500]  # 2 ms past it, fading out
    for i in range(1, rate // 500 + 1):
        samples[-i] *= i / (rate // 500 + 1)
    gain = 10 ** ((LOUDNESS[os.path.basename(name)[:-4]] - loudness(samples, rate)) / 20)
    gain = min(gain, PEAK / peak)
    with wave.open(path, "wb") as w:
        w.setnchannels(1), w.setsampwidth(2), w.setframerate(rate)
        w.writeframes(b"".join(struct.pack("<h", int(s * gain * 32767)) for s in samples))


def chime(notes, seconds, decay, overtone=0.0):
    """Struck tones, (frequency, start, gain) each; `overtone` adds a fourth harmonic that dies fast (a marimba)."""
    return [sum(g * math.exp(-max(0, i / RATE - o) * decay) *
                (math.sin(2 * math.pi * f * i / RATE) +
                 overtone * math.sin(8 * math.pi * f * i / RATE) * math.exp(-max(0, i / RATE - o) * decay * 4))
                for f, o, g in notes if i / RATE >= o) for i in range(int(RATE * seconds))]


# Bubbles: a sine whose pitch rises fast as it dies away, as a bubble does when it closes.
def bubble(f0, f1, seconds, decay):
    out, ph = [], 0.0
    for i in range(int(RATE * seconds)):
        t = i / RATE
        ph += 2 * math.pi * (f0 + (f1 - f0) * (1 - math.exp(-t * 60))) / RATE
        out.append(math.sin(ph) * math.exp(-t * decay) * min(1, t * 2000))
    return out


def bubbles(notes, seconds):
    """A tune of bubbles, each closing on its note."""
    return mix(*[(o, bubble(f * 0.45, f, min(0.3, seconds - o), 14), g) for f, o, g in notes])


def effects(folder, hit, tone):
    """A set's effects in its own sound: `hit(seed, pitch)` a knock, `tone(notes, seconds)` a tune."""
    write(f"{folder}/start.wav", mix((0, hit(1, 1.0), 1.0), (0.22, hit(2, 0.9), 0.8)))
    write(f"{folder}/check.wav", mix((0, hit(5, 1.0), 1.0), (0.012, hit(6, 1.0), 0.35)))
    # The move that wins on the board (checkmate, the lion's try): lower and heavier than check, struck twice.
    write(f"{folder}/mate.wav", mix((0, hit(12, 0.8), 1.0), (0.014, hit(13, 0.8), 0.5), (0.16, hit(14, 0.7), 0.9)))
    # A mate problem's move that is not the mate: two low knocks, falling.
    write(f"{folder}/wrong.wav", mix((0, hit(15, 0.62), 1.0), (0.11, hit(16, 0.5), 0.85)))
    write(f"{folder}/notify.wav", mix((0, hit(9, 1.25), 1.0), (0.09, hit(10, 1.45), 0.8)))
    write(f"{folder}/tick.wav", hit(11, 0.8)[:int(RATE * 0.06)])
    win = [(523.3, 0, 0.5), (784, 0.12, 0.4), (1046.5, 0.24, 0.3)]
    write(f"{folder}/win.wav", mix((0, hit(3, 1.0), 0.8), (0.18, tone(win, 1.1), 1.0)), half=True)
    write(f"{folder}/lose.wav", tone([(392, 0, 0.5), (311.1, 0.2, 0.45)], 1.1), half=True)
    write(f"{folder}/draw.wav", tone([(440, 0, 0.5), (660, 0, 0.25)], 1.0), half=True)


def scaled(parts, k):
    return [(f * k,) + tuple(rest) for f, *rest in parts]


# Piece sound sets, in the order of the chooser (the number is only for the order): modes, bands,
# noise decay, and how fast the bells of their tunes die.
# Pachi peaks at 5.6 kHz with half its energy at 3-7 kHz, as the real one; Soft is the same click muffled, at 3 kHz.
PACHI = {
    "2-Pachi": ([(4300, 130, 0.2), (5560, 160, 0.18), (6840, 190, 0.16)], [(5000, 0.9, 1.0), (6800, 1.1, 0.6), (9500, 1.0, 0.3)],
                170, 3.2),
    "4-Soft": ([(3000, 110, 0.2), (4000, 140, 0.12)], [(3000, 0.7, 1.0), (4500, 1.1, 0.4), (1800, 1.2, 0.5)], 130, 2.6),
}
for name, (modes, bands, nd, ring) in PACHI.items():
    write(f"{name}/move.wav", strike(modes, bands, nd))
    write(f"{name}/capture.wav", strike([(f * 0.97, d, g) for f, d, g in modes], bands, nd * 0.85,
                                        thud=(70, 45, 0.06), seconds=0.25))
    effects(name, lambda seed, k, m=modes, b=bands, d=nd: strike(scaled(m, k), scaled(b, k), d, seconds=0.2, seed=seed),
            lambda notes, sec, r=ring: chime(notes, sec, r))

# A wooden knock with a board under it, for whoever prefers it to a pachi; its tunes on a marimba.
WOOD = ([(2100, 160, 0.3), (3300, 230, 0.15), (540, 55, 0.4), (260, 40, 0.25)], 800)
CLICK = [(3200, 1.2, 1.0), (6500, 1.5, 0.6), (1600, 1.0, 0.5)]
write("5-Wood/move.wav", strike(WOOD[0], CLICK, WOOD[1], seconds=0.25, seed=7, ramp=8000))
write("5-Wood/capture.wav", strike([(f * 0.94, d * 0.9, g) for f, d, g in WOOD[0]], CLICK, WOOD[1] * 0.83,
                                   seconds=0.3, seed=8, ramp=8000))
effects("5-Wood", lambda seed, k: strike(scaled(WOOD[0], k), scaled(CLICK, k), WOOD[1], seconds=0.25, seed=seed, ramp=8000),
        lambda notes, sec: chime(notes, sec, 6, overtone=0.35))

def burst(seconds=0.025, seed=21):
    """The pop of a bubble bursting: a short band of noise."""
    random.seed(seed)
    noise = [random.uniform(-1, 1) * math.exp(-i / RATE * 260) for i in range(int(RATE * seconds))]
    return bandpass(noise, 2600, 1.2)


# Bubbles, a little low; the tunes are bubbles too. A capture: a big bubble, then a small one that
# bursts, apart so that they do not beat. Check: two bubbles going up, as an alert.
write("6-Bubbles/move.wav", bubble(420, 1050, 0.13, 42))
write("6-Bubbles/capture.wav", mix((0, bubble(290, 800, 0.15, 32), 1.0), (0.09, bubble(720, 1750, 0.08, 58), 0.6),
                                   (0.09, burst(), 0.5)))
effects("6-Bubbles", lambda seed, k: bubble(420 * k, 1050 * k, 0.13, 42), bubbles)
write("6-Bubbles/check.wav", mix((0, bubble(520, 1300, 0.1, 44), 0.85), (0.11, bubble(690, 1740, 0.13, 36), 1.0)))


# The koto: each harmonic's level over time read off tuning recordings of a real one (tools/strings.json, by
# harmonic number, as where the pick plucks shapes them; only these levels are kept), and what those recordings
# also show: the string starts up to 1.5% sharp and settles in 50 ms, the ivory pick (爪) scrapes the silk for 15 ms (2% of
# the energy above 3 kHz), the harmonics from the 3rd up beat by 1-1.5 dB (the string's two planes), and a room.
KOTO = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "strings.json")))["koto"]


def room(x, seconds=0.45, wet=0.12, seed=7):
    """A small room: the sound convolved with decaying noise, darker as it dies."""
    rng = np.random.default_rng(seed)
    n = int(RATE * seconds)
    t = np.arange(n) / RATE
    ir = rng.normal(0, 1, n) * np.exp(-t * 6.9 / seconds)
    ir = np.convolve(ir, np.ones(6) / 6, "same")
    ir *= wet / np.sqrt((ir ** 2).sum())
    y = np.fft.irfft(np.fft.rfft(x, len(x) + n) * np.fft.rfft(ir, len(x) + n))[:len(x) + n]
    y[:len(x)] += x
    return y


def koto(f, seconds=1.4, seed=31):
    rng = np.random.default_rng(seed)
    times, db = np.array(KOTO["times"]), np.array(KOTO["db"])
    n = int(RATE * seconds)
    t = np.arange(n) / RATE
    bend = 1 + rng.uniform(0.004, 0.015) * np.exp(-t / 0.02)
    base = 2 * np.pi * f * np.cumsum(bend) / RATE
    out = np.zeros(n)
    for k in range(1, len(db) + 1):
        if f * k > RATE * 0.45:
            break
        level = db[k - 1]
        slope = min(0.0, (level[-1] - level[-2]) / (times[-1] - times[-2]))
        env = np.interp(t, times, level) + np.where(t > times[-1], (t - times[-1]) * slope, 0)
        if k >= 3:  # the two planes of the string, a little apart
            env = env + 1.2 * np.sin(2 * np.pi * rng.uniform(1.5, 4) * t + rng.uniform(0, 2 * np.pi))
        out += 10 ** (env / 20) * np.sin(k * base + rng.uniform(0, 2 * np.pi))
    m = int(RATE * 0.015)
    scrape = rng.normal(0, 1, m) * np.exp(-np.arange(m) / m * 3)
    scrape = np.array(bandpass(list(scrape), 4500, 0.7))
    out[:m] += scrape * 0.15 * np.abs(out[:int(RATE * 0.05)]).max() / (np.abs(scrape).max() + 1e-9)
    out *= np.minimum(1, np.minimum(np.arange(n) / 30, (n - np.arange(n)) / (RATE * 0.25)))
    return room(out).tolist()


def tune_of(instrument, octave=0.5):
    """The tunes an octave down: on these strings they sit in the instrument's range."""
    return lambda notes, seconds: mix(*[(o, instrument(f * octave, min(1.6, seconds - o + 0.5), seed=60 + i), g)
                                        for i, (f, o, g) in enumerate(notes)])


# Tuned 平調子 (D G A B♭ D): a move is the D string, a capture G then D.
write("7-Koto/move.wav", koto(293.7), half=True)
write("7-Koto/capture.wav", mix((0, koto(196, seed=32), 1.0), (0.09, koto(293.7, seed=33), 0.8)), half=True)
effects("7-Koto", lambda seed, k: koto(293.7 * k, 0.9, seed=seed), tune_of(koto))


def square(f, seconds, decay, slide=1.0):
    """A square wave made of its odd harmonics below 10 kHz (no aliasing), its pitch sliding by `slide`."""
    out, ph = [], 0.0
    for i in range(int(RATE * seconds)):
        t = i / RATE
        fi = f * slide ** (t / seconds)
        ph += 2 * math.pi * fi / RATE
        v = sum(math.sin(h * ph) / h for h in range(1, int(10000 / fi) + 1, 2))
        out.append(v * math.exp(-t * decay) * min(1, i / 40))
    return out


def chip_tune(notes, seconds):
    return mix(*[(o, square(f, min(0.25, seconds - o), 6), g) for f, o, g in notes])


# Eight-bit: a move is a short blip, a capture a blip falling; the tunes as an old console played them.
write("8-Retro/move.wav", square(880, 0.05, 40))
write("8-Retro/capture.wav", mix((0, square(880, 0.05, 40), 1.0), (0.05, square(660, 0.08, 30, slide=0.7), 1.0)))
effects("8-Retro", lambda seed, k: square(880 * k, 0.06, 35, slide=1.0 + (seed % 3) * 0.1), chip_tune)


def chirp(f0, f1, seconds, trill=0.0, seed=51):
    """A bird's chirp: a whistle sweeping from f0 to f1 (mostly at the start), a little second harmonic, and
    `trill` the depth of a fast warble."""
    random.seed(seed)
    rate = random.uniform(55, 75)
    out, ph = [], 0.0
    n = int(RATE * seconds)
    for i in range(n):
        t = i / RATE
        f = f0 + (f1 - f0) * (1 - math.exp(-t / seconds * 4)) / (1 - math.exp(-4))
        f *= 1 + trill * math.sin(2 * math.pi * rate * t)
        ph += 2 * math.pi * f / RATE
        env = math.sin(math.pi * min(1, i / n)) ** 1.5
        out.append(env * (math.sin(ph) + 0.18 * math.sin(2 * ph)))
    return out


def whistle(notes, seconds):
    """A tune whistled as a bird would: each note an octave up, gliding onto its pitch, with a slow warble."""
    return mix(*[(o, chirp(f * 1.8, f * 2, min(0.32, seconds - o), trill=0.012, seed=60 + i), g) for i, (f, o, g) in enumerate(notes)])


# Birds, for Tori shōgi: a move is a short chirp, a capture two (one falling, one rising), check a call of three
# rising; the tunes whistled.
write("9-Birds/move.wav", chirp(2600, 3700, 0.07))
write("9-Birds/capture.wav", mix((0, chirp(3900, 2700, 0.07, seed=52), 1.0), (0.085, chirp(2700, 4000, 0.08, seed=53), 0.9)))
effects("9-Birds", lambda seed, k: chirp(2600 * k, 3700 * k, 0.07, trill=0.02 * (seed % 2), seed=seed), whistle)
write("9-Birds/check.wav", mix(*[(i * 0.075, chirp(2800 + 300 * i, 3800 + 300 * i, 0.06, seed=54 + i), 0.8 + 0.1 * i) for i in range(3)]))


# Hyōshigi (拍子木), the kabuki theatre's two oak bars struck together: a bar's modes (1 : 2.76 : 5.40) at
# 1.5 kHz, a second bar a little higher, and the contact's crack. The start is the kabuki's curtain: two full
# strokes (柝の頭), a short きざみ that quickens but never closer than 80 ms (the bars ring that long) and
# lighter, then the closing stroke (止め柝); each stroke at its own peak, so that overlaps do not make it waver.
def clack(f=1500, seed=71):
    bar = lambda g: [(f * g, 45, 0.8), (f * g * 2.756, 70, 0.35), (f * g * 5.404, 120, 0.15)]
    return strike(bar(1.0) + bar(1.05)[:2], [(2500, 1.0, 0.6), (5000, 1.3, 0.3)], 600, seconds=0.2, seed=seed, ramp=30000)


write("11-Clappers/move.wav", clack())
write("11-Clappers/capture.wav", mix((0, clack(seed=72), 1.0), (0.11, clack(1400, seed=73), 0.9)))
effects("11-Clappers", lambda seed, k: clack(1500 * k, seed=seed),
        lambda notes, sec: mix(*[(o, clack(f * 2, seed=80 + i), g) for i, (f, o, g) in enumerate(notes)]))
def even(samples):
    peak = max(abs(v) for v in samples)
    return [v / peak for v in samples]


KIZAMI = [(0, 1.0), (0.3, 1.0), (0.58, 0.5), (0.72, 0.55), (0.84, 0.6), (0.94, 0.65), (1.02, 0.7), (1.24, 0.95)]
write("11-Clappers/start.wav", mix(*[(t, even(clack(seed=90 + i)), g) for i, (t, g) in enumerate(KIZAMI)]))


# Bamboo: a tube closed at a node, struck with a stick (竹筒): the air inside sounds its odd harmonics
# (f, 3f, 5f) under the wall's dry click, a hollow "pok". A capture is two tubes, the second lower.
def tube(f=420, seed=101, seconds=0.3):
    modes = [(f, 28, 1.0), (f * 3.02, 45, 0.35), (f * 5.1, 70, 0.12), (f * 7.3, 110, 0.05)]
    return strike(modes, [(2600, 1.2, 0.5), (5200, 1.5, 0.2)], 900, seconds=seconds, seed=seed, ramp=15000)


write("12-Bamboo/move.wav", tube())
write("12-Bamboo/capture.wav", mix((0, tube(seed=102), 1.0), (0.09, tube(380, seed=103), 0.85)))
effects("12-Bamboo", lambda seed, k: tube(420 * k, seed=seed),
        lambda notes, sec: mix(*[(o, tube(f, seed=110 + i, seconds=min(0.6, sec - o)), g) for i, (f, o, g) in enumerate(notes)]))


# Taiko (太鼓): the big drum's skin struck in the middle ("don", its pitch settling as the skin relaxes, the
# membrane's modes 1 : 1.59 : 2.14 and the stick's thump), and the small tight drum of the ensemble (締太鼓,
# "ten", higher and short) for check and the clock. A capture is "don-don", and a game opens "don, don-don".
def don(f=95, seconds=0.45, seed=171, k=1.0, decay=7):
    random.seed(seed)
    n = int(RATE * seconds)
    out = [0.0] * n
    f *= k
    for ratio, d, g in [(1.0, decay, 1.0), (1.59, decay * 1.6, 0.35), (2.14, decay * 2.2, 0.2), (2.3, decay * 2.6, 0.12)]:
        ph = 0.0
        for i in range(n):
            t = i / RATE
            ph += 2 * math.pi * f * ratio * (1 + 0.18 * math.exp(-t * 25)) / RATE
            out[i] += g * math.sin(ph) * math.exp(-t * d) * min(1, t * 4000)
    thump = [random.uniform(-1, 1) * math.exp(-i / RATE * 120) for i in range(n)]
    for i, v in enumerate(bandpass(thump, 420 * k, 0.9)):
        out[i] += v * 1.2
    return out


def ten(k=1.0, seed=181):
    return don(330, 0.18, seed=seed, k=k, decay=22)


write("13-Taiko/move.wav", don())
write("13-Taiko/capture.wav", mix((0, don(seconds=0.3, seed=172), 1.0), (0.13, don(seed=173, k=0.97), 1.0)))
effects("13-Taiko", lambda seed, k: don(seconds=0.4, seed=seed, k=k),
        lambda notes, sec: mix(*[(o, don(f / 5, min(0.8, sec - o), seed=190 + i), g) for i, (f, o, g) in enumerate(notes)]))
write("13-Taiko/check.wav", mix((0, ten(seed=182), 1.0), (0.11, ten(1.04, seed=183), 0.9)))
write("13-Taiko/tick.wav", ten(0.95, seed=184)[:int(RATE * 0.06)])
write("13-Taiko/start.wav", mix((0, even(don(seconds=0.2, seed=200)), 1.0), (0.17, even(don(seconds=0.12, seed=201)), 0.75),
                                (0.27, even(don(seconds=0.35, seed=202)), 1.0)))
