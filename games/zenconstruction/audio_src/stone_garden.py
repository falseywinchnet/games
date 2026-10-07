"""Zen Construction score: "Stone Garden".

Music for setting stones by a brook, meant to sit under work, not over it.
No beat and no hook to hum: 60 beats a minute, sixteen bars of four, in D
major's warm chords (maj9s, a Bm9, a Gmaj9 that lifts to #11, an Asus4 that
never quite resolves, so the loop never lands).

  - stone bars (a lithophone, struck slate) carry a sparse tune on the
    pentatonic, a few notes a bar, phrased in twos with rests to breathe,
    echoed once across the stereo field;
  - a felt piano sets each bar's root low and rolls a soft chord now and then;
  - a pad holds the harmony, slow in, slow out, far back;
  - the odd kalimba glint high up.

One seamless 64-second loop (everything wraps round). Writes a 48 kHz WAV
master into masters/; the game ships an AAC encoding (afconvert -f m4af -d
aac -b 160000) and reads the loop length and bar from the manifest.
"""
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'engine'))
from synth import SR, LoopMix, make_ir, conv_reverb, pingpong, fft_filter, felt_piano, pad, kalimba, mtof, tarr, write_wav  # noqa: E402

HERE = os.path.dirname(__file__)
OUT = os.path.join(HERE, '..', 'assets', 'audio')
MASTERS = os.path.join(HERE, 'masters')
rng = np.random.default_rng(1964)

BEAT = 1.0
BAR = 4 * BEAT
BARS = 16
LOOP = BARS * BAR

# the harmony, bar by bar: (bass, upper voices)
CHORDS = [
    (38, [57, 61, 64, 66]),        # Dmaj9
    (38, [57, 61, 64, 66]),
    (35, [54, 57, 61, 62]),        # Bm9
    (35, [54, 57, 61, 62]),
    (31, [54, 57, 59, 62]),        # Gmaj9
    (31, [54, 57, 61, 62]),        # Gmaj9#11 (C# over G)
    (33, [57, 62, 64, 69]),        # Asus4
    (33, [57, 59, 61, 64]),        # A add9
    (42, [57, 61, 64, 69]),        # Dmaj9 over F#
    (43, [54, 59, 62, 66]),        # Gmaj7
    (40, [55, 59, 62, 66]),        # Em9
    (33, [57, 62, 64, 67]),        # A7sus4
    (35, [54, 57, 61, 64]),        # Bm9
    (31, [54, 57, 59, 64]),        # Gmaj9 (6/9 colour)
    (40, [55, 59, 62, 66]),        # Em9
    (33, [57, 62, 64, 69]),        # Asus4, left hanging
]
SCALE = [62, 64, 66, 69, 71, 74, 76, 78, 81, 83, 86]   # D major pentatonic, D4 .. D6


def stone_bar(m, vel=0.6):
    """A struck slate bar: a firm tap, a clear fundamental, the stone's
    inharmonic overtones dying fast, a ring of a second or so."""
    f = mtof(m)
    n = int(2.6 * SR)
    t = tarr(n)
    y = np.zeros(n)
    for r, a, tau in ((1.0, 1.0, 1.1), (2.76, 0.32 * vel, 0.32), (5.40, 0.14 * vel, 0.10), (8.93, 0.05 * vel, 0.04)):
        fr = f * r
        if fr < 16000:
            y += a * np.exp(-t / tau * (1 + 0.15 * (f / 600))) * np.sin(2 * np.pi * fr * t + rng.uniform(0, 6.28))
    k = int(0.003 * SR)
    y[:k] += 0.35 * vel * fft_filter(rng.standard_normal(k), 1500, 8000)[:k] * np.linspace(1, 0, k)
    att = int(0.0015 * SR)
    y[:att] *= np.linspace(0, 1, att)
    return y * (0.35 + 0.65 * vel)


