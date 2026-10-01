"""Batch 2 scores: nine game themes (+ Sudoku night arrangement).

Each theme has its own chord chart, rhythm vocabulary, orchestration and an
arrangement function written for it, so the themes differ in composition and
not just in tempo or key. Render with: python3 render_music.py --batch2
"""
import numpy as np
from compose import (Ctx, MAJOR, DORIAN, AEOLIAN, melody, counter, bass_line, comp, arp, ostinato, near,
                     voicing, scale_step)
from songs import S, POP_R, POP_C, BOSSA_R, BOSSA_C, SWING_R, SWING_C, MENU_R, MENU_C

MIXO = [0, 2, 4, 5, 7, 9, 10]
PENTA = [0, 2, 4, 7, 9]
LYDIAN = [0, 2, 4, 6, 7, 9, 11]

GEM_R = [[(0, .5), (.5, .5), (1, 1), (2, .5), (2.5, .5), (3, 1), (4, .5), (4.5, 1), (5.5, .5), (6, 1.5)],
         [(0, 1.5), (1.5, .5), (2, .5), (2.5, 1), (3.5, .5), (4, 1), (5, .5), (5.5, .5), (6, 2)],
         [(0, .75), (.75, .75), (1.5, 1), (2.5, .5), (3, 1), (4, .75), (4.75, .75), (5.5, 2.5)]]
GEM_C = [[(0, .5), (.5, .5), (1, .5), (1.5, .5), (2, 2), (4, 3.5)], [(0, 1), (1, 1), (2, 1), (3, 1), (4, 4)]]
CALM_R = [[(0, 2), (2, 1), (3, 1), (4, 3)], [(0, 1), (1, 1), (2, 2), (4, 1.5), (5.5, .5), (6, 2)],
          [(1, 1), (2, 1), (3, 2), (5, 3)]]
CALM_C = [[(0, 2), (2, 2), (4, 4)], [(0, 1), (1, 1), (2, 2), (4, 4)]]
NATURE_R = [[(0, 2), (2, 1), (3, 3)], [(1, 1), (2, 1), (3, 1.5), (4.5, 1.5)], [(0, 3), (4, 2)]]
NATURE_C = [[(0, 1), (1, 2), (3, 3)], [(0, 6)]]
ANIME_R = [[(0, .5), (.5, .25), (.75, .25), (1, .5), (1.5, .5), (2, 1), (3, .5), (3.5, .5), (4, .5), (4.5, .5), (5, .5), (5.5, 1), (6.5, 1)],
           [(0, .25), (.25, .25), (.5, .5), (1, .5), (1.5, 1), (2.5, .5), (3, 1), (4, .5), (4.5, .5), (5, 1), (6, .5), (6.5, .5), (7, 1)],
           [(0, 1), (1, .5), (1.5, .5), (2, .5), (2.5, .5), (3, .5), (3.5, .5), (4, 1.5), (5.5, .5), (6, 1.5)]]
ANIME_C = [[(0, .5), (.5, .5), (1, .5), (1.5, .5), (2, .5), (2.5, .5), (3, 1), (4, 3.5)], [(0, 1), (1, 1), (2, 2), (4, 4)]]
CAVE_R = [[(0, 1), (1, 1), (2, 2), (4, 1), (5, 1), (6, 2)], [(0, .5), (.5, .5), (1, 1), (2, 2), (4, .5), (4.5, .5), (5, 1), (6, 2)],
          [(0, 1.5), (1.5, .5), (2, 2), (4, 1.5), (5.5, .5), (6, 2)]]
CAVE_C = [[(0, 1), (1, 1), (2, 1), (3, 1), (4, 4)], [(0, 2), (2, 2), (4, 4)]]
TOY_R = [[(0, 1), (1, .5), (1.5, .5), (2, 1), (3, 1), (4, .5), (4.5, .5), (5, .5), (5.5, .5), (6, 1.5)],
         [(0, .5), (.5, .5), (1, .5), (1.5, .5), (2, 2), (4, .5), (4.5, .5), (5, 1), (6, 2)],
         [(.5, .5), (1, 1), (2, .5), (2.5, .5), (3, 1), (4, 1), (5, 1), (6, 1.5)]]
TOY_C = [[(0, .5), (.5, .5), (1, 1), (2, 2), (4, 3.5)], [(0, 1), (1, 1), (2, 1), (3, 1), (4, 3)]]


# ------------------------------------------------------------------ shared helpers
def pdrums(ctx, s0, nb, bars, e=1.0, fill=None):
    """Pattern drums: `bars` is a list of per-bar patterns [(name, beat, vel, pan)], cycled."""
    for b in range(nb):
        pat = fill if (fill and b == nb - 1) else bars[b % len(bars)]
        for (name, t, v, p) in pat:
            ctx.drum(s0 + b * ctx.bpb + t, name, v * e + ctx.rng.uniform(-.02, .02), p)


