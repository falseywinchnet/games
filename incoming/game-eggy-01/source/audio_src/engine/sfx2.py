"""Batch 2 SFX, win stingers and top-score cues for the nine additional games.
python3 sfx2.py -> outputs/sfx/<game>_*.wav, outputs/music/stinger_{win,topscore}_<game>.wav
"""
import os, json
import numpy as np
import synth as S
from synth import SR, tarr, fft_filter, noise, write_wav, render_note
from sfx import nz, env, place_at, verb, finish, chime, card_tick, stinger, lufs, ROOT, OUT, MOUT


def seq(notes, inst, gap, vel=.6, dur=.4, tail=1.5):
    """notes: list of midi (or (midi, t)); returns mono buffer."""
    ev = [(m, i * gap) if not isinstance(m, tuple) else m for i, m in enumerate(notes)]
    y = np.zeros(int((max(t for _, t in ev) + tail + dur) * SR))
    for m, t in ev:
        place_at(y, render_note(inst, m, dur, vel), t)
    return y


def mix(*parts):
    n = max(len(p) + int(o * SR) for p, o, g in parts)
    y = np.zeros(n)
    for p, o, g in parts:
        place_at(y, p * g, o)
    return y


def swoosh(dur, lo, hi, seed, rise=True, amp=.3):
    n = int(dur * SR)
    t = tarr(n) / dur
    shape = np.sin(np.pi * (t ** (0.6 if rise else 1.6))) ** 2
    return nz(n, seed, lo, hi) * shape * amp


def glide(f0, f1, dur, kind='sine', amp=.4, tau=None):
    n = int(dur * SR)
    t = tarr(n)
    f = f0 * (f1 / f0) ** (t / dur)
    y = np.sin(2 * np.pi * np.cumsum(f) / SR) if kind == 'sine' else S.wt(kind, f, 6)
    e = env(n, .004, tau or dur / 3)
    return y * e * amp


# ---------------------------------------------------------------- Gems (E major)
def gems():
    d = {}
    for v in range(3):
        d[f'gems_hover_{v + 1:02d}'] = (verb(render_note('ping', 96 + v * 2, .1, .3), .2, .3), -26)
        a, b = 76 + v, 81 + v
        d[f'gems_swap_{v + 1:02d}'] = (verb(mix((swoosh(.14, 2500, 7000, 10 + v), 0, .4),
                                                 (seq([a, b], 'celesta', .07, .5), 0, .5)), .2, .4), -13)
    for v in range(2):
        d[f'gems_swap_return_{v + 1:02d}'] = (verb(mix((swoosh(.12, 2000, 6000, 20 + v), 0, .3),
                                                        (seq([79 - v, 76 - v, 72 - v], 'marimba', .08, .45), 0, .45)), .15, .3), -15)
    roots = [76, 78, 80, 81, 83, 85]
    for i, r in enumerate(roots[:5]):
        sp = seq([r, r + 4 - (i % 2), r + 7, r + 12], 'celesta', .045, .55, .5) * .5
        mar = seq([r - 12, r - 5], 'marimba', .045, .5) * .35
        d[f'gems_match_{i + 1:02d}'] = (verb(mix((sp, 0, 1), (mar, 0, 1), (swoosh(.25, 5000, 11000, 30 + i), 0, .08)), .3, .7), -11 + i * .5)
    d['gems_powerup_created'] = (verb(seq([88, 92, 95, 100], 'celesta', .05, .5) + 0, .4, .9), -12)
    gl = seq(list(range(76, 101, 2)), 'celesta', .025, .5, .3) * .5
    d['gems_star_line'] = (verb(mix((gl, 0, 1), (swoosh(.45, 3000, 9000, 40), 0, .25)), .35, .9), -9)
    thump = glide(160, 50, .4, amp=.9, tau=.12)
    tink = np.zeros(int(1.6 * SR))
    r = np.random.default_rng(5)
    for k in range(14):
        place_at(tink, render_note('ping', int(r.choice([88, 91, 92, 95, 96, 99])), .1, .35) * .35, .02 + k * .03 + r.uniform(0, .02))
    d['gems_bomb'] = (verb(mix((thump, 0, 1), (nz(int(.3 * SR), 41, 200, 2000) * env(int(.3 * SR), .002, .05), 0, .4),
                               (tink, 0.03, 1)), .35, 1.0), -8)
    sw = np.zeros(int(2.2 * SR))
    for k, m in enumerate([64, 68, 71, 75, 76, 80, 83, 87, 88]):
        place_at(sw, render_note('glass', m, 1.2, .5) * .25, k * .07)
    d['gems_hypercube'] = (verb(mix((sw, 0, 1), (seq([88, 95, 100], 'celesta', .12, .5), .5, .4)), .45, 1.2), -9)
    d['gems_level_up'] = (verb(seq([71, 76, 80, 83, 88], 'vibes', .1, .55, .8), .4, 1.2), -10)
    d['gems_no_moves_shuffle'] = (verb(mix((swoosh(.6, 2000, 8000, 50), 0, .4), (seq([83, 80, 76, 80, 83], 'celesta', .09, .4), .1, .4)), .3, .8), -13)
    return d


