"""Pen the Sheep score: "Pasture Haze" (the main track).

A continuation of the classic hex-pen puzzle's music, studied closely (not
sampled). What the study found, and what this does:

  - no melody and no beat: a shimmering wash. Soft, nearly pure tones (a
    little second harmonic, nothing inharmonic) swell in at random moments,
    about two and a half a second, hold, and fade, overlapping;
  - every tone is an ensemble: several copies spread about forty cents either
    side of the pitch, so each note shimmers and beats;
  - the colour is C6/9 (C D E G A) over a held low C, with passing major-
    seventh tints: a B over C for a while, then an F underneath (Fmaj9 over C);
  - the low C beats slowly (about half a cycle a second);
  - the whole track sits a few cents flat, warm, very wide, roomy;
  - birdsong in the meadow.

One seamless 48-second loop. The earlier "Long Grass" (long_grass.py) is
kept as a second track. Writes 48 kHz 16-bit WAV masters into masters/ and the
manifest (both tracks) into ../assets/audio. The game ships AAC encodings
(afconvert -f m4af -d aac -b 192000).
"""
import json
import os
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'engine'))
from synth import SR, make_ir, conv_reverb, fft_filter, LoopMix, tarr  # noqa: E402

OUT = os.path.join(os.path.dirname(__file__), '..', 'assets', 'audio')
MASTERS = os.path.join(os.path.dirname(__file__), 'masters')
rng = np.random.default_rng(31415)
LOOP = 48.0
FLAT = -4  # cents: the original sits a little flat
LEVELS = dict(wash=-18, bass=-23, birds=-24)


def hz(m, cents=0.0):
    return 440 * 2 ** ((m - 69 + (cents + FLAT) / 100) / 12)


def ensemble_tone(m, attack, hold, release, voices=5, spread=14, vel=1.0):
    """One soft tone as a chorus of detuned near-sines: swells in, holds, fades."""
    dur = attack + hold + release
    n = int(dur * SR)
    t = tarr(n)
    env = np.where(t < attack, np.sin(np.pi / 2 * t / attack) ** 2, 1.0)
    env = env * np.where(t > attack + hold, np.cos(np.pi / 2 * np.clip((t - attack - hold) / release, 0, 1)) ** 2, 1.0)
    L = np.zeros(n)
    R = np.zeros(n)
    for v in range(voices):
        c = rng.uniform(-spread, spread)
        f = hz(m, c) * (1 + .0035 * np.sin(2 * np.pi * rng.uniform(.25, .6) * t + rng.uniform(0, 6.28)))  # a slow, gentle drift
        ph = 2 * np.pi * np.cumsum(f) / SR + rng.uniform(0, 6.28)
        y = np.sin(ph) + .32 * np.sin(2 * ph + rng.uniform(0, 6.28)) + .06 * np.sin(3 * ph)
        p = rng.uniform(-1, 1)  # each copy somewhere else in the stereo field
        L += y * np.cos((p + 1) * np.pi / 4)
        R += y * np.sin((p + 1) * np.pi / 4)
    return np.stack([L, R]) * env * vel / voices


def arrange():
    L = int(LOOP * SR)
    mix = LoopMix(L, circular=True)
    # the colour of each stretch of the loop: which notes the wash draws on (weights), and the bass beneath
    #   0-16 s  C6/9 over C          16-28 s  with a B: Cmaj9          28-40 s  F underneath: Fmaj9 over C         40-48 s  C6/9
    # only the pentatonic (C D E G A): no sevenths, no F; the stretches differ only in which notes lead
    sections = [
        (0, 16, {60: 2, 64: 3, 67: 3, 69: 2, 72: 3, 74: 2, 76: 3, 79: 2, 81: 1, 84: 1}, [36, 48, 55]),
        (16, 28, {62: 1, 64: 2, 67: 3, 69: 3, 72: 3, 74: 2, 76: 3, 79: 2, 81: 2, 84: 1}, [36, 48, 55]),
        (28, 40, {60: 2, 64: 3, 67: 2, 69: 3, 72: 3, 76: 3, 79: 2, 81: 2, 84: 1}, [36, 48, 57]),
        (40, 48, {60: 2, 64: 3, 67: 3, 69: 2, 72: 3, 76: 3, 79: 2, 81: 1, 84: 1}, [36, 48, 55]),
    ]
    for a, b, weights, bass in sections:
        notes = list(weights)
        p = np.array([weights[k] for k in notes], dtype=float)
        p /= p.sum()
        t = a
        while t < b:
            m = int(rng.choice(notes, p=p))
            hold = rng.uniform(1.2, 3.5)
            att, rel, vel = rng.uniform(.5, 1.1), rng.uniform(1.8, 3.2), rng.uniform(.5, 1.0)
            y = ensemble_tone(m, att, hold, rel, vel=vel)
            r = rng.uniform()
            if r < .25 and m - 12 >= 55:  # with its octave below
                y = y + ensemble_tone(m - 12, att, hold, rel, vel=vel * .6)
            elif r < .4 and m - 7 >= 55 and (m - 7) % 12 in (0, 2, 4, 7, 9):  # with its fifth below
                y = y + ensemble_tone(m - 7, att, hold, rel, vel=vel * .55)
            start = int(t * SR)
            for ch in range(2):
                seg = y[ch]
                for k in range(0, len(seg), L):  # wrap round the loop
                    part = seg[k:k + L]
                    s = (start + k) % L
                    e = min(L, s + len(part))
                    mix.bus('wash')[ch, s:e] += part[:e - s]
                    if e - s < len(part):
                        mix.bus('wash')[ch, :len(part) - (e - s)] += part[e - s:]
            t += rng.exponential(1 / 2.3)
        # the bass: held through the stretch, overlapping into the next, beating slowly (two copies a hair apart)
        for m in bass:
            dur = (b - a) + 3
            n = int(dur * SR)
            tt = tarr(n)
            env = np.minimum(1, tt / 1.5) * np.minimum(1, (dur - tt) / 2.0)
            y = (np.sin(2 * np.pi * hz(m) * tt) + .6 * np.sin(2 * np.pi * (hz(m) + .25) * tt) + .2 * np.sin(4 * np.pi * hz(m) * tt)) * env * (1 if m == 48 else .45 if m == 36 else .55)
            start = int((a - 1.5) * SR) % L
            for ch in range(2):
                s = start
                e = min(L, s + n)
                mix.bus('bass')[ch, s:e] += y[:e - s] * (.9 if ch == 0 else 1.0)
                if e - s < n:
                    mix.bus('bass')[ch, :n - (e - s)] += y[e - s:] * (.9 if ch == 0 else 1.0)
    # birds: little chirping figures, near and far
    for j in range(20):
        at = rng.uniform(0, LOOP)
        pan = rng.uniform(-.9, .9)
        f0 = rng.uniform(2400, 4400)
        for q in range(rng.integers(2, 7)):
            n = int(rng.uniform(.04, .1) * SR)
            tt = tarr(n)
            f = f0 * (1 + rng.uniform(-.3, .35) * tt / tt[-1]) * (1 + .05 * np.sin(2 * np.pi * 40 * tt))
            y = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.sin(np.pi * tt / tt[-1]) ** 2
            mix.add('birds', y, int((at + q * rng.uniform(.08, .17)) * SR) % L, pan)
    return mix, L


