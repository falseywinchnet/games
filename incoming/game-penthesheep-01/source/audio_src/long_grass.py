"""Pen the Sheep score: "Long Grass" (the second track; every even meadow).

Written as a continuation of the classic hex-pen puzzle's music (studied, not
sampled): C major pentatonic over a held C, no fixed beat, soft nearly pure
flute and ocarina tones drifting in short phrases, a few harp sparkles, all of
it warm and low-passed below about two kilohertz, very wide and roomy, with a
slow swell and fall across the loop, and birdsong in the meadow. The melody
is new. One seamless 64-second loop, plus little stingers (penned, got away).

Everything is synthesized (engine/synth.py); no samples. Writes 48 kHz 16-bit
WAV masters into masters/ and the manifest into ../assets/audio. The game
ships AAC encodings of the masters (afconvert -f m4af -d aac -b 192000).
"""
import json
import os
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'engine'))
from synth import SR, render_note, make_ir, conv_reverb, fft_filter, LoopMix, tarr  # noqa: E402

OUT = os.path.join(os.path.dirname(__file__), '..', 'assets', 'audio')
MASTERS = os.path.join(os.path.dirname(__file__), 'masters')
rng = np.random.default_rng(2611)

PENTA = [0, 2, 4, 7, 9]  # C D E G A