# ---------------------------------------------------------------- Sudoku (F lydian, felt)
def sudoku():
    d = {}
    for v in range(3):
        n = int(.05 * SR)
        tickp = nz(n, 60 + v, 2500 + 400 * v, 8000) * env(n, .0005, .006)
        d[f'sudoku_note_place_{v + 1:02d}'] = (verb(tickp, .1, .2), -20)
        d[f'sudoku_digit_place_{v + 1:02d}'] = (verb(render_note('felt', (65, 69, 72)[v], .5, .45), .25, .6), -14)
    n = int(.05 * SR)
    d['sudoku_note_remove'] = (verb(nz(n, 70, 1500, 5000) * env(n, .0005, .005), .1, .2), -22)
    d['sudoku_erase'] = (verb(swoosh(.2, 1500, 6000, 71, False, .4), .1, .2), -20)
    err = mix((render_note('felt', 53, .35, .35), 0, 1), (render_note('felt', 54, .3, .25), .09, .7))
    d['sudoku_error'] = (verb(fft_filter(err, 60, 900, 1), .15, .4), -17)
    for name, notes in (('row', [65, 72, 76]), ('column', [69, 76, 79]), ('box', [65, 69, 72, 76, 79])):
        g = np.zeros(int(2.5 * SR))
        for k, m in enumerate(notes):
            place_at(g, render_note('glass', m + 12, .6, .4) * .3, k * .06)
        d[f'sudoku_{name}_complete'] = (verb(g, .4, 1.0), -15)
    return d


# ---------------------------------------------------------------- Nature cube (D pentatonic)
def nature():
    d = {}
    for v, m in enumerate([86, 88, 90, 93]):
        d[f'nature_cube_trace_{v + 1:02d}'] = (verb(render_note('kalimba', m, .2, .35), .35, .8), -19)
    for v in range(2):
        d[f'nature_cube_connect_{v + 1:02d}'] = (verb(mix((seq([74 + 2 * v, 81 + 2 * v], 'glass', .12, .5, .5), 0, .5),
                                                          (seq([86 + 2 * v], 'kalimba', 0, .4), .12, .4)), .45, 1.2), -13)
    d['nature_cube_disconnect'] = (verb(seq([78, 74], 'pluck', .1, .4, .3), .35, .8), -17)
    d['nature_cube_path_full'] = (verb(seq([74, 78, 81, 86], 'harp', .08, .5, 1.0), .45, 1.2), -12)
    return d


# ---------------------------------------------------------------- Untangle (D)
def untangle():
    d = {}
    for v in range(3):
        d[f'untangle_grab_{v + 1:02d}'] = (verb(mix((render_note('pizz', 69 + 2 * v, .2, .5), 0, 1),
                                                   (card_tick(80 + v, .8), 0, .25)), .15, .3), -16)
        d[f'untangle_release_{v + 1:02d}'] = (verb(render_note('marimba', 62 + 2 * v, .3, .4), .2, .4), -17)
    d['untangle_edges_clear'] = (verb(seq([74, 78, 81], 'marimba', .05, .4, .4), .3, .6), -16)
    y = np.zeros(int(3.2 * SR))
    for k, (a, b) in enumerate([(62, 74), (66, 78), (69, 81), (73, 85), (74, 86)]):
        place_at(y, render_note('marimba', a, .4, .5) * .4, k * .16)
        place_at(y, render_note('pizz', b, .3, .45) * .3, k * .16 + .08)
    for m in (62, 66, 69, 73):
        place_at(y, render_note('glass', m, 1.4, .4) * .18, .8)
    d['untangle_resolve'] = (verb(y, .4, 1.4), -10)
    return d


