"""Algorithmic-but-authored composer/arranger for the card-game music family.

Each song is described by a hand-written spec (key, tempo, feel, chord chart,
section layering, lead instruments) in songs.py. This module turns a spec into
note events (seeded, so output is reproducible) and renders a seamless loop.
"""
import numpy as np
from synth import (SR, LoopMix, render_note, render_drum, brush_swish, make_ir, conv_reverb,
                   pingpong, autopan, fft_filter)

NOTE = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
QUAL = {'': [0, 4, 7], 'm': [0, 3, 7], '7': [0, 4, 7, 10], 'maj7': [0, 4, 7, 11], 'm7': [0, 3, 7, 10],
        'm7b5': [0, 3, 6, 10], 'dim': [0, 3, 6], 'dim7': [0, 3, 6, 9], 'sus4': [0, 5, 7], 'sus2': [0, 2, 7],
        '6': [0, 4, 7, 9], 'm6': [0, 3, 7, 9], '9': [0, 4, 7, 10, 14], 'maj9': [0, 4, 7, 11, 14],
        'm9': [0, 3, 7, 10, 14], 'add9': [0, 4, 7, 14], '7sus4': [0, 5, 7, 10], '69': [0, 4, 7, 9, 14],
        'aug': [0, 4, 8], '7b9': [0, 4, 7, 10, 13], '13': [0, 4, 7, 10, 21], 'm11': [0, 3, 7, 10, 17]}
MAJOR = [0, 2, 4, 5, 7, 9, 11]
DORIAN = [0, 2, 3, 5, 7, 9, 10]
AEOLIAN = [0, 2, 3, 5, 7, 8, 10]


def pcname(s):
    v = NOTE[s[0]]
    for c in s[1:]:
        v += 1 if c == '#' else -1
    return v % 12


def parse_chord(s):
    bass = None
    if '/' in s:
        s, b = s.split('/')
        bass = pcname(b)
    i = 1
    while i < len(s) and s[i] in '#b':
        i += 1
    root = pcname(s[:i])
    iv = QUAL[s[i:]]
    return dict(root=root, iv=iv, q=s[i:], bass=root if bass is None else bass,
                pcs=sorted({(root + x) % 12 for x in iv}))


def near(pc, target):
    """Midi note with pitch class pc nearest to target."""
    m = target - ((target - pc) % 12)
    return m if target - m <= 6 else m + 12


class Ctx:
    def __init__(self, song, seed):
        self.s = song
        self.bpb = song['bpb']
        self.rng = np.random.default_rng(seed)
        self.ev = []
        self.chords = []  # (start, dur, chord, sec_index)
        self.secs = []    # (start_beat, nbars, spec)
        b = 0.0
        for si, sec in enumerate(song['sections']):
            self.secs.append((b, len(sec['bars']), sec))
            for bar in sec['bars']:
                parts = bar.split()
                d = self.bpb / len(parts)
                for k, p in enumerate(parts):
                    self.chords.append((b + k * d, d, parse_chord(p), si))
                b += self.bpb
        self.total = b
        self.starts = [c[0] for c in self.chords]

    def chord_at(self, t):
        t %= self.total
        i = int(np.searchsorted(self.starts, t + 1e-6, 'right')) - 1
        return self.chords[i]

    def next_chord(self, t):
        st, d, _, _ = self.chord_at(t)
        return self.chord_at(st + d)[2]

    def scale_at(self, t):
        ch = self.chord_at(t)[2]
        key = [(self.s['key'] + x) % 12 for x in self.s['scale']]
        sc = set(key)
        for c in ch['pcs']:
            if c not in sc:
                for k in list(sc):
                    if min((k - c) % 12, (c - k) % 12) == 1 and k not in ch['pcs']:
                        sc.discard(k)
                sc.add(c)
        return sorted(sc)

    def note(self, t, inst, m, d, v, pan=0.0, bus=None):
        self.ev.append(('n', t, inst, int(m), d, float(np.clip(v, 0.05, 1)), pan, bus or inst))

    def drum(self, t, name, v, pan=0.0, bus='drums'):
        self.ev.append(('d', t, name, 0, 0, float(np.clip(v, 0.02, 1)), pan, bus))

    def swish(self, t, beats, v, pan=0.0):
        self.ev.append(('s', t, 'swish', 0, beats, v, pan, 'drums'))


