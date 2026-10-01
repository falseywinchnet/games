"""Hand-written song specs (the 'scores') for the card-game music family."""
from compose import (Ctx, MAJOR, DORIAN, melody, counter, bass_line, comp, arp, ostinato, drums)

# 2-bar lead rhythms, (onset beat, duration beats)
SWING_R = [[(0, 1), (1, .5), (1.5, 1), (2.5, .5), (3, 1), (4, 1.5), (5.5, .5), (6, 1.5)],
           [(0, .5), (.5, .5), (1, 1), (2, .5), (2.5, 1.5), (4, .5), (4.5, .5), (5, .5), (5.5, 1), (6.5, 1)],
           [(0, 1.5), (1.5, 1.5), (3, 1), (4, .5), (4.5, .5), (5, .5), (5.5, 1.5)],
           [(.5, .5), (1, .5), (1.5, .5), (2, 1), (3, .5), (3.5, 1.5), (5, 1), (6, 1.5)]]
SWING_C = [[(0, 1), (1, 1), (2, 1), (3, 1), (4, 3)], [(0, .5), (.5, .5), (1, 1), (2, 2), (4, 3.2)],
           [(0, 1.5), (1.5, .5), (2, 2), (4, 3)]]
POP_R = [[(0, .5), (.5, .5), (1, .5), (2, .5), (2.5, 1), (4, .5), (4.5, .5), (5, .5), (6, 1)],
         [(0, .75), (.75, .75), (1.5, .5), (2, 1), (3.5, .5), (4, .5), (4.5, .5), (5, 1), (6, .5), (6.5, .5)],
         [(0, 1), (1, .5), (1.5, .5), (2, .5), (3, 1), (4, 1.5), (5.5, .5), (6, 1.5)]]
POP_C = [[(0, .5), (.5, .5), (1, .5), (1.5, .5), (2, 1), (4, 2.5)], [(0, 1), (1, 1), (2, 2), (4, 3)]]
BOSSA_R = [[(0, 1.5), (1.5, 1), (2.5, 1.5), (4, 1), (5, 1), (6, 2)],
           [(0, .5), (.5, 1), (1.5, .5), (2, 1.5), (3.5, 2.5), (6.5, 1)],
           [(1, 1), (2, .5), (2.5, 1), (3.5, .5), (4, 3)],
           [(0, 2), (2, .5), (2.5, .5), (3, .5), (3.5, 1.5), (5, 1), (6, 1.5)]]
BOSSA_C = [[(0, 1.5), (1.5, 1.5), (3, 1), (4, 3.5)], [(0, .5), (.5, 1), (1.5, 2.5), (4, 3.5)]]
WALTZ_R = [[(0, 2), (2, 1), (3, 1.5), (4.5, .5), (5, 1)], [(0, 1), (1, 1), (2, 1), (3, 3)],
           [(0, 1.5), (1.5, .5), (2, 1), (3, 2), (5, 1)], [(0, .5), (.5, .5), (1, 1), (2, 1), (3, 2.5)]]
WALTZ_C = [[(0, 1), (1, 1), (2, 1), (3, 3)], [(0, 2), (2, 1), (3, 3)]]
MENU_R = [[(0, 1.5), (1.5, .5), (2, 2), (4, 1), (5, 1), (6, 2)], [(0, 3), (3, .5), (3.5, .5), (4, 2), (6, 1)],
          [(.5, .5), (1, 1), (2, 1.5), (3.5, .5), (4, 3)]]
MENU_C = [[(0, 1), (1, 1), (2, 2), (4, 3.5)], [(0, 2), (2, 2), (4, 3.5)]]


def S(name, bars, layers, lead=None, center=72, e=1.0, **kw):
    d = dict(name=name, bars=bars, layers=set(layers.split()), lead=lead, center=center, e=e)
    d.update(kw)
    return d


