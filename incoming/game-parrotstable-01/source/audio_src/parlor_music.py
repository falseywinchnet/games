"""The Parrot's Table score: "Tea for Seven".

A small chamber waltz for a parlor full of suspicious birds: harp-ish plucks
on the "pah-pah", pizzicato on the "oom", a clarinet tune that keeps glancing
over its shoulder, a flute answering it in the B section, celesta sparkles,
and a sleepy pad underneath. D major, 3/4, 126 BPM, a seamless 32-bar loop
(A, A', B, A''). The B section drifts to the relative minor and tiptoes, as
though someone has just noticed the cake is missing.

Plus stingers: case closed (a triumphant little cadence) and the wrong bird
(a deflating clarinet slide while the culprit flies off). Everything is
synthesized (engine/synth.py); no samples. Writes 48 kHz 16-bit WAV masters
into masters/ and the manifest into ../assets/audio. The game ships AAC
encodings of the masters (afconvert -f m4af -d aac -b 192000).
"""
import json
import os
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'engine'))
from synth import SR, render_note, render_drum, make_ir, conv_reverb, fft_filter, LoopMix  # noqa: E402

OUT = os.path.join(os.path.dirname(__file__), '..', 'assets', 'audio')
MASTERS = os.path.join(os.path.dirname(__file__), 'masters')
rng = np.random.default_rng(1877)

MAJOR = [0, 2, 4, 5, 7, 9, 11]
TONIC = 62  # D


