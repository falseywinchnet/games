"""Eggy's soundtrack: one seamless loop per region of the mountain.

Part nature, part marching, part monastery, part mountain man, part dwarven
kingdom, part heroic march, part spaghetti western, part mystery, part
fantasy - all cute. Original compositions; nothing sampled or quoted.
python3 eggy_music.py [id ...]  ->  ../assets/audio/<id>.m4a + manifest
"""
import json, os, subprocess, sys
import numpy as np
import eggy_synth  # noqa: F401  (registers extra instruments)
from synth import SR, write_wav
from compose import MAJOR, DORIAN, AEOLIAN, render, master, melody, counter, bass_line, comp, arp, ostinato, near
from songs import S, SWING_R, SWING_C, POP_R, POP_C, MENU_R, MENU_C
from songs2 import pdrums, bass_oct, scatter, new_ctx, CALM_R, CALM_C, CAVE_R, CAVE_C, TOY_R, TOY_C
from render_music import lufs, soft_limit

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'assets', 'audio')
WORK = os.path.join(HERE, 'out')
PENTA = [0, 2, 4, 7, 9]
MINOR_PENTA = [0, 3, 5, 7, 10]

MARCH_R = [[(0, 1.5), (1.5, .5), (2, 1), (3, 1), (4, 1.5), (5.5, .5), (6, 2)],
           [(0, .75), (.75, .25), (1, 1), (2, .75), (2.75, .25), (3, 1), (4, 2), (6, 1), (7, 1)],
           [(0, 1), (1, .5), (1.5, .5), (2, 1), (3, 1), (4, .75), (4.75, .25), (5, 1), (6, 2)]]
MARCH_C = [[(0, 1), (1, 1), (2, 2), (4, 4)], [(0, .75), (.75, .25), (1, 1), (2, 2), (4, 3.5)]]
WEST_R = [[(0, 3), (3, .5), (3.5, .5), (4, 4)], [(0, .5), (.5, .5), (1, 2.5), (4, 1), (5, 3)], [(0, 2), (2, 1), (3, 1), (4, 4)]]
WEST_C = [[(0, 1), (1, 1), (2, 2), (4, 4)]]

SNARE_MARCH = [[('snare', 0, .45, .1), ('ghost', .25, .12, .1), ('ghost', .5, .15, .1), ('snare', .75, .3, .1), ('snare', 1, .4, .1),
                ('snare', 2, .45, .1), ('ghost', 2.5, .15, .1), ('snare', 2.75, .3, .1), ('snare', 3, .4, .1), ('ghost', 3.5, .15, .1),
                ('kick', 0, .35, 0), ('kick', 2, .3, 0)]]
SNARE_ROLL = [b for b in SNARE_MARCH[0] if b[1] < 2] + [('ghost', 2 + k * .125, .1 + .03 * k, .1) for k in range(16)]


# ------------------------------------------------------------------ 1 meadow: Little Boots March
MA = ['G', 'D/F#', 'Em', 'C', 'G', 'Am7 D7', 'G', 'D7']
MB = ['C', 'G/B', 'Am', 'D', 'Em', 'C', 'Am7 D7', 'G']
MEADOW = dict(id='eggy_meadow', title='Little Boots March', bpm=112, bpb=4, swing=0.0, key=7, scale=MAJOR, seed=3101, bass_inst='tuba',
              sections=[S('intro', ['G', 'D', 'G', 'D7'], 'snare tuba'), S('A1', MA, 'snare tuba pizz lead', 'flute', 79, .9),
                        S('B', MB, 'snare tuba pizz lead glock', 'brass', 74), S('C', ['Em', 'C', 'G', 'D', 'Em', 'C', 'Am7', 'D7'], 'pizz box lead', 'whistle', 79, .75),
                        S('A2', MA, 'snare tuba pizz lead glock', 'flute', 81)],
              bus=dict(lead=dict(level=-17, rev=.2), glock=dict(level=-24, rev=.3), pizz=dict(level=-23, rev=.15), bass=dict(level=-19, rev=.04),
                       drums=dict(level=-22, rev=.12), box=dict(level=-24, rev=.3)),
              wet=.45, ir=dict(rt_low=1.6, rt_mid=1.3, rt_high=.6))
