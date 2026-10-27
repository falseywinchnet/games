"""Tumbled Stones' sounds: soft clicks of polished stone on dark wood, and one cheerful loop.

Effects are modal: a stone rings briefly at a few inharmonic frequencies, high and quick;
the tray's wood answers low and woody; a contact adds a sliver of noise. Everything is
drawn from fixed seeds, so the files are the same on every run.

  ts_lift    a stone lifted from its socket: a light tick
  ts_set     a stone set down: a clack of stone on wood
  ts_land    a stone dropping into a socket, with a little bounce
  ts_flip    a stone turned over in the fingers: a rock and a tap
  ts_turn    a stone turned round: a short scrape and a tick
  ts_pull    stones lifted out together: a clink and a soft stone chime
  ts_tumble  the tray juggled: a rattle of stones in the air
  ts_settle  the stones landing after a juggle: a patter that dies away
  ts_deny    a swap refused: a dull thunk
  ts_button  a panel's button
  ts_win     the pull reached: stone chimes up the major chord

The music, "Polishing Day": D major, 104 BPM, a kalimba and lithophone tune over marimba
chords, a log-drum bass and a pebble shaker; 24 bars, a seamless loop (A, B, A with a
counter-line). Major, bright and moving, for an afternoon of play.

Writes 48 kHz WAV into ../assets/audio (effects) and masters/ (the music), and the
manifest. The game ships the music as AAC: afconvert -f m4af -d aac -b 160000.

    python3 games/tumbledstones/audio_src/stones_audio.py
"""
import json
import os
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'engine'))
from synth import SR, render_note, render_drum, make_ir, conv_reverb, fft_filter, LoopMix, noise  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'assets', 'audio')
MASTERS = os.path.join(HERE, 'masters')


def modal(freqs, amps, taus, seconds, phase_seed=1):
    """Damped sinusoids: a struck solid."""
    n = int(seconds * SR)
    t = np.arange(n) / SR
    r = np.random.default_rng(phase_seed)
    y = np.zeros(n)
    for f, a, tau in zip(freqs, amps, taus):
        y += a * np.exp(-t / tau) * np.sin(2 * np.pi * f * t + r.uniform(0, 2 * np.pi))
    return y


def contact(seconds, lo, hi, seed, length=.002):
    """A sliver of filtered noise where two things meet."""
    n = int(seconds * SR)
    k = max(8, int(length * SR))
    burst = np.zeros(n)
    burst[:k] = fft_filter(noise(k + 256, seed), lo, hi)[:k] * np.linspace(1, 0, k) ** 2
    return burst


def stone(seconds, seed, bright=1.0, ring=1.0):
    r = np.random.default_rng(seed)
    base = r.uniform(2300, 3000) * bright
    freqs = [base, base * r.uniform(1.55, 1.7), base * r.uniform(2.3, 2.6)]
    return modal(freqs, [1, .55, .3], [.016 * ring, .009 * ring, .005 * ring], seconds, seed)


def wood(seconds, seed, low=1.0, ring=1.0):
    r = np.random.default_rng(seed + 50)
    base = r.uniform(190, 240) * low
    freqs = [base, base * 2.4, base * 5.3]
    return modal(freqs, [1, .5, .25], [.07 * ring, .035 * ring, .014 * ring], seconds, seed + 50)


def place(dst, y, at, gain=1.0):
    s = int(at * SR)
    e = min(len(dst), s + len(y))
    if s < len(dst):
        dst[s:e] += gain * y[:e - s]


def normal(y, peak=.6):
    m = np.abs(y).max()
    y = y / m * peak if m > 0 else y
    fade = int(.006 * SR)
    y[-fade:] *= np.linspace(1, 0, fade)
    return y


def lift():
    y = .45 * stone(.12, 1, 1.1, .6) + .5 * wood(.12, 1, 1.6, .4) + .3 * contact(.12, 2000, 9000, 1)
    return normal(y, .35)


