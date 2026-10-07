"""Four Pegs score: "The Mastermind's Console".

One lair theme in D minor, written once as a 16-bar form (played twice per
loop) and arranged in three tiers that share it, so the game can cut between
them on a bar line as the player runs out of turns:

  tier 1  Scheming     116 BPM  pizzicato bass, harpsichord, a solo string line
  tier 2  Rising       128 BPM  driving strings, snare, brass stabs, a ticking clock
  tier 3  Doom clock   144 BPM  sixteenth-note strings, full brass, timpani, choir, alarm

Plus stingers: his defeat (you cracked it), his triumph (you ran out), and a
top-score fanfare. Everything is synthesized (engine/synth.py + the timbres
below); no samples. Writes 48 kHz 16-bit WAV masters into masters/ and the
manifest (exact loop and bar lengths) into ../assets/audio. The game ships AAC
encodings of the masters (afconvert -f m4af -d aac -b 192000); their decoded
lengths match the manifest's loop points exactly.
"""
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'engine'))
from synth import SR, mtof, tarr, gate, noise, fft_filter, pluck, bass, kick, hat, rim, shaker, woodblock, make_ir, conv_reverb  # noqa: E402

OUT = os.path.join(os.path.dirname(__file__), '..', 'assets', 'audio')
MASTERS = os.path.join(os.path.dirname(__file__), 'masters')
rng = np.random.default_rng(1966)


# ------------------------------------------------------------------ timbres
def additive(f_t, n, amp_k, K):
    """Sum of harmonics with per-harmonic amplitude arrays amp_k(k) -> array(n)."""
    ph = 2 * np.pi * np.cumsum(f_t) / SR
    y = np.zeros(n)
    for k in range(1, K + 1):
        a = amp_k(k)
        if np.max(np.abs(a)) < 1e-4:
            continue
        y += a * np.sin(k * ph + rng.uniform(0, 6.28))
    return y


def harpsichord(m, dur, vel=.7):
    f = mtof(m)
    y = pluck(m, min(dur, .5), vel, pos=.08, decay=.55, bright=2.2, body=True)
    y2 = pluck(m + 12, min(dur, .4), vel * .4, pos=.11, decay=.45, bright=2.0, body=False)
    n = max(len(y), len(y2))
    out = np.zeros(n)
    out[:len(y)] += y
    out[:len(y2)] += .5 * y2
    return out * .9


def organ(m, dur, vel=.6):
    f = mtof(m)
    rel = .25
    n = int((dur + rel) * SR)
    t = tarr(n)
    lesl = 1 + .004 * np.sin(2 * np.pi * 6.2 * t)
    bars = {1: 1, 2: .7, 3: .45, 4: .5, 6: .25, 8: .3}
    y = np.zeros(n)
    for k, a in bars.items():
        if k * f < 9000:
            y += a * np.sin(2 * np.pi * k * f * np.cumsum(lesl) / SR)
    y *= (1 + .12 * np.sin(2 * np.pi * 5.8 * t))
    return y * gate(n, dur, .05, rel) * vel * .35


def brass(m, dur, vel=.7, stab=False):
    f = mtof(m)
    rel = .08 if stab else .18
    n = int((dur + rel) * SR)
    t = tarr(n)
    att = .03 if stab else .06
    env = np.minimum(1, t / att)
    bright = (900 + 2600 * vel) * (.55 + .45 * env) * (1 + .3 * np.exp(-t / .12))
    vib = 1 + .005 * np.sin(2 * np.pi * 5.5 * t) * np.minimum(1, t / .35)
    f_t = f * vib * (1 - .015 * np.exp(-t / .04))
    K = int(min(30, 7000 / f))
    y = additive(f_t, n, lambda k: np.exp(-(k * f) / bright) / k ** .6, K)
    y += .06 * fft_filter(noise(n), 800, 3500) * np.exp(-t / .05)
    return y * gate(n, dur, att * .5, rel) * (.3 + .6 * vel) * .4