TITLE = dict(MEADOW, id='eggy_title', title='Little Boots March (title)', bpm=96, seed=3102,
             sections=[S('A1', MA, 'box tuba lead', 'whistle', 79, .8), S('B', MB, 'box tuba pizz lead', 'flute', 79, .85),
                       S('A2', MA, 'box tuba pizz lead snare', 'celesta', 84, .8)])


def arr_meadow(song):
    ctx = new_ctx(song)
    for (s0, nb, sec) in ctx.secs:
        L, e = sec['layers'], sec['e']
        if 'lead' in L:
            melody(ctx, s0, nb, sec['lead'], sec['center'], MARCH_R, MARCH_C, vel=.7 * e ** .5, art=.9)
        if 'glock' in L:
            counter(ctx, s0, nb, 'celesta', sec['center'] + 5, .35, .35, 'glock', delay=0)
        if 'snare' in L:
            pdrums(ctx, s0, nb, SNARE_MARCH, e * .9, fill=SNARE_ROLL)
            for b in range(0, nb, 8):
                ctx.drum(s0 + b * 4, 'triangle', .12, -.4)
        if 'tuba' in L:
            bass_line(ctx, s0, nb, 'two', lo=31, vel=.62 * e)
        if 'pizz' in L:
            comp(ctx, s0, nb, 'chunk', 'pizz', 55, 69, .45 * e, -.4, 'pizz')
        if 'box' in L:
            arp(ctx, s0, nb, 'musicbox', 72, 91, .3 * e, [0, 2, 4, 2, 3, 1, 4, 2], .5, .2, 'box', dur=.4)
    return ctx


# ------------------------------------------------------------------ 2 forest: Mossy Mystery
FA = ['Dm9', 'Em7', 'Fmaj7', 'Em7', 'Dm9', 'Gm7', 'Bbmaj7', 'A7sus4 A7']
FB = ['Bbmaj7', 'C', 'Am7', 'Dm', 'Gm7', 'C', 'Fmaj7', 'A7']
FOREST = dict(id='eggy_forest', title='Mossy Mystery', bpm=90, bpb=4, swing=0.3, key=2, scale=DORIAN, seed=3201,
              sections=[S('intro', FA[:4], 'arp pad'), S('A1', FA, 'arp pad bass lead', 'clarinet', 69, .85),
                        S('A2', FA, 'arp pad bass lead harp sneak', 'clarinet', 72), S('B', FB, 'harp pad bass lead sneak', 'celesta', 81),
                        S('C', FA, 'arp pad lead', 'whistle', 76, .7), S('A3', FA, 'arp pad bass lead harp sneak', 'clarinet', 72)],
              bus=dict(lead=dict(level=-17, rev=.3, delay=(.75, .25), delay_mix=.3), arp=dict(level=-23, rev=.35), harp=dict(level=-24, rev=.35),
                       pad=dict(level=-27, rev=.5), bass=dict(level=-21, rev=.05), drums=dict(level=-28, rev=.15)),
              wet=.6, ir=dict(rt_low=2.2, rt_mid=1.8, rt_high=.8))