# ---------------------------------------------------------------- Klondike: sunny porch swing
KA = ['Gmaj7', 'Em7', 'Am7', 'D7', 'Gmaj7', 'E7', 'Am7 D7', 'G6']
KA2 = ['Gmaj7', 'Em7', 'Am7', 'D7', 'Bm7', 'E7', 'Am7 D7', 'G6 D7']
KB = ['Cmaj7', 'Bm7', 'Am7', 'D7', 'Cmaj7', 'B7', 'Em7 A7', 'Am7 D7']
KC = ['Em7', 'Cmaj7', 'Am7', 'D7sus4 D7', 'Em7', 'Cmaj7', 'Am7', 'D7sus4 D7']
KLONDIKE = dict(
    id='klondike', title='Sunny Porch (Klondike)', bpm=112, bpb=4, swing=0.7, key=7, scale=MAJOR, seed=101,
    sections=[S('intro', ['Gmaj7', 'Cmaj7', 'Am7', 'D7sus4 D7'], 'ep bass2 swish', e=.7),
              S('A1', KA, 'ep bass2 swish lead', 'marimba', 76, .8),
              S('A2', KA2, 'ep guitar bass drums lead', 'marimba', 76),
              S('B', KB, 'ep guitar bass drums lead counter', 'flute', 79, counter_inst='vibes'),
              S('A3', KA2, 'ep guitar bass drums lead', 'flute', 79),
              S('C', KC, 'ep bass2 swish lead pad', 'vibes', 74, .75),
              S('B2', KB, 'ep guitar bass drums lead counter', 'marimba', 79, counter_inst='flute'),
              S('A4', KA2, 'ep guitar bass drums lead', 'marimba', 76),
              S('tag', ['Gmaj7 E7', 'Am7 D7', 'Bm7 E7', 'Am7 D7'], 'ep guitar bass drums', e=.85)],
    bus=dict(lead=dict(level=-17, rev=.22, delay=(.75, .25), delay_mix=.35), counter=dict(level=-24, rev=.3),
             ep=dict(level=-22, rev=.2, autopan=24), guitar=dict(level=-24, rev=.12), bass=dict(level=-19, rev=.04),
             drums=dict(level=-25, rev=.1), pad=dict(level=-28, rev=.4)),
    wet=.55, ir=dict(rt_low=1.8, rt_mid=1.5, rt_high=.7))


# ---------------------------------------------------------------- Spider: tiptoe dorian bounce
SA = ['Am7', 'D9', 'Am7', 'D9', 'Fmaj7', 'G6', 'Em7', 'Am7']
SB = ['Fmaj7', 'G', 'Em7', 'Am7', 'Dm7', 'G7', 'Cmaj7', 'E7sus4 E7']
SC = ['Dm7', 'Em7', 'Fmaj7', 'E7sus4 E7', 'Dm7', 'Em7', 'Fmaj7 G', 'E7sus4 E7']
SPIDER = dict(
    id='spider', title='Tiptoe Web (Spider)', bpm=118, bpb=4, swing=0.3, key=9, scale=DORIAN, seed=202,
    sections=[S('intro', ['Am7', 'D9', 'Am7', 'D9'], 'ost bass perc', e=.7),
              S('A1', SA, 'ost bass drums lead', 'clarinet', 69, .85),
              S('A2', SA, 'ost pizz bass drums lead', 'clarinet', 69),
              S('B', SB, 'pizz bass drums lead pad', 'marimba', 79),
              S('A3', SA, 'ost pizz bass drums lead counter', 'marimba', 81, counter_inst='clarinet'),
              S('C', SC, 'pizz bass perc pad lead', 'vibes', 76, .75),
              S('B2', SB, 'ost pizz bass drums lead', 'clarinet', 72),
              S('A4', SA, 'ost pizz bass drums lead', 'marimba', 81)],
    bus=dict(lead=dict(level=-17, rev=.2, delay=(.5, .22), delay_mix=.3), counter=dict(level=-25, rev=.3),
             ost=dict(level=-23, rev=.15), pizz=dict(level=-24, rev=.18), bass=dict(level=-19, rev=.03),
             drums=dict(level=-24, rev=.08), pad=dict(level=-29, rev=.45)),
    wet=.5, ir=dict(rt_low=1.6, rt_mid=1.3, rt_high=.6))