def scale_step(m, k, sc):
    notes = [x for x in range(m - 30, m + 31) if x % 12 in sc]
    i = int(np.argmin([abs(x - m) for x in notes]))
    return notes[int(np.clip(i + k, 0, len(notes) - 1))]


def voicing(ch, prev, lo, hi, rootless=True, add9=True, maxn=4):
    pcs = [(ch['root'] + x) % 12 for x in ch['iv']]
    if rootless and len(pcs) >= 4:
        pcs = pcs[1:]
    if add9 and ch['q'] in ('maj7', 'm7', '6', 'm6') and len(pcs) < maxn:
        pcs.append((ch['root'] + 2) % 12)
    pcs = sorted(set(pcs))[:maxn] if len(set(pcs)) > maxn else sorted(set(pcs))
    best, bs = None, 1e9
    center = (lo + hi) / 2
    for r in range(len(pcs)):
        rot = pcs[r:] + pcs[:r]
        for base in range(lo - 12, hi):
            v = []
            m = base
            for p in rot:
                m = m + ((p - m) % 12)
                v.append(m)
            if v[0] < lo or v[-1] > hi:
                continue
            sc = abs(np.mean(v) - center) * 0.35
            if prev is not None:
                sc += abs(np.mean(v) - np.mean(prev)) + 0.3 * abs(v[0] - prev[0])
            if sc < bs:
                best, bs = v, sc
    return best


# ------------------------------------------------------------------- melody
STEPW = ([-1, 1, -2, 2, 0, 3, -3, 4, -4], [0.24, 0.24, 0.12, 0.12, 0.08, 0.07, 0.07, 0.03, 0.03])


def contour(rng, n):
    out, run = [], 0
    for _ in range(n):
        s = int(rng.choice(STEPW[0], p=STEPW[1]))
        if run >= 2 and s * np.sign(out[-1]) > 0:
            s = -s
        run = run + 1 if out and np.sign(s) == np.sign(out[-1]) and s != 0 else 0
        out.append(s)
    return out


def realize(ctx, t0, rhythm, cont, target, lo, hi, inst, vel, art, pan, bus, end_on=None, harmony=None):
    p = None
    out = []
    strong_div = 2 if ctx.bpb == 4 else 3
    for i, (on, du) in enumerate(rhythm):
        t = t0 + on
        ch = ctx.chord_at(t)[2]
        sc = ctx.scale_at(t)
        if i == 0:
            cands = [near(pc, target) for pc in ch['pcs']]
            p = min(cands, key=lambda x: abs(x - target))
        else:
            k = cont[(i - 1) % len(cont)]
            q = scale_step(p, k, sc)
            if q < lo or q > hi:
                q = scale_step(p, -k, sc)
            p = q
        strong = (abs(on - round(on)) < 1e-6 and round(on) % strong_div == 0) or du >= 1.5
        if strong and p % 12 not in ch['pcs']:
            p = min([near(pc, p) for pc in ch['pcs']], key=lambda x: (abs(x - p), -x))
        if i == len(rhythm) - 1 and end_on is not None:
            pcs = [(ch['root'] + x) % 12 for x in end_on]
            p = min([near(pc, p) for pc in pcs], key=lambda x: abs(x - p))
        p = int(np.clip(p, lo, hi))
        v = vel * (1.08 if abs(on % strong_div) < 1e-6 else 0.94) + ctx.rng.uniform(-0.04, 0.04)
        ctx.note(t, inst, p, du * art, v, pan, bus)
        if harmony and du >= 1:
            h = scale_step(p, -2, sc)
            if h % 12 in ch['pcs'] or du < 2:
                ctx.note(t, harmony[0], h, du * art, v * harmony[1], -pan - 0.25, harmony[2])
        out.append(p)
    return out[-1]