def arr_forest(song):
    ctx = new_ctx(song)
    for (s0, nb, sec) in ctx.secs:
        L, e = sec['layers'], sec['e']
        if 'lead' in L:
            melody(ctx, s0, nb, sec['lead'], sec['center'], SWING_R, SWING_C, vel=.62 * e ** .5, art=.92)
        if 'arp' in L:
            arp(ctx, s0, nb, 'celesta', 69, 86, .3 * e, [0, 2, 1, 3, 2, 4, 3, 1], .5, .25, 'arp', dur=.5)
        if 'harp' in L:
            arp(ctx, s0, nb, 'harp', 50, 74, .35 * e, [0, 2, 4, 3, 1, 2], 1.0, -.3, 'harp')
        if 'pad' in L:
            comp(ctx, s0, nb, 'sustain', 'pad', 52, 69, .4 * e, 0, 'pad')
        if 'bass' in L:
            bass_line(ctx, s0, nb, 'walk', lo=31, vel=.48 * e)
        if 'sneak' in L:  # a tiny soldier tiptoeing: soft snare ghosts and woodblocks
            pdrums(ctx, s0, nb, [[('ghost', 1, .16, .3), ('ghost', 3, .16, .3), ('wblo', 2.5, .1, -.3), ('tap', 0, .12, 0)]], e)
    return ctx


# ------------------------------------------------------------------ 3 pond: Brook Hoedown
PA = ['G', 'G', 'C', 'G', 'G', 'G', 'D7', 'G']
PB = ['C', 'C', 'G', 'G', 'D', 'D7', 'G', 'G']
POND = dict(id='eggy_pond', title='Brook Hoedown', bpm=120, bpb=4, swing=0.5, key=7, scale=MAJOR, seed=3301, bass_inst='tuba',
            sections=[S('intro', ['G', 'C', 'D7', 'G'], 'banjo board'), S('A1', PA, 'banjo board jug lead', 'reed', 74, .9),
                      S('B', PB, 'banjo board jug lead', 'whistle', 79), S('A2', PA, 'banjo board jug lead', 'reed', 76),
                      S('C', ['Em', 'C', 'G', 'D7', 'Em', 'C', 'D7', 'G'], 'banjo jug lead', 'pluck', 76, .8), S('A3', PA, 'banjo board jug lead', 'reed', 76)],
            bus=dict(lead=dict(level=-17, rev=.15), banjo=dict(level=-21, rev=.12), bass=dict(level=-19, rev=.03), drums=dict(level=-25, rev=.08)),
            wet=.4, ir=dict(rt_low=1.3, rt_mid=1.1, rt_high=.5))


def arr_pond(song):
    ctx = new_ctx(song)
    for (s0, nb, sec) in ctx.secs:
        L, e = sec['layers'], sec['e']
        if 'lead' in L:
            melody(ctx, s0, nb, sec['lead'], sec['center'], TOY_R, TOY_C, vel=.68 * e ** .5, art=.85)
        if 'banjo' in L:  # forward rolls
            arp(ctx, s0, nb, 'banjo', 59, 79, .42 * e, [0, 2, 4, 0, 3, 4, 1, 4], .5, -.35, 'banjo', dur=.4)
        if 'jug' in L:
            bass_line(ctx, s0, nb, 'two', lo=31, vel=.62 * e)
        if 'board' in L:
            pdrums(ctx, s0, nb, [[('washboard', k / 2, (.28, .16)[k % 2], .35) for k in range(8)] + [('kick', 0, .22, 0), ('kick', 2, .18, 0)]], e)
    return ctx


# ------------------------------------------------------------------ 4 ravine: Fistful of Pebbles
RA = ['Am', 'Am', 'G', 'Am', 'F', 'G', 'Am', 'E7']
RB = ['Dm', 'Am', 'G', 'C', 'F', 'E7', 'Am', 'Am']
RAVINE = dict(id='eggy_ravine', title='Fistful of Pebbles', bpm=96, bpb=4, swing=0.0, key=9, scale=AEOLIAN, seed=3401,
              sections=[S('intro', ['Am', 'Am', 'G', 'E7'], 'twang drone gallop'), S('A1', RA, 'twang drone gallop lead', 'whistle', 76, .9),
                        S('A2', RA, 'twang drone gallop lead anvil', 'whistle', 79), S('B', RB, 'twang gallop lead anvil', 'brass', 72),
                        S('C', ['Am', 'G', 'F', 'E7', 'Am', 'G', 'F', 'E7'], 'twang drone lead', 'twang', 69, .75), S('A3', RA, 'twang drone gallop lead anvil', 'whistle', 79)],
              bus=dict(lead=dict(level=-16, rev=.4, delay=(.75, .35), delay_mix=.45), twang=dict(level=-22, rev=.5), pad=dict(level=-28, rev=.5),
                       drums=dict(level=-24, rev=.25), bass=dict(level=-21, rev=.05)),
              wet=.7, ir=dict(rt_low=2.8, rt_mid=2.4, rt_high=1.0))


