"""Liar's Dice score: "The Locker Waltz".

A drowned sailors' waltz in D minor, 3/4 at 132 BPM: a wheezing concertina
tune, a banjo-ish pluck on the "pah-pah", a tuba on the "oom", glassy shimmer
for the water overhead and a deep, cold pad underneath. Two arrangements of
the same 24 bars, so the game can cut between them on any bar line:

  calm    the tune, the oom-pah, a little woodblock: a tavern at the bottom of the sea
  tense   no tune; a heartbeat kick, a low pizzicato ostinato, a trembling glass
          and a long low clarinet: the bids have climbed past what's likely

Plus stingers: "Liar!" (a brass-and-reed stab), the bid stands / the bid was a
lie, a game won, a game lost, and freedom (the debt paid). Everything is
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
rng = np.random.default_rng(1715)

MINOR = [0, 2, 3, 5, 7, 8, 10]
TONIC = 62  # D
BPM = 132


def deg(d, tonic=TONIC, scale=MINOR):
    return tonic + scale[d % 7] + 12 * (d // 7)


# one chord per bar (root degree); a raised seventh on the dominant bars (5 = A major)
CHORDS = [0, 0, 3, 3, 4, 4, 0, 4,   5, 5, 2, 2, 3, 3, 4, 4,   0, 0, 3, 3, 4, 4, 0, 0]
TUNE = [
    [(4, 1), (2, 1), (0, 1)], [(4, 2), (5, .5), (4, .5)], [(3, 1), (5, 1), (7, 1)], [(5, 2), (None, 1)],
    [(4, 1), (6, 1), (8, 1)], [(7, 1.5), (6, .5), (4, 1)], [(2, 1), (4, 1), (2, 1)], [(1, 2), (None, 1)],
    [(5, 1), (7, 1), (9, 1)], [(9, 2), (8, 1)], [(7, 1), (5, 1), (4, 1)], [(2, 2), (None, 1)],
    [(3, 1), (5, 1), (3, 1)], [(2, 1), (0, 1), (2, 1)], [(4, 1.5), (5, .5), (4, 1)], [(1, 2), (None, 1)],
    [(4, 1), (2, 1), (0, 1)], [(4, 2), (5, .5), (4, .5)], [(3, 1), (5, 1), (7, 1)], [(5, 2), (None, 1)],
    [(4, 1), (6, 1), (8, 1)], [(7, 1.5), (6, .5), (4, 1)], [(2, 1), (1, 1), (-1, 1)], [(0, 2), (None, 1)],
]
LEVELS = dict(lead=-16, chords=-22, bass=-17, shimmer=-27, pad=-25, drums=-26, pulse=-19, low=-20, tremolo=-26)


def note(inst, m, dur, vel):
    return np.asarray(render_note(inst, int(m), dur, vel), dtype=float)


def triad(root):
    t = [root, root + 2, root + 4]
    return t


def chord_notes(root, base):
    ms = [deg(x, base) for x in triad(root)]
    if root % 7 == 4:  # the dominant, with its leading note raised (A major in D minor)
        ms[1] += 1
    return ms


def arrange(kind):
    spb = int(round(SR * 60 / BPM))
    bars = len(CHORDS)
    L = bars * 3 * spb
    mix = LoopMix(L, circular=True)

    def at(bar, beat):
        lilt = -.05 * spb if beat % 3 == 1 else 0
        return int(round((bar * 3 + beat) * spb + lilt))

    for bar in range(bars):
        root = CHORDS[bar]
        ch = chord_notes(root, TONIC - 12)
        r = deg(root, TONIC - 24)
        if kind == 'calm':
            beat = 0.0
            for d, b in TUNE[bar]:
                if d is not None:
                    m = deg(d)
                    if root % 7 == 4 and (d % 7) == 6:
                        m += 1  # the leading note
                    mix.add('lead', note('reed', m, b * spb / SR * .9, .7 + rng.uniform(-.05, .05)), at(bar, beat), .15)
                beat += b
            mix.add('bass', note('tuba', r, .8 * spb / SR, .85), at(bar, 0), -.1)
            for b2 in (1, 2):
                for k, m in enumerate(ch):
                    mix.add('chords', note('pluck', m, .4 * spb / SR, .5 if b2 == 1 else .42), at(bar, b2) + k * 70, .3 - .2 * k)
            if bar % 4 == 3:
                for k, m in enumerate(ch):
                    mix.add('shimmer', note('glass', m + 24, 1.4, .4), at(bar, 1) + k * int(.1 * SR), .5 - .4 * k)
            mix.add('drums', render_drum('wblo', .3, bar), at(bar, 0), -.3)
            mix.add('drums', render_drum('wbhi', .22, bar), at(bar, 2), -.3)
        else:
            # the heartbeat: lub-dub on the downbeat
            mix.add('drums', render_drum('kick', .8, bar), at(bar, 0), 0)
            mix.add('drums', render_drum('kick', .55, bar + 1), at(bar, .45), 0)
            # a low pizzicato ostinato, the chord's root and fifth
            for b2, m in ((0, r + 12), (1, deg(root + 4, TONIC - 12)), (2, r + 12)):
                mix.add('pulse', note('pizz', m, .45 * spb / SR, .7), at(bar, b2), -.15)
            # a long low clarinet line, a note every two bars
            if bar % 2 == 0:
                mix.add('low', note('clarinet', deg(root, TONIC - 12), 5.6 * spb / SR, .5), at(bar, 0), .2)
            # a trembling glass
            for k in range(6):
                mix.add('tremolo', note('glass', ch[2] + 24, .18, .35), at(bar, k * .5), .4)
        for k, m in enumerate(ch):
            mix.add('pad', note('pad', m - (12 if kind == 'tense' else 0), 3 * spb / SR, .35), at(bar, 0), (k - 1) * .5)
    return mix, L, spb


def bus_rms_db(b):
    m = b.mean(0)
    r = np.sqrt(np.mean(m ** 2))
    return 20 * np.log10(max(r, 1e-9))


def master(mix, L, wet=.32):
    out = np.zeros((2, L))
    send = np.zeros((2, L))
    for name, b in mix.buses.items():
        g = 10 ** ((LEVELS.get(name, -20) - bus_rms_db(b)) / 20)
        b = b * g
        out += b
        send += b * {'lead': .35, 'chords': .3, 'pad': .5, 'shimmer': .6, 'tremolo': .6, 'drums': .15, 'bass': .08, 'pulse': .2, 'low': .4}.get(name, .1)
    ir = make_ir(length=2.6, rt_low=2.2, rt_mid=1.8, rt_high=.8)  # a big wet hull
    out += conv_reverb(send, ir, True) * wet
    out = np.stack([fft_filter(c, 35, 9000, 2, circular=True) for c in out])  # the sea takes the top off
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
    n = int((4.2 if kind in ('win', 'lose', 'free') else 1.8) * SR)
    out = np.zeros((2, n))
    spb = SR * 60 / BPM

    def put(y, beat, pan=0.0, g=1.0):
        s = int(beat * spb)
        e = min(n, s + len(y))
        if e <= s:
            return
        gl, gr = np.cos((pan + 1) * np.pi / 4), np.sin((pan + 1) * np.pi / 4)
        out[0, s:e] += gl * g * y[:e - s]
        out[1, s:e] += gr * g * y[:e - s]

    if kind == 'liar':  # a stab: tuba and reed on a diminished chord, a thump
        for m in (38, 50, 53, 56, 59):
            put(note('reed' if m > 45 else 'tuba', m, .9, .9), 0, .1 * (m % 3 - 1))
        put(render_drum('kick', 1, 0), 0)
        put(note('glass', 86, 1.2, .5), .1, .4)
    elif kind == 'true':  # the bid stands: a plain major chord, plucked
        for k, m in enumerate((62, 66, 69, 74)):
            put(note('pluck', m, 1.0, .7), k * .12, .2 * (k - 1.5))
    elif kind == 'false':  # a lie found out: a descending minor plunk
        for k, m in enumerate((69, 65, 62, 57)):
            put(note('pluck', m, .7, .7), k * .18, .2 * (k - 1.5))
        put(note('tuba', 38, .8, .7), .6)
    elif kind == 'win':  # a jolly cadence, concertina and banjo, in D major
        for k, d in enumerate((0, 2, 4, 7)):
            put(note('reed', 62 + [0, 4, 7, 12][k], .35, .8), k * .5, .1)
        for m in (50, 62, 66, 69, 74):
            put(note('pluck', m, 2.0, .7), 2.0, 0, .7)
        put(note('reed', 78, 1.6, .8), 2.0)
        put(note('tuba', 38, 1.4, .8), 2.0)
        put(render_drum('triangle', .5, 1), 2.0, -.3)
        for k in range(4):
            put(note('glass', 86 + [0, 4, 7, 12][k], .6, .45), 2.2 + k * .25, .4)
    elif kind == 'lose':  # the tuba's sorrowful slide down; a door closing on the locker
        for k, m in enumerate((45, 44, 43, 38)):
            put(note('tuba', m, .6 if k < 3 else 1.8, .8), k * .7)
        for m in (50, 53, 57):
            put(note('reed', m, 1.8, .55), 2.1, .2, .7)
        put(render_drum('kick', .9, 2), 2.1)
    else:  # freedom: the tune itself, in the major, bright, with everything
        beat = 0.0
        for d, b in [(4, 1), (2, 1), (0, 1), (4, 2), (7, 1)]:
            put(note('reed', [62, 64, 66, 67, 69, 71, 73][d % 7] + 12 * (d // 7) + 12, b * spb / SR, .85), beat, .1)
            beat += b
        for m in (50, 62, 66, 69, 74):
            put(note('pluck', m, 2.4, .7), 4, 0, .7)
        put(note('glass', 98, 2.0, .5), 4, .4)
    send = out * .35
    ir = make_ir(length=2.4)
    out = out + conv_reverb(send, ir, False)[:, :n] * .35
    fade = int(.3 * SR)
    out[:, -fade:] *= np.linspace(1, 0, fade)
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(MASTERS, exist_ok=True)
    manifest = {'music': [], 'stingers': []}
    for kind, name in (('calm', 'ld_music'), ('tense', 'ld_music_tense')):
        mix, L, spb = arrange(kind)
        x = finish(master(mix, L), -20 if kind == 'calm' else -21)
        write(os.path.join(MASTERS, name + '.wav'), x)
        manifest['music'].append({'id': name, 'bpm': BPM, 'bars': len(CHORDS), 'bar_samples': 3 * spb, 'loop_end_sample_exclusive': L, 'seconds': round(L / SR, 3)})
        print(name, round(L / SR, 2), 's')
    for kind, name in (('liar', 'ld_liar'), ('true', 'ld_true'), ('false', 'ld_false'), ('win', 'ld_stinger_win'), ('lose', 'ld_stinger_lose'), ('free', 'ld_stinger_free')):
        y = finish(stinger(kind), -18, -2.5)
        write(os.path.join(MASTERS, name + '.wav'), y)
        manifest['stingers'].append(name)
    json.dump(manifest, open(os.path.join(OUT, 'dice_audio_manifest.json'), 'w'), indent=1)


if __name__ == '__main__':
    main()