def melody(ctx, sec_start, nbars, inst, center, rhythms, cadences, vel=0.7, art=0.92, rng_span=(-9, 10),
           pan=0.08, bus='lead', harmony=None, rest_tail=False):
    rng = getattr(ctx, 'mrng', None) or ctx.rng
    unit = 2 * ctx.bpb
    U = nbars // 2
    lo, hi = center + rng_span[0], center + rng_span[1]
    rA = rhythms[rng.integers(len(rhythms))]
    rB = rhythms[rng.integers(len(rhythms))]
    cA = contour(rng, len(rA) + 2)
    cB = contour(rng, len(rB) + 2)
    last = center
    for u in range(U):
        t0 = sec_start + u * unit
        role = 'cad' if u == U - 1 else ('A', 'var', 'shift', 'half')[u % 4]
        if U >= 8 and u % 8 >= 4 and role in ('A', 'shift'):
            role = 'B'
        tgt = int(0.5 * center + 0.5 * last)
        if role == 'A':
            last = realize(ctx, t0, rA, cA, tgt, lo, hi, inst, vel, art, pan, bus, harmony=harmony)
        elif role == 'var':
            c = cA[:-2] + contour(rng, 2)
            last = realize(ctx, t0, rA, c, tgt, lo, hi, inst, vel, art, pan, bus, harmony=harmony)
        elif role == 'shift':
            last = realize(ctx, t0, rA, cA, tgt + 3, lo, hi, inst, vel * 1.04, art, pan, bus, harmony=harmony)
        elif role == 'B':
            last = realize(ctx, t0, rB, cB, tgt + 2, lo, hi, inst, vel * 1.03, art, pan, bus, harmony=harmony)
        else:
            cad = cadences[rng.integers(len(cadences))]
            c = [int(x) for x in rng.choice([-1, -1, -2, 1, 0], len(cad))]
            end = [0, 4, 3] if role == 'cad' else [7, 4, 3, 2]
            last = realize(ctx, t0, cad, c, tgt, lo, hi, inst, vel * 0.96, art, pan, bus, end_on=end, harmony=harmony)


def counter(ctx, s0, nbars, inst, center, vel, pan, bus, delay=0.0, art=0.95):
    prev = center
    t = s0
    while t < s0 + nbars * ctx.bpb - 1e-6:
        st, d, ch, _ = ctx.chord_at(t)
        tones = [(ch['root'] + x) % 12 for x in ch['iv'][1:]]
        tones = [tones[0]] + ([tones[2]] if len(tones) > 2 else [tones[-1]])
        p = min([near(pc, prev) for pc in tones], key=lambda x: abs(x - prev) + 0.2 * abs(x - center))
        dd = min(d, s0 + nbars * ctx.bpb - t)
        ctx.note(t + delay, inst, p, (dd - delay) * art, vel + ctx.rng.uniform(-.04, .04), pan, bus)
        prev = p
        t += dd