# ---------------------------------------------------------------- Atom Probe (A minor, retro-sci)
def atom():
    d = {}
    for v in range(2):
        d[f'atom_probe_fire_{v + 1:02d}'] = (verb(mix((glide(600 + 80 * v, 1500 + 120 * v, .18, amp=.5, tau=.08), 0, 1),
                                                     (swoosh(.15, 3000, 8000, 90 + v), 0, .1)), .2, .5), -15)
    d['atom_probe_result_pass'] = (verb(seq([81, 88], 'ping', .09, .5), .3, .6), -15)
    d['atom_probe_result_absorb'] = (verb(mix((glide(700, 220, .3, amp=.6, tau=.12), 0, 1), (render_note('vibes', 57, .3, .4), .05, .3)), .3, .6), -14)
    d['atom_probe_result_reflect'] = (verb(seq([81, 84, 81], 'ping', .07, .5), .3, .6), -15)
    d['atom_probe_result_deflect'] = (verb(seq([81, 86], 'ping', .07, .5) + 0, .3, .6) * 1.0, -15)
    for v in range(2):
        d[f'atom_probe_mark_{v + 1:02d}'] = (verb(mix((render_note('vibes', 76 + 3 * v, .3, .45), 0, 1),
                                                     (render_note('ping', 88 + 3 * v, .1, .3), 0, .3)), .25, .5), -16)
    d['atom_probe_unmark'] = (verb(render_note('vibes', 69, .2, .35), .2, .4), -18)
    d['atom_probe_check_correct'] = (verb(seq([69, 76, 81, 88], 'vibes', .09, .55, .8), .4, 1.2), -11)
    d['atom_probe_check_wrong'] = (verb(mix((render_note('analog', 57, .5, .4), 0, 1), (render_note('analog', 56, .6, .35), .2, 1)), .25, .6), -15)
    return d


# ---------------------------------------------------------------- Four Pegs (C)
def fourpegs():
    d = {}
    for v in range(4):
        m = (67, 70, 72, 75)[v]
        d[f'four_pegs_place_{v + 1:02d}'] = (verb(mix((S.woodblock(.5, v % 2 == 0), 0, .5),
                                                     (render_note('ep', m, .2, .45), .005, .6)), .15, .3), -14)
    d['four_pegs_remove'] = (verb(mix((S.woodblock(.35, False), 0, .4), (render_note('ep', 60, .15, .3), 0, .4)), .1, .2), -18)
    sub = np.zeros(int(1.5 * SR))
    for m in (60, 63, 67, 70, 74):
        place_at(sub, render_note('ep', m, .5, .45) * .25, 0)
    d['four_pegs_submit'] = (verb(mix((sub, 0, 1), (S.brush_swish(.25, .3, 3), 0, .5)), .2, .5), -13)
    d['four_pegs_feedback_exact'] = (verb(mix((render_note('vibes', 84, .3, .5), 0, 1), (S.woodblock(.3, True), 0, .3)), .25, .5), -16)
    d['four_pegs_feedback_partial'] = (verb(mix((render_note('vibes', 79, .3, .4), 0, 1), (S.woodblock(.3, False), 0, .3)), .25, .5), -18)
    d['four_pegs_feedback_none'] = (verb(S.woodblock(.3, False) * .6, .15, .3), -21)
    d['four_pegs_out_of_turns'] = (verb(seq([67, 63, 62, 60], 'ep', .16, .45, .5), .3, .8), -13)
    return d


