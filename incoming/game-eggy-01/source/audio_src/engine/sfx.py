"""Synthesised SFX + win stingers for the card games and the block-pushing puzzle.
python3 sfx.py   -> outputs/sfx/*.wav (48 kHz mono 24-bit) and outputs/music/stinger_*.wav
"""
import os, json, re, subprocess
import numpy as np
import synth as S
from synth import SR, tarr, fft_filter, noise, gate, write_wav, render_note
from compose import make_ir, conv_reverb

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(ROOT, 'outputs', 'sfx')
MOUT = os.path.join(ROOT, 'outputs', 'music')
IR = make_ir(1.6, 1.2, 1.0, 0.5, 0.01, seed=11)


def nz(n, seed, lo, hi, order=2):
    x = fft_filter(noise(n, seed), lo, hi, order)
    return x / (np.abs(x).max() + 1e-12)


def env(n, att, tau):
    t = tarr(n)
    return (1 - np.exp(-t / max(att, 1e-4))) * np.exp(-t / tau)


def place_at(buf, y, sec):
    s = int(sec * SR)
    e = min(len(buf), s + len(y))
    buf[s:e] += y[:e - s]
    return buf


def verb(x, amt=0.15, tail=0.6):
    st = np.stack([x, x])
    st = np.pad(st, ((0, 0), (0, int(tail * SR))))
    w = conv_reverb(st, IR, circular=False)[:, :st.shape[1]]
    return (st[0] + amt * w.mean(0))


def finish(x, peak_db, fade=0.01):
    x = x - np.mean(x)
    k = int(fade * SR)
    x[-k:] *= np.linspace(1, 0, k) ** 2
    x[:16] *= np.linspace(0, 1, 16)
    # trim trailing silence below -70 dB
    thr = 10 ** (-70 / 20) * np.abs(x).max()
    last = np.nonzero(np.abs(x) > thr)[0][-1]
    x = x[:last + k]
    x[-k:] *= np.linspace(1, 0, k)
    return x / np.abs(x).max() * 10 ** (peak_db / 20)


# ----------------------------------------------------------------- card sounds
def card_tick(seed, bright=1.0):
    n = int(0.03 * SR)
    return nz(n, seed, 1800 * bright, 7000 * bright) * env(n, 0.0004, 0.004)


def card_body(seed, f=170):
    n = int(0.12 * SR)
    t = tarr(n)
    return 0.6 * nz(n, seed, 120, 900) * env(n, 0.001, 0.018) + 0.5 * np.sin(2 * np.pi * f * t) * env(n, 0.001, 0.022)


def pickup(v):
    n = int(0.16 * SR)
    x = nz(n, 10 + v, 1500, 6500) * env(n, 0.018, 0.035) * 0.6
    x = place_at(x, card_tick(20 + v, 1.2) * 0.5, 0.0)
    return verb(x, 0.06, 0.15)


def place(v):
    n = int(0.25 * SR)
    x = np.zeros(n)
    place_at(x, nz(int(.05 * SR), 30 + v, 1200, 5000) * env(int(.05 * SR), 0.004, 0.012) * 0.3, 0)
    place_at(x, card_tick(40 + v) * 0.8, 0.012)
    place_at(x, card_body(50 + v, 150 + 12 * v), 0.012)
    return verb(x, 0.08, 0.2)


def deal(v):
    n = int(0.14 * SR)
    t = tarr(n)
    sw = nz(n, 60 + v, 2200, 8000) * np.sin(np.pi * np.clip(t / 0.07, 0, 1)) ** 2 * 0.5
    x = place_at(sw, card_tick(70 + v) * 0.7 + 0 * sw[:1440], 0.065)
    place_at(x, card_body(80 + v, 190)[:int(.06 * SR)] * 0.35, 0.065)
    return verb(x, 0.06, 0.15)


def flip(v):
    n = int(0.16 * SR)
    x = np.zeros(n)
    place_at(x, nz(int(.06 * SR), 90 + v, 1500, 7000) * env(int(.06 * SR), .01, .015) * .35, 0)
    place_at(x, card_tick(100 + v, 1.1) * 0.7, 0.02)
    place_at(x, card_tick(110 + v, 0.8) * 0.9, 0.045 + 0.004 * v)
    return verb(x, 0.06, 0.15)