def set_down(seed=2, bright=1.0):
    y = stone(.3, seed, bright) * .7 + wood(.3, seed) + .5 * contact(.3, 1500, 9000, seed)
    return normal(y, .55)


def land():
    y = np.zeros(int(.32 * SR))
    place(y, set_down(3, 1.08), 0)
    place(y, set_down(4, 1.12), .045, .3)
    return normal(y, .5)


def flip():
    y = np.zeros(int(.34 * SR))
    place(y, normal(stone(.1, 5, 1.2, .5) + .3 * contact(.1, 3000, 9000, 5), .3), 0)
    slide = fft_filter(noise(int(.07 * SR), 6), 1800, 5000) * np.hanning(int(.07 * SR)) * .08
    place(y, slide, .02)
    place(y, set_down(7, 1.05), .09, .7)
    return normal(y, .45)


def turn():
    n = int(.11 * SR)
    grain = 1 + .6 * np.sin(2 * np.pi * 90 * np.arange(n) / SR)
    scrape = fft_filter(noise(n, 8), 1200, 3800) * np.hanning(n) * grain * .25
    y = np.zeros(int(.2 * SR))
    place(y, scrape, 0)
    place(y, normal(stone(.1, 9, 1.15, .5), .25), .1)
    return normal(y, .4)


def pull():
    y = np.zeros(int(.9 * SR))
    clink = modal([3150, 4720, 7350], [1, .6, .3], [.22, .12, .05], .8, 10)
    place(y, clink, 0, .5)
    place(y, modal([3380, 5020], [1, .5], [.16, .08], .7, 11), .035, .35)
    chime = render_note('litho', 86, .5, .6)
    place(y, np.asarray(chime, dtype=float), .01, .5)
    whoosh = fft_filter(noise(int(.25 * SR), 12), 300, 2000) * np.hanning(int(.25 * SR)) * .05
    place(y, whoosh, 0)
    return normal(y, .5)


def clatter(seconds, count, seed, early):
    r = np.random.default_rng(seed)
    y = np.zeros(int(seconds * SR))
    for k in range(count):
        when = (r.beta(1.2, 3.0) if early else r.beta(2.2, 2.2)) * (seconds - .2)
        click = stone(.12, seed * 100 + k, r.uniform(.85, 1.2), .6) * .6 + wood(.12, seed * 100 + k, r.uniform(1.2, 2.2), .3) * .3
        place(y, click, when, r.uniform(.25, 1))
    return y


def tumble():
    y = clatter(1.2, 64, 13, False)
    rumble = fft_filter(noise(len(y), 14), 40, 260) * np.hanning(len(y)) * .25
    return normal(y + rumble, .45)


def settle():
    return normal(clatter(.7, 44, 15, True), .5)


def deny():
    y = wood(.25, 16, .7, 1.2) + .2 * contact(.25, 200, 1500, 16, .004)
    return normal(fft_filter(y, None, 1400), .4)


def button():
    y = wood(.1, 17, 3.5, .35) + .3 * contact(.1, 2000, 8000, 17)
    return normal(y, .3)


def win():
    n = int(2.6 * SR)
    y = np.zeros(n)
    for i, m in enumerate([74, 78, 81, 86, 90]):
        place(y, np.asarray(render_note('litho', m, .6, .75), dtype=float), i * .11)
        place(y, np.asarray(render_note('kalimba', m - 12, .8, .5), dtype=float), i * .11, .5)
    for m in (62, 69, 74, 78, 81):
        place(y, np.asarray(render_note('marimba', m, 1.4, .6), dtype=float), .6, .45)
    send = np.stack([y, y]) * .3
    wet = conv_reverb(send, make_ir(length=1.6), False)[:, :n]
    y = y + wet[0] * .35
    return normal(y, .6)