def bass_oct(ctx, s0, nb, inst, lo, vel, pat=((0, 0, .4), (.75, 12, .3), (1.5, 0, .4), (2.5, 12, .3), (3, 7, .4), (3.5, 12, .3))):
    for b in range(nb):
        b0 = s0 + b * ctx.bpb
        for (t, iv, d) in pat:
            c = ctx.chord_at(b0 + t)[2]
            r = near(c['bass'], lo + 6)
            ctx.note(b0 + t, inst, r + iv, d, vel * (1 if iv == 0 else .8) + ctx.rng.uniform(-.04, .04), 0, 'bass')


def scatter(ctx, s0, nb, inst, lo, hi, rate, vel, bus='scatter', scale=None):
    """Sparse, randomly-timed chord/scale tones (water drops, sparkles, pings)."""
    n = ctx.rng.poisson(rate * nb)
    for _ in range(n):
        t = s0 + ctx.rng.uniform(0, nb * ctx.bpb)
        t = round(t * 4) / 4
        c = ctx.chord_at(t)[2]
        pcs = c['pcs'] if scale is None else [(ctx.s['key'] + x) % 12 for x in scale]
        m = near(int(ctx.rng.choice(pcs)), int(ctx.rng.integers(lo, hi)))
        ctx.note(t, inst, m, .5, vel * ctx.rng.uniform(.6, 1), ctx.rng.uniform(-.6, .6), bus)


def interlock(ctx, s0, nb, inst, center, length, degrees, vel, pan, bus, step=0.5, dur=0.4):
    """Cycle a `length`-step motif of scale degrees over a 1/8 grid; odd lengths drift against the bar."""
    n = int(nb * ctx.bpb / step)
    for i in range(n):
        t = s0 + i * step
        sc = ctx.scale_at(t)
        c = ctx.chord_at(t)[2]
        base = near(c['root'], center)
        m = scale_step(base, degrees[i % length], sc)
        acc = 1.0 if i % length == 0 else .8
        ctx.note(t, inst, m, dur, vel * acc + ctx.rng.uniform(-.04, .04), pan, bus)


def shift_bus(ctx, bus, semis, start=0):
    ctx.ev[start:] = [(k, t, n, m + semis if b == bus else m, d, v, p, b) for (k, t, n, m, d, v, p, b) in ctx.ev[start:]]


def new_ctx(song):
    ctx = Ctx(song, song['seed'])
    ctx.mrng = np.random.default_rng(song.get('mel_seed', song['seed'] + 1))
    return ctx


# ------------------------------------------------------------------ 1 Gems
GA = ['Emaj9', 'C#m7', 'Amaj7', 'B7sus4 B7', 'Emaj9', 'G#m7', 'Amaj7', 'F#m7 B7']
GB = ['Amaj7', 'B', 'G#m7', 'C#m7', 'F#m7', 'B7', 'E', 'E7']
GC = ['Cmaj7', 'D', 'Emaj7', 'Emaj7', 'Cmaj7', 'D', 'F#7sus4', 'B7']
GEMS = dict(id='gems', title='Prism Parade (Gems)', bpm=118, bpb=4, swing=0.0, key=4, scale=MAJOR, seed=1101,
            sections=[S('intro', GA[:4], 'arp bass'), S('A1', GA, 'arp bass drums lead', 'marimba', 76, .85),
                      S('A2', GA, 'arp bass drums lead counter', 'celesta', 81),
                      S('B', GB, 'arp bass drums lead pad', 'vibes', 81),
                      S('C', GC, 'glass pad lead scatter', 'celesta', 81, .7),
                      S('B2', GB, 'arp bass drums lead counter pad', 'marimba', 81),
                      S('A3', GA, 'arp bass drums lead', 'celesta', 81)],
            bus=dict(lead=dict(level=-17, rev=.25, delay=(.75, .25), delay_mix=.3), counter=dict(level=-25, rev=.3),
                     arp=dict(level=-23, rev=.3, autopan=13, lp=7000), bass=dict(level=-19, rev=.03),
                     drums=dict(level=-25, rev=.08), pad=dict(level=-27, rev=.45), scatter=dict(level=-26, rev=.5),
                     glass=dict(level=-24, rev=.5)),
            wet=.55, presence_db=1.5, ir=dict(rt_low=1.8, rt_mid=1.5, rt_high=.7))


