"""Maze 95 effects. Synthesized, no samples.

Soft steps on carpet, a turn's swish, a bump into brick, a locked door's
clunk, a pad's click, a door grinding down into the floor, the flip stone's
great roll, a bulb popping out and humming back, the elevator's motor and
its ding, a portal's shimmer, the marble rumbling, knocking and thudding,
someone appearing and vanishing, and the click of a 1995 button. Writes
48 kHz 16-bit mono WAVs into ../assets/audio.
"""
import os
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'engine'))
from synth import SR, tarr, noise, fft_filter, mtof  # noqa: E402

OUT = os.path.join(os.path.dirname(__file__), '..', 'assets', 'audio')
rng = np.random.default_rng(95)


def write(name, x, peak_db):
    x = np.asarray(x, dtype=float)
    x = x / (np.max(np.abs(x)) + 1e-12) * 10 ** (peak_db / 20)
    fade = min(len(x), int(.005 * SR))
    x[-fade:] *= np.linspace(1, 0, fade)
    x[:8] *= np.linspace(0, 1, 8)
    with wave.open(os.path.join(OUT, name + '.wav'), 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes((x * 32767).astype('<i2').tobytes())


def sweep(f0, f1, dur, curve=1.0):
    n = int(dur * SR)
    u = np.linspace(0, 1, n) ** curve
    return np.sin(2 * np.pi * np.cumsum(f0 * (f1 / f0) ** u) / SR)


def env(n, att, tau):
    t = tarr(n)
    return np.minimum(1, t / max(att, 1e-6)) * np.exp(-t / tau)


def chime(freqs, dur, tau):
    n = int(dur * SR)
    t = tarr(n)
    return sum(a * np.sin(2 * np.pi * f * t) * np.exp(-t / tau) for f, a in freqs) * np.minimum(1, t / .002)


def main():
    os.makedirs(OUT, exist_ok=True)
    for i in range(3):  # carpet steps: a muffled pad of a footfall
        n = int(.16 * SR)
        t = tarr(n)
        write(f'mz_step_0{i + 1}', fft_filter(noise(n), 80, 700 + 120 * i) * env(n, .008, .035) + .5 * np.sin(2 * np.pi * (70 + 8 * i) * t) * env(n, .004, .04), -19)
    n = int(.22 * SR)
    t = tarr(n)
    write('mz_turn', fft_filter(noise(n), 300, 2500) * np.sin(np.pi * t / .22) ** 2, -26)
    n = int(.25 * SR)
    t = tarr(n)
    write('mz_bump', np.sin(2 * np.pi * (110 + 60 * np.exp(-t / .02)) * t) * env(n, .002, .06) + .3 * fft_filter(noise(n), 200, 2000) * env(n, .001, .02), -15)
    # locked: a heavy clunk and a short buzz
    n = int(.45 * SR)
    t = tarr(n)
    clunk = np.sin(2 * np.pi * 90 * t) * env(n, .002, .08) + .5 * fft_filter(noise(n), 400, 3000) * env(n, .001, .015)
    buzz = np.sign(np.sin(2 * np.pi * 110 * t)) * env(n, .15, .08) * (t > .15)
    write('mz_locked', clunk + fft_filter(buzz, 80, 1600) * .4, -13)
    # a pad pressed: a click and a bright two-note chime
    n = int(.6 * SR)
    t = tarr(n)
    click = fft_filter(noise(n), 2000, 9000) * env(n, .0005, .004)
    write('mz_pad', click + chime([(1318.5, 1), (1975.5, .6)], .6, .25), -14)
    # a door grinding down into the floor
    n = int(1.5 * SR)
    t = tarr(n)
    grind = fft_filter(noise(n), 60, 600) * (1 + .5 * np.sin(2 * np.pi * 11 * t)) * np.minimum(1, t / .1) * np.exp(-np.maximum(0, t - 1.2) / .08)
    motor = np.sign(np.sin(2 * np.pi * np.cumsum(55 - 15 * t / 1.5) / SR)) * .15
    thud = np.zeros(n)
    k = int(1.28 * SR)
    tt = tarr(n - k)
    thud[k:] = np.sin(2 * np.pi * 60 * tt) * np.exp(-tt / .08)
    write('mz_door', grind + fft_filter(motor, 40, 400) + thud, -12)
    # the flip stone: the world rolling over
    n = int(1.3 * SR)
    t = tarr(n)
    whoosh = fft_filter(noise(n), 200, 4000) * np.sin(np.pi * np.clip(t / 1.2, 0, 1)) ** 2
    roll = sweep(80, 320, 1.3, .7) * np.sin(np.pi * np.clip(t / 1.3, 0, 1)) * .5
    write('mz_flip', whoosh + roll + chime([(mtof(85), .4), (mtof(92), .3)], 1.3, .5) * (t > .9), -12)
    # bulbs: pop and fizz out, hum back on
    n = int(.6 * SR)
    t = tarr(n)
    pop = fft_filter(noise(n), 1000, 9000) * env(n, .0005, .01)
    fizz = fft_filter(noise(n), 3000, 9000) * env(n, .01, .12) * (1 + np.sign(np.sin(2 * np.pi * 47 * t))) * .2
    hum = np.sin(2 * np.pi * 120 * t) * np.exp(-t / .2) * .3
    write('mz_bulb_off', pop + fizz + hum * (t < .1), -12)
    n = int(.7 * SR)
    t = tarr(n)
    write('mz_bulb_on', (np.sin(2 * np.pi * 120 * t) + .5 * np.sin(2 * np.pi * 240 * t)) * np.minimum(1, t / .4) * np.exp(-np.maximum(0, t - .5) / .1) * .5
          + fft_filter(noise(n), 2000, 6000) * env(n, .001, .02) * .3, -16)
    # the elevator: a motor winding up, then the ding
    n = int(2.6 * SR)
    t = tarr(n)
    motor = np.sin(2 * np.pi * np.cumsum(90 + 50 * np.minimum(1, t / 2.2)) / SR) * np.minimum(1, t / .3) * np.exp(-np.maximum(0, t - 2.2) / .1)
    rattle = fft_filter(noise(n), 100, 900) * .3 * np.minimum(1, t / .3) * np.exp(-np.maximum(0, t - 2.2) / .1)
    ding = np.zeros(n)
    k = int(2.3 * SR)
    ding[k:] = chime([(mtof(84), 1), (mtof(88), .5)], (n - k) / SR, .4)[:n - k]
    write('mz_elevator', motor * .6 + rattle + ding * .8, -12)
    # a portal: a shimmer that falls and rises
    n = int(.8 * SR)
    t = tarr(n)
    shim = sum(np.sin(2 * np.pi * f * t * (1 + .3 * np.sin(2 * np.pi * 3 * t))) for f in (880, 1320, 1760)) * env(n, .01, .3)
    write('mz_portal', shim + fft_filter(noise(n), 3000, 10000) * env(n, .001, .05) * .5, -13)
    # the marble: a rumble as it rolls a square, a knock off a wall, a thud face to face
    n = int(.75 * SR)
    t = tarr(n)
    write('mz_marble_roll', fft_filter(noise(n), 30, 220) * np.sin(np.pi * t / .75) * (1 + .3 * np.sin(2 * np.pi * 9 * t)), -12)
    n = int(.4 * SR)
    t = tarr(n)
    write('mz_marble_bounce', np.sin(2 * np.pi * (140 + 80 * np.exp(-t / .01)) * t) * env(n, .001, .07) + .3 * fft_filter(noise(n), 500, 4000) * env(n, .0005, .01), -14)
    n = int(.7 * SR)
    t = tarr(n)
    write('mz_marble_bump', np.sin(2 * np.pi * (45 + 40 * np.exp(-t / .03)) * t) * env(n, .002, .25) + .3 * fft_filter(noise(n), 60, 400) * env(n, .002, .1), -10)
    # someone appears (a sparkle), and vanishes (a poof)
    n = int(.6 * SR)
    t = tarr(n)
    sparkle = sum(chime([(mtof(m), .5)], .6, .15) * 0 + np.roll(chime([(mtof(m), .5)], .6, .15), int(i * .05 * SR)) for i, m in enumerate((88, 91, 95, 100)))
    write('mz_appear', sparkle, -16)
    n = int(.4 * SR)
    t = tarr(n)
    write('mz_poof', fft_filter(noise(n), 300, 3000) * env(n, .005, .08) + .3 * sweep(600, 200, .4, 1) * env(n, .001, .1), -17)
    n = int(.05 * SR)
    t = tarr(n)
    write('mz_click', np.sin(2 * np.pi * 2200 * t) * np.exp(-t / .006), -22)
    print('ok')


if __name__ == '__main__':
    main()