# ------------------------------------------------------------------- accompaniment
def bass_line(ctx, s0, nbars, kind, lo=33, vel=0.7, bus='bass'):
    bpb = ctx.bpb
    rng = ctx.rng
    prev = lo + 7
    for bar in range(nbars):
        b0 = s0 + bar * bpb
        st, d, ch, _ = ctx.chord_at(b0)
        root = near(ch['bass'], prev)
        root = root + 12 if root < lo else (root - 12 if root > lo + 14 else root)
        fifth = root + 7 if root + 7 <= lo + 19 else root - 5
        if kind == 'walk':
            for beat in range(bpb):
                t = b0 + beat
                c = ctx.chord_at(t)[2]
                if beat == 0 or abs(ctx.chord_at(t)[0] - t) < 1e-6:
                    p = near(c['bass'], prev)
                    p = p + 12 if p < lo else (p - 12 if p > lo + 16 else p)
                elif beat == bpb - 1 or abs(ctx.chord_at(t + 1)[0] - (t + 1)) < 1e-6:
                    nr = near(ctx.chord_at(t + 1)[2]['bass'], prev)
                    nr = nr + 12 if nr < lo else (nr - 12 if nr > lo + 16 else nr)
                    p = nr + (rng.choice([-1, 1]) if rng.random() < 0.7 else (2 if nr + 2 <= lo + 18 else -2))
                else:
                    cand = [near((c['root'] + x) % 12, prev + rng.choice([-3, 3])) for x in c['iv'][1:4]]
                    p = int(min(cand, key=lambda x: abs(x - prev) if x != prev else 99))
                ctx.note(t, 'bass', p, 0.88, vel * (1.05 if beat % 2 == 0 else 0.92) + rng.uniform(-.04, .04), 0, bus)
                prev = p
        elif kind == 'two':
            ctx.note(b0, ctx.s.get('bass_inst', 'bass'), root, bpb / 2 * 0.6, vel, 0, bus)
            c2 = ctx.chord_at(b0 + bpb / 2)[2]
            p2 = fifth if c2 is ch else near(c2['bass'], root)
            ctx.note(b0 + bpb / 2, ctx.s.get('bass_inst', 'bass'), p2, bpb / 2 * 0.6, vel * 0.9, 0, bus)
            prev = root
        elif kind == 'whole':
            for (cs, cd, c, _) in [x for x in ctx.chords if b0 - 1e-6 <= x[0] < b0 + bpb - 1e-6]:
                p = near(c['bass'], prev)
                p = p + 12 if p < lo else (p - 12 if p > lo + 14 else p)
                ctx.note(cs, 'bass', p, cd * 0.95, vel, 0, bus)
                prev = p
        elif kind == 'bossa':
            c2 = ctx.chord_at(b0 + 2)[2]
            r2 = near(c2['bass'], root) if c2 is not ch else root
            f2 = r2 + 7 if r2 + 7 <= lo + 19 else r2 - 5
            for (t, p, d, v) in ((0, root, 1.4, 1), (1.5, fifth if c2 is ch else r2, .45, .8),
                                 (2, f2 if c2 is ch else r2, 1.4, .95), (3.5, r2, .45, .8)):
                ctx.note(b0 + t, 'bass', p, d, vel * v, 0, bus)
            prev = root
        elif kind == 'pop':
            pat = [(0, root, .45, 1), (1.5, root, .4, .8), (2, fifth, .45, .9), (3, root + 12 if root + 12 <= lo + 22 else root, .3, .75), (3.5, fifth, .4, .8)]
            if bar % 2 == 1:
                pat[-1] = (3.5, near(ctx.next_chord(b0 + bpb - 0.01)['bass'], root) - 1 if rng.random() < .5 else fifth, .4, .8)
            for (t, p, d, v) in pat:
                c = ctx.chord_at(b0 + t)[2]
                if c is not ch and t >= 2:
                    p = near(c['bass'], p)
                ctx.note(b0 + t, 'bass', p, d, vel * v + rng.uniform(-.04, .04), 0, bus)
            prev = root
        elif kind == 'waltz':
            p = root if bar % 2 == 0 else fifth
            ctx.note(b0, 'bass', p, 1.6, vel, 0, bus)
            if rng.random() < 0.25:
                ctx.note(b0 + 2, 'bass', root + (12 if root + 12 < lo + 22 else 0), .8, vel * .7, 0, bus)
            prev = root