def strings(m, dur, vel=.6, stacc=False):
    f = mtof(m)
    rel = .07 if stacc else .3
    n = int((dur + rel) * SR)
    t = tarr(n)
    y = np.zeros(n)
    K = int(min(24, 8000 / f))
    for det in (-6, 0, 7):
        vib = 1 + .004 * np.sin(2 * np.pi * (5.2 + det * .05) * t + det)
        f_t = f * 2 ** (det / 1200) * vib
        y += additive(f_t, n, lambda k: np.full(n, 1 / k ** 1.1 * np.exp(-k * f / 4500)), K)
    att = .012 if stacc else .12
    bow = fft_filter(noise(n), 1500, 6000) * np.exp(-t / .04) * .08
    return (y / 3 + bow) * gate(n, dur, att, rel) * vel * .32


def timpani(m, dur=1.2, vel=.8):
    f = mtof(m)
    n = int(1.6 * SR)
    t = tarr(n)
    y = np.zeros(n)
    for r, a, tau in ((1, 1, .9), (1.5, .5, .5), (1.99, .35, .4), (2.44, .2, .3)):
        y += a * np.exp(-t / tau) * np.sin(2 * np.pi * f * r * t * (1 + .01 * np.exp(-t / .05)))
    y += .5 * fft_filter(noise(n), 60, 800) * np.exp(-t / .03)
    return y * vel * .5


def snare(vel=.7):
    n = int(.25 * SR)
    t = tarr(n)
    body = np.sin(2 * np.pi * 185 * t) * np.exp(-t / .05)
    nz = fft_filter(noise(n), 1500, 9000) * np.exp(-t / .07)
    return (body * .6 + nz) * vel * .5


def tom(m, vel=.7):
    n = int(.4 * SR)
    t = tarr(n)
    f = mtof(m) * (1 + .3 * np.exp(-t / .03))
    return np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t / .18) * vel * .6


def choir(m, dur, vel=.5):
    f = mtof(m)
    rel = .5
    n = int((dur + rel) * SR)
    t = tarr(n)
    y = np.zeros(n)
    form = ((700, 1, 110), (1150, .5, 120), (2700, .25, 180))  # "ah"
    for det in (-9, 0, 8):
        vib = 1 + .006 * np.sin(2 * np.pi * (5 + det * .05) * t + det)
        f_t = f * 2 ** (det / 1200) * vib
        K = int(min(28, 4500 / f))
        y += additive(f_t, n, lambda k: np.full(n, sum(a * np.exp(-((k * f - F) / bw) ** 2 / 8) for F, a, bw in form) + .02 / k), K)
    return y / 3 * gate(n, dur, .25, rel) * vel * .5


def alarm(m1, m2, dur, vel=.35):
    """A two-tone klaxon, half the duration on each note."""
    n = int(dur * SR)
    t = tarr(n)
    f = np.where(t < dur / 2, mtof(m1), mtof(m2))
    f = fft_filter(f, None, 30)  # soften the step
    K = 9
    y = additive(f, n, lambda k: np.full(n, (1 / k if k % 2 else 0)), K)
    return y * gate(n, dur - .02, .01, .02) * vel * .3


def pizz_bass(m, dur, vel=.7):
    return bass(m, min(dur, .35), vel) * .9


# ------------------------------------------------------------------ the form
# 16 bars in D minor. Each bar: (root midi, chord tones as midi offsets from root)
CHORDS = [
    (50, (0, 3, 7)), (50, (0, 3, 8)), (55, (0, 3, 7)), (57, (0, 4, 7, 10)),
    (50, (0, 3, 7)), (53, (0, 4, 7)), (46, (0, 4, 7)), (57, (0, 5, 7, 10)),
    (55, (0, 3, 7)), (50, (0, 3, 7)), (52, (0, 3, 6, 10)), (57, (0, 4, 7, 10, 13)),
    (50, (0, 3, 7)), (46, (0, 4, 7)), (55, (0, 3, 7, 9)), (57, (0, 4, 7, 10)),
]
# the villain's theme: (midi, beats); rests are None
MELODY = [
    [(74, 1), (77, .5), (76, .5), (74, 1), (69, 1)],
    [(70, 1.5), (69, .5), (68, 1), (69, 1)],
    [(70, .5), (72, .5), (74, 1), (79, 1), (77, .5), (76, .5)],
    [(76, 2), (73, 1), (69, 1)],
    [(74, 1), (77, .5), (81, .5), (80, 1), (81, 1)],
    [(77, 1.5), (76, .5), (74, 1), (72, 1)],
    [(70, 1), (74, 1), (79, 1), (77, .5), (74, .5)],
    [(76, 3), (None, 1)],
    [(79, 1.5), (77, .5), (76, 1), (74, 1)],
    [(77, 1.5), (76, .5), (74, 1), (69, 1)],
    [(70, 1), (79, 1), (76, 1), (74, .5), (73, .5)],
    [(69, .5), (70, .5), (73, .5), (76, .5), (79, 1), (82, 1)],
    [(81, 1.5), (77, .5), (74, 1), (69, 1)],
    [(70, 1), (74, 1), (77, 1), (74, 1)],
    [(76, 1), (79, 1), (82, 1), (79, 1)],
    [(81, 2), (73, 1), (76, .5), (79, .5)],
]