def bus_rms_db(b):
    return 20 * np.log10(max(np.sqrt(np.mean(b.mean(0) ** 2)), 1e-9))


def master(mix, L):
    out = np.zeros((2, L))
    birds = np.zeros((2, L))
    for name, b in mix.buses.items():
        g = 10 ** ((LEVELS.get(name, -22) - bus_rms_db(b)) / 20)
        if name == 'birds':
            birds += b * g
        else:
            out += b * g
    ir = make_ir(length=3.6, rt_low=2.4, rt_mid=2.8, rt_high=2.2)  # airy: the highs ring on
    out = out + conv_reverb(out * .5, ir, True) * .5
    out = np.stack([fft_filter(c, 35, 6000, 2, circular=True) for c in out])
    # as wide as the original (mid/side)
    mid, side = (out[0] + out[1]) / 2, (out[0] - out[1]) / 2
    out = np.stack([mid + side * 1.9, mid - side * 1.9])
    out += birds + conv_reverb(birds * .4, make_ir(length=1.5), True) * .3
    # a gentle breathing of the whole across the loop (seamless: one cycle)
    t = np.arange(L) / L
    out *= .85 + .15 * np.sin(2 * np.pi * t) ** 2
    return out


def finish(x, target_rms_db=-21.0, ceiling_db=-3.0):
    x = x * 10 ** ((target_rms_db - bus_rms_db(x)) / 20)
    c = 10 ** (ceiling_db / 20)
    return np.tanh(x / c) * c


def write(path, x):
    y = np.clip(x, -1, 1)
    with wave.open(path, 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes((y.T * 32767).astype('<i2').tobytes())


def stinger(kind):
    n = int(3.2 * SR)
    out = np.zeros((2, n))

    def put(y, sec, g=1.0):
        s = int(sec * SR)
        e = min(n, s + y.shape[1])
        out[:, s:e] += g * y[:, :e - s]

    if kind == 'penned':  # the wash blooms: C, E, G, C, E swelling in one after another
        for k, m in enumerate((60, 64, 67, 72, 76)):
            put(ensemble_tone(m, .25, .8, 1.4, vel=.8), k * .14)
    else:  # got away: two tones drifting down
        for k, m in enumerate((76, 69)):
            put(ensemble_tone(m, .2, .5, 1.2, vel=.7), k * .35)
    out = out + conv_reverb(out * .5, make_ir(length=2.6), False)[:, :n] * .5
    fade = int(.4 * SR)
    out[:, -fade:] *= np.linspace(1, 0, fade)
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(MASTERS, exist_ok=True)
    mix, L = arrange()
    write(os.path.join(MASTERS, 'sh_music.wav'), finish(master(mix, L)))
    manifest = {'music': [{'id': 'sh_music', 'loop_end_sample_exclusive': L, 'seconds': LOOP},
                          {'id': 'sh_music_grass', 'loop_end_sample_exclusive': int(64.0 * SR), 'seconds': 64.0}], 'stingers': []}
    for kind in ('penned', 'away'):
        write(os.path.join(MASTERS, f'sh_tune_{kind}.wav'), finish(stinger(kind), -19, -3))
        manifest['stingers'].append(f'sh_tune_{kind}')
    json.dump(manifest, open(os.path.join(OUT, 'sheep_audio_manifest.json'), 'w'), indent=1)
    print('ok', L / SR)


if __name__ == '__main__':
    main()