def comp(ctx, s0, nbars, kind, inst, lo, hi, vel, pan=0.0, bus='comp', rootless=True):
    bpb = ctx.bpb
    rng = ctx.rng
    prev = None
    for bar in range(nbars):
        b0 = s0 + bar * bpb
        bar_chords = [x for x in ctx.chords if b0 - 1e-6 <= x[0] < b0 + bpb - 1e-6]
        if kind == 'sustain':
            for (cs, cd, c, _) in bar_chords:
                v = voicing(c, prev, lo, hi, rootless)
                for k, m in enumerate(v):
                    ctx.note(cs + k * 0.008, inst, m, cd * 0.97, vel + rng.uniform(-.05, .05), pan, bus)
                prev = v
        elif kind == 'swing':  # EP: hit on 1, occasional anticipation on 4&
            for (cs, cd, c, _) in bar_chords:
                v = voicing(c, prev, lo, hi, rootless)
                d = min(cd, 1.6 if rng.random() < .6 else cd * .9)
                for k, m in enumerate(v):
                    ctx.note(cs + k * 0.006, inst, m, d, vel + rng.uniform(-.05, .05), pan, bus)
                if cd >= 4 and rng.random() < 0.55:
                    for k, m in enumerate(v):
                        ctx.note(cs + 2.5 + k * 0.006, inst, m, 0.45, vel * 0.8, pan, bus)
                prev = v
        elif kind == 'chunk':  # guitar on 2 and 4, short
            for beat in (1, 3):
                c = ctx.chord_at(b0 + beat)[2]
                v = voicing(c, prev, lo, hi, False, False, 4)
                for k, m in enumerate(v):
                    ctx.note(b0 + beat + k * 0.012, inst, m, 0.32, vel * (0.85 + .1 * k / 3) + rng.uniform(-.05, .05), pan, bus)
                prev = v
        elif kind == 'bossa':  # 2-bar guitar pattern in eighths
            pat = [0, 2, 5, 7] if bar % 2 == 0 else [1, 4, 6]
            for e in pat:
                t = b0 + e / 2
                c = ctx.chord_at(t)[2]
                v = voicing(c, prev, lo, hi, True, True, 4)
                for k, m in enumerate(v):
                    ctx.note(t + k * 0.01, inst, m, 0.42, vel * (1 if e in (0, 5) else .85) + rng.uniform(-.04, .04), pan, bus)
                prev = v
        elif kind == 'pahpah':  # waltz beats 2 and 3
            for beat in (1, 2):
                c = ctx.chord_at(b0 + beat)[2]
                v = voicing(c, prev, lo, hi, False, False, 3)
                for k, m in enumerate(v):
                    ctx.note(b0 + beat + k * 0.005, inst, m, 0.5, vel * (1 if beat == 1 else .85) + rng.uniform(-.04, .04), pan, bus)
                prev = v
        elif kind == 'stab':  # pizz on 1 and 2&
            for t in (0, 1.5, 3) if bar % 2 else (0, 1.5):
                c = ctx.chord_at(b0 + t)[2]
                v = voicing(c, prev, lo, hi, False, False, 3)
                for k, m in enumerate(v):
                    ctx.note(b0 + t + k * 0.007, inst, m, 0.25, vel * (1 if t == 0 else .8) + rng.uniform(-.04, .04), pan, bus)
                prev = v