def deg(d, tonic=TONIC, scale=MAJOR):
    return tonic + scale[d % 7] + 12 * (d // 7)


# one chord per bar: (root degree, quality tweak) -- degrees in D major; 'm' chords borrow from B minor
A_CHORDS = [0, 4, 0, 4, 3, 0, 1, 4]
A2_CHORDS = [0, 4, 0, 5, 3, 0, 4, 0]
B_CHORDS = [5, 2, 5, 2, 3, 1, 4, 4]
# the tune (degree relative to the tonic an octave up, beats); None rests. 3 beats a bar.
A_TUNE = [
    [(2, 1), (4, 1), (7, 1)],
    [(6, 2), (4, 1)],
    [(5, 1), (4, .5), (2, .5), (0, 1)],
    [(1, 2), (None, 1)],
    [(3, 1), (5, 1), (8, 1)],
    [(7, 1.5), (6, .5), (4, 1)],
    [(5, 1), (3, 1), (1, 1)],
    [(4, 2), (None, 1)],
]
A2_TUNE = A_TUNE[:6] + [
    [(5, .5), (4, .5), (3, 1), (1, 1)],
    [(0, 2), (None, 1)],
]
# B: tiptoeing, staccato, in the relative minor; the flute takes it
B_TUNE = [
    [(5, .5), (None, .5), (4, .5), (None, .5), (5, 1)],
    [(4, .5), (None, .5), (3, .5), (None, .5), (2, 1)],
    [(5, .5), (None, .5), (7, .5), (None, .5), (9, 1)],
    [(8, 2), (None, 1)],
    [(3, .5), (None, .5), (5, .5), (None, .5), (8, 1)],
    [(7, .5), (6, .5), (5, 1), (3, 1)],
    [(4, 1), (6, 1), (8, 1)],
    [(7, 1), (6, 1), (5, 1)],  # a little turn back home
]
# a counter-line under the reprise: long notes
COUNTER = [[(0, 3)], [(-1, 3)], [(0, 3)], [(-2, 3)], [(1, 3)], [(0, 3)], [(-3, 3)], [(-1, 3)]]

BPM = 126
LEVELS = dict(lead=-16, flute=-18, counter=-22, chords=-22, bass=-18, sparkle=-26, pad=-27, drums=-28)


def note(inst, m, dur, vel):
    return np.asarray(render_note(inst, int(m), dur, vel), dtype=float)


def arrange():
    spb = int(round(SR * 60 / BPM))
    form = [('A', i) for i in range(8)] + [('A2', i) for i in range(8)] + [('B', i) for i in range(8)] + [('A3', i) for i in range(8)]
    bars = len(form)
    L = bars * 3 * spb
    mix = LoopMix(L, circular=True)

    def at(bar, beat):
        # a hint of Viennese lilt: the second beat a touch early
        lilt = -.06 * spb if beat % 3 == 1 else 0
        return int(round((bar * 3 + beat) * spb + lilt))

    for bar, (part, i) in enumerate(form):
        chords = {'A': A_CHORDS, 'A2': A2_CHORDS, 'B': B_CHORDS, 'A3': A2_CHORDS}[part]
        tune = {'A': A_TUNE, 'A2': A2_TUNE, 'B': B_TUNE, 'A3': A2_TUNE}[part][i]
        root = chords[i]
        triad = [root, root + 2, root + 4]
        lead_inst, bus = ('flute', 'flute') if part == 'B' else ('clarinet', 'lead')
        beat = 0.0
        for d, b in tune:
            if d is not None:
                dur = b * spb / SR * (.55 if part == 'B' and b <= .5 else .9)
                mix.add(bus, note(lead_inst, deg(d), dur, .7 + rng.uniform(-.05, .05)), at(bar, beat), .1)
            beat += b
        # the last A gets a celesta doubling an octave up, and a clarinet counter-line
        if part == 'A3':
            beat = 0.0
            for d, b in tune:
                if d is not None:
                    mix.add('sparkle', note('celesta', deg(d) + 12, b * spb / SR * .8, .45), at(bar, beat), -.4)
                beat += b
            for d, b in COUNTER[i]:
                mix.add('counter', note('clarinet', deg(d), b * spb / SR * .95, .45), at(bar, 0), -.35)
        if part == 'A2':
            for d, b in COUNTER[i]:
                mix.add('counter', note('flute', deg(d) + 12, b * spb / SR * .9, .4), at(bar, 0), -.35)
        # oom-pah-pah: pizz root on one, harp chords on two and three
        r = deg(root, TONIC - 24)
        mix.add('bass', note('pizz', r, .8 * spb / SR, .85), at(bar, 0), -.1)
        if bar % 2 == 1:  # every other bar the bass steps to the fifth on beat three for a little swing
            mix.add('bass', note('pizz', deg(root + 4, TONIC - 24), .5 * spb / SR, .5), at(bar, 2.5), -.1)
        chord = [deg(t, TONIC - 12) for t in triad]
        for b2 in (1, 2):
            for k, m in enumerate(chord):
                mix.add('chords', note('harp', m, .5 * spb / SR, .5 if b2 == 1 else .42), at(bar, b2) + k * 60, .3 - .15 * k)
        # sparkles at phrase ends
        if i % 4 == 3:
            for k, m in enumerate(chord):
                mix.add('sparkle', note('celesta', m + 24, .6, .35), at(bar, 1.5) + k * int(.08 * SR), .5 - .3 * k)
        # pad: soft sustain under the A sections; thinner in B
        for k, m in enumerate(chord):
            mix.add('pad', note('pad', m, 3 * spb / SR, .35 if part != 'B' else .22), at(bar, 0), (k - 1) * .5)
        # the faintest brush and triangle, keeping time
        mix.add('drums', render_drum('tap', .25, bar), at(bar, 0), .2)
        if bar % 4 == 0:
            mix.add('drums', render_drum('triangle', .22, bar), at(bar, 0), -.35)
        if part == 'B':
            mix.add('drums', render_drum('wbhi', .25, bar), at(bar, 1), -.3)
            mix.add('drums', render_drum('wblo', .25, bar), at(bar, 2), -.3)
    return mix, L, spb


def bus_rms_db(b):
    m = b.mean(0)
    r = np.sqrt(np.mean(m ** 2))
    return 20 * np.log10(max(r, 1e-9))


def master(mix, L, wet=.28):
    out = np.zeros((2, L))
    send = np.zeros((2, L))
    for name, b in mix.buses.items():
        g = 10 ** ((LEVELS.get(name, -20) - bus_rms_db(b)) / 20)
        b = b * g
        out += b
        send += b * {'lead': .35, 'flute': .35, 'counter': .4, 'chords': .3, 'pad': .45, 'sparkle': .5, 'drums': .1, 'bass': .08}.get(name, .1)
    ir = make_ir(length=2.0, rt_low=1.5, rt_mid=1.3, rt_high=.7)
    out += conv_reverb(send, ir, True) * wet
    out = np.stack([fft_filter(c, 40, None, 2, circular=True) for c in out])
    return out


def finish(x, target_rms_db=-20.0, ceiling_db=-3.0):
    x = x * 10 ** ((target_rms_db - bus_rms_db(x)) / 20)
    c = 10 ** (ceiling_db / 20)
    return np.tanh(x / c) * c


def write(path, x):
    y = np.clip(x, -1, 1)
    y16 = (y.T * 32767).astype('<i2')
    with wave.open(path, 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(y16.tobytes())


def stinger(kind):
    n = int(3.4 * SR)
    out = np.zeros((2, n))
    spb = SR * 60 / BPM

    def put(y, beat, pan=0.0, g=1.0):
        s = int(beat * spb)
        e = min(n, s + len(y))
        gl, gr = np.cos((pan + 1) * np.pi / 4), np.sin((pan + 1) * np.pi / 4)
        out[0, s:e] += gl * g * y[:e - s]
        out[1, s:e] += gr * g * y[:e - s]

    if kind == 'win':  # case closed: a run up the arpeggio, then a full cadence with a celesta flourish
        for k, d in enumerate((0, 2, 4, 7, 9)):
            put(note('clarinet', deg(d), .3, .75), k * .4, .2)
        for m in (deg(-7) - 12, deg(0) - 12, deg(2) - 12, deg(4) - 12, deg(7)):
            put(note('harp', m, 2.0, .7), 2.0, 0, .7)
        put(note('flute', deg(11), 1.4, .8), 2.0, -.1)
        for k, d in enumerate((7, 9, 11, 14)):
            put(note('celesta', deg(d) + 12, .5, .5), 2.0 + k * .25, .4)
        put(render_drum('triangle', .5, 1), 2.0, -.3)
    else:  # the wrong bird: a sagging clarinet, a sour little harp chord, and wings flapping away
        for k, (m, d) in enumerate([(deg(4), .5), (deg(3), .5), (deg(2) - 1, 1.6)]):
            put(note('clarinet', m, d, .75), k * .7, 0)
        for m in (deg(-1) - 12, deg(1) - 12, deg(3) - 12 - 1):
            put(note('harp', m, 1.6, .55), 1.4, .2, .7)
        put(note('pizz', deg(0) - 24, .8, .8), 1.4)
    send = out * .3
    ir = make_ir(length=2.0)
    out = out + conv_reverb(send, ir, False)[:, :n] * .3
    fade = int(.4 * SR)
    out[:, -fade:] *= np.linspace(1, 0, fade)
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(MASTERS, exist_ok=True)
    manifest = {'music': [], 'stingers': []}
    mix, L, spb = arrange()
    x = finish(master(mix, L))
    write(os.path.join(MASTERS, 'pt_music.wav'), x)
    manifest['music'].append({'id': 'pt_music', 'bpm': BPM, 'bars': L // (3 * spb), 'bar_samples': 3 * spb,
                              'loop_end_sample_exclusive': L, 'seconds': round(L / SR, 3)})
    print('pt_music', BPM, 'BPM', round(L / SR, 2), 's')
    for kind in ('win', 'lose'):
        y = finish(stinger(kind), -18, -2.5)
        write(os.path.join(MASTERS, f'pt_stinger_{kind}.wav'), y)
        manifest['stingers'].append(f'pt_stinger_{kind}')
    json.dump(manifest, open(os.path.join(OUT, 'parrots_audio_manifest.json'), 'w'), indent=1)


if __name__ == '__main__':
    main()
