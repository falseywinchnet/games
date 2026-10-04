"""Maze 95 score: "Atrium".

Vaporwave of the mallsoft kind: a slow, smooth-jazz loop for an empty
shopping centre in 1995, in D-flat major at 78 BPM. Electric piano comping on
ninth chords, a vibraphone tune, a round synth bass, a soft swung drum machine,
all bathed in a big wet hall and a touch of tape wobble. Three versions:

  mz_music       the loop as it is
  mz_music_dark  for a blackout: the same loop heard through a wall (low-passed, slowed in feel by a longer reverb, quieter)
  mz_music_flip  for walking on the ceiling: the whole loop played backwards

Plus a win jingle and a little start-up chime. Everything is synthesized
(engine/synth.py); no samples. Writes 48 kHz 16-bit WAV masters into masters/
and the manifest (exact loop lengths) into ../assets/audio. The game ships
AAC encodings (afconvert -f m4af -d aac -b 192000).
"""
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'engine'))
from synth import SR, render_note, render_drum, make_ir, conv_reverb, fft_filter, LoopMix, tarr  # noqa: E402

OUT = os.path.join(os.path.dirname(__file__), '..', 'assets', 'audio')
MASTERS = os.path.join(os.path.dirname(__file__), 'masters')
rng = np.random.default_rng(1995)

BPM = 78
SPB = int(round(SR * 60 / BPM))
BARS = 16
L = BARS * 4 * SPB
ROOT = 61  # D-flat

# chords as (root offset from D-flat, voicing intervals): I maj9, vi m9, IV maj9, V 13sus
CH = {
    'I': (0, [4, 7, 11, 14]),
    'vi': (-3, [3, 7, 10, 14]),
    'IV': (5, [4, 7, 11, 14]),
    'V': (7, [5, 10, 14, 16]),
    'ii': (2, [3, 7, 10, 14]),
    'iii': (4, [3, 7, 10, 14]),
}
PROG = ['I', 'vi', 'IV', 'V', 'I', 'iii', 'ii', 'V', 'IV', 'iii', 'vi', 'ii', 'IV', 'V', 'I', 'V']
# the vibraphone tune, in semitones over D-flat (beat, note, beats)
TUNE = {
    0: [(0, 16, 1.5), (1.5, 14, .5), (2, 11, 2)], 1: [(0, 12, 1), (1, 9, 1), (2, 7, 2)],
    2: [(0, 9, 1.5), (1.5, 11, .5), (2, 12, 1), (3, 14, 1)], 3: [(0, 12, 3)],
    4: [(0, 16, 1.5), (1.5, 14, .5), (2, 11, 2)], 5: [(0, 11, 1), (1, 12, 1), (2, 16, 2)],
    6: [(0, 14, 1.5), (1.5, 12, .5), (2, 9, 2)], 7: [(0, 7, 2), (2.5, 9, .5), (3, 11, 1)],
    8: [(0, 12, 2), (2, 16, 2)], 9: [(0, 19, 1.5), (1.5, 16, .5), (2, 14, 2)],
    10: [(0, 12, 1), (1, 11, 1), (2, 9, 2)], 11: [(0, 7, 3)],
    12: [(0, 9, 1), (1, 12, 1), (2, 16, 1), (3, 19, 1)], 13: [(0, 17, 2), (2, 14, 2)],
    14: [(0, 16, 3), (3, 14, 1)], 15: [(0, 11, 2), (2.5, 12, 1.5)],
}


def note(inst, m, dur, vel):
    return np.asarray(render_note(inst, int(m), dur, vel), dtype=float)


def at(bar, beat, swing=.12):
    sw = swing * SPB if (beat * 2) % 2 == 1 else 0
    return int(round((bar * 4 + beat) * SPB + sw))


def arrange():
    mix = LoopMix(L, circular=True)
    for bar, name in enumerate(PROG):
        r, iv = CH[name]
        base = ROOT - 12 + r
        # electric piano: the chord on 1, a softer push on the and-of-2
        for k, i in enumerate(iv):
            mix.add('keys', note('ep', base + i, 1.6 * SPB / SR, .55), at(bar, 0) + k * 120, -.2 + k * .12)
            mix.add('keys', note('ep', base + i, .9 * SPB / SR, .4), at(bar, 2.5) + k * 90, -.2 + k * .12)
        # the bass: root, a fifth, an approach note into the next bar
        nxt = CH[PROG[(bar + 1) % BARS]][0]
        b = ROOT - 24 + r
        mix.add('bass', note('synthbass', b, 1.4 * SPB / SR, .8), at(bar, 0), 0)
        mix.add('bass', note('synthbass', b + 7, .9 * SPB / SR, .6), at(bar, 2), 0)
        mix.add('bass', note('synthbass', ROOT - 24 + nxt - 1, .4 * SPB / SR, .5), at(bar, 3.5), 0)
        # the tune
        for (beat, m, d) in TUNE.get(bar, []):
            mix.add('lead', note('vibes', ROOT + m, d * SPB / SR * .95, .65), at(bar, beat), .15)
        # a pad underneath, very soft
        for k, i in enumerate(iv[:3]):
            mix.add('pad', note('pad', base + 12 + i, 4 * SPB / SR, .35), at(bar, 0), (k - 1) * .5)
        # drum machine: kick, a rim click on 2 and 4, swung hats
        mix.add('drums', render_drum('kick', .5, bar), at(bar, 0), 0)
        mix.add('drums', render_drum('kick', .35, bar + 1), at(bar, 2.5), 0)
        for bt in (1, 3):
            mix.add('drums', render_drum('rim', .45, bar * 2 + bt), at(bar, bt), .1)
        for k in range(8):
            mix.add('drums', render_drum('hat', .25 if k % 2 else .15, bar * 8 + k), at(bar, k / 2), .3)
    return mix


