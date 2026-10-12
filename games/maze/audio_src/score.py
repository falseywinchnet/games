"""Maze 95 score: three upbeat loops, one chosen at random for each level.

After the action-game music of the late 90s (3D Maze Man's was the owner's
reference): major keys, a punchy bass that does most of the work, busy
hi-hats, a lead that runs in sixteenths and slides into its long notes, loud
and bright.

  mz_music        "Maze Run": synth rock. A major, 98 BPM: driving octave bass,
                  chugging power chords, a sliding square lead, brass stabs; a
                  D-C-G middle and an F-G-A climb back to the top.
  mz_music_bonus  "Bonus Round": a bouncy romp. C major, 124 BPM: oom-pah
                  pizzicato and marimba, celesta, clarinet and flute passing a
                  tune around, brass hits, woodblocks over a real beat.
  mz_music_turbo  "Turbo": a funky groove. B-flat, 112 BPM: slap-ish synth bass,
                  Rhodes stabs, a brass section, vibraphone and lead trading
                  phrases, claps and a breakbeat.

Each loop runs about two minutes in sections, so it wears for an hour. Each
has two companions: _dark, for a blackout (heard through a wall), and _flip,
for walking on the ceiling (backwards). Everything is synthesized
(engine/synth.py and the instruments below); no samples. Writes 48 kHz 16-bit
WAV masters into masters/ (not tracked: run this to remake them) and the music entries of the manifest (exact loop
lengths) into ../assets/audio; the start chime and win jingle stay as
mall_music.py made them. The game ships AAC encodings (afconvert -f m4af -d
aac -b 160000; the muffled _dark versions at 64000).
"""
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'engine'))
from synth import (SR, INSTR, DRUMS, mtof, tarr, gate, noise, fft_filter, render_note, render_drum, make_ir, conv_reverb,  # noqa: E402
                   pingpong, LoopMix, wt)
from mall_music import bus_rms_db, finish, write  # noqa: E402

OUT = os.path.join(os.path.dirname(__file__), '..', 'assets', 'audio')
MASTERS = os.path.join(os.path.dirname(__file__), 'masters')


# --------------------------------------------------------------------- instruments the score adds
def _lead(m, dur, vel, slide):
    """Square-and-saw lead with a delayed vibrato; `slide` semitones below, gliding up."""
    f = mtof(m)
    rel = .07
    n = int((dur + rel) * SR)
    t = tarr(n)
    vib = .006 * np.clip((t - .18) / .2, 0, 1) * np.sin(2 * np.pi * 5.6 * t)
    fr = f * 2 ** (-slide / 12 * np.exp(-t / .035)) * (1 + vib)
    y = .55 * wt('saw', fr) + .7 * wt('clar', fr * 1.003, phase0=.3)
    dark = fft_filter(y, 120, min(f * 5, 5000), 2)
    y = dark + .5 * (fft_filter(y, 120, 7000, 2) - dark) * np.exp(-t / .08)
    return np.tanh(1.6 * y) * gate(n, dur, .004, rel) * (.4 + .6 * vel) * .45


def lead(m, dur, vel=0.7):
    return _lead(m, dur, vel, 0)


def leadslide(m, dur, vel=0.7):
    return _lead(m, dur, vel, 2)


def sqbass(m, dur, vel=0.8):
    """Punchy synth bass: a filter that snaps shut, over a sine sub."""
    f = mtof(m)
    rel = .04
    n = int((dur + rel) * SR)
    t = tarr(n)
    y = wt('saw', f * np.ones(n)) + .6 * wt('clar', f * 1.002 * np.ones(n), phase0=.2)
    dark = fft_filter(y, 30, f * 2.2, 2)
    y = dark + vel * (fft_filter(y, 30, f * 12, 2) - dark) * np.exp(-t / .045)
    y = y + 1.1 * np.sin(2 * np.pi * f * t)
    return np.tanh(1.4 * y) * gate(n, dur, .002, rel) * (.4 + .6 * vel) * .6


def gtr(m, dur, vel=0.7):
    """A distorted power chord (root, fifth, octave)."""
    f = mtof(m)
    rel = .05
    n = int((dur + rel) * SR)
    t = tarr(n)
    y = np.zeros(n)
    for iv in (0, 7, 12):
        for c in (-7, 7):
            y += wt('saw', f * 2 ** (iv / 12 + c / 1200) * np.ones(n), phase0=(iv * .13 + c) % 1)
    y = fft_filter(np.tanh(y * 3.5), 90, 3400, 2)
    y = y * (1 + .6 * np.exp(-t / .03))
    return y * gate(n, dur, .003, rel) * (.35 + .65 * vel) * .22