def arp(ctx, s0, nbars, inst, lo, hi, vel, pattern, step=0.5, pan=0.0, bus='arp', dur=None):
    rng = ctx.rng
    prev = None
    n_steps = int(nbars * ctx.bpb / step)
    for i in range(n_steps):
        t = s0 + i * step
        c = ctx.chord_at(t)[2]
        v = voicing(c, prev, lo, hi, False, True, 4)
        prev = v
        tones = [v[0] - 12 + ((c['root'] - v[0]) % 12)] + v
        tones = sorted(set(tones))
        idx = pattern[i % len(pattern)]
        m = tones[idx % len(tones)] + 12 * (idx // len(tones))
        acc = 1.0 if abs((t - s0) % ctx.bpb) < 1e-6 else 0.88
        ctx.note(t, inst, m, dur or step * 1.6, vel * acc + rng.uniform(-.05, .05), pan + rng.uniform(-.1, .1), bus)


def ostinato(ctx, s0, nbars, inst, center, vel, pan, bus, pattern=(0, 2, 1, 2, 3, 2, 1, 2)):
    for i in range(int(nbars * ctx.bpb * 2)):
        t = s0 + i * 0.5
        c = ctx.chord_at(t)[2]
        root = near(c['root'], center - 5)
        tones = [root + x for x in (0, c['iv'][1], c['iv'][2], 12, 12 + c['iv'][1])]
        m = tones[pattern[i % len(pattern)]]
        ctx.note(t, inst, m, 0.3, vel * (1 if i % 2 == 0 else .82) + ctx.rng.uniform(-.05, .05), pan, bus)


def drums(ctx, s0, nbars, kind, e=1.0, fill=True):
    bpb = ctx.bpb
    rng = ctx.rng
    for bar in range(nbars):
        b0 = s0 + bar * bpb
        last = fill and bar == nbars - 1
        if kind == 'brush':
            for beat in range(bpb):
                ctx.swish(b0 + beat, 1.0, 0.22 * e, -0.3 if beat % 2 else 0.3)
            ctx.drum(b0, 'kick', .16 * e)
            ctx.drum(b0 + 2, 'kick', .11 * e)
            if e > 0.6:
                for beat in (1, 3):
                    ctx.drum(b0 + beat, 'tap', .42 * e, 0.15)
                for k in range(bpb * 2):
                    ctx.drum(b0 + k / 2, 'shaker', (.14 if k % 2 else .09) * e, -0.45)
                if rng.random() < 0.4:
                    ctx.drum(b0 + 3.5, 'tap', .17 * e, 0.15)
            if last and e > 0.6:
                for k, t in enumerate((2.5, 3, 3.5)):
                    ctx.drum(b0 + t, 'tap', (.25 + .08 * k) * e, 0.15)
        elif kind == 'pop':
            ctx.drum(b0, 'kick', .34 * e)
            ctx.drum(b0 + 2.5, 'kick', .26 * e)
            ctx.drum(b0 + 1, 'wbhi', .3 * e, 0.35)
            ctx.drum(b0 + 3, 'wblo', .3 * e, 0.35)
            if e > 0.6:
                for k in range(bpb * 4):
                    ctx.drum(b0 + k / 4, 'shaker', (.18, .07, .12, .07)[k % 4] * e, -0.4)
                if bar % 2 == 1:
                    ctx.drum(b0 + 3.5, 'clave', .2 * e, -0.2)
            if last:
                for k, t in enumerate((3, 3 + 1 / 3, 3 + 2 / 3)):
                    ctx.drum(b0 + t, 'wbhi' if k % 2 == 0 else 'wblo', (.22 + .06 * k) * e, 0.35)
        elif kind == 'bossa':
            for t, v in ((0, .2), (1.5, .09), (2, .17), (3.5, .09)):
                ctx.drum(b0 + t, 'kick', v * e)
            for t in ((0, 1.5, 3) if bar % 2 == 0 else (1, 2)):
                ctx.drum(b0 + t, 'rim', .28 * e, 0.25)
            if e > 0.6:
                for k in range(bpb * 4):
                    ctx.drum(b0 + k / 4, 'shaker', (.14, .05, .09, .05)[k % 4] * e, -0.4)
        elif kind == 'waltz':
            ctx.drum(b0, 'kick', .17 * e)
            ctx.swish(b0, 1.0, .18 * e, 0.25)
            ctx.drum(b0 + 1, 'tap', .2 * e, 0.15)
            ctx.drum(b0 + 2, 'tap', .16 * e, 0.15)
            if bar % 8 == 0:
                ctx.drum(b0, 'triangle', .12 * e, -0.5)
        elif kind == 'menu':
            for k in range(bpb * 2):
                ctx.drum(b0 + k / 2, 'shaker', (.08 if k % 2 else .04) * e, -0.4)
            if bar % 4 == 0:
                ctx.drum(b0, 'triangle', .09 * e, 0.45)


# ------------------------------------------------------------------- rendering
def swing_pos(t, swing):
    beat = np.floor(t)
    p = t - beat
    sp = 0.5 + swing / 6.0
    q = p * 2 * sp if p < 0.5 else sp + (p - 0.5) * 2 * (1 - sp)
    return beat + q


def render(song, ctx, circular=True, extra_tail=0.0):
    spb = int(round(SR * 60 / song['bpm']))
    L = int(round(ctx.total * spb)) if circular else int(round(ctx.total * spb + extra_tail * SR))
    mix = LoopMix(L, circular)
    rng = np.random.default_rng(song['seed'] + 99)
    sec_beat = spb / SR
    rr = {}
    for (kind, t, name, m, d, v, pan, bus) in sorted(ctx.ev, key=lambda x: x[1]):
        jit = rng.normal(0, 0.0025 if kind == 'n' else 0.0015)
        tt = swing_pos(t, song['swing'])
        s = int(round(tt * spb + jit * SR))
        if kind == 'n':
            y = render_note(name, m, d * sec_beat, v)
        elif kind == 's':
            y = brush_swish(d * sec_beat, v, seed=int(t * 7) % 5)
        else:
            rr[name] = rr.get(name, 0) + 1
            y = render_drum(name, v, rr[name])
        mix.add(bus, y, s, pan, song['bus'].get(bus, {}).get('gain', 1.0))
    return mix, spb


def active_rms_db(x):
    mono = x.mean(0)
    w = SR
    rms = np.array([np.sqrt(np.mean(mono[i:i + w] ** 2)) for i in range(0, len(mono) - w, w)])
    rms = rms[rms > 1e-6]
    if len(rms) == 0:
        return -120
    top = np.sort(rms)[len(rms) // 2:]
    return 20 * np.log10(np.sqrt(np.mean(top ** 2)))


def presence(x, db, f0=2500.0):
    """Smooth high shelf (+db above ~f0), circular so loops stay seamless."""
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(len(x), 1 / SR)
    g = 10 ** (db / 20 * (1 / (1 + (f0 / np.maximum(f, 1)) ** 2)))
    return np.fft.irfft(X * g, len(x))


def master(song, mix, spb, circular=True):
    L = mix.L
    dry = np.zeros((2, L))
    send = np.zeros((2, L))
    stats = {}
    for name, b in mix.buses.items():
        cfg = song['bus'].get(name, {})
        if 'level' in cfg:
            cur = active_rms_db(b)
            b = b * 10 ** ((cfg['level'] - cur) / 20)
        if cfg.get('autopan'):
            b = autopan(b, cfg['autopan'], 0.3)
        if cfg.get('delay'):
            beats, fb = cfg['delay']
            b = b + pingpong(b, int(round(beats * spb)), fb) * cfg.get('delay_mix', 0.5) if circular else b
        if cfg.get('lp'):
            b = np.stack([fft_filter(c, None, cfg['lp'], 1, circular=True) for c in b])
        stats[name] = round(active_rms_db(b), 1)
        dry += b
        send += b * cfg.get('rev', 0.15)
    ir = make_ir(**song.get('ir', {}))
    wet = conv_reverb(send, ir, circular)
    if not circular:
        dry = np.pad(dry, ((0, 0), (0, wet.shape[1] - L)))
    out = dry + wet * song.get('wet', 0.5)
    out = np.stack([fft_filter(c, 28, None, 2, circular=circular) for c in out])
    out = np.stack([presence(c, song.get('presence_db', 3.0)) for c in out])
    return out, stats