def arr_gems(song):
    ctx = new_ctx(song)
    four = [[('kick', 0, .3, 0), ('kick', 2, .25, 0), ('clap', 1, .22, .1), ('clap', 3, .22, .1)] +
            [('shaker', k / 4, (.12, .05, .08, .05)[k % 4], -.4) for k in range(16)] +
            [('tick', k + .5, .12, .45) for k in range(4)]]
    for (s0, nb, sec) in ctx.secs:
        L, e = sec['layers'], sec['e']
        if 'lead' in L:
            melody(ctx, s0, nb, sec['lead'], sec['center'], GEM_R, GEM_C, vel=.72 * e ** .5, art=1.0)
        if 'counter' in L:
            counter(ctx, s0, nb, 'vibes', sec['center'] - 9, .45, -.4, 'counter', delay=.5)
        if 'arp' in L:
            arp(ctx, s0, nb, 'celesta', 64, 84, .38 * e, [0, 2, 4, 3, 1, 3, 4, 2], 0.25, 0, 'arp', dur=.3)
        if 'bass' in L:
            bass_oct(ctx, s0, nb, 'synthbass', 33, .7 * e)
        if 'drums' in L:
            pdrums(ctx, s0, nb, four, e)
        if 'pad' in L:
            comp(ctx, s0, nb, 'sustain', 'pad', 56, 74, .5 * e, 0, 'pad')
        if 'glass' in L:
            comp(ctx, s0, nb, 'sustain', 'glass', 60, 79, .45 * e, 0, 'glass')
        if 'scatter' in L:
            scatter(ctx, s0, nb, 'celesta', 84, 96, 3, .35)
    return ctx


# ------------------------------------------------------------------ 2 Sudoku (day + night share the theme)
DA = ['Fmaj7', 'Am7', 'Bbmaj7', 'Csus4 C', 'Dm9', 'Am7', 'Bbmaj7', 'Gm7 C7sus4']
DB = ['Bbmaj7', 'C/Bb', 'Am7', 'Dm7', 'Gm7', 'Am7', 'Bbmaj7', 'Csus4']
_sdk_secs = lambda day: [S('A1', DA, 'pianoarp pad lead', 'felt', 72, .8),
                         S('A2', DA, 'pianoarp pad lead counter bass', 'felt', 74, .9),
                         S('B', DB, 'pianoarp pad lead counter bass', 'felt', 76),
                         S('A3', DA, 'pianoarp pad lead bass', 'felt', 72, .85)]
SUDOKU_DAY = dict(id='sudoku_day', title='Quiet Grid — Day (Sudoku)', bpm=76, bpb=4, swing=0.0, key=5, scale=LYDIAN,
                  seed=1201, mel_seed=1299, sections=_sdk_secs(True), variant='day',
                  bus=dict(lead=dict(level=-17, rev=.3), counter=dict(level=-25, rev=.4), arp=dict(level=-24, rev=.35),
                           pad=dict(level=-25, rev=.5), bass=dict(level=-23, rev=.05)),
                  wet=.6, presence_db=0.5, ir=dict(rt_low=2.2, rt_mid=1.8, rt_high=.8))
SUDOKU_NIGHT = dict(SUDOKU_DAY, id='sudoku_night', title='Quiet Grid — Night (Sudoku)', bpm=68, variant='night',
                    sections=_sdk_secs(False),
                    bus=dict(lead=dict(level=-18, rev=.35, lp=2500), counter=dict(level=-26, rev=.5),
                             arp=dict(level=-26, rev=.4, lp=2200), pad=dict(level=-24, rev=.55, lp=1800),
                             bass=dict(level=-23, rev=.05)),
                    wet=.7, presence_db=-1.5, ir=dict(rt_low=2.8, rt_mid=2.2, rt_high=.9))


def arr_sudoku(song):
    ctx = new_ctx(song)
    night = song['variant'] == 'night'
    for (s0, nb, sec) in ctx.secs:
        L, e = sec['layers'], sec['e']
        n0 = len(ctx.ev)
        melody(ctx, s0, nb, 'felt', sec['center'], CALM_R, CALM_C, vel=(.5 if night else .6) * e, art=1.0)
        if night:
            shift_bus(ctx, 'lead', -12, n0)
        if 'counter' in L:
            counter(ctx, s0, nb, 'glass' if night else 'vibes', sec['center'] - (14 if night else 7),
                    .35 if night else .4, -.35, 'counter', delay=1.0)
        if night:
            arp(ctx, s0, nb, 'felt', 45, 64, .3 * e, [0, 3, 1, 4, 2, 3], 1.0, .2, 'arp', dur=2.0)
        else:
            arp(ctx, s0, nb, 'kalimba', 62, 81, .3 * e, [0, 2, 1, 3, 2, 4, 3, 1], 0.5, .25, 'arp', dur=.6)
        comp(ctx, s0, nb, 'sustain', 'glass' if night else 'pad', 52 if night else 57, 69 if night else 76,
             .4 * e, 0, 'pad')
        if 'bass' in L:
            bass_line(ctx, s0, nb, 'whole', lo=29 if night else 33, vel=.45 * e)
    return ctx