def arr_ravine(song):
    ctx = new_ctx(song)
    gallop = [[('logkick', 0, .35, 0), ('loghi', .5, .2, -.2), ('loghi', .75, .18, -.2), ('logkick', 1, .3, 0), ('loghi', 1.5, .2, -.2), ('loghi', 1.75, .18, -.2),
               ('logkick', 2, .35, 0), ('loghi', 2.5, .2, -.2), ('loghi', 2.75, .18, -.2), ('logkick', 3, .3, 0), ('loghi', 3.5, .2, -.2), ('loghi', 3.75, .18, -.2),
               ('rim', 1, .12, .3), ('rim', 3, .12, .3)]]
    for (s0, nb, sec) in ctx.secs:
        L, e = sec['layers'], sec['e']
        if 'lead' in L:
            melody(ctx, s0, nb, sec['lead'], sec['center'], WEST_R, WEST_C, vel=.7 * e ** .5, art=.95)
        if 'twang' in L:
            arp(ctx, s0, nb, 'twang', 45, 64, .45 * e, [0, 4, 2, 4], 1.0, .35, 'twang', dur=1.2)
        if 'drone' in L:
            comp(ctx, s0, nb, 'sustain', 'drone', 45, 57, .5 * e, 0, 'pad')
        if 'gallop' in L:
            pdrums(ctx, s0, nb, gallop, e)
        if 'anvil' in L:
            for b in range(0, nb, 2):
                ctx.drum(s0 + b * 4, 'anvil', .2, .4)
        bass_line(ctx, s0, nb, 'two', lo=33, vel=.5 * e)
    return ctx


# ------------------------------------------------------------------ 5 highland: Dwarven Halls
HA = ['Dm', 'Dm', 'C', 'Dm', 'Bb', 'C', 'Dm', 'A7']
HB = ['Gm', 'Dm', 'Bb', 'F', 'Gm', 'Dm', 'Bb', 'A7']
HIGH = dict(id='eggy_highland', title='Dwarven Halls', bpm=84, bpb=4, swing=0.0, key=2, scale=DORIAN, seed=3501, bass_inst='tuba',
            sections=[S('intro', HA[:4], 'deep drums drone'), S('A1', HA, 'deep drums drone lead', 'brass', 62, .9),
                      S('B', HB, 'deep drums forge lead march', 'reed', 69), S('C', HA, 'drone lead box', 'flute', 74, .75),
                      S('A2', HA, 'deep drums forge drone lead march', 'brass', 64)],
            bus=dict(lead=dict(level=-17, rev=.35), pad=dict(level=-24, rev=.45), bass=dict(level=-18, rev=.06), drums=dict(level=-21, rev=.3),
                     box=dict(level=-26, rev=.4)),
            wet=.65, ir=dict(rt_low=3.0, rt_mid=2.4, rt_high=1.0))