LEVELS = dict(keys=-20, bass=-18, lead=-17, pad=-25, drums=-24)


def bus_rms_db(b):
    m = b.mean(0)
    r = np.sqrt(np.mean(m ** 2))
    return 20 * np.log10(max(r, 1e-9))


def wobble(x, depth=.0018, rate=.5):
    """Tape wobble: a slow, slight pitch drift (a moving fractional delay, circular)."""
    n = x.shape[1]
    t = np.arange(n)
    # keep it seamless: a whole number of wobble cycles per loop
    cycles = max(1, round(n / SR * rate))
    d = depth * SR * (1 + np.sin(2 * np.pi * cycles * t / n))
    idx = (t - d) % n
    i0 = np.floor(idx).astype(int)
    f = idx - i0
    return np.stack([c[i0] * (1 - f) + c[(i0 + 1) % n] * f for c in x])


def master(mix, wet=.4):
    out = np.zeros((2, L))
    send = np.zeros((2, L))
    for name, b in mix.buses.items():
        g = 10 ** ((LEVELS.get(name, -20) - bus_rms_db(b)) / 20)
        b = b * g
        out += b
        send += b * {'lead': .5, 'keys': .45, 'pad': .5, 'drums': .2, 'bass': .05}.get(name, .2)
    ir = make_ir(length=3.4, rt_low=2.8, rt_mid=2.4, rt_high=1.2)
    out += conv_reverb(send, ir, True) * wet
    out = np.stack([fft_filter(c, 35, 11000, 2, circular=True) for c in out])
    return wobble(out)


def finish(x, target_rms_db=-19.0, ceiling_db=-3.0):
    x = x * 10 ** ((target_rms_db - bus_rms_db(x)) / 20)
    c = 10 ** (ceiling_db / 20)
    return np.tanh(x / c) * c


def write(path, x):
    import wave
    y = np.clip(x, -1, 1)
    y16 = (y.T * 32767).astype('<i2')
    with wave.open(path, 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(y16.tobytes())


def jingle(kind):
    n = int(3.2 * SR)
    out = np.zeros((2, n))

    def put(y, beat, pan=0.0, g=1.0):
        s = int(beat * SPB)
        e = min(n, s + len(y))
        gl, gr = np.cos((pan + 1) * np.pi / 4), np.sin((pan + 1) * np.pi / 4)
        out[0, s:e] += gl * g * y[:e - s]
        out[1, s:e] += gr * g * y[:e - s]

    if kind == 'win':  # found it: a bright run and a ninth chord that hangs in the atrium
        for i, m in enumerate([0, 4, 7, 11, 14, 16]):
            put(note('vibes', ROOT + 12 + m, .5, .7), i * .2, .4 * np.sin(i))
        for m in (0, 4, 7, 11, 14):
            put(note('ep', ROOT + m, 2.2, .6), 1.2, 0, .8)
        put(note('synthbass', ROOT - 24, 2, .7), 1.2)
    else:  # start-up: a slow rising chord, the sound of a computer waking in 1995
        for i, m in enumerate([0, 7, 11, 16, 19]):
            put(note('pad', ROOT - 12 + m, 2.6 - i * .2, .5), i * .25, -.4 + .2 * i)
        put(note('vibes', ROOT + 16, 1.6, .5), 1.0, .3)
        put(note('vibes', ROOT + 23, 1.6, .45), 1.25, -.3)
    send = out * .4
    out = out + conv_reverb(send, make_ir(length=3.0), False)[:, :n] * .5
    fade = int(.5 * SR)
    out[:, -fade:] *= np.linspace(1, 0, fade)
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(MASTERS, exist_ok=True)
    manifest = {'music': [], 'stingers': []}
    x = finish(master(arrange()))
    # the blackout: through a wall
    dark = np.stack([fft_filter(c, 40, 700, 2, circular=True) for c in x])
    dark = dark + conv_reverb(dark * .5, make_ir(length=4.0, rt_low=3.5, rt_mid=3.0, rt_high=1.0), True) * .6
    dark = finish(dark, -24, -6)
    # the ceiling: backwards
    flip = x[:, ::-1].copy()
    for name, y in (('mz_music', x), ('mz_music_dark', dark), ('mz_music_flip', flip)):
        write(os.path.join(MASTERS, name + '.wav'), y)
        manifest['music'].append({'id': name, 'bpm': BPM, 'bars': BARS, 'bar_samples': 4 * SPB, 'loop_end_sample_exclusive': L, 'seconds': round(L / SR, 3)})
        print(name, round(L / SR, 2), 's')
    for kind, name in (('win', 'mz_stinger_win'), ('start', 'mz_start')):
        write(os.path.join(MASTERS, name + '.wav'), finish(jingle(kind), -18, -2.5))
        manifest['stingers'].append(name)
    json.dump(manifest, open(os.path.join(OUT, 'maze_audio_manifest.json'), 'w'), indent=1)


if __name__ == '__main__':
    main()