# ------------------------------------------------------------------ 3 Nature cube
NA = ['Dadd9', 'G/D', 'Dadd9', 'Asus4', 'Bm7', 'Gmaj7', 'Em7', 'Asus4']
NB = ['Gmaj7', 'Gmaj7', 'Dadd9', 'Dadd9', 'Em7', 'Em7', 'Asus4', 'A']
NATURE = dict(id='nature_cube', title='Mirror Meadow (Nature Cube)', bpm=66, bpb=3, swing=0.0, key=2, scale=PENTA,
              seed=1301, sections=[S('A1', NA, 'harp glass drops', None, 76, .8),
                                   S('A2', NA, 'harp glass drops lead perc', 'flute', 76, .85),
                                   S('B', NB, 'harp glass drops lead perc counter', 'flute', 79),
                                   S('C', NB, 'glass drops rain', None, 76, .7),
                                   S('A3', NA, 'harp glass drops lead perc', 'flute', 79, .9)],
              bus=dict(lead=dict(level=-17, rev=.35, delay=(1.5, .25), delay_mix=.3), counter=dict(level=-25, rev=.4),
                       harp=dict(level=-22, rev=.35), glass=dict(level=-25, rev=.55), scatter=dict(level=-24, rev=.55),
                       drums=dict(level=-29, rev=.2), bass=dict(level=-24, rev=.05)),
              wet=.7, presence_db=1.0, ir=dict(rt_low=3.0, rt_mid=2.4, rt_high=1.0))


def arr_nature(song):
    ctx = new_ctx(song)
    for (s0, nb, sec) in ctx.secs:
        L, e = sec['layers'], sec['e']
        if 'lead' in L:
            melody(ctx, s0, nb, 'flute', sec['center'], NATURE_R, NATURE_C, vel=.55 * e, art=.95)
        if 'counter' in L:
            counter(ctx, s0, nb, 'pluck', sec['center'] - 12, .4, -.3, 'counter')
        if 'harp' in L:
            arp(ctx, s0, nb, 'harp', 50, 74, .38 * e, [0, 2, 4, 3, 1, 2], 1.0, -.2, 'harp')
        if 'glass' in L:
            comp(ctx, s0, nb, 'sustain', 'glass', 62, 78, .35 * e, 0, 'glass')
        if 'drops' in L:
            scatter(ctx, s0, nb, 'kalimba', 79, 96, 1.6, .35, scale=PENTA)
        if 'rain' in L:
            for b in range(0, nb, 4):
                ctx.drum(s0 + b * 3, 'rainstick', .25, -.3)
        if 'perc' in L:
            pdrums(ctx, s0, nb, [[('shaker', 0, .1, -.3), ('shaker', 1, .05, -.3), ('shaker', 2, .06, -.3)],
                                 [('shaker', 0, .1, -.3), ('wblo', 1.5, .08, .4), ('shaker', 2, .06, -.3)]], e)
        bass_line(ctx, s0, nb, 'whole', lo=38, vel=.35 * e)
    return ctx


# ------------------------------------------------------------------ 4 Untangle
UK = ['Dm9', 'Dm9', 'Bbmaj7', 'C', 'Dm9', 'Gm7', 'Bbmaj7', 'Asus4']
UR = ['Dmaj7', 'Gmaj7', 'Dmaj7', 'Asus4', 'Bm7', 'Gmaj7', 'Dmaj7', 'Asus4 A']
UNTANGLE = dict(id='untangle', title='Slow Unknotting (Untangle)', bpm=90, bpb=4, swing=0.0, key=2, scale=DORIAN,
                seed=1401, sections=[S('intro', ['Dm9', 'Dm9', 'Bbmaj7', 'C'], 'k5'),
                                     S('K1', UK, 'k5 k7 bass'), S('K2', UK, 'k5 k7 k3 bass lead', 'vibes', 74, .9),
                                     S('R1', UR, 'r8 bass pad', None, 74), S('R2', UR, 'r8 bass pad lead', 'glass', 79),
                                     S('R3', UR, 'r8 k3 bass lead', 'vibes', 76, .85)],
                bus=dict(m5=dict(level=-21, rev=.25, autopan=11), p7=dict(level=-22, rev=.25),
                         k3=dict(level=-25, rev=.3), lead=dict(level=-18, rev=.35, delay=(.75, .25), delay_mix=.3),
                         bass=dict(level=-23, rev=.05), pad=dict(level=-27, rev=.5)),
                wet=.6, presence_db=1.0, ir=dict(rt_low=2.4, rt_mid=2.0, rt_high=.8))