class Mix:
    def __init__(self, L):
        self.L = L
        self.buses = {}

    def add(self, bus, y, start, pan=0.0, gain=1.0):
        b = self.buses.setdefault(bus, np.zeros((2, self.L)))
        gl, gr = np.cos((pan + 1) * np.pi / 4), np.sin((pan + 1) * np.pi / 4)
        s = start % self.L
        pos = 0
        while pos < len(y):  # wrap tails into the loop start: seamless
            k = min(len(y) - pos, self.L - s)
            b[0, s:s + k] += gl * gain * y[pos:pos + k]
            b[1, s:s + k] += gr * gain * y[pos:pos + k]
            pos += k
            s = 0


def arrange(tier):
    bpm = {1: 116, 2: 128, 3: 144}[tier]
    spb = SR * 60 / bpm
    bars = 32  # the form twice: the second pass hands the theme to other voices
    L = int(round(bars * 4 * spb))
    mix = Mix(L)
    at = lambda beat: int(round(beat * spb + rng.normal(0, .002) * SR))  # noqa: E731
    sec = lambda beats: beats * spb / SR  # noqa: E731
    for bi in range(bars):
        root, tones = CHORDS[bi % 16]
        b0 = bi * 4
        # ---- bass: a sneaky pizzicato walk; driving eighths later
        pat = [0, 0, 7, 8, 7, 0, 12, 11] if tier == 1 else [0, 0, 12, 0, 7, 0, 10, 12]
        bass_root = root - 12 if root >= 50 else root
        for k, iv in enumerate(pat):
            if tier == 1 and k in (1, 6) and bi % 2:
                continue
            mix.add('bass', pizz_bass(bass_root + iv, sec(.45), .75 if k % 2 == 0 else .55), at(b0 + k * .5), 0)
        # ---- harpsichord: arpeggiated chord tones (tier 1 and 2)
        if tier <= 2:
            notes = [root + 12 + t for t in tones]
            order = [0, 1, 2, 1, 3 if len(notes) > 3 else 2, 2, 1, 2]
            for k in range(8):
                m = notes[order[k] % len(notes)]
                mix.add('harp', harpsichord(m, sec(.4), .5 + .15 * (k % 2 == 0)), at(b0 + k * .5), .35 if k % 2 else -.15)
        # ---- strings: staccato ostinato (tier 2 eighths, tier 3 sixteenths)
        if tier >= 2:
            step = .5 if tier == 2 else .25
            notes = [root + t for t in tones[:3]]
            for k in range(int(4 / step)):
                m = notes[[0, 1, 2, 1][k % 4]] + (12 if tier == 3 and k % 8 >= 4 else 0)
                mix.add('strings', strings(m, sec(step * .6), .55 + .1 * (k % 2 == 0), stacc=True), at(b0 + k * step), -.3)
        # ---- organ swells at phrase ends; continuous in tier 3
        if bi % 4 == 3 or tier == 3:
            for t in tones[:4]:
                mix.add('organ', organ(root + t, sec(4 if tier == 3 else 4), .45 if tier < 3 else .3), at(b0), 0)
        # ---- brass stabs on the downbeat (tiers 2-3)
        if tier >= 2 and bi % 2 == 0:
            for t in tones[:3]:
                mix.add('brass', brass(root + t, sec(.3), .7, stab=True), at(b0), .15)
            if tier == 3:
                for t in tones[:3]:
                    mix.add('brass', brass(root + t + 12, sec(.25), .65, stab=True), at(b0 + 2.5), -.1)
        # ---- choir (tier 3): a dark "ah" on the chord
        if tier == 3:
            for t in tones[:3]:
                mix.add('choir', choir(root + 12 + t, sec(3.8), .45), at(b0), 0)
        # ---- percussion
        for beat in range(4):
            tb = b0 + beat
            if tier == 1:
                if beat in (0, 2):
                    mix.add('drums', kick(.45), at(tb))
                if beat in (1, 3):
                    mix.add('drums', rim(.45), at(tb), .2)
                for e in (0, .5):
                    mix.add('drums', shaker(.25 + .1 * (e == 0)), at(tb + e), -.3)
            else:
                if beat in (0, 2) or (tier == 3):
                    mix.add('drums', kick(.7 if beat in (0, 2) else .5), at(tb))
                if tier == 3 and beat in (1, 3):
                    mix.add('drums', kick(.45), at(tb + .5))
                if beat in (1, 3):
                    mix.add('drums', snare(.75), at(tb), .05)
                hats = (0, .5) if tier == 2 else (0, .25, .5, .75)
                for e in hats:
                    mix.add('drums', hat(.35 if e == 0 else .22), at(tb + e), .3)
                # the ticking clock: time is running out
                mix.add('tick', woodblock(.35 if tier == 2 else .45, beat % 2 == 0), at(tb), .5 if beat % 2 else -.5)
        if tier >= 2 and bi % 4 == 0:
            mix.add('timp', timpani(38, vel=.75), at(b0))
        if tier == 3:
            mix.add('timp', timpani(38 if bi % 2 == 0 else 45, vel=.6), at(b0 + 2))
            # the klaxon, low in the mix, every other bar
            if bi % 2 == 1:
                mix.add('alarm', alarm(81, 77, sec(2)), at(b0 + 2), 0)
        # fills at phrase ends
        if bi % 4 == 3 and tier >= 2:
            for k, m in enumerate([50, 47, 45, 43] if tier == 2 else [52, 50, 47, 45, 43, 40, 38, 36]):
                step = .25 if tier == 2 else .125
                mix.add('drums', tom(m, .6), at(b0 + 3 + k * step), -.2 + .1 * k)
    # ---- melody. Pass 1: theremin (1), theremin over brass (2), brass in octaves (3).
    # Pass 2: organ with a theremin countermelody (1), strings and brass (2), brass and choir (3).
    beat = 0
    for pas in (0, 1):
        for bi, bar in enumerate(MELODY):
            root, tones = CHORDS[bi]
            if pas == 1 and tier == 1:  # a long-note countermelody on the chord's third
                mix.add('lead', strings(root + 24 + tones[1] - (12 if root + 24 + tones[1] > 80 else 0), sec(3.8), .4), at(bi * 4 + 64), .2)
            for m, d in bar:
                if m is not None:
                    if tier == 1:
                        if pas == 0: mix.add('lead', strings(m, sec(d * .98), .62), at(beat), .1)
                        else: mix.add('organ', organ(m - 12, sec(d * .9), .55), at(beat), -.1)
                    elif tier == 2:
                        if pas == 0: mix.add('lead', strings(m, sec(d * .98), .55), at(beat), .1)
                        else: mix.add('strings', strings(m, sec(d * .95), .6), at(beat), .2)
                        mix.add('brass', brass(m - 12, sec(d * .9), .55), at(beat), -.1)
                    else:
                        mix.add('brass', brass(m, sec(d * .9), .75), at(beat), 0)
                        mix.add('brass', brass(m - 12, sec(d * .9), .65), at(beat), -.15)
                        if pas == 1: mix.add('choir', choir(m, sec(d * .95), .4), at(beat), .1)
                beat += d
    return mix, bpm, L