# ---------------------------------------------------------------- Switchbox (cute)
def switchbox():
    d = {}
    for v in range(3):
        clk = mix((S.tick(.6, v), 0, .6), (card_tick(100 + v, 1.0), 0, .3))
        d[f'switchbox_switch_on_{v + 1:02d}'] = (verb(mix((clk, 0, 1), (glide(700 + 60 * v, 1200 + 90 * v, .09, amp=.35, tau=.05), .01, 1)), .2, .3), -15)
        d[f'switchbox_switch_off_{v + 1:02d}'] = (verb(mix((clk, 0, 1), (glide(1100 + 60 * v, 650 + 50 * v, .09, amp=.35, tau=.05), .01, 1)), .2, .3), -15)
    y = mix((glide(500, 1400, .25, 'flute', .4, .2), 0, 1), (seq([88, 91, 88, 91], 'musicbox', .05, .4, .2), .2, .4),
            (S.woodblock(.3, True), .45, .3))
    d['switchbox_reach'] = (verb(y, .25, .5), -12)
    d['switchbox_reset'] = (verb(seq([93, 90, 88, 85, 81, 78, 76], 'musicbox', .045, .45, .3) * .6, .3, .8), -13)
    d['switchbox_unlock'] = (verb(mix((seq([81, 85, 88, 93], 'celesta', .06, .55, .5), 0, .6), (S.triangle(.15, 4), .2, 1),
                                      (render_note('vibes', 81, .8, .4), .2, .3)), .4, 1.2), -10)
    d['switchbox_giggle_boing'] = (verb(mix((glide(400, 900, .12, 'flute', .4, .08), 0, 1), (glide(900, 500, .12, 'flute', .3, .08), .14, 1),
                                            (glide(500, 1000, .14, 'flute', .3, .1), .28, 1)), .2, .4), -14)
    return d


# ---------------------------------------------------------------- Puzzle Solve (Bb, toy wood)
def puzzlesolve():
    d = {}
    d['puzzle_solve_pickup'] = (verb(mix((S.woodblock(.35, True), 0, .5), (swoosh(.1, 2000, 6000, 120), 0, .3)), .1, .2), -16)
    for v in range(3):
        rat = np.zeros(int(.2 * SR))
        for k in range(3):
            place_at(rat, card_tick(130 + 3 * v + k, .9) * (.5 + .2 * k), k * .03)
        d[f'puzzle_solve_rotate_{v + 1:02d}'] = (verb(mix((rat, 0, .7), (render_note('marimba', 70 + 2 * v, .2, .4), .06, .5)), .1, .25), -15)
    for v in range(2):
        d[f'puzzle_solve_flip_{v + 1:02d}'] = (verb(mix((swoosh(.16, 1200, 5000, 140 + v), 0, .5), (S.woodblock(.4, v == 0), .14, .5)), .12, .3), -15)
    d['puzzle_solve_place'] = (verb(mix((glide(180, 90, .15, amp=.8, tau=.04), 0, 1), (S.woodblock(.3, False), 0, .4)), .1, .2), -15)
    d['puzzle_solve_fit'] = (verb(mix((S.woodblock(.45, True), 0, .5), (seq([70, 77], 'marimba', .06, .5), .03, .5)), .2, .5), -13)
    d['puzzle_solve_no_fit'] = (verb(mix((S.woodblock(.35, False), 0, .5), (S.woodblock(.3, False), .1, .4)), .1, .2), -18)
    return d


# ---------------------------------------------------------------- Sticks & Stones (G)
def sticks():
    d = {}
    for v in range(3):
        d[f'sticks_stones_stick_place_{v + 1:02d}'] = (verb(mix((render_note('logdrum', 55 + 2 * v, .2, .55), 0, 1),
                                                               (card_tick(150 + v, .6), 0, .3)), .1, .3), -14)
        d[f'sticks_stones_stone_place_{v + 1:02d}'] = (verb(mix((render_note('litho', 72 + 3 * v, .2, .5), 0, .6),
                                                               (S.stone_click(.6, v), 0, .6),
                                                               (glide(140, 80, .12, amp=.5, tau=.03), 0, 1)), .1, .3), -13)
    d['sticks_stones_remove'] = (verb(swoosh(.18, 800, 4000, 160, False, .4), .1, .2), -20)
    for v in range(2):
        d[f'sticks_stones_constraint_ok_{v + 1:02d}'] = (verb(seq([79 + 2 * v, 86 + 2 * v], 'litho', .08, .45), .3, .6), -15)
    d['sticks_stones_constraint_broken'] = (verb(render_note('logdrum', 43, .3, .4), .1, .3), -18)
    return d