def arr_untangle(song):
    ctx = new_ctx(song)
    for (s0, nb, sec) in ctx.secs:
        L, e = sec['layers'], sec['e']
        if 'k5' in L:
            interlock(ctx, s0, nb, 'marimba', 64, 5, [0, 2, 4, 1, 3], .5, .35, 'm5')
        if 'k7' in L:
            interlock(ctx, s0, nb, 'pizz', 57, 7, [4, 2, 0, 5, 3, 1, 2], .45, -.4, 'p7')
        if 'k3' in L:
            interlock(ctx, s0, nb, 'kalimba', 76, 3, [0, 4, 2], .3, .1, 'k3', step=1.0)
        if 'r8' in L:  # the knots resolve: both voices share one 8-step, bar-aligned pattern
            interlock(ctx, s0, nb, 'marimba', 64, 8, [0, 2, 4, 2, 5, 4, 2, 1], .5, .35, 'm5')
            interlock(ctx, s0, nb, 'pizz', 57, 8, [0, 4, 2, 4, 0, 4, 2, 4], .42, -.4, 'p7')
        if 'lead' in L:
            melody(ctx, s0, nb, sec['lead'], sec['center'], CALM_R, CALM_C, vel=.55 * e, art=1.0)
        if 'bass' in L:
            bass_line(ctx, s0, nb, 'whole', lo=33, vel=.5 * e)
        if 'pad' in L:
            comp(ctx, s0, nb, 'sustain', 'pad', 57, 74, .4 * e, 0, 'pad')
    return ctx


# ------------------------------------------------------------------ 5 Atom Probe
AA = ['Am9', 'Fmaj7', 'Am9', 'E7sus4', 'Am9', 'Fmaj7', 'Dm9', 'E7sus4 E7']
AB = ['Fmaj7', 'Ebmaj7', 'Fmaj7', 'Gsus4 G', 'Cmaj7', 'Bbmaj7', 'Dm9', 'E7sus4']
ATOM = dict(id='atom_probe', title='Beam & Bounce (Atom Probe)', bpm=92, bpb=4, swing=0.15, key=9, scale=AEOLIAN,
            seed=1501, sections=[S('intro', AA[:4], 'sbass pings'), S('A1', AA, 'sbass pings drums lead', 'vibes', 72, .85),
                                 S('A2', AA, 'sbass pings drums lead pad', 'vibes', 76),
                                 S('B', AB, 'sbass drums lead pad counter', 'analog', 76),
                                 S('C', AB, 'pings pad lead', 'glass', 79, .7),
                                 S('A3', AA, 'sbass pings drums lead pad', 'vibes', 76)],
            bus=dict(lead=dict(level=-17, rev=.25, delay=(.75, .3), delay_mix=.3), counter=dict(level=-25, rev=.3),
                     bass=dict(level=-19, rev=.04), scatter=dict(level=-24, rev=.35, delay=(.5, .45), delay_mix=.6),
                     drums=dict(level=-26, rev=.1), pad=dict(level=-25, rev=.4)),
            wet=.5, presence_db=1.0, ir=dict(rt_low=2.0, rt_mid=1.6, rt_high=.7))


def arr_atom(song):
    ctx = new_ctx(song)
    groove = [[('kick', 0, .28, 0), ('kick', 2.5, .2, 0), ('rim', 1, .2, .2), ('rim', 3, .2, .2)] +
              [('tick', k / 4, (.14, .05, .09, .05)[k % 4], .45) for k in range(16)]]
    for (s0, nb, sec) in ctx.secs:
        L, e = sec['layers'], sec['e']
        if 'lead' in L:
            melody(ctx, s0, nb, sec['lead'], sec['center'], BOSSA_R, BOSSA_C, vel=.65 * e ** .5,
                   art=.9 if sec['lead'] == 'analog' else 1.0)
        if 'counter' in L:
            counter(ctx, s0, nb, 'vibes', sec['center'] - 10, .4, -.4, 'counter', delay=.5)
        if 'sbass' in L:
            bass_oct(ctx, s0, nb, 'synthbass', 33, .62 * e,
                     pat=tuple((k / 2, (0, 12, 7, 12, 0, 12, 10, 7)[k], .35) for k in range(8)))
        if 'pings' in L:
            scatter(ctx, s0, nb, 'ping', 79, 96, 2.2, .4)
        if 'drums' in L:
            pdrums(ctx, s0, nb, groove, e)
        if 'pad' in L:
            comp(ctx, s0, nb, 'sustain', 'analog', 55, 72, .35 * e, 0, 'pad')
    return ctx