def brass(m, dur, vel=0.7):
    """A synth brass voice: detuned saws, the filter swelling open then settling."""
    f = mtof(m)
    rel = .1
    n = int((dur + rel) * SR)
    t = tarr(n)
    y = sum(wt('saw', f * 2 ** (c / 1200) * np.ones(n), phase0=(c * .07) % 1) for c in (-9, 0, 8))
    dark = fft_filter(y, 100, f * 2.5, 2)
    br = fft_filter(y, 100, min(f * 9, 6000), 2)
    env = np.clip(t / .025, 0, 1) * (.55 + .45 * np.exp(-t / .12))
    return (dark + (br - dark) * env * vel) * gate(n, dur, .012, rel) * (.4 + .6 * vel) * .3


def bright_pluck(m, dur, vel=0.6):
    f = mtof(m)
    n = int(.5 * SR)
    t = tarr(n)
    y = wt('saw', f * np.ones(n)) + wt('saw', f * 1.006 * np.ones(n), phase0=.4)
    dark = fft_filter(y, 150, f * 1.5, 2)
    y = dark + (fft_filter(y, 150, 8000, 2) - dark) * np.exp(-t / .06)
    return y * np.exp(-t / .16) * gate(n, 10, .001) * (.4 + .6 * vel) * .35


def slap(m, dur, vel=0.8):
    """Slap-ish bass: a bright thumb attack and a round body."""
    f = mtof(m)
    rel = .05
    n = int((dur + rel) * SR)
    t = tarr(n)
    ph = 2 * np.pi * f * t
    y = np.sin(ph) + .5 * np.sin(2 * ph) * np.exp(-t / .08) + .35 * np.sin(3 * ph + 1) * np.exp(-t / .03)
    k = int(.006 * SR)
    y[:k] += .6 * fft_filter(noise(k, 4), 800, 5000)[:k] * np.linspace(1, 0, k)
    return np.tanh(1.3 * y) * gate(n, dur, .001, rel) * (.4 + .6 * vel) * .7


INSTR.update(lead=lead, leadslide=leadslide, sqbass=sqbass, gtr=gtr, brass=brass, bpluck=bright_pluck, slap=slap)


def punchkick(vel=0.8, seed=None):
    n = int(.35 * SR)
    t = tarr(n)
    y = np.sin(2 * np.pi * np.cumsum(50 + 110 * np.exp(-t / .025)) / SR) * np.exp(-t / .17)
    k = int(.004 * SR)
    y[:k] += .5 * fft_filter(noise(k, seed), 1500, 6000)[:k]
    return np.tanh(2 * y) * vel * .9


def bigsnare(vel=0.7, seed=None):
    n = int(.32 * SR)
    t = tarr(n)
    body = np.sin(2 * np.pi * np.cumsum(195 + 50 * np.exp(-t / .01)) / SR) * np.exp(-t / .06)
    wires = fft_filter(noise(n, seed), 1200, 9000, 1) * np.exp(-t / .13)
    crack = fft_filter(noise(n, (seed or 0) + 1), 2000, 6000, 1) * np.exp(-t / .015)
    y = .6 * body + wires / (np.abs(wires).max() + 1e-9) + .6 * crack / (np.abs(crack).max() + 1e-9)
    return np.tanh(y * 1.3) * vel * .75


def ohat(vel=0.4, seed=None):
    n = int(.4 * SR)
    t = tarr(n)
    y = fft_filter(noise(n, seed), 6500, None, 2) * np.exp(-t / .12)
    return y / (np.abs(y).max() + 1e-9) * vel * .5


def crash(vel=0.5, seed=None):
    n = int(2.2 * SR)
    t = tarr(n)
    y = fft_filter(noise(n, seed), 3500, 14000, 1) * np.exp(-t / .7)
    r = np.random.default_rng(seed)
    y = y / (np.abs(y).max() + 1e-9) + .15 * sum(np.sin(2 * np.pi * r.uniform(3000, 9000) * t) for _ in range(8)) / 8 * np.exp(-t / .5)
    return y * np.clip(t / .002, 0, 1) * vel * .55


def tom(pitch):
    def make(vel=0.7, seed=None):
        n = int(.5 * SR)
        t = tarr(n)
        y = np.sin(2 * np.pi * np.cumsum(pitch * (1 + .5 * np.exp(-t / .03))) / SR) * np.exp(-t / .2)
        return np.tanh(1.5 * y) * vel * .8
    return make


DRUMS.update(punch=punchkick, bigsnare=bigsnare, ohat=ohat, crash=crash, tomhi=tom(180), tommid=tom(130), tomlo=tom(95))