# ---------------------------------------------------------------- shared top-score flow + new game
def shared():
    d = {}
    for v in range(3):
        d[f'ui_name_key_{v + 1:02d}'] = (verb(mix((S.tick(.5, v), 0, .5), (render_note('vibes', 84 + 2 * v, .1, .3), 0, .4)), .15, .3), -18)
    d['ui_name_backspace'] = (verb(mix((S.tick(.4, 9), 0, .5), (render_note('vibes', 79, .1, .25), 0, .4)), .15, .3), -20)
    d['ui_name_confirm'] = (verb(seq([79, 84, 88], 'vibes', .07, .5, .6), .35, .8), -12)
    d['ui_new_game'] = (verb(mix((swoosh(.35, 1500, 7000, 170), 0, .3), (seq([72, 79], 'vibes', .1, .45, .5), .1, .5)), .3, .7), -14)
    return d


def topscore(lead, comp, key, extra=None, bass='bass'):
    """~3.5 s top-score fanfare: repeated pickup notes, leap to the octave, warm add9 chord."""
    y = np.zeros(int(5.5 * SR))
    b = 60 + key
    for m, t, d in [(b + 7, 0, .1), (b + 7, .12, .1), (b + 7, .24, .1), (b + 12, .4, .3), (b + 14, .75, .15), (b + 16, .9, 1.4)]:
        place_at(y, render_note(lead, m + (12 if lead in ('celesta', 'musicbox', 'marimba', 'kalimba', 'litho') else 0), d, .62) * .3, t)
    for m in (0, 4, 7, 14):
        place_at(y, render_note(comp, b - 12 + m, 2.0, .45) * .17, .9)
    place_at(y, render_note(bass, b - 24 + (12 if key > 6 else 0), 2.0, .6) * .4, .9)
    if extra:
        place_at(y, render_note(extra, b + 28, 1.0, .45) * .18, .9)
    return verb(y, .35, 1.5)


STING = dict(gems=('celesta', 'pad', 4, [4, 7, 11, 14], 'glass'), sudoku=('felt', 'glass', 5, [4, 7, 11, 14], None),
             nature_cube=('flute', 'harp', 2, [4, 7, 14, 16], 'kalimba'), untangle=('marimba', 'pizz', 2, [4, 7, 11, 14], 'glass'),
             atom_probe=('vibes', 'analog', 9, [3, 7, 10, 14], 'ping'), four_pegs=('ep', 'ep', 0, [3, 7, 10, 14], 'vibes'),
             switchbox=('celesta', 'pizz', 9, [4, 7, 11, 14], 'musicbox'), puzzle_solve=('marimba', 'pluck', 10, [4, 7, 12, 16], 'clarinet'),
             sticks_stones=('flute', 'litho', 7, [4, 7, 12, 14], 'logdrum'))


def main():
    rep = {}

    def save(name, x, peak, folder=OUT, stereo=False):
        x = finish(x, peak)
        path = os.path.join(folder, name + '.wav')
        write_wav(path, np.stack([x, x]) if stereo else x)
        l = lufs(path)
        rep[name] = dict(file=os.path.relpath(path, ROOT), seconds=round(len(x) / SR, 3), peak_dbfs=peak,
                         lufs_integrated=l if l is not None and l > -69 else None,
                         max_rms50ms_dbfs=round(20 * np.log10(max(np.sqrt(np.mean(x[i:i + 2400] ** 2))
                                                                  for i in range(0, max(1, len(x) - 2400), 240))), 1),
                         channels=2 if stereo else 1)

    for fn in (gems, sudoku, nature, untangle, atom, fourpegs, switchbox, puzzlesolve, sticks, shared):
        for name, (x, pk) in fn().items():
            save(name, x, pk)
    for g, (lead, comp, key, chord, extra) in STING.items():
        save(f'stinger_win_{g}', stinger(lead, comp, key, chord, extra), -6, MOUT, True)
        save(f'stinger_topscore_{g}', topscore(lead, comp, key, extra, 'tuba' if g == 'sticks_stones' else 'bass'), -6, MOUT, True)
    json.dump(rep, open(os.path.join(ROOT, 'work', 'sfx2_render_report.json'), 'w'), indent=1)
    print(len(rep), 'files')


if __name__ == '__main__':
    main()