# ------------------------------------------------------------------ 6 Four Pegs
FPA = ['Cm7', 'F7', 'Cm7', 'F7', 'Ebmaj7', 'Dm7b5 G7', 'Cm7', 'Ab7 G7']
FPB = ['Abmaj7', 'Gm7', 'Fm7', 'Bb7', 'Ebmaj7', 'Abmaj7', 'Dm7b5', 'G7']
FOURPEGS = dict(id='four_pegs', title='Clever Pegs (Four Pegs)', bpm=98, bpb=4, swing=0.55, key=0, scale=DORIAN,
                seed=1601, sections=[S('intro', FPA[:4], 'bass clock eps'),
                                     S('A1', FPA, 'bass clock eps drums lead', 'ep', 74, .85),
                                     S('A2', FPA, 'bass clock eps drums lead', 'ep', 76),
                                     S('B', FPB, 'bass clock eps drums lead counter', 'clarinet', 72),
                                     S('C', FPA, 'bass clock lead', 'vibes', 76, .7),
                                     S('A3', FPA, 'bass clock eps drums lead', 'ep', 76)],
                bus=dict(lead=dict(level=-17, rev=.2), counter=dict(level=-25, rev=.25), comp=dict(level=-23, rev=.15),
                         bass=dict(level=-19, rev=.03), drums=dict(level=-25, rev=.08), clock=dict(level=-29, rev=.1)),
                wet=.45, presence_db=1.5, ir=dict(rt_low=1.4, rt_mid=1.2, rt_high=.6))


def arr_fourpegs(song):
    ctx = new_ctx(song)
    brush = [[('kick', 0, .14, 0), ('tap', 1, .4, .15), ('tap', 3, .4, .15), ('tap', 2.5, .12, .15)] +
             [('shaker', k / 2, (.09, .14)[k % 2], -.4) for k in range(8)]]
    for (s0, nb, sec) in ctx.secs:
        L, e = sec['layers'], sec['e']
        if 'lead' in L:
            melody(ctx, s0, nb, sec['lead'], sec['center'], SWING_R, SWING_C, vel=.7 * e ** .5,
                   art=.55 if sec['lead'] == 'ep' else .9)
        if 'counter' in L:
            counter(ctx, s0, nb, 'vibes', sec['center'] - 8, .4, -.4, 'counter', delay=.5)
        bass_line(ctx, s0, nb, 'pop', lo=31, vel=.65 * e)
        if 'clock' in L:
            for b in range(nb):
                for k in range(4):
                    ctx.drum(s0 + b * 4 + k + .5, 'wbhi' if k % 2 else 'wblo', .18 * e, .45, 'clock')
        if 'eps' in L:
            comp(ctx, s0, nb, 'stab', 'ep', 55, 70, .45 * e, .3, 'comp')
        if 'drums' in L:
            pdrums(ctx, s0, nb, brush, e)
            for b in range(nb):
                for t in range(4):
                    ctx.swish(s0 + b * 4 + t, 1.0, .14 * e, -.3 if t % 2 else .3)
    return ctx


# ------------------------------------------------------------------ 7 Switchbox (cute anime comedy)
WV = ['A', 'E/G#', 'F#m7', 'C#m7', 'D', 'A/C#', 'Bm7', 'E']
WC = ['Dmaj7', 'E', 'C#m7', 'F#m7', 'Dmaj7', 'E', 'A', 'A7']
WB = ['Bm7', 'C#m7', 'Dmaj7', 'E', 'Bm7', 'C#m7', 'D', 'E7sus4 E7']
SWITCHBOX = dict(id='switchbox', title='Peekaboo Switches (Switchbox)', bpm=128, bpb=4, swing=0.0, key=9, scale=MAJOR,
                 seed=1701, sections=[S('intro', ['Dmaj7', 'E', 'C#m7', 'F#m7'], 'mbox'),
                                      S('A1', WV, 'mbox pizz sbass drums lead', 'celesta', 81, .85),
                                      S('A2', WV, 'mbox pizz sbass drums lead counter', 'celesta', 81),
                                      S('CH', WC, 'mbox pizz sbass drums lead pad', 'analog', 81),
                                      S('BR', WB, 'mbox pizz half lead', 'musicbox', 84, .75),
                                      S('CH2', WC, 'mbox pizz sbass drums lead pad counter', 'analog', 81),
                                      S('A3', WV, 'mbox pizz sbass drums lead', 'celesta', 81)],
                 bus=dict(lead=dict(level=-17, rev=.2, delay=(.75, .25), delay_mix=.3), counter=dict(level=-25, rev=.25),
                          arp=dict(level=-23, rev=.3, lp=6500), pizz=dict(level=-23, rev=.15), bass=dict(level=-19, rev=.03),
                          drums=dict(level=-24, rev=.08), pad=dict(level=-27, rev=.35)),
                 wet=.45, presence_db=1.5, ir=dict(rt_low=1.6, rt_mid=1.3, rt_high=.6))


