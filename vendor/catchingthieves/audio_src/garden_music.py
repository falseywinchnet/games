"""Catching Thieves score: "Pumpkin Patrol".

One cheeky garden theme, written in scale degrees so it can be played in the
major (G) or turned sneaky in the relative minor (E), and arranged five ways,
one per season of the campaign:

  spring   G major 112 BPM  marimba tune, flute counter-line, ukulele-ish plucks, pizzicato oom-pah, shaker
  summer   G major 120 BPM  slide-whistle tune, strummed plucks, upright bass, claps and shaker
  autumn   E minor 104 BPM  clarinet tune, accordion chords, tuba oom-pah, woodblocks: a tiptoeing, mischievous turn
  winter   G major  96 BPM  music-box tune, glassy chords, pizzicato, sleigh-bell shaker and triangle
  night    E minor  84 BPM  vibraphone lullaby over a soft pad and a felt-piano bass

Each is a seamless 24-bar loop (A, B, then A again with a counter-line).
Plus stingers: a garden cleared, a pumpkin stuck for good (the sad trombone),
and the whole book finished. Everything is synthesized (engine/synth.py); no
samples. Writes 48 kHz 16-bit WAV masters into masters/ and the manifest
(exact loop and bar lengths) into ../assets/audio. The game ships AAC
encodings of the masters (afconvert -f m4af -d aac -b 192000).
"""
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'engine'))
from synth import SR, render_note, render_drum, make_ir, conv_reverb, fft_filter, LoopMix, mtof, tarr, gate, noise, wt  # noqa: E402

OUT = os.path.join(os.path.dirname(__file__), '..', 'assets', 'audio')
MASTERS = os.path.join(os.path.dirname(__file__), 'masters')
rng = np.random.default_rng(2002)

MAJOR = [0, 2, 4, 5, 7, 9, 11]
MINOR = [0, 2, 3, 5, 7, 8, 10]