def note(inst, m, dur, vel):
    return np.asarray(render_note(inst, int(m), dur, vel), dtype=float)


class Piece:
    """Timing for one loop: bars of four beats, a little swing and a little looseness."""

    def __init__(self, bpm, bars, swing=0.0, loose=.003, seed=1):
        self.bpm, self.bars, self.swing = bpm, bars, swing
        self.spb = int(round(SR * 60 / bpm))
        self.L = bars * 4 * self.spb
        self.mix = LoopMix(self.L, circular=True)
        self.rng = np.random.default_rng(seed)
        self.loose = loose

    def at(self, bar, beat, exact=False):
        sw = self.swing * self.spb if (beat * 2) % 2 == 1 else 0
        jit = 0 if exact else self.rng.normal(0, self.loose * SR)
        return int(round((bar * 4 + beat) * self.spb + sw + jit))

    def beats(self, b):
        return b * self.spb / SR

    def play(self, bus, inst, m, bar, beat, dur, vel, pan=0.0):
        v = float(np.clip(vel * (1 + self.rng.normal(0, .05)), .05, 1))
        self.mix.add(bus, note(inst, m, self.beats(dur), v), self.at(bar, beat), pan)

    def hit(self, bus, name, bar, beat, vel, pan=0.0, rr=0):
        v = float(np.clip(vel * (1 + self.rng.normal(0, .06)), .05, 1))
        self.mix.add(bus, render_drum(name, v, rr), self.at(bar, beat), pan)

    def fill(self, bar, vel=.7):
        """A tom fill across the last two beats of a bar, and a crash on the next downbeat."""
        for k, (name, beat) in enumerate((('bigsnare', 2), ('bigsnare', 2.25), ('tomhi', 2.5), ('tomhi', 2.75),
                                          ('tommid', 3), ('tommid', 3.25), ('tomlo', 3.5), ('tomlo', 3.75))):
            self.hit('drums', name, bar, beat, vel * (.75 + k * .04), .4 - k * .1, k)
        self.hit('cym', 'crash', bar + 1, 0, .6, .3, bar)


def tune(p, bus, inst, phrase, bar0, root, vel, pan=0.0, octave=0, legato=.92, slides=True):
    """A phrase of (beat, semitones, beats); notes of a beat or longer slide in, as the lead's do."""
    for i, notes in enumerate(phrase):
        for (beat, m, d) in notes:
            name = 'leadslide' if inst == 'lead' and slides and d >= 1 else inst
            p.play(bus, name, root + m + 12 * octave, bar0 + i, beat, d * legato, vel, pan)


def master(p, levels, sends, wet, ir, lo=30, hi=15000):
    out = np.zeros((2, p.L))
    send = np.zeros((2, p.L))
    for name, b in p.mix.buses.items():
        g = 10 ** ((levels.get(name, -22) - bus_rms_db(b)) / 20)
        b = b * g
        out += b
        send += b * sends.get(name, .1)
    out += conv_reverb(send, ir, True) * wet
    return np.stack([fft_filter(c, lo, hi, 2, circular=True) for c in out])


def rock_beat(p, bar, half=False, open_hat=True):
    kicks = (0, 2) if half else (0, .75, 2, 2.5)
    for b in kicks:
        p.hit('drums', 'punch', bar, b, .9 if b in (0, 2) else .7, 0, bar)
    for b in ((2,) if half else (1, 3)):
        p.hit('drums', 'bigsnare', bar, b, .8, .05, bar + int(b))
    for k in range(16):
        if open_hat and k == 14:
            p.hit('hats', 'ohat', bar, 3.5, .45, .3, bar)
        elif k != 15 or not open_hat:
            p.hit('hats', 'hat', bar, k / 4, (.42 if k % 4 == 0 else .3 if k % 2 == 0 else .2), .3, bar * 16 + k)