def pen(i, base=60):
    """The i-th pentatonic step above middle C (negative goes down)."""
    return base + PENTA[i % 5] + 12 * (i // 5)


def note(inst, m, dur, vel):
    return np.asarray(render_note(inst, int(m), dur, vel), dtype=float)


def ocarina(m, dur, vel):
    """A soft, nearly pure tone: a sine with a whisper of second harmonic, a slow breathy attack and a gentle vibrato."""
    n = int(dur * SR)
    t = tarr(n)
    f = 440 * 2 ** ((m - 69) / 12) * (1 + .004 * np.sin(2 * np.pi * 5 * t) * np.minimum(1, t / .6))
    ph = 2 * np.pi * np.cumsum(f) / SR
    y = np.sin(ph) + .08 * np.sin(2 * ph) + .02 * fft_filter(rng.normal(0, 1, n), 800, 3000) * np.minimum(1, t / .2)
    env = np.minimum(1, t / .12) * np.exp(-np.maximum(0, t - dur * .7) / (dur * .25))
    return y * env * vel


# the phrases: pentatonic steps above middle C (5 = C5), with lengths in seconds; None is a breath
PHRASES = [
    [(4, .7), (5, .7), (6, 1.3), (None, .4), (7, .8), (6, .7), (5, 1.6)],
    [(7, .8), (8, .8), (9, 1.4), (8, .7), (7, 1.2), (None, .5), (6, 1.8)],
    [(5, .6), (6, .6), (7, .9), (8, 1.4), (None, .4), (7, .8), (5, .7), (4, 1.9)],
    [(4, .9), (5, .7), (6, 1.2), (None, .6), (5, .8), (4, .8), (3, 2.0)],
    [(7, 1.0), (None, .3), (6, .6), (5, .6), (6, .8), (7, .9), (8, 2.0)],
    [(9, .9), (8, .8), (7, .8), (6, 1.2), (None, .4), (5, 2.2)],
    [(3, .8), (4, .8), (5, 1.0), (7, 1.2), (6, .9), (5, 2.0)],
    [(8, 1.1), (7, .7), (8, .7), (9, 1.6), (None, .5), (7, .9), (5, 2.2)],
]
LOOP = 64.0
LEVELS = dict(lead=-17, pad=-22, sparkle=-27, birds=-27, bass=-23)


def arrange():
    L = int(LOOP * SR)
    mix = LoopMix(L, circular=True)
    # the drone: C held throughout, with colours drifting over it every 16 seconds (C, C6, Fmaj7 over C, Gsus over C)
    colours = [(48, 55, 60, 64), (48, 55, 57, 64), (48, 53, 57, 64), (48, 55, 62, 67)]
    seg = LOOP / len(colours)
    for k, ch in enumerate(colours):
        for j, m in enumerate(ch):
            mix.add('pad', note('pad', m, seg + 4, .4), int((k * seg - 1.5) * SR) % L, (j - 1.5) * .45)
        mix.add('bass', note('felt', 36, 6.0, .35), int((k * seg) * SR), 0)
    # the melody: phrases in free time, alternating flute and ocarina, panned about, with long breaths between
    t = 1.0
    order = list(range(len(PHRASES)))
    k = 0
    while t < LOOP - 4:
        ph = PHRASES[order[k % len(order)]]
        voice = 'flute' if k % 2 == 0 else 'ocarina'
        pan = .45 * np.sin(k * 1.7)
        for step, dur in ph:
            d = dur * rng.uniform(.9, 1.15)
            if step is not None:
                m = pen(step)
                y = ocarina(m, d * 1.3, .55) if voice == 'ocarina' else note('flute', m, d * 1.2, .5)
                mix.add('lead', y, int(t * SR), pan)
            t += d
        t += rng.uniform(1.8, 3.6)
        k += 1
    # harp sparkles: a few soft pentatonic notes, here and there
    for j in range(14):
        at = rng.uniform(0, LOOP)
        for q in range(rng.integers(1, 4)):
            mix.add('sparkle', note('harp', pen(int(rng.integers(8, 13))), 2.0, .35), int((at + q * .18) * SR) % L, rng.uniform(-.8, .8))
    # birds: little chirping figures, far off and near
    for j in range(22):
        at = rng.uniform(0, LOOP)
        pan = rng.uniform(-.9, .9)
        f0 = rng.uniform(2600, 4200)
        for q in range(rng.integers(2, 6)):
            n = int(rng.uniform(.04, .09) * SR)
            tt = tarr(n)
            f = f0 * (1 + rng.uniform(-.25, .35) * tt / tt[-1]) * (1 + .05 * np.sin(2 * np.pi * 40 * tt))
            y = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.sin(np.pi * tt / tt[-1]) ** 2
            mix.add('birds', y, int((at + q * rng.uniform(.08, .16)) * SR) % L, pan)
    return mix, L


def bus_rms_db(b):
    m = b.mean(0)
    return 20 * np.log10(max(np.sqrt(np.mean(m ** 2)), 1e-9))


def master(mix, L):
    out = np.zeros((2, L))
    send = np.zeros((2, L))
    birds = np.zeros((2, L))
    for name, b in mix.buses.items():
        g = 10 ** ((LEVELS.get(name, -22) - bus_rms_db(b)) / 20)
        b = b * g
        if name == 'birds':
            birds += b
            continue
        out += b
        send += b * {'lead': .55, 'pad': .5, 'sparkle': .7, 'bass': .2}.get(name, .3)
    ir = make_ir(length=3.2, rt_low=2.6, rt_mid=2.4, rt_high=1.2)  # a wide, open, soft space
    out += conv_reverb(send, ir, True) * .45
    # warm and muffled, like the original: nothing much above two kilohertz
    out = np.stack([fft_filter(c, 40, 2500, 2, circular=True) for c in out])
    # as wide as the original: lift the sides (mid/side)
    mid, side = (out[0] + out[1]) / 2, (out[0] - out[1]) / 2
    side = side * 2.4
    out = np.stack([mid + side, mid - side])
    # birds sit on top, brighter, with a little space of their own
    out += birds + conv_reverb(birds * .4, make_ir(length=1.5), True) * .3
    # the slow swell and fall across the loop (seamless: one full cycle)
    t = np.arange(L) / L
    out *= .78 + .22 * np.sin(2 * np.pi * (t - .25)) ** 2
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
    n = int(3.0 * SR)
    out = np.zeros((2, n))

    def put(y, sec, pan=0.0, g=1.0):
        s = int(sec * SR)
        e = min(n, s + len(y))
        gl, gr = np.cos((pan + 1) * np.pi / 4), np.sin((pan + 1) * np.pi / 4)
        out[0, s:e] += gl * g * y[:e - s]
        out[1, s:e] += gr * g * y[:e - s]

    if kind == 'penned':  # a harp running up the pentatonic, an ocarina landing on C
        for k, s in enumerate((5, 6, 7, 8, 9, 10)):
            put(note('harp', pen(s), 1.6, .6), k * .11, -.5 + k * .2)
        put(ocarina(72, 1.6, .6), .7, 0)
    else:  # got away: a falling little figure, unbothered
        for k, s in enumerate((9, 8, 7, 5)):
            put(ocarina(pen(s), .5, .5), k * .22, .3 - k * .2)
    out = out + conv_reverb(out * .4, make_ir(length=2.4), False)[:, :n] * .4
    out = np.stack([fft_filter(c, 40, 3500, 2) for c in out])
    fade = int(.4 * SR)
    out[:, -fade:] *= np.linspace(1, 0, fade)
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(MASTERS, exist_ok=True)
    mix, L = arrange()
    write(os.path.join(MASTERS, 'sh_music_grass.wav'), finish(master(mix, L)))
    manifest = {'music': [{'id': 'sh_music_grass', 'loop_end_sample_exclusive': L, 'seconds': LOOP}], 'stingers': []}
    # (the stingers come from meadow_music.py now)
    json.dump(manifest, open(os.path.join(MASTERS, 'long_grass_manifest.json'), 'w'), indent=1)
    print('ok', L / SR)


if __name__ == '__main__':
    main()