def arr_switchbox(song):
    ctx = new_ctx(song)
    bounce = [[('kick', 0, .38, 0), ('kick', 2.5, .3, 0), ('clap', 1, .3, .1), ('clap', 3, .3, .1)] +
              [('hat', k / 2, (.14, .1)[k % 2], .4) for k in range(8)] + [('shaker', k / 4 + .25, .06, -.4) for k in range(0, 16, 2)]]
    fill = [b for b in bounce[0] if b[1] < 3] + [('wbhi', 3, .25, .3), ('wblo', 3.25, .25, -.3), ('wbhi', 3.5, .3, .3), ('clap', 3.75, .3, 0)]
    for (s0, nb, sec) in ctx.secs:
        L, e = sec['layers'], sec['e']
        if 'lead' in L:
            melody(ctx, s0, nb, sec['lead'], sec['center'], ANIME_R, ANIME_C, vel=.68 * e ** .5,
                   art=.8 if sec['lead'] == 'analog' else 1.0)
        if 'counter' in L:
            counter(ctx, s0, nb, 'vibes', sec['center'] - 9, .4, -.4, 'counter', delay=.5)
        if 'mbox' in L:
            arp(ctx, s0, nb, 'musicbox', 69, 88, .32 * e, [0, 3, 2, 4, 1, 3, 2, 4], 0.5, .15, 'arp', dur=.4)
        if 'pizz' in L:
            comp(ctx, s0, nb, 'stab', 'pizz', 57, 72, .5 * e, -.4, 'pizz')
        if 'sbass' in L:
            bass_oct(ctx, s0, nb, 'synthbass', 33, .65 * e,
                     pat=((0, 0, .3), (.5, 12, .2), (1, 0, .3), (1.5, 12, .2), (2, 0, .3), (2.5, 12, .2), (3, 7, .3), (3.5, 12, .2)))
        if 'half' in L:
            bass_line(ctx, s0, nb, 'whole', lo=33, vel=.5)
            pdrums(ctx, s0, nb, [[('kick', 0, .25, 0), ('clap', 2, .2, 0)] + [('shaker', k / 2, .07, -.4) for k in range(8)]], e)
        if 'drums' in L:
            pdrums(ctx, s0, nb, bounce, e, fill=fill)
        if 'pad' in L:
            comp(ctx, s0, nb, 'sustain', 'pad', 57, 74, .4 * e, 0, 'pad')
    return ctx


# ------------------------------------------------------------------ 8 Puzzle Solve (toy workshop)
TA = ['Bb', 'Gm7', 'Cm7', 'F7', 'Bb', 'Eb', 'Cm7 F7', 'Bb']
TB = ['Ebmaj7', 'F', 'Dm7', 'Gm7', 'Cm7', 'F7', 'Bb', 'F7sus4 F7']
PUZZLESOLVE = dict(id='puzzle_solve', title='Toy Workshop (Puzzle Solve)', bpm=102, bpb=4, swing=0.9, key=10, scale=MAJOR,
                   seed=1801, sections=[S('intro', ['Bb', 'Gm7', 'Cm7', 'F7'], 'clock bass'),
                                        S('A1', TA, 'clock bass guitar lead', 'marimba', 77, .85),
                                        S('A2', TA, 'clock bass guitar drums lead', 'marimba', 77),
                                        S('B', TB, 'clock bass guitar drums lead counter', 'clarinet', 74),
                                        S('C', TB, 'clock ost lead', 'kalimba', 82, .75),
                                        S('A3', TA, 'clock bass guitar drums lead', 'clarinet', 74)],
                   bus=dict(lead=dict(level=-17, rev=.18), counter=dict(level=-25, rev=.25), guitar=dict(level=-23, rev=.12),
                            ost=dict(level=-23, rev=.15), bass=dict(level=-20, rev=.03), drums=dict(level=-25, rev=.08),
                            clock=dict(level=-27, rev=.1)),
                   wet=.45, presence_db=1.5, ir=dict(rt_low=1.4, rt_mid=1.2, rt_high=.6))


def arr_puzzlesolve(song):
    ctx = new_ctx(song)
    for (s0, nb, sec) in ctx.secs:
        L, e = sec['layers'], sec['e']
        if 'lead' in L:
            melody(ctx, s0, nb, sec['lead'], sec['center'], TOY_R, TOY_C, vel=.7 * e ** .5, art=.85)
        if 'counter' in L:
            counter(ctx, s0, nb, 'marimba', sec['center'] - 10, .4, -.4, 'counter', delay=.5)
        if 'clock' in L:
            for b in range(nb):
                for k in range(8):
                    ctx.drum(s0 + b * 4 + k / 2, 'wbhi' if k % 2 == 0 else 'wblo', (.2, .12)[k % 2] * e, .4 - .8 * (k % 2), 'clock')
        if 'bass' in L:
            bass_line(ctx, s0, nb, 'two', lo=34, vel=.6 * e)
        if 'guitar' in L:
            comp(ctx, s0, nb, 'chunk', 'pluck', 55, 70, .5 * e, -.4, 'guitar')
        if 'drums' in L:
            pdrums(ctx, s0, nb, [[('kick', 0, .3, 0), ('kick', 2, .25, 0), ('clave', 3.5, .15, -.2)]], e)
        if 'ost' in L:
            ostinato(ctx, s0, nb, 'marimba', 64, .45, .35, 'ost', (0, 2, 4, 2, 1, 3, 2, 1))
    return ctx