def arr_highland(song):
    ctx = new_ctx(song)
    for (s0, nb, sec) in ctx.secs:
        L, e = sec['layers'], sec['e']
        if 'lead' in L:
            melody(ctx, s0, nb, sec['lead'], sec['center'], MARCH_R, MARCH_C, vel=.68 * e ** .5, art=.95)
        if 'deep' in L:
            bass_oct(ctx, s0, nb, 'tuba', 26, .6 * e, pat=((0, 0, .9), (1.5, 0, .4), (2, 7, .9), (3, 0, .9)))
        if 'drums' in L:
            pdrums(ctx, s0, nb, [[('frame', 0, .5, 0), ('logkick', 1.5, .25, 0), ('frame', 2, .4, 0), ('loghi', 3, .2, .2), ('loghi', 3.5, .2, .2)]], e)
        if 'forge' in L:
            pdrums(ctx, s0, nb, [[('anvil', 1, .16, .4), ('anvil', 3, .16, .4)]], e)
        if 'march' in L:
            pdrums(ctx, s0, nb, SNARE_MARCH, e * .55)
        if 'drone' in L:
            comp(ctx, s0, nb, 'sustain', 'reed', 50, 62, .35 * e, 0, 'pad')
        if 'box' in L:
            arp(ctx, s0, nb, 'musicbox', 74, 91, .25, [0, 2, 1, 3], 1.0, .2, 'box', dur=.6)
    return ctx


# ------------------------------------------------------------------ 6 snow: Snow Bell Monastery
SA = ['Dsus2', 'Dsus2', 'C', 'Dsus2', 'Bbmaj7', 'C', 'Dsus2', 'Asus4']
MONASTERY = dict(id='eggy_monastery', title='Snow Bell Monastery', bpm=66, bpb=4, swing=0.0, key=2, scale=PENTA, seed=3601,
                 sections=[S('A1', SA, 'drone bowls heart', None, 74, .8), S('A2', SA, 'drone bowls heart bells lead', 'whistle', 74, .85),
                           S('B', ['Bbmaj7', 'C', 'Dsus2', 'Dsus2', 'Bbmaj7', 'C', 'Asus4', 'Asus4'], 'drone bowls bells lead box', 'flute', 79),
                           S('A3', SA, 'drone bowls heart lead', 'whistle', 76, .85)],
                 bus=dict(lead=dict(level=-18, rev=.5, delay=(1.5, .3), delay_mix=.35), pad=dict(level=-22, rev=.55), bowls=dict(level=-22, rev=.6),
                          bells=dict(level=-25, rev=.6), drums=dict(level=-25, rev=.4), box=dict(level=-27, rev=.5)),
                 wet=.8, ir=dict(rt_low=3.6, rt_mid=3.0, rt_high=1.2))


def arr_monastery(song):
    ctx = new_ctx(song)
    for (s0, nb, sec) in ctx.secs:
        L, e = sec['layers'], sec['e']
        if 'lead' in L:
            melody(ctx, s0, nb, sec['lead'], sec['center'], CALM_R, CALM_C, vel=.55 * e, art=1.0)
        if 'drone' in L:
            for b in range(0, nb, 2):
                ctx.note(s0 + b * 4, 'drone', 38, 8.0, .5 * e, 0, 'pad')
                ctx.note(s0 + b * 4, 'drone', 45, 8.0, .35 * e, 0, 'pad')
        if 'bowls' in L:
            scatter(ctx, s0, nb, 'bowl', 55, 72, .9, .45, bus='bowls', scale=PENTA)
        if 'bells' in L:
            scatter(ctx, s0, nb, 'vibes', 79, 91, 1.2, .3, bus='bells', scale=PENTA)
        if 'heart' in L:
            pdrums(ctx, s0, nb, [[('frame', 0, .35, 0), ('frame', .5, .2, 0)], [('frame', 0, .3, 0)]], e)
        if 'box' in L:
            arp(ctx, s0, nb, 'musicbox', 76, 93, .22, [0, 2, 1, 3, 2, 4], 1.0, .3, 'box', dur=.6)
    return ctx