LEVELS = {  # relative bus levels (dB) per tier
    1: dict(bass=-15, harp=-19, lead=-15, organ=-22, drums=-20),
    2: dict(bass=-14, harp=-21, lead=-17, organ=-22, drums=-17, strings=-17, brass=-17, tick=-24, timp=-18),
    3: dict(bass=-13, lead=-16, organ=-24, drums=-15, strings=-16, brass=-14, tick=-23, timp=-16, choir=-21, alarm=-27),
}


def bus_rms_db(b):
    m = b.mean(0)
    r = np.sqrt(np.mean(m ** 2))
    return 20 * np.log10(max(r, 1e-9))


def master(mix, levels, wet=.22):
    out = np.zeros((2, mix.L))
    send = np.zeros((2, mix.L))
    for name, b in mix.buses.items():
        g = 10 ** ((levels.get(name, -20) - bus_rms_db(b)) / 20)
        b = b * g
        out += b
        send += b * (.35 if name in ('lead', 'harp', 'brass', 'choir', 'strings') else .12)
    ir = make_ir(length=2.2, rt_low=1.8, rt_mid=1.4, rt_high=.7)
    out += conv_reverb(send, ir, True) * wet
    out = np.stack([fft_filter(c, 30, None, 2, circular=True) for c in out])
    return out