def tune():
    """The stone bars' line: two-bar phrases, a call and a looser answer,
    nearest notes of the pentatonic that sit in each bar's chord, rests."""
    # rhythmic cells within two bars (beats from the phrase start, length in beats)
    cells = [
        [(0.0, 1.5), (1.5, 0.5), (2.0, 2.0), (5.0, 1.0)],
        [(0.0, 1.0), (1.0, 1.0), (2.0, 3.0), (6.0, 1.5)],
        [(0.5, 1.0), (1.5, 2.5), (4.5, 0.5), (5.0, 2.0)],
        [(0.0, 3.0), (4.0, 1.0), (5.0, 1.0), (6.0, 2.0)],
        [(0.0, 2.0), (3.0, 1.0), (4.0, 3.0)],
    ]
    notes = []
    degree = 5   # start on D5
    for phrase in range(BARS // 2):
        if phrase in (3, 7):
            # every fourth phrase rests on one long note and breathes
            notes.append((phrase * 2 * BAR, 74 if phrase == 3 else 76, 0.45))
            continue
        cell = cells[rng.integers(len(cells))] if phrase not in (0, 4) else cells[0]
        for k, (at, length) in enumerate(cell):
            when = phrase * 2 * BAR + at * BEAT
            bar = int(when // BAR) % BARS
            chord = [c % 12 for c in CHORDS[bar][1]] + [CHORDS[bar][0] % 12]
            step = int(rng.choice([-2, -1, -1, 1, 1, 2, 0]))
            degree = int(np.clip(degree + step, 2, len(SCALE) - 2))
            # on a long note, prefer a chord tone
            if length >= 2 and SCALE[degree] % 12 not in chord:
                for d in (degree - 1, degree + 1):
                    if 0 <= d < len(SCALE) and SCALE[d] % 12 in chord:
                        degree = d
                        break
            vel = 0.42 + 0.2 * rng.random() - (0.08 if k else 0)
            notes.append((when, SCALE[degree], vel))
        # phrases climb a little in the middle of the loop, fall back toward the end
        degree = int(np.clip(degree + (1 if phrase < 4 else -1), 3, 8))
    return notes


def arrange():
    L = int(LOOP * SR)
    mix = LoopMix(L, circular=True)
    # pad: the upper voices, each bar, slow in and slow out, a touch dark
    for bar, (bass, upper) in enumerate(CHORDS):
        start = int(bar * BAR * SR)
        for i, m in enumerate(upper):
            y = pad(m, BAR + 0.6, vel=0.32, att=1.4, rel=2.2, cutoff=1500)
            mix.add('pad', y, start - int(0.3 * SR), -0.5 + i / 3, 0.20)
    # felt piano: the root on each bar, a fifth on some; a rolled chord every other bar
    for bar, (bass, upper) in enumerate(CHORDS):
        start = int(bar * BAR * SR)
        mix.add('piano', felt_piano(bass + 12, 3.6, 0.42), start, -0.1, 0.9)
        if bar % 4 == 1:
            mix.add('piano', felt_piano(bass + 19, 1.8, 0.30), start + int(2 * BEAT * SR), 0.1, 0.7)
        if bar % 2 == 0:
            for i, m in enumerate(upper[:3]):
                mix.add('piano', felt_piano(m, 2.6, 0.26), start + int((1.0 + 0.09 * i) * SR), -0.25 + 0.25 * i, 0.55)
    # the stone bars
    for when, m, vel in tune():
        y = stone_bar(m, vel)
        pan = float(np.clip((m - 76) / 14, -0.6, 0.6))
        mix.add('stone', y, int(when * SR), pan, 0.55)
    # kalimba glints, high and rare
    for k in range(7):
        when = rng.uniform(0, LOOP)
        bar = int(when // BAR) % BARS
        m = CHORDS[bar][1][rng.integers(4)] + 24
        mix.add('glint', kalimba(m, 1.0, 0.35), int(when * SR), rng.uniform(-0.8, 0.8), 0.22)
    stone = mix.bus('stone')
    echo = pingpong(stone, int(0.75 * SR), fb=0.32, n=4, lp=3200)
    dry = mix.bus('pad') + mix.bus('piano') + stone + 0.55 * echo + mix.bus('glint')
    ir = make_ir(3.4, 2.8, 2.2, 1.0, 0.028, seed=64)
    wet = conv_reverb(dry, ir, circular=True)
    out = dry + 0.42 * wet * np.max(np.abs(dry)) / np.max(np.abs(wet))
    out[0] = fft_filter(out[0], 35, 9000, 1, circular=True)
    out[1] = fft_filter(out[1], 35, 9000, 1, circular=True)
    return out


def main():
    os.makedirs(MASTERS, exist_ok=True)
    x = arrange()
    x = x / np.max(np.abs(x)) * 10 ** (-3 / 20)
    write_wav(os.path.join(MASTERS, 'zc_music.wav'), x, bits=16)
    rms = 20 * np.log10(np.sqrt((x ** 2).mean()))
    print(f'Stone Garden: {LOOP:.0f} s, rms {rms:.1f} dB')


if __name__ == '__main__':
    main()