# ------------------------------------------------------------------ 7 summit
SUMMIT = dict(id='eggy_summit', title='Summit, With Medal', bpm=100, bpb=4, swing=0.0, key=2, scale=MAJOR, seed=3701, bass_inst='tuba',
              sections=[S('A1', ['D', 'A/C#', 'Bm', 'G', 'D', 'Em7 A7', 'D', 'A7'], 'snare tuba lead bowls', 'brass', 74, .9),
                        S('B', ['G', 'D/F#', 'Em', 'A', 'Bm', 'G', 'Em7 A7', 'D'], 'snare tuba lead glock bowls', 'flute', 81),
                        S('A2', ['D', 'A/C#', 'Bm', 'G', 'D', 'Em7 A7', 'D', 'A7'], 'tuba lead glock bowls', 'celesta', 84, .85)],
              bus=dict(lead=dict(level=-17, rev=.35), glock=dict(level=-24, rev=.4), bass=dict(level=-20, rev=.05), drums=dict(level=-23, rev=.2),
                       bowls=dict(level=-25, rev=.6)),
              wet=.6, ir=dict(rt_low=2.6, rt_mid=2.2, rt_high=1.0))


def arr_summit(song):
    ctx = new_ctx(song)
    for (s0, nb, sec) in ctx.secs:
        L, e = sec['layers'], sec['e']
        if 'lead' in L:
            melody(ctx, s0, nb, sec['lead'], sec['center'], MARCH_R, MARCH_C, vel=.68 * e ** .5, art=.9)
        if 'glock' in L:
            counter(ctx, s0, nb, 'celesta', sec['center'] + 5, .35, .35, 'glock')
        if 'snare' in L:
            pdrums(ctx, s0, nb, SNARE_MARCH, e * .7, fill=SNARE_ROLL)
        if 'tuba' in L:
            bass_line(ctx, s0, nb, 'two', lo=33, vel=.58 * e)
        if 'bowls' in L:
            scatter(ctx, s0, nb, 'bowl', 62, 74, .5, .35, bus='bowls')
    return ctx


SONGS = [(TITLE, arr_meadow), (MEADOW, arr_meadow), (FOREST, arr_forest), (POND, arr_pond), (RAVINE, arr_ravine),
         (HIGH, arr_highland), (MONASTERY, arr_monastery), (SUMMIT, arr_summit)]


def main(ids):
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(WORK, exist_ok=True)
    mpath = os.path.join(OUT, 'eggy_audio_manifest.json')
    manifest = json.load(open(mpath)) if os.path.exists(mpath) else {'music': []}
    entries = {m['id']: m for m in manifest['music']}
    for song, arr in SONGS:
        if ids and song['id'] not in ids:
            continue
        ctx = arr(song)
        mix, spb = render(song, ctx)
        out, _ = master(song, mix, spb)
        wav = os.path.join(WORK, song['id'] + '.wav')
        out = out / np.abs(out).max() * .5
        write_wav(wav, out)
        i, _ = lufs(wav)
        out = out * 10 ** ((-19.0 - i) / 20)
        if np.abs(out).max() > .5:
            out = soft_limit(out, 10 ** (-3 / 20))
        write_wav(wav, out, 24)
        i, tp = lufs(wav)
        m4a = os.path.join(OUT, song['id'] + '.m4a')
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', wav, '-c:a', 'aac_at', '-b:a', '160k', m4a], check=True)
        entries[song['id']] = dict(id=song['id'], title=song['title'], file=os.path.basename(m4a), sample_rate=SR, channels=2,
                                   loop_start_sample=0, loop_end_sample_exclusive=int(out.shape[1]), seconds=round(out.shape[1] / SR, 2),
                                   bpm=round(SR * 60 / spb, 3), lufs=i, true_peak_dbtp=tp)
        print(song['id'], entries[song['id']]['seconds'], 's', i, 'LUFS', flush=True)
    manifest['music'] = sorted(entries.values(), key=lambda m: m['id'])
    json.dump(manifest, open(mpath, 'w'), indent=1)


if __name__ == '__main__':
    main(sys.argv[1:])