def finish(x, target_rms_db=-19.0, ceiling_db=-3.0):
    x = x * 10 ** ((target_rms_db - bus_rms_db(x)) / 20)
    c = 10 ** (ceiling_db / 20)
    x = np.tanh(x / c) * c  # gentle soft limit
    return x


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
    n = int(4.2 * SR)
    mix = Mix(n)
    mix.L = n
    spb = SR * 60 / 116

    def put(bus, y, beat, pan=0):
        s = int(beat * spb)
        b = mix.buses.setdefault(bus, np.zeros((2, n)))
        e = min(n, s + len(y))
        b[:, s:e] += y[:e - s]

    if kind == 'defeat':  # you won: his plan collapses, comically
        for i, m in enumerate([62, 61, 60, 59]):
            d = .75 if i < 3 else 2.5
            put('brass', brass(m, d * spb / SR, .75), i * .75)
            put('brass', brass(m - 12, d * spb / SR, .6), i * .75)
        put('timp', timpani(38, vel=.9), 0)
        put('timp', timpani(33, vel=.9), 2.25)
        for i, m in enumerate([86, 81, 77, 74, 69, 65, 62]):
            put('harp', harpsichord(m, .2, .6), 2.25 + i * .125)
    elif kind == 'triumph':  # you lost: his triumph
        for t in (0, 3, 7, 12):
            put('organ', organ(38 + t + 12, 3.2, .7), 0)
        for i, (m, d) in enumerate([(62, .5), (65, .5), (69, .5), (74, 2.5)]):
            put('brass', brass(m, d * spb / SR, .85), [0, .5, 1, 1.5][i])
            put('brass', brass(m - 12, d * spb / SR, .7), [0, .5, 1, 1.5][i])
        put('timp', timpani(38, vel=1), 0)
        put('timp', timpani(38, vel=.8), 1.5)
        for i in range(8):
            put('choir', choir(62 + [0, 3, 7][i % 3], .6, .5), 1.5 + i * .25)
    else:  # top score: the heroic turn to D major
        for i, (m, d) in enumerate([(62, .5), (66, .5), (69, .5), (74, 2)]):
            put('brass', brass(m, d * spb / SR, .8), [0, .5, 1, 1.5][i])
            put('strings', strings(m - 12, d * spb / SR, .6), [0, .5, 1, 1.5][i])
        for t in (0, 4, 7):
            put('organ', organ(50 + t, 2.5, .5), 1.5)
        put('timp', timpani(38, vel=.8), 1.5)
    out = np.zeros((2, n))
    send = np.zeros((2, n))
    for name, b in mix.buses.items():
        out += b
        send += b * .3
    ir = make_ir(length=2.0)
    out += conv_reverb(send, ir, False)[:, :n] * .3
    fade = int(.3 * SR)
    out[:, -fade:] *= np.linspace(1, 0, fade)
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(MASTERS, exist_ok=True)
    manifest = {'music': [], 'stingers': []}
    for tier in (1, 2, 3):
        mix, bpm, L = arrange(tier)
        x = finish(master(mix, LEVELS[tier]))
        name = f'fp_music_t{tier}'
        write(os.path.join(MASTERS, name + '.wav'), x)
        bar = int(round(4 * SR * 60 / bpm))
        manifest['music'].append({'id': name, 'bpm': bpm, 'bars': 32, 'bar_samples': bar,
                                  'loop_end_sample_exclusive': L, 'seconds': round(L / SR, 3)})
        print(name, bpm, 'BPM', round(L / SR, 2), 's')
    for kind in ('defeat', 'triumph', 'topscore'):
        x = finish(stinger(kind), -18, -2.5)
        write(os.path.join(MASTERS, f'fp_stinger_{kind}.wav'), x)
        manifest['stingers'].append(f'fp_stinger_{kind}')
    json.dump(manifest, open(os.path.join(OUT, 'fourpegs_audio_manifest.json'), 'w'), indent=1)


if __name__ == '__main__':
    main()