# ---------------------------------------------------------------- FreeCell: clear-headed bossa
FA = ['Dmaj7', 'Dmaj7', 'Em7', 'A7', 'F#m7', 'B7', 'Em7', 'A7sus4 A7']
FB = ['Gmaj7', 'Gm6', 'F#m7', 'B7', 'Em9', 'A7', 'Dmaj7', 'Bm7 A7']
FC = ['Gmaj7', 'F#m7', 'Em7', 'A7sus4', 'Gmaj7', 'F#m7', 'Em7', 'A7']
FREECELL = dict(
    id='freecell', title='Clear Thinking (FreeCell)', bpm=100, bpb=4, swing=0.0, key=2, scale=MAJOR, seed=303,
    sections=[S('intro', ['Dmaj7', 'Gmaj7', 'Dmaj7', 'A7sus4'], 'guitar bassw', e=.7),
              S('A1', FA, 'guitar bass drums lead', 'vibes', 74, .85),
              S('A2', FA, 'guitar ep bass drums lead', 'vibes', 76),
              S('B', FB, 'guitar ep bass drums lead counter', 'flute', 79, counter_inst='vibes'),
              S('A3', FA, 'guitar ep bass drums lead', 'flute', 79),
              S('C', FC, 'eparp bassw pad lead', 'vibes', 74, .75),
              S('B2', FB, 'guitar ep bass drums lead', 'vibes', 79),
              S('A4', FA, 'guitar ep bass drums lead counter', 'flute', 79, counter_inst='vibes')],
    bus=dict(lead=dict(level=-17, rev=.25, delay=(.75, .25), delay_mix=.35), counter=dict(level=-25, rev=.3),
             guitar=dict(level=-22, rev=.12), ep=dict(level=-25, rev=.2, autopan=16), arp=dict(level=-23, rev=.25),
             bass=dict(level=-19, rev=.03), drums=dict(level=-25, rev=.08), pad=dict(level=-28, rev=.45)),
    wet=.55, ir=dict(rt_low=2.0, rt_mid=1.6, rt_high=.8))


# ---------------------------------------------------------------- Hearts: parlour waltz
HA = ['F', 'F', 'Gm7', 'C7', 'Am7', 'Dm7', 'Gm7', 'C7', 'F', 'F7', 'Bbmaj7', 'Bbm6', 'Am7', 'D7', 'Gm7 C7', 'F']
HB = ['Bbmaj7', 'Bbmaj7', 'Am7', 'Dm7', 'Gm7', 'C7', 'Fmaj7', 'D7', 'Gm7', 'Gm7', 'Am7', 'Dm7', 'G7', 'G7', 'Gm7', 'C7']
HC = ['Dm', 'Dm', 'Bbmaj7', 'Bbmaj7', 'Gm7', 'C7', 'F', 'A7', 'Dm', 'Dm', 'Bbmaj7', 'Bbm6', 'F', 'D7', 'G7', 'C7sus4 C7']
HEARTS = dict(
    id='hearts', title='Parlour Waltz (Hearts)', bpm=150, bpb=3, swing=0.0, key=5, scale=MAJOR, seed=404,
    sections=[S('intro', ['F', 'Bbmaj7', 'Gm7', 'C7'], 'harp bass', e=.7),
              S('A1', HA, 'pah bass drums lead', 'clarinet', 72, .85),
              S('B', HB, 'pah harp bass drums lead', 'flute', 79),
              S('A2', HA, 'pah harp bass drums lead harm', 'flute', 79),
              S('C', HC, 'harp bass pad lead', 'clarinet', 70, .75),
              S('A3', HA, 'pah harp bass drums lead counter', 'clarinet', 72, counter_inst='reed')],
    bus=dict(lead=dict(level=-17, rev=.25), harm=dict(level=-23, rev=.3), counter=dict(level=-26, rev=.3),
             pah=dict(level=-24, rev=.15), harp=dict(level=-23, rev=.3), bass=dict(level=-20, rev=.04),
             drums=dict(level=-27, rev=.1), pad=dict(level=-28, rev=.45)),
    wet=.6, ir=dict(rt_low=2.2, rt_mid=1.8, rt_high=.8))