# --------------------------------------------------------------------- "Maze Run"
def maze_run():
    p = Piece(98, 52, seed=95)
    A4 = 69
    # roots as MIDI bass notes, and the chord's tones above the root
    CH = {'A': (33, [0, 4, 7]), 'E': (40, [0, 4, 7]), 'D': (38, [0, 4, 7]), 'F#m': (42, [0, 3, 7]), 'C': (36, [0, 4, 7]),
          'G': (43, [0, 4, 7]), 'F': (41, [0, 4, 7])}
    VA = ['A', 'E', 'D', 'A', 'F#m', 'D', 'E', 'E']
    VB = ['D', 'C', 'G', 'D'] * 2
    TAG = ['F', 'G', 'A', 'A']
    form = ['A'] * 4 + VA + VA + VB + ['A', 'D', 'A', 'E'] + VA + VB + TAG
    starts = {'intro': 0, 'A': 4, 'A2': 12, 'B': 20, 'break': 28, 'A3': 32, 'B2': 40, 'tag': 48}
    assert len(form) == p.bars
    TA = [[(0, 0, .25), (.25, 4, .25), (.5, 7, .5), (1, 12, .75), (1.75, 11, .25), (2, 12, .5), (2.5, 7, .5), (3, 4, .5), (3.5, 7, .5)],
          [(0, 11, 1), (1, 9, .25), (1.25, 11, .25), (1.5, 14, .5), (2, 16, 1.5), (3.5, 14, .5)],
          [(0, 12, .5), (.5, 9, .5), (1, 5, .5), (1.5, 9, .5), (2, 14, 1), (3, 12, .5), (3.5, 9, .5)],
          [(0, 7, .25), (.25, 9, .25), (.5, 11, .25), (.75, 12, .25), (1, 16, 1.5), (2.5, 14, .5), (3, 12, 1)],
          [(0, 9, .5), (.5, 12, .5), (1, 16, .5), (1.5, 14, .5), (2, 12, .5), (2.5, 9, .5), (3, 4, 1)],
          [(0, 5, .5), (.5, 9, .5), (1, 12, .5), (1.5, 17, 1), (2.5, 14, .5), (3, 12, .5), (3.5, 9, .5)],
          [(0, 11, .75), (.75, 14, .25), (1, 16, .5), (1.5, 19, 1.5), (3, 16, .5), (3.5, 14, .5)],
          [(0, 11, .25), (.25, 14, .25), (.5, 16, .25), (.75, 19, .25), (1, 23, 2), (3, 19, .5), (3.5, 16, .5)]]
    TB = [[(0, 17, 2), (2, 16, .5), (2.5, 14, .5), (3, 12, 1)],
          [(0, 15, 1.5), (1.5, 14, .5), (2, 10, 1), (3, 7, 1)],
          [(0, 14, 2), (2, 17, 1), (3, 19, 1)],
          [(0, 17, .5), (.5, 21, .5), (1, 24, 2), (3, 21, 1)],
          [(0, 12, 1), (1, 14, .5), (1.5, 17, .5), (2, 21, 2)],
          [(0, 19, 1), (1, 15, 1), (2, 10, 2)],
          [(0, 14, .5), (.5, 17, .5), (1, 22, 1.5), (2.5, 19, .5), (3, 17, 1)],
          [(0, 21, 2.5), (2.5, 19, .5), (3, 16, .5), (3.5, 14, .5)]]
    for bar, name in enumerate(form):
        root, iv = CH[name]
        sec = max((s for s in starts if starts[s] <= bar), key=lambda s: starts[s])
        # the bass: driving eighths with octave jumps
        shape = [0, 0, 12, 0, 7, 0, 12, 7] if sec not in ('B', 'B2') else [0, 12, 0, 12, 0, 12, 7, 12]
        for k, s in enumerate(shape):
            p.play('bass', 'sqbass', root + s, bar, k / 2, .42, .9 if k % 2 == 0 else .75)
        # drums
        if sec == 'break':
            rock_beat(p, bar, half=True, open_hat=False)
        else:
            rock_beat(p, bar, half=sec in ('B', 'B2') and bar % 2 == 0)
        if bar + 1 in starts.values() or bar == p.bars - 1:
            p.fill(bar)
        # guitar: chugging eighths under the tune, big held chords in the middle
        gr = root + 12 if root < 40 else root
        if sec in ('A', 'A2', 'A3', 'tag'):
            for k in range(8):
                p.play('gtr', 'gtr', gr, bar, k / 2, .35 if k % 4 != 3 else .45, .8 if k % 4 == 0 else .6, -.45)
                p.play('gtr2', 'gtr', gr, bar, k / 2 + .01, .35 if k % 4 != 3 else .45, .7, .45)
        elif sec in ('B', 'B2'):
            p.play('gtr', 'gtr', gr, bar, 0, 1.9, .8, -.45)
            p.play('gtr2', 'gtr', gr, bar, 2, 1.9, .75, .45)
        # a bright arpeggio in the break
        if sec == 'break' or sec == 'intro':
            for k in range(16):
                p.play('arp', 'bpluck', root + 36 + [0, 7, 12, 7][k % 4] + iv[(k // 4) % 3] - iv[0], bar, k / 4, .2, .55, .5 * np.sin(k))
        # brass stabs in the middle and the climb
        if sec in ('B', 'B2', 'tag'):
            rhythm = [(0, .4), (.75, .25), (1.5, .4), (2.5, .25), (3, .6)] if sec == 'tag' else [(1.5, .3), (2, .3), (3.5, .4)]
            for b, d in rhythm:
                for i in iv:
                    p.play('brass', 'brass', root + 24 + i, bar, b, d, .8, (i - 4) * .1)
        if sec == 'tag':
            p.play('lead', 'leadslide', A4 + (root - 33) % 12 + (12 if root == 33 else 0), bar, 0, 3.5, .8, 0)
    tune(p, 'lead', 'lead', TA, 4, A4, .75, .05)
    tune(p, 'lead', 'lead', TA, 12, A4, .75, .05, octave=0)
    tune(p, 'lead2', 'lead', [[(b, m + 4 if m % 12 in (0, 7) else m + 3, d) for b, m, d in x] for x in TA], 12, A4, .45, -.3)
    tune(p, 'lead', 'lead', TB, 20, A4, .75, .05)
    tune(p, 'lead', 'lead', TA, 32, A4, .78, .05)
    tune(p, 'lead2', 'lead', TA, 32, A4, .45, -.3, octave=-1)
    tune(p, 'lead', 'lead', TB, 40, A4, .78, .05)
    tune(p, 'lead2', 'lead', TB, 40, A4, .4, -.3, octave=-1)
    p.mix.buses['lead'] = p.mix.buses['lead'] + .35 * pingpong(p.mix.buses['lead'], int(p.spb * .75), fb=.4, n=3, lp=5000)
    levels = dict(bass=-15, drums=-16, hats=-27, cym=-27, gtr=-24, gtr2=-24, arp=-25, brass=-22, lead=-17, lead2=-25)
    sends = dict(drums=.12, lead=.25, lead2=.3, brass=.25, arp=.3, gtr=.08, gtr2=.08)
    return p, master(p, levels, sends, .22, make_ir(length=1.8, rt_low=1.2, rt_mid=1.1, rt_high=.6, seed=21))


# --------------------------------------------------------------------- "Bonus Round"
def bonus_round():
    p = Piece(124, 64, swing=.08, seed=96)
    C4 = 60
    CH = {'C': (48, [0, 4, 7]), 'A7': (45, [0, 4, 7, 10]), 'Dm': (50, [0, 3, 7]), 'G7': (43, [0, 4, 7, 10]), 'F': (41, [0, 4, 7]),
          'D7': (50, [0, 4, 7, 10]), 'G': (43, [0, 4, 7])}
    A = ['C', 'A7', 'Dm', 'G7'] * 2
    B = ['F', 'C', 'D7', 'G7', 'F', 'C', 'G7', 'C']
    form = A[:4] + A + A + B + A + ['C', 'F', 'C', 'G7'] * 2 + B + A + ['F', 'G', 'F', 'G7']
    starts = {'intro': 0, 'A': 4, 'A2': 12, 'B': 20, 'A3': 28, 'stop': 36, 'B2': 44, 'A4': 52, 'tag': 60}
    assert len(form) == p.bars
    TA = [[(0, 7, .5), (.5, 12, .5), (1, 16, .5), (1.5, 12, .5), (2, 19, 1), (3, 16, .5), (3.5, 12, .5)],
          [(0, 13, .5), (.5, 16, .5), (1, 19, .5), (1.5, 22, .5), (2, 21, 1.5), (3.5, 19, .5)],
          [(0, 17, 1), (1, 14, .5), (1.5, 17, .5), (2, 21, 1), (3, 17, 1)],
          [(0, 19, .5), (.5, 17, .5), (1, 14, .5), (1.5, 11, .5), (2, 7, 1), (3, 11, .5), (3.5, 14, .5)],
          [(0, 16, .5), (.5, 19, .5), (1, 24, 1), (2, 23, .5), (2.5, 21, .5), (3, 19, 1)],
          [(0, 22, .5), (.5, 21, .5), (1, 19, .5), (1.5, 16, .5), (2, 13, 1), (3, 16, 1)],
          [(0, 14, .5), (.5, 17, .5), (1, 21, .5), (1.5, 24, .5), (2, 26, 1), (3, 24, .5), (3.5, 21, .5)],
          [(0, 23, 1), (1, 19, .5), (1.5, 17, .5), (2, 19, 1), (3, 17, .5), (3.5, 14, .5)]]
    TB = [[(0, 21, 1.5), (1.5, 19, .5), (2, 17, 1), (3, 21, 1)],
          [(0, 19, 2), (2, 16, 1), (3, 12, 1)],
          [(0, 18, 1), (1, 21, 1), (2, 24, 1.5), (3.5, 21, .5)],
          [(0, 23, 2), (2, 19, 1), (3, 17, 1)],
          [(0, 17, .5), (.5, 21, .5), (1, 24, 1), (2, 21, 1), (3, 17, 1)],
          [(0, 16, .5), (.5, 19, .5), (1, 24, 1), (2, 19, 1), (3, 16, 1)],
          [(0, 14, 1), (1, 17, 1), (2, 19, .5), (2.5, 21, .5), (3, 23, 1)],
          [(0, 24, 2), (2.5, 19, .5), (3, 16, .5), (3.5, 12, .5)]]
    for bar, name in enumerate(form):
        root, iv = CH[name]
        sec = max((s for s in starts if starts[s] <= bar), key=lambda s: starts[s])
        # oom-pah: pizzicato bass on 1 and 3, a marimba chord on 2 and 4
        if sec == 'stop':
            if bar % 2 == 0:
                p.play('bass', 'pizz', root - 12, bar, 0, .5, 1)
                for i in iv:
                    p.play('marimba', 'marimba', root + 12 + i, bar, 0, .3, .9, (i - 5) * .1)
                    p.play('brass', 'brass', root + 12 + i, bar, 0, .3, .9, (i - 5) * .1)
            p.hit('drums', 'wbhi' if bar % 2 else 'wblo', bar, 2, .6, -.3, bar)
            p.hit('drums', 'wbhi', bar, 2.5, .5, .3, bar)
            p.hit('drums', 'clave', bar, 3, .5, 0, bar)
            continue
        p.play('bass', 'pizz', root - 12, bar, 0, .9, .95)
        p.play('bass', 'pizz', root - 12 + 7, bar, 2, .9, .85)
        if bar % 2:
            p.play('bass', 'pizz', root - 12 + 4, bar, 3.5, .4, .6)
        for b in (1, 3):
            for i in iv[1:]:
                p.play('marimba', 'marimba', root + 12 + i, bar, b, .3, .7, (i - 5) * .12)
        if sec in ('A2', 'A3', 'A4', 'B2'):
            for k in range(8):
                p.play('marimba2', 'marimba', root + 24 + [0, 7, 12, 7, 4, 7, 12, 7][k] - (12 if root > 48 else 0), bar, k / 2, .3, .45, .5)
        # the beat
        if sec != 'intro':
            p.hit('drums', 'punch', bar, 0, .8, 0, bar)
            p.hit('drums', 'punch', bar, 2, .7, 0, bar)
            p.hit('drums', 'bigsnare', bar, 1, .55, .05, bar)
            p.hit('drums', 'bigsnare', bar, 3, .6, .05, bar + 1)
            p.hit('drums', 'clap', bar, 3, .4, -.1, bar)
            for k in range(8):
                p.hit('hats', 'shaker' if sec in ('B', 'B2') else 'hat', bar, k / 2, .4 if k % 2 else .28, .35, bar * 8 + k)
        else:
            p.hit('drums', 'wblo', bar, 1, .5, -.3, bar)
            p.hit('drums', 'wbhi', bar, 3, .5, .3, bar)
        if bar % 4 == 3 and sec != 'intro':
            p.hit('drums', 'wbhi', bar, 3.5, .55, -.4, bar)
            p.hit('drums', 'wbhi', bar, 3.75, .45, -.4, bar + 1)
        if bar + 1 in starts.values() or bar == p.bars - 1:
            p.hit('cym', 'crash', bar + 1, 0, .5, .3, bar)
        # brass hits on the B sections and the tag
        if sec in ('B', 'B2', 'tag'):
            for b, d in ((0, .25), (1.5, .4)) if sec != 'tag' else ((0, .3), (.75, .3), (1.5, .3), (2.5, .3), (3, .8)):
                for i in iv[:3]:
                    p.play('brass', 'brass', root + 12 + i, bar, b, d, .85, (i - 4) * .1)
    tune(p, 'lead', 'celesta', TA, 4, C4, .75, .15, octave=1, legato=.8)
    tune(p, 'lead2', 'marimba', TA, 4, C4, .4, -.2, legato=.8)
    tune(p, 'lead', 'clarinet', TA, 12, C4, .75, -.1, legato=.85)
    tune(p, 'lead2', 'celesta', [[n for n in x if n[0] % 1 == 0] for x in TA], 12, C4, .45, .35, octave=1)
    tune(p, 'lead', 'flute', TB, 20, C4, .75, .1, legato=.88)
    tune(p, 'lead', 'celesta', TA, 28, C4, .75, .15, octave=1, legato=.8)
    tune(p, 'lead2', 'flute', TA, 28, C4, .5, -.25, legato=.85)
    tune(p, 'lead', 'clarinet', TB, 44, C4, .75, -.1, legato=.85)
    tune(p, 'lead2', 'marimba', TB, 44, C4, .5, .3, octave=1, legato=.6)
    tune(p, 'lead', 'flute', TA, 52, C4, .75, .1, octave=0, legato=.85)
    tune(p, 'lead2', 'celesta', TA, 52, C4, .55, -.25, octave=1, legato=.8)
    for b, m, d in ((0, 19, .5), (.5, 24, .5), (1, 28, 2)):  # the tag's last word
        p.play('lead', 'celesta', C4 + m, 63, b, d, .7, .1)
    levels = dict(bass=-17, marimba=-21, marimba2=-26, drums=-19, hats=-28, cym=-28, brass=-22, lead=-16, lead2=-23)
    sends = dict(marimba=.2, marimba2=.25, lead=.3, lead2=.3, brass=.2, drums=.1)
    return p, master(p, levels, sends, .25, make_ir(length=2.0, rt_low=1.4, rt_mid=1.3, rt_high=.7, seed=22))


# --------------------------------------------------------------------- "Turbo"
def turbo():
    p = Piece(112, 56, swing=.1, seed=97)
    Bb4 = 70
    CH = {'Bb7': (46, [0, 4, 7, 10]), 'Eb7': (39, [0, 4, 7, 10]), 'G7': (43, [0, 4, 7, 10]), 'Cm7': (48, [0, 3, 7, 10]),
          'F7': (41, [0, 4, 7, 10]), 'Eb': (39, [0, 4, 7, 11]), 'Bb': (46, [0, 4, 7, 11])}
    A = ['Bb7', 'Bb7', 'Eb7', 'Eb7', 'Bb7', 'G7', 'Cm7', 'F7']
    B = ['Eb', 'Eb', 'Bb', 'Bb', 'Cm7', 'F7', 'Bb', 'F7']
    form = ['Bb7'] * 4 + A + A + B + A + ['Bb7', 'Bb7', 'Eb7', 'F7'] + B + A
    starts = {'intro': 0, 'A': 4, 'A2': 12, 'B': 20, 'A3': 28, 'break': 36, 'B2': 40, 'A4': 48}
    assert len(form) == p.bars
    TV = [[(0, 0, .5), (.5, 2, .5), (1, 4, .5), (1.5, 7, 1), (3, 4, .5), (3.5, 2, .5)],
          [(0, 0, 1), (1.5, -2, .5), (2, 0, 1), (3.5, 3, .5)],
          [(0, 5, 1), (1, 7, .5), (1.5, 9, .5), (2, 12, 1), (3, 9, 1)],
          [(0, 9, .5), (.5, 12, .5), (1, 15, 1), (2, 12, .5), (2.5, 9, .5), (3, 7, 1)],
          [(0, 7, .5), (.5, 4, .5), (1, 2, .5), (1.5, 0, 1.5), (3.5, -2, .5)],
          [(0, 13, 1), (1, 16, 1), (2, 9, 2)],
          [(0, 2, 1), (1, 5, .5), (1.5, 9, .5), (2, 12, 1.5), (3.5, 9, .5)],
          [(0, 11, 1), (1, 14, 1), (2, 17, 1), (3, 14, 1)]]
    for bar, name in enumerate(form):
        root, iv = CH[name]
        sec = max((s for s in starts if starts[s] <= bar), key=lambda s: starts[s])
        # the bass: syncopated, octave pops
        pattern = [(0, 0, .4, .95), (.75, 0, .2, .6), (1, 12, .2, .8), (1.5, 0, .4, .8), (2.25, 10, .2, .6), (2.5, 12, .2, .8),
                   (3, 7, .4, .8), (3.5, 9 if iv[1] == 4 else 8, .4, .75)]
        for b, s, d, v in pattern:
            p.play('bass', 'slap', root - 12 + s if root > 44 else root + s, bar, b, d, v)
        # Rhodes stabs on the offbeats
        if sec != 'break':
            for b in (.5, 1.75, 2.5, 3.75) if bar % 2 else (.5, 2.5):
                for i in iv:
                    p.play('keys', 'ep', root + 24 + i - (12 if root > 44 else 0), bar, b, .3, .7, (i - 5) * .08)
        # breakbeat
        kicks = (0, .75, 2.5) if bar % 2 else (0, 1.75, 2.5)
        for b in kicks:
            p.hit('drums', 'punch', bar, b, .85 if b == 0 else .7, 0, bar)
        if sec != 'intro' or bar >= 2:
            for b in (1, 3):
                p.hit('drums', 'bigsnare', bar, b, .75, .05, bar + b)
                p.hit('drums', 'clap', bar, b, .5, -.05, bar + b)
        for k in range(16):
            v = .38 if k % 4 == 2 else .28 if k % 2 == 0 else .16
            p.hit('hats', 'ohat' if k == 6 else 'hat', bar, k / 4, v, .3, bar * 16 + k)
        if sec == 'break':
            p.hit('drums', 'tomhi', bar, 3.5, .6, .3, bar)
            p.hit('drums', 'tomlo', bar, 3.75, .6, -.3, bar)
        if bar + 1 in starts.values() or bar == p.bars - 1:
            p.hit('cym', 'crash', bar + 1, 0, .5, .3, bar)
        # the brass section: answers in the A sections, the tune in the middle
        if sec in ('A2', 'A3', 'A4') and bar % 2 == 1:
            for b, d in ((2.5, .25), (3, .25), (3.5, .4)):
                for i in iv[1:]:
                    p.play('brass', 'brass', root + 24 + i - (12 if root > 44 else 0), bar, b, d, .85, (i - 6) * .08)
        if sec in ('B', 'B2'):
            for b, d in ((0, 1.5), (2.5, .25), (3, .75)):
                for i in iv:
                    p.play('brass', 'brass', root + 24 + i - (12 if root > 44 else 0), bar, b, d, .8, (i - 6) * .08)
    tune(p, 'lead', 'vibes', TV, 12, Bb4, .8, .15, legato=.85)
    tune(p, 'lead', 'lead', [[(b, m + 12, d) for b, m, d in x] for x in TV[:4]], 20, Bb4, .65, .05)
    tune(p, 'lead', 'lead', [[(b, m + 12, d) for b, m, d in x] for x in TV[4:]], 24, Bb4, .65, .05)
    tune(p, 'lead', 'vibes', TV, 28, Bb4, .8, .15, legato=.85)
    tune(p, 'lead2', 'lead', TV, 28, Bb4, .45, -.3, octave=1)
    tune(p, 'lead', 'lead', [[(b, m + 12, d) for b, m, d in x] for x in TV], 40, Bb4, .65, .05)
    tune(p, 'lead', 'vibes', TV, 48, Bb4, .8, .15, legato=.85)
    tune(p, 'lead2', 'flute', TV, 48, Bb4, .5, -.3, octave=1, legato=.85)
    levels = dict(bass=-15, keys=-22, drums=-16, hats=-26, cym=-28, brass=-21, lead=-17, lead2=-24)
    sends = dict(keys=.2, brass=.2, lead=.25, lead2=.3, drums=.1)
    return p, master(p, levels, sends, .2, make_ir(length=1.6, rt_low=1.1, rt_mid=1.0, rt_high=.5, seed=23))


PIECES = (('mz_music', maze_run), ('mz_music_bonus', bonus_round), ('mz_music_turbo', turbo))


def companions(x):
    dark = np.stack([fft_filter(c, 40, 700, 2, circular=True) for c in x])
    dark = dark + conv_reverb(dark * .5, make_ir(length=4.0, rt_low=3.5, rt_mid=3.0, rt_high=1.0), True) * .6
    return finish(dark, -22, -6), x[:, ::-1].copy()


def main():
    only = sys.argv[1:]
    os.makedirs(MASTERS, exist_ok=True)
    path = os.path.join(OUT, 'maze_audio_manifest.json')
    manifest = json.load(open(path))
    keep = {n + s for n, _ in PIECES for s in ('', '_dark', '_flip')}
    entries = {e['id']: e for e in manifest['music'] if e['id'] in keep}
    for name, make in PIECES:
        if only and name not in only:
            continue
        p, x = make()
        x = finish(x, -15, -1.5)
        dark, flip = companions(x)
        for n, y in ((name, x), (name + '_dark', dark), (name + '_flip', flip)):
            write(os.path.join(MASTERS, n + '.wav'), y)
            entries[n] = {'id': n, 'bpm': p.bpm, 'bars': p.bars, 'bar_samples': 4 * p.spb, 'loop_end_sample_exclusive': p.L,
                          'seconds': round(p.L / SR, 3)}
            print(n, round(p.L / SR, 1), 's')
    manifest['music'] = [entries[n + s] for n, _ in PIECES for s in ('', '_dark', '_flip') if n + s in entries]
    json.dump(manifest, open(path, 'w'), indent=1)


if __name__ == '__main__':
    main()