# ------------------------------------------------------------------ 9 Sticks & Stones (caveman)
CA = ['G', 'G', 'F/G', 'G', 'G', 'C', 'F', 'G']
CB = ['C', 'C', 'G', 'G', 'F', 'F', 'Dsus4', 'D']
STICKS = dict(id='sticks_stones', title='Boulder Boogie (Sticks & Stones)', bpm=100, bpb=4, swing=0.3, key=7, scale=PENTA,
              seed=1901, bass_inst='tuba',
              sections=[S('intro', ['G', 'G', 'F/G', 'G'], 'logs'),
                        S('A1', CA, 'logs stones bass lead', 'flute', 74, .85),
                        S('A2', CA, 'logs stones bass woody lead', 'flute', 76),
                        S('B', CB, 'logs stones bass woody lead counter', 'marimba', 72),
                        S('C', CB, 'logs stones lead rain', 'flute', 79, .75),
                        S('A3', CA, 'logs stones bass woody lead', 'flute', 76)],
              bus=dict(lead=dict(level=-17, rev=.25), counter=dict(level=-25, rev=.2), logs=dict(level=-21, rev=.12),
                       stones=dict(level=-28, rev=.1), woody=dict(level=-23, rev=.12), bass=dict(level=-19, rev=.03),
                       drums=dict(level=-28, rev=.2)),
              wet=.45, presence_db=0.5, ir=dict(rt_low=1.6, rt_mid=1.2, rt_high=.5))


def arr_sticks(song):
    ctx = new_ctx(song)
    logpat = [(0, 43, .7), (.75, 50, .45), (1.5, 43, .5), (2, 48, .6), (2.75, 50, .4), (3, 55, .5), (3.5, 48, .45)]
    logpat2 = [(0, 43, .7), (1, 50, .45), (1.5, 50, .4), (2, 48, .6), (3, 43, .5), (3.25, 48, .4), (3.5, 50, .45), (3.75, 55, .5)]
    for (s0, nb, sec) in ctx.secs:
        L, e = sec['layers'], sec['e']
        if 'lead' in L:
            melody(ctx, s0, nb, sec['lead'], sec['center'], CAVE_R, CAVE_C, vel=.62 * e ** .5, art=.9)
        if 'counter' in L:
            counter(ctx, s0, nb, 'flute', sec['center'] - 5, .35, -.35, 'counter', delay=1.0)
        if 'logs' in L:
            for b in range(nb):
                for (t, m, v) in (logpat2 if b % 4 == 3 else logpat):
                    ctx.note(s0 + b * 4 + t, 'logdrum', m, .3, v * e + ctx.rng.uniform(-.05, .05),
                             -.25 if m < 48 else .25, 'logs')
        if 'stones' in L:
            for b in range(nb):
                for k in (1, 3):
                    ctx.drum(s0 + b * 4 + k, 'stone', .35 * e, .35, 'stones')
                ctx.drum(s0 + b * 4 + 3.5, 'stone', .2 * e, -.35, 'stones')
        if 'woody' in L:
            ostinato(ctx, s0, nb, 'marimba', 57, .45 * e, -.4, 'woody', (0, 2, 0, 3, 0, 2, 4, 2))
        if 'bass' in L:
            bass_line(ctx, s0, nb, 'two', lo=31, vel=.62 * e)
        if 'rain' in L:
            for b in range(0, nb, 2):
                ctx.drum(s0 + b * 4, 'rainstick', .25, .3)
    return ctx


SONGS2 = [GEMS, SUDOKU_DAY, SUDOKU_NIGHT, NATURE, UNTANGLE, ATOM, FOURPEGS, SWITCHBOX, PUZZLESOLVE, STICKS]
ARR2 = dict(gems=arr_gems, sudoku_day=arr_sudoku, sudoku_night=arr_sudoku, nature_cube=arr_nature, untangle=arr_untangle,
            atom_probe=arr_atom, four_pegs=arr_fourpegs, switchbox=arr_switchbox, puzzle_solve=arr_puzzlesolve,
            sticks_stones=arr_sticks)


def arrange2(song):
    return ARR2[song['id']](song)