# ---------------------------------------------------------------- Menu: shuffle lounge
MA = ['Cmaj9', 'Am9', 'Fmaj9', 'G7sus4 G7', 'Em7', 'A7', 'Dm9', 'G7sus4']
MB = ['Fmaj7', 'Em7', 'Dm7', 'Cmaj7', 'Bbmaj7', 'Am7', 'Dm7', 'G7sus4 G7']
MENU = dict(
    id='menu', title='Shuffle Lounge (Menu)', bpm=84, bpb=4, swing=0.45, key=0, scale=MAJOR, seed=505,
    sections=[S('A1', MA, 'eparp pad bassw', None, 79, .8),
              S('A2', MA, 'eparp pad bassw lead', 'celesta', 81, .85),
              S('B', MB, 'eparp pad bassw lead perc counter', 'vibes', 79, .9, counter_inst='flute'),
              S('A3', MA, 'eparp pad bassw lead perc', 'celesta', 84, .85)],
    bus=dict(lead=dict(level=-18, rev=.35, delay=(1.5, .3), delay_mix=.4), counter=dict(level=-26, rev=.35),
             arp=dict(level=-21, rev=.3, autopan=8), bass=dict(level=-22, rev=.05),
             drums=dict(level=-30, rev=.15), pad=dict(level=-25, rev=.45)),
    wet=.7, ir=dict(rt_low=2.4, rt_mid=2.0, rt_high=.9))


# ---------------------------------------------------------------- Puzzle (isometric block-pusher)
# Original; broad cues taken from measured features of the recovered reference loops
# (G-major centre, ~130 BPM bounce, reed-like sustained tones, glide lead) - no material copied.
PA = ['G', 'G', 'D7', 'D7', 'D7', 'D7', 'G', 'G']
PA2 = ['G', 'G7', 'C', 'Cm6', 'G', 'E7', 'A7 D7', 'G']
PB = ['C', 'C', 'G', 'G', 'A7', 'A7', 'D7', 'D7sus4 D7']
PUZZLE = dict(
    id='puzzle_main', title='Woozle Warehouse (Puzzle main)', bpm=132, bpb=4, swing=0.25, key=7, scale=MAJOR,
    seed=606, bass_inst='tuba',
    sections=[S('intro', ['G', 'D7', 'G', 'D7'], 'acc bass2 perc', e=.75),
              S('A1', PA, 'acc bass2 drums lead', 'reed', 71, .85),
              S('A2', PA2, 'acc bass2 drums lead', 'marimba', 79),
              S('B', PB, 'acc pizz bass2 drums lead counter', 'slide', 76, counter_inst='reed'),
              S('A3', PA2, 'acc bass2 drums lead', 'reed', 74),
              S('C', ['Em', 'Em', 'C', 'C', 'Am7', 'D7', 'G', 'D7sus4 D7'], 'pizz bassw perc lead', 'marimba', 79, .75),
              S('A4', PA2, 'acc pizz bass2 drums lead', 'slide', 76)],
    bus=dict(lead=dict(level=-17, rev=.15, delay=(.5, .2), delay_mix=.25), counter=dict(level=-25, rev=.2),
             acc=dict(level=-22, rev=.12), pizz=dict(level=-25, rev=.15), bass=dict(level=-19, rev=.03),
             drums=dict(level=-24, rev=.08)),
    wet=.4, ir=dict(rt_low=1.3, rt_mid=1.1, rt_high=.5))

QA = ['C', 'Am', 'Dm7', 'G7', 'C', 'Am', 'F G7', 'C']
QB = ['F', 'Fm6', 'C', 'A7', 'Dm7', 'G7', 'Em7 A7', 'Dm7 G7']
PUZZLE_ALT = dict(
    id='puzzle_tiptoe', title='Tiptoe Trunks (Puzzle alternate)', bpm=116, bpb=4, swing=0.15, key=0, scale=MAJOR,
    seed=707,
    sections=[S('intro', ['C', 'Am', 'Dm7', 'G7'], 'pizz bass perc', e=.75),
              S('A1', QA, 'pizz bass drums lead', 'slide', 76, .85),
              S('B', QB, 'pizz ost bass drums lead', 'clarinet', 72),
              S('A2', QA, 'pizz ost bass drums lead counter', 'slide', 79, counter_inst='clarinet'),
              S('C', ['Am', 'Em', 'F', 'C', 'Dm7', 'Em7', 'F', 'G7sus4 G7'], 'ost bassw perc lead pad', 'marimba', 79, .75),
              S('A3', QA, 'pizz bass drums lead', 'clarinet', 72)],
    bus=dict(lead=dict(level=-17, rev=.15, delay=(.75, .2), delay_mix=.25), counter=dict(level=-25, rev=.2),
             ost=dict(level=-24, rev=.12), pizz=dict(level=-22, rev=.15), bass=dict(level=-19, rev=.03),
             drums=dict(level=-24, rev=.08), pad=dict(level=-29, rev=.4)),
    wet=.4, ir=dict(rt_low=1.3, rt_mid=1.1, rt_high=.5))