def write_mono(path, y):
    y16 = (np.clip(y, -1, 1) * 32767).astype('<i2')
    stereo = np.stack([y16, y16], axis=1)
    with wave.open(path, 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(stereo.tobytes())
    print('wrote', os.path.basename(path), f'{len(y) / SR:.2f} s')


# ------------------------------------------------------------------ the music

MAJOR = [0, 2, 4, 5, 7, 9, 11]
TONIC = 74  # D5


def deg(d, tonic=TONIC):
    return tonic + MAJOR[d % 7] + 12 * (d // 7)


A_CHORDS = [0, 4, 5, 3, 0, 4, 3, 4]
B_CHORDS = [3, 4, 2, 5, 3, 4, 1, 4]
A_TUNE = [
    [(0, .5), (2, .5), (4, 1), (2, .5), (4, .5), (5, 1)],
    [(4, .75), (3, .25), (2, .5), (1, .5), (4, 2)],
    [(5, .5), (4, .5), (2, .5), (4, .5), (5, .5), (7, .5), (9, 1)],
    [(8, .5), (7, .5), (5, .5), (4, .5), (3, 1), (1, 1)],
    [(0, .5), (2, .5), (4, 1), (7, .5), (6, .5), (7, 1)],
    [(8, .75), (7, .25), (6, .5), (4, .5), (6, 1), (8, 1)],
    [(7, .5), (5, .5), (3, .5), (5, .5), (7, .5), (5, .5), (3, 1)],
    [(4, .5), (3, .5), (1, .5), (2, .5), (4, 2)],
]
B_TUNE = [
    [(5, 1), (7, .5), (8, .5), (10, 1), (8, 1)],
    [(7, .5), (8, .5), (7, .5), (6, .5), (4, 2)],
    [(4, 1), (6, .5), (7, .5), (9, 1), (7, 1)],
    [(5, .5), (7, .5), (9, .5), (10, .5), (12, 2)],
    [(10, .5), (9, .5), (8, .5), (7, .5), (8, 1), (5, 1)],
    [(7, .5), (6, .5), (4, .5), (6, .5), (8, 2)],
    [(8, .5), (9, .5), (8, .5), (6, .5), (5, 1), (3, 1)],
    [(4, .5), (6, .5), (8, .5), (6, .5), (4, 1), (None, 1)],
]
COUNTER = [[(2, 2), (4, 2)], [(1, 2), (2, 2)], [(3, 2), (5, 2)], [(3, 2), (1, 2)],
           [(2, 2), (4, 2)], [(4, 2), (6, 2)], [(5, 2), (3, 2)], [(1, 2), (2, 2)]]
LEVELS = dict(lead=-16, sparkle=-24, counter=-21, chords=-21, bass=-16, drums=-21)


def note(inst, m, dur, vel):
    return np.asarray(render_note(inst, int(m), dur, vel), dtype=float)


def arrange():
    rng = np.random.default_rng(104)
    bpm = 104
    spb = int(round(SR * 60 / bpm))
    bars = 24
    L = bars * 4 * spb
    mix = LoopMix(L, circular=True)
    form = [('A', i) for i in range(8)] + [('B', i) for i in range(8)] + [('A2', i) for i in range(8)]

    def at(bar, beat):
        return int(round((bar * 4 + beat) * spb))

    for bar, (part, i) in enumerate(form):
        root = (A_CHORDS if part != 'B' else B_CHORDS)[i]
        tune = (A_TUNE if part != 'B' else B_TUNE)[i]
        beat = 0.0
        for d, b in tune:
            if d is not None:
                m = deg(d) - 12
                mix.add('lead', note('kalimba', m, b * spb / SR * .95, .7 + rng.uniform(-.06, .06)), at(bar, beat), -.1)
                if part == 'A2' or (part == 'B' and b >= 1):
                    mix.add('sparkle', note('litho', m + 12, .3, .45), at(bar, beat), .35)
            beat += b
        if part == 'A2':
            beat = 0.0
            for d, b in COUNTER[i]:
                mix.add('counter', note('vibes', deg(d) - 24, b * spb / SR * .95, .5), at(bar, beat), -.4)
                beat += b
        # marimba chords: a bouncing off-beat figure
        chord = [deg(root + k * 2, TONIC - 24) for k in range(3)]
        for b2 in (.5, 1.5, 2, 2.5, 3.5):
            for k, m in enumerate(chord):
                mix.add('chords', note('marimba', m, .25, .48 if b2 % 1 else .38), at(bar, b2) + k * 60, .25)
        # log-drum bass: root on 1, fifth on the and of 2, root on 4, a pickup into each 4th bar
        r = deg(root, TONIC - 36)
        fifth = deg(root + 4, TONIC - 36)
        mix.add('bass', note('logdrum', r, .4, .8), at(bar, 0), 0)
        mix.add('bass', note('logdrum', fifth, .3, .65), at(bar, 1.5), 0)
        mix.add('bass', note('logdrum', r + 12, .3, .6), at(bar, 3), 0)
        if bar % 4 == 3:
            mix.add('bass', note('logdrum', fifth, .2, .6), at(bar, 3.5), 0)
        # a pebble shaker and stone clicks for percussion
        for b2 in range(8):
            mix.add('drums', render_drum('shaker', .32 if b2 % 2 else .2, bar * 8 + b2), at(bar, b2 / 2), .3)
        mix.add('drums', render_drum('logkick', .55, bar), at(bar, 0), 0)
        mix.add('drums', render_drum('logkick', .4, bar + 1), at(bar, 2.5), 0)
        for b2 in (1, 3):
            mix.add('drums', render_drum('stone', .5, bar * 2 + b2), at(bar, b2), -.25)
        if bar % 2 == 1:
            mix.add('drums', render_drum('tick', .35, bar), at(bar, 3.75), .4)
    return mix, bpm, L, spb


def bus_rms_db(b):
    m = b.mean(0)
    r = np.sqrt(np.mean(m ** 2))
    return 20 * np.log10(max(r, 1e-9))


def master(mix, L):
    out = np.zeros((2, L))
    send = np.zeros((2, L))
    for name, b in mix.buses.items():
        g = 10 ** ((LEVELS.get(name, -20) - bus_rms_db(b)) / 20)
        b = b * g
        out += b
        send += b * {'lead': .25, 'sparkle': .4, 'counter': .35, 'chords': .2, 'drums': .08, 'bass': .04}.get(name, .1)
    out += conv_reverb(send, make_ir(length=1.6, rt_low=1.2, rt_mid=1.0, rt_high=.5), True) * .22
    out = np.stack([fft_filter(c, 35, None, 2, circular=True) for c in out])
    x = out * 10 ** ((-19 - bus_rms_db(out)) / 20)
    c = 10 ** (-3 / 20)
    return np.tanh(x / c) * c


def write_stereo(path, x):
    y16 = (np.clip(x, -1, 1).T * 32767).astype('<i2')
    with wave.open(path, 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(y16.tobytes())
    print('wrote', os.path.basename(path), f'{x.shape[1] / SR:.2f} s')


def main():
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(MASTERS, exist_ok=True)
    effects = dict(ts_lift=lift, ts_set=set_down, ts_land=land, ts_flip=flip, ts_turn=turn, ts_pull=pull,
                   ts_tumble=tumble, ts_settle=settle, ts_deny=deny, ts_button=button, ts_win=win)
    for name, make in effects.items():
        write_mono(os.path.join(OUT, name + '.wav'), make())
    if len(sys.argv) > 1 and sys.argv[1] == 'effects':
        return
    mix, bpm, L, spb = arrange()
    write_stereo(os.path.join(MASTERS, 'ts_music.wav'), master(mix, L))
    manifest = {'music': [{'id': 'ts_music', 'bpm': bpm, 'bars': 24, 'bar_samples': 4 * spb,
                           'loop_end_sample_exclusive': L, 'seconds': round(L / SR, 3)}]}
    with open(os.path.join(OUT, 'stones_audio_manifest.json'), 'w') as f:
        json.dump(manifest, f, indent=1)
        f.write('\n')


if __name__ == '__main__':
    main()