def deg(d, scale, tonic):
    """A scale degree (any integer, 0 = tonic) to MIDI."""
    return tonic + scale[d % 7] + 12 * (d // 7)


# chords as scale degrees of their roots, one per bar
A_CHORDS = [0, 5, 3, 4, 0, 5, 1, 4]
B_CHORDS = [3, 0, 1, 4, 3, 2, 1, 4]
# the tune: (degree, beats); None is a rest. Degrees are relative to the tonic an octave up.
A_TUNE = [
    [(4, .75), (4, .25), (5, .5), (4, .5), (2, 1), (0, 1)],
    [(2, .75), (2, .25), (4, .5), (2, .5), (0, 1), (-2, 1)],
    [(3, .5), (4, .5), (5, .5), (7, .5), (6, 1), (5, 1)],
    [(4, .5), (None, .5), (1, .5), (None, .5), (4, .5), (3, .5), (1, 1)],
    [(4, .75), (4, .25), (5, .5), (4, .5), (7, 1), (9, .5), (7, .5)],
    [(8, .75), (7, .25), (5, .5), (4, .5), (2, 1), (4, 1)],
    [(3, .5), (5, .5), (8, .5), (7, .5), (5, .5), (3, .5), (1, 1)],
    [(4, 1.5), (6, .5), (7, 2)],
]
B_TUNE = [
    [(7, 1.5), (8, .5), (9, 1), (7, 1)],
    [(7, 1), (4, 1), (2, 1), (4, 1)],
    [(5, 1.5), (4, .5), (3, 1), (1, 1)],
    [(4, 3), (None, 1)],
    [(7, 1.5), (8, .5), (9, 1), (11, 1)],
    [(9, 1), (8, 1), (6, 1), (4, 1)],
    [(8, 1), (7, .5), (5, .5), (3, 1), (1, 1)],
    [(4, .5), (3, .5), (1, .5), (-1, .5), (-3, 2)],
]
# a counter-line for the second A: long notes a third or sixth under the tune
COUNTER = [[(2, 2), (0, 2)], [(0, 2), (-2, 2)], [(0, 2), (2, 2)], [(1, 2), (-1, 2)],
           [(2, 2), (4, 2)], [(5, 2), (2, 2)], [(1, 2), (3, 2)], [(1, 2), (2, 2)]]

SEASONS = {
    'spring': dict(bpm=112, scale=MAJOR, tonic=67, lead='marimba', lead_oct=0, counter='flute', chords='pluck', bass='pizz', drums='spring', pad=None, swing=0.0),
    'summer': dict(bpm=120, scale=MAJOR, tonic=67, lead='slide', lead_oct=0, counter='marimba', chords='pluck', bass='bass', drums='summer', pad=None, swing=0.08),
    'autumn': dict(bpm=104, scale=MINOR, tonic=64, lead='clarinet', lead_oct=0, counter='reed', chords='reed', bass='tuba', drums='autumn', pad=None, swing=0.12),
    'winter': dict(bpm=96, scale=MAJOR, tonic=67, lead='musicbox', lead_oct=12, counter='celesta', chords='glass', bass='pizz', drums='winter', pad='pad', swing=0.0),
    'night': dict(bpm=84, scale=MINOR, tonic=64, lead='vibes', lead_oct=0, counter='flute', chords='felt', bass='felt', drums=None, pad='pad', swing=0.1),
}
LEVELS = dict(lead=-16, counter=-21, chords=-22, bass=-17, drums=-22, pad=-25, fx=-28)


def note(inst, m, dur, vel):
    y = render_note(inst, int(m), dur, vel)
    return np.asarray(y, dtype=float)


def arrange(name):
    a = SEASONS[name]
    spb = int(round(SR * 60 / a['bpm']))
    bars = 24
    L = bars * 4 * spb
    mix = LoopMix(L, circular=True)
    sc, ton = a['scale'], a['tonic']
    form = [('A', i) for i in range(8)] + [('B', i) for i in range(8)] + [('A2', i) for i in range(8)]

    def at(bar, beat):
        sw = a['swing'] * spb if (beat * 2) % 2 == 1 else 0  # swing the off-beat eighths
        return int(round((bar * 4 + beat) * spb + sw))

    for bar, (part, i) in enumerate(form):
        root = (A_CHORDS if part != 'B' else B_CHORDS)[i]
        tune = (A_TUNE if part != 'B' else B_TUNE)[i]
        triad = [root, root + 2, root + 4]
        # the tune
        beat = 0.0
        for d, b in tune:
            if d is not None:
                m = deg(d, sc, ton) + a['lead_oct']
                mix.add('lead', note(a['lead'], m, b * spb / SR * .92, .72 + rng.uniform(-.05, .05)), at(bar, beat), .05)
            beat += b
        # the counter-line in the reprise
        if part == 'A2':
            beat = 0.0
            for d, b in COUNTER[i]:
                mix.add('counter', note(a['counter'], deg(d, sc, ton), b * spb / SR * .95, .55), at(bar, beat), -.35)
                beat += b
        # chords: off-beat plucks (an oom-pah's "pah"), or long glassy ones
        chord = [deg(t, sc, ton - 12) for t in triad]
        if a['chords'] in ('pluck', 'reed'):
            for b2 in (1, 3) if a['chords'] == 'reed' else (.5, 1.5, 2.5, 3.5):
                for k, m in enumerate(chord):
                    mix.add('chords', note(a['chords'], m, .3 * spb / SR * (2 if a['chords'] == 'reed' else 1), .5), at(bar, b2) + k * 90, .3)
        else:
            for k, m in enumerate(chord):
                mix.add('chords', note(a['chords'], m + 12 * (a['chords'] == 'glass'), 3.8 * spb / SR, .45), at(bar, 0) + k * 200, .25 - .25 * k)
        # the bass: oom on 1 and 3, a cheeky walk up into every fourth bar
        r = deg(root, sc, ton - 24)
        fifth = deg(root + 4, sc, ton - 24)
        bdur = .9 * spb / SR
        mix.add('bass', note(a['bass'], r, bdur, .8), at(bar, 0), -.1)
        mix.add('bass', note(a['bass'], fifth if a['bass'] != 'felt' else r, bdur, .7), at(bar, 2), -.1)
        if bar % 4 == 3 and a['bass'] != 'felt':
            nxt = (A_CHORDS + B_CHORDS + A_CHORDS)[(bar + 1) % 24]
            target = deg(nxt, sc, ton - 24)
            for k, step in enumerate((-3, -2, -1)):
                mix.add('bass', note(a['bass'], target + step, .4 * spb / SR, .6), at(bar, 3 + k / 3), -.1)
        if a['pad']:
            for k, m in enumerate(chord):
                mix.add('pad', note('pad', m, 4 * spb / SR, .4), at(bar, 0), (k - 1) * .4)
        # percussion
        dk = a['drums']
        if dk == 'spring':
            for b2 in range(8):
                mix.add('drums', render_drum('shaker', .35 if b2 % 2 else .2, b2), at(bar, b2 / 2), .3)
            mix.add('drums', render_drum('kick', .5, 0), at(bar, 0), 0)
            mix.add('drums', render_drum('wbhi', .45, bar), at(bar, 1), -.3)
            mix.add('drums', render_drum('wblo', .45, bar), at(bar, 3), -.3)
        elif dk == 'summer':
            for b2 in range(16):
                mix.add('drums', render_drum('shaker', .3 if b2 % 2 else .18, b2), at(bar, b2 / 4), .3)
            mix.add('drums', render_drum('kick', .6, 0), at(bar, 0), 0)
            mix.add('drums', render_drum('kick', .45, 1), at(bar, 2.5), 0)
            for b2 in (1, 3):
                mix.add('drums', render_drum('clap', .5, bar * 2 + b2), at(bar, b2), 0)
        elif dk == 'autumn':
            for b2 in (0, 1.5, 2, 3):
                mix.add('drums', render_drum('wbhi' if b2 % 1 else 'wblo', .45, int(b2 * 2)), at(bar, b2), -.25)
            mix.add('drums', render_drum('tap', .35, bar), at(bar, 1), .25)
            mix.add('drums', render_drum('tap', .35, bar + 1), at(bar, 3), .25)
        elif dk == 'winter':
            for b2 in range(8):
                mix.add('drums', render_drum('shaker', .3 if b2 % 2 else .2, b2), at(bar, b2 / 2), .35)
            if bar % 2 == 0:
                mix.add('drums', render_drum('triangle', .35, bar), at(bar, 0), -.35)
    return mix, a['bpm'], L, spb


def bus_rms_db(b):
    m = b.mean(0)
    r = np.sqrt(np.mean(m ** 2))
    return 20 * np.log10(max(r, 1e-9))


def master(mix, L, wet=.22):
    out = np.zeros((2, L))
    send = np.zeros((2, L))
    for name, b in mix.buses.items():
        g = 10 ** ((LEVELS.get(name, -20) - bus_rms_db(b)) / 20)
        b = b * g
        out += b
        send += b * {'lead': .3, 'counter': .35, 'chords': .25, 'pad': .4, 'drums': .1, 'bass': .05}.get(name, .1)
    ir = make_ir(length=1.8, rt_low=1.4, rt_mid=1.2, rt_high=.6)
    out += conv_reverb(send, ir, True) * wet
    out = np.stack([fft_filter(c, 35, None, 2, circular=True) for c in out])
    return out


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


def stinger(kind):
    n = int(3.6 * SR)
    out = np.zeros((2, n))
    spb = SR * 60 / 120

    def put(y, beat, pan=0.0, g=1.0):
        s = int(beat * spb)
        e = min(n, s + len(y))
        gl, gr = np.cos((pan + 1) * np.pi / 4), np.sin((pan + 1) * np.pi / 4)
        out[0, s:e] += gl * g * y[:e - s]
        out[1, s:e] += gr * g * y[:e - s]

    if kind == 'win':  # all caught: a bright run up and a "ta-da"
        for i, m in enumerate([67, 71, 74, 79, 83]):
            put(note('marimba', m, .25, .8), i * .25, .3 * np.sin(i))
        for m in (55, 67, 71, 74, 79):
            put(note('pluck', m, 1.6, .7), 1.5, 0, .7)
        put(note('slide', 86, 1.2, .8), 1.25, .1)
        put(render_drum('kick', .8, 0), 1.5)
        put(render_drum('clap', .6, 1), 1.5)
        put(render_drum('triangle', .5, 2), 1.5, -.3)
    elif kind == 'stuck':  # the sad trombone: wah, wah, wah, waaah
        for i, (m, d) in enumerate([(58, .45), (57, .45), (56, .45), (55, 1.6)]):
            y = note('slide', m - 12, d, .8)
            # a muted brassy buzz on top of the slide voice
            t = tarr(len(y))
            buzz = wt('reed', mtof(m - 12) * np.ones(len(y)), 10) * gate(len(y), d, .03, .08) * .5
            wah = .6 + .4 * np.sin(2 * np.pi * 5 * t) if i == 3 else 1
            put((y * .5 + buzz) * wah, i * .55, 0)
        put(note('tuba', 31, 1.6, .7), 1.65)
    else:  # the whole book: a fanfare on the theme, in the major, with everything
        beat = 0
        for d, b in [(4, .5), (5, .5), (7, .5), (11, 2)]:
            put(note('slide', deg(d, MAJOR, 67), b * spb / SR, .85), beat, .1)
            put(note('marimba', deg(d, MAJOR, 67) - 12, b * spb / SR, .7), beat, -.2, .6)
            beat += b
        for m in (43, 55, 59, 62, 67):
            put(note('pluck', m, 1.8, .7), 1.5, 0, .7)
        put(render_drum('kick', .9, 0), 1.5)
        put(render_drum('clap', .7, 2), 1.5)
    send = out * .3
    ir = make_ir(length=1.8)
    out = out + conv_reverb(send, ir, False)[:, :n] * .3
    fade = int(.3 * SR)
    out[:, -fade:] *= np.linspace(1, 0, fade)
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(MASTERS, exist_ok=True)
    manifest = {'music': [], 'stingers': []}
    for name in SEASONS:
        mix, bpm, L, spb = arrange(name)
        x = finish(master(mix, L))
        write(os.path.join(MASTERS, f'ct_music_{name}.wav'), x)
        manifest['music'].append({'id': f'ct_music_{name}', 'bpm': bpm, 'bars': 24, 'bar_samples': 4 * spb,
                                  'loop_end_sample_exclusive': L, 'seconds': round(L / SR, 3)})
        print(f'ct_music_{name}', bpm, 'BPM', round(L / SR, 2), 's')
    for kind in ('win', 'stuck', 'book'):
        y = finish(stinger(kind), -18, -2.5)
        write(os.path.join(MASTERS, f'ct_stinger_{kind}.wav'), y)
        manifest['stingers'].append(f'ct_stinger_{kind}')
    json.dump(manifest, open(os.path.join(OUT, 'thieves_audio_manifest.json'), 'w'), indent=1)


if __name__ == '__main__':
    main()