SONGS = [MENU, KLONDIKE, SPIDER, FREECELL, HEARTS, PUZZLE, PUZZLE_ALT]

STYLE = dict(
    klondike=dict(R=SWING_R, C=SWING_C, bass='walk', drums='brush'),
    spider=dict(R=POP_R, C=POP_C, bass='pop', drums='pop'),
    freecell=dict(R=BOSSA_R, C=BOSSA_C, bass='bossa', drums='bossa'),
    hearts=dict(R=WALTZ_R, C=WALTZ_C, bass='waltz', drums='waltz'),
    menu=dict(R=MENU_R, C=MENU_C, bass='whole', drums='menu'),
    puzzle_main=dict(R=POP_R, C=POP_C, bass='two', drums='pop'),
    puzzle_tiptoe=dict(R=POP_R, C=POP_C, bass='pop', drums='pop'))

ART = dict(slide=0.85, marimba=1.0, vibes=0.9, flute=0.93, clarinet=0.93, celesta=1.0, reed=0.95)


def arrange(song):
    ctx = Ctx(song, song['seed'])
    st = STYLE[song['id']]
    for (s0, nb, sec) in ctx.secs:
        L, e = sec['layers'], sec['e']
        if 'lead' in L:
            melody(ctx, s0, nb, sec['lead'], sec['center'], st['R'], st['C'], vel=.72 * e ** .5,
                   art=ART.get(sec['lead'], .92),
                   harmony=('clarinet', .7, 'harm') if 'harm' in L else None)
        if 'counter' in L:
            counter(ctx, s0, nb, sec['counter_inst'], sec['center'] - 10, .5 * e, -0.45, 'counter',
                    delay=0.5 if song['bpb'] == 4 else 0)
        if 'bass' in L:
            bass_line(ctx, s0, nb, st['bass'], vel=.72 * e)
        if 'bass2' in L:
            bass_line(ctx, s0, nb, 'two', vel=.66 * e)
        if 'bassw' in L:
            bass_line(ctx, s0, nb, 'whole', vel=.62 * e)
        if 'drums' in L:
            drums(ctx, s0, nb, st['drums'], e)
        if 'swish' in L:
            drums(ctx, s0, nb, 'brush', 0.5 * e)
        if 'perc' in L:
            drums(ctx, s0, nb, 'menu' if song['id'] == 'menu' else 'bossa', 0.55 * e, fill=False)
        if 'pad' in L:
            comp(ctx, s0, nb, 'sustain', 'pad', 55, 72, .55 * e, 0, 'pad')
        sid = song['id']
        if 'ep' in L:
            comp(ctx, s0, nb, 'swing' if sid == 'klondike' else 'sustain', 'ep', 52, 70, .5 * e, 0.25, 'ep')
        if 'guitar' in L:
            comp(ctx, s0, nb, 'chunk' if sid == 'klondike' else 'bossa', 'pluck', 50, 67, .55 * e, -0.5, 'guitar',
                 rootless=False)
        if 'eparp' in L:
            arp(ctx, s0, nb, 'ep', 55, 76, .45 * e, [0, 2, 3, 4, 1, 3, 2, 4], 0.5, 0.0, 'arp', dur=1.2)
        if 'ost' in L:
            ostinato(ctx, s0, nb, 'marimba', 64, .5 * e, 0.45, 'ost',
                     (0, 2, 1, 2, 3, 2, 1, 2) if 'A' in sec['name'] or sec['name'] == 'intro' else (0, 1, 2, 3, 4, 3, 2, 1))
        if 'pizz' in L:
            comp(ctx, s0, nb, 'stab', 'pizz', 55, 70, .55 * e, -0.45, 'pizz')
        if 'pah' in L:
            comp(ctx, s0, nb, 'pahpah', 'ep', 55, 69, .45 * e, 0.3, 'pah')
        if 'acc' in L:
            comp(ctx, s0, nb, 'chunk', 'reed', 55, 69, .5 * e, 0.4, 'acc', rootless=False)
        if 'harp' in L:
            arp(ctx, s0, nb, 'harp', 53, 77, .45 * e, [0, 2, 3, 4, 3, 2], 0.5, -0.35, 'harp')
    return ctx