def shuffle(v):
    dur = 1.15
    n = int(dur * SR)
    x = np.zeros(n)
    r = np.random.default_rng(200 + v)
    t = 0.05
    while t < 0.9:
        gap = 0.032 * (1 - 0.55 * t / 0.9) * r.uniform(0.7, 1.3)
        place_at(x, card_tick(int(r.integers(1e6)), r.uniform(.7, 1.2)) * r.uniform(.35, .7), t)
        t += gap
    tt = tarr(n)
    x += nz(n, 210 + v, 1500, 6000) * np.exp(-((tt - 0.5) / 0.3) ** 2) * 0.12
    place_at(x, card_body(220 + v, 140) * 0.8, 0.95)
    place_at(x, card_tick(230 + v) * 0.6, 0.95)
    return verb(x, 0.08, 0.3)


def chime(notes, inst='marimba', vel=0.6, gap=0.07, dur=0.5):
    n = int((gap * len(notes) + 2.0) * SR)
    x = np.zeros(n)
    for i, m in enumerate(notes):
        place_at(x, render_note(inst, m, dur, vel), i * gap)
    return x


def foundation(v):
    roots = [72, 74, 76, 79]
    x = place(v) * 0.8
    c = chime([roots[v], roots[v] + 7], 'vibes', 0.5, 0.06, 0.4) * 0.35
    y = np.zeros(max(len(x), len(c) + int(0.02 * SR)))
    y[:len(x)] += x
    place_at(y, c, 0.02)
    return verb(y, 0.2, 0.5)


def undo(v):
    n = int(0.3 * SR)
    t = tarr(n)
    sw = nz(n, 300 + v, 1500, 6000) * (t / t[-1]) ** 2 * np.exp(-np.maximum(t - 0.22, 0) / 0.01) * 0.4
    c = chime([76 - 2 * v, 71 - 2 * v], 'vibes', 0.4, 0.07, 0.3) * 0.35
    y = np.zeros(len(c) + n)
    y[:n] += sw
    place_at(y, c, 0.18)
    return verb(y, 0.2, 0.4)


def hint(v):
    seq = [[84, 88, 91], [86, 91, 95]][v]
    return verb(chime(seq, 'celesta', 0.45, 0.08) * 0.5, 0.3, 0.8)


def invalid(v):
    seq = [[55, 52], [53, 50]][v]
    x = chime(seq, 'marimba', 0.45, 0.11, 0.3)
    x = fft_filter(x, 80, 1400, 1)
    return verb(x, 0.1, 0.3)


def victory():
    seq = [72, 76, 79, 84, 88, 91, 96]
    x = chime(seq, 'celesta', 0.55, 0.075) * 0.5
    v = chime([72, 76, 79, 84], 'vibes', 0.5, 0.0, 1.8) * 0.3
    y = np.zeros(len(x) + SR)
    y[:len(x)] += x
    place_at(y, v, 0.53)
    place_at(y, S.triangle(0.15, 1), 0.53)
    return verb(y, 0.35, 1.5)


# ----------------------------------------------------------------- puzzle sounds
def walk(v):
    n = int(0.14 * SR)
    t = tarr(n)
    pat = nz(n, 400 + v, 90, 700) * env(n, 0.002, 0.022)
    sq_f = 1300 * 2 ** ((v - 1.5) / 12) * (1 + 0.15 * np.exp(-t / 0.02))
    sq = np.sin(2 * np.pi * np.cumsum(sq_f) / SR) * env(n, 0.003, 0.018) * 0.12
    thud = np.sin(2 * np.pi * (110 + 8 * v) * t) * env(n, 0.002, 0.03) * 0.6
    return verb(pat * 0.7 + thud + sq, 0.05, 0.12)


def push(v):
    dur = 0.42
    n = int(dur * SR)
    t = tarr(n)
    r = np.random.default_rng(500 + v)
    rough = np.repeat(r.uniform(0.3, 1, n // 240 + 1), 240)[:n]
    rough = fft_filter(rough, None, 60, 1)
    shape = np.sin(np.pi * np.clip(t / dur, 0, 1)) ** 0.7
    scrape = nz(n, 510 + v, 180, 1400) * rough * shape * 0.55
    creak_f = 330 * 2 ** (v / 12) * (1 + 0.25 * t / dur)
    creak = S.wt('reed', creak_f, 10) * shape * 0.08
    y = np.zeros(n + int(0.2 * SR))
    y[:n] += scrape + creak
    k = int(0.15 * SR)
    tk = tarr(k)
    thunk = np.sin(2 * np.pi * 95 * tk) * env(k, 0.002, 0.04) + 0.4 * nz(k, 520 + v, 100, 1200) * env(k, 0.001, 0.015)
    place_at(y, thunk * 0.9, dur - 0.04)
    return verb(y, 0.1, 0.25)


def blocked(v):
    n = int(0.6 * SR)
    t = tarr(n)
    bonk_f = (240 - 30 * v) * (1 + 0.6 * np.exp(-t / 0.015))
    bonk = np.sin(2 * np.pi * np.cumsum(bonk_f) / SR) * env(n, 0.001, 0.06)
    spring_f = 520 * (1 + 0.06 * np.sin(2 * np.pi * 13 * t) * np.exp(-t / 0.2)) * (1 - 0.15 * t)
    spring = np.sin(2 * np.pi * np.cumsum(spring_f) / SR) * env(n, 0.01, 0.12) * 0.25
    wood = S.woodblock(0.5, False) * 0.5
    y = bonk * 0.8 + spring
    place_at(y, wood, 0)
    return verb(y, 0.1, 0.25)


def on_target(v):
    seq = [[79, 83, 86], [81, 85, 88]][v]
    x = chime(seq, 'marimba', 0.6, 0.06) * 0.6
    place_at(x, chime([seq[-1] + 12], 'celesta', 0.4) * 0.3, 0.12)
    return verb(x, 0.25, 0.6)


def puzzle_fail():
    seq = [(67, .35), (66, .35), (65, .35), (64, 1.1)]
    y = np.zeros(int(3.0 * SR))
    t0 = 0
    for i, (m, d) in enumerate(seq):
        n = int((d + .1) * SR)
        tt = tarr(n)
        f = S.mtof(m) * (1 - 0.03 * (tt / d if i == 3 else 0)) * (1 + (0.012 * np.sin(2 * np.pi * 6 * tt) if i == 3 else 0))
        x = S.wt('reed', f, 12) * gate(n, d, 0.02, 0.1)
        x = fft_filter(x, 120, 1800 + 600 * np.cos(i), 1)
        place_at(y, x * 0.5, t0)
        place_at(y, render_note('bass', m - 24, d * 0.9, 0.6) * 0.6, t0)
        t0 += d
    return verb(y, 0.2, 0.8)


def puzzle_win():
    y = np.zeros(int(3.5 * SR))
    mel = [(67, 0, .14), (71, .15, .14), (74, .3, .14), (79, .45, .5), (78, .95, .12), (79, 1.1, 1.2)]
    for m, t, d in mel:
        place_at(y, render_note('reed', m, d, .7) * 0.35, t)
        place_at(y, render_note('marimba', m + 12, d, .6) * 0.3, t)
    for m in (55, 59, 62, 67):
        place_at(y, render_note('pluck', m, 1.2, .55) * 0.25, 1.1)
    place_at(y, render_note('bass', 43, 1.2, .7) * 0.5, 1.1)
    place_at(y, render_note('bass', 50, .3, .6) * 0.5, 0.45)
    place_at(y, S.triangle(0.2, 2), 1.1)
    return verb(y, 0.3, 1.2)


# ----------------------------------------------------------------- stingers
def stinger(lead, comp, key, chord, extra=None):
    """~4 s win cue: rising lead flourish then a warm held tonic chord."""
    y = np.zeros(int(6 * SR))
    base = 60 + key
    run = [base + x for x in (0, 4, 7, 12, 11, 12, 16, 19)]
    times = [0, .1, .2, .3, .45, .55, .7, .9]
    for i, (m, t) in enumerate(zip(run, times)):
        place_at(y, render_note(lead, m + (12 if lead in ('celesta', 'vibes', 'marimba') else 0),
                                1.4 if i == len(run) - 1 else .14, .65) * 0.3, t)
    for m in chord:
        place_at(y, render_note(comp, base - 12 + m, 2.2, .5) * 0.18, 0.9)
    place_at(y, render_note('bass', base - 24 + (12 if key > 6 else 0), 2.2, .7) * 0.45, 0.9)
    place_at(y, S.triangle(0.18, 3), 0.9)
    if extra:
        place_at(y, render_note(extra, base + 24, 1.5, .5) * 0.2, 0.9)
    return verb(y, 0.35, 1.5)


def lufs(path):
    r = subprocess.run(['ffmpeg', '-hide_banner', '-nostats', '-i', path, '-af', 'ebur128=peak=true', '-f', 'null', '-'],
                       capture_output=True, text=True).stderr
    try:
        i = float(re.findall(r'I:\s+(-?[\d.]+) LUFS', r)[-1])
    except Exception:
        i = None
    return i


def main():
    os.makedirs(OUT, exist_ok=True)
    rep = {}

    def save(name, x, peak, folder=OUT, stereo=False):
        x = finish(x, peak)
        path = os.path.join(folder, name + '.wav')
        write_wav(path, np.stack([x, x]) if stereo else x)
        rep[name] = dict(file=os.path.relpath(path, ROOT), seconds=round(len(x) / SR, 3), peak_dbfs=peak,
                         lufs_integrated=(lambda l: l if l is not None and l > -69 else None)(lufs(path)),
                         max_rms50ms_dbfs=round(20 * np.log10(max(np.sqrt(np.mean(x[i:i + 2400] ** 2)) for i in range(0, max(1, len(x) - 2400), 240))), 1),
                         channels=2 if stereo else 1)

    for v in range(4):
        save(f'card_pickup_{v + 1:02d}', pickup(v), -11)
        save(f'card_place_{v + 1:02d}', place(v), -11)
        save(f'card_deal_{v + 1:02d}', deal(v), -11)
        save(f'card_flip_{v + 1:02d}', flip(v), -9)
        save(f'card_foundation_{v + 1:02d}', foundation(v), -9)
    for v in range(2):
        save(f'card_shuffle_{v + 1:02d}', shuffle(v), -11)
        save(f'ui_undo_{v + 1:02d}', undo(v), -12)
        save(f'ui_hint_{v + 1:02d}', hint(v), -12)
        save(f'ui_invalid_{v + 1:02d}', invalid(v), -14)
    save('ui_victory', victory(), -7)
    for v in range(4):
        save(f'puzzle_walk_{v + 1:02d}', walk(v), -15)
    for v in range(3):
        save(f'puzzle_push_{v + 1:02d}', push(v), -10)
    for v in range(2):
        save(f'puzzle_blocked_{v + 1:02d}', blocked(v), -11)
        save(f'puzzle_on_target_{v + 1:02d}', on_target(v), -10)
    save('puzzle_undo_01', undo(0), -12)
    save('puzzle_win', puzzle_win(), -7)
    save('puzzle_fail', puzzle_fail(), -8)
    # music stingers (stereo-duplicated mono, -6 dBFS peak)
    st = dict(menu=('celesta', 'ep', 0, [4, 7, 11, 14], 'vibes'), klondike=('marimba', 'ep', 7, [4, 7, 11, 14], None),
              spider=('clarinet', 'pizz', 9, [3, 7, 10, 14], 'marimba'), freecell=('vibes', 'pluck', 2, [4, 7, 11, 14], 'flute'),
              hearts=('flute', 'harp', 5, [4, 7, 12, 16], 'celesta'), puzzle=('reed', 'pluck', 7, [4, 7, 12, 16], 'marimba'))
    for k, (lead, comp, key, chord, extra) in st.items():
        save(f'stinger_win_{k}', stinger(lead, comp, key, chord, extra), -6, MOUT, stereo=True)
    json.dump(rep, open(os.path.join(ROOT, 'work', 'sfx_render_report.json'), 'w'), indent=1)
    for k, v in rep.items():
        print(k, v['seconds'], v['lufs_integrated'], v['max_rms50ms_dbfs'])


if __name__ == '__main__':
    main()
