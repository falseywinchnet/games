"""Catching Thieves effects. Synthesized, no samples.

Soft paw steps on soil, a pumpkin scraping along with a little grunt, the
hollow thunk of a pumpkin dropping onto a burrow, raccoons popping up and
ducking down, muffled grumbling from underneath, a raspberry, a carrot crunch,
a chittering laugh, a bump into the hedge, undo, start over, a hint and a
click. Writes 48 kHz 16-bit mono WAVs into ../assets/audio.
"""
import os
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'engine'))
from synth import SR, tarr, noise, fft_filter  # noqa: E402

OUT = os.path.join(os.path.dirname(__file__), '..', 'assets', 'audio')
rng = np.random.default_rng(1999)


def write(name, x, peak_db):
    x = np.asarray(x, dtype=float)
    x = x / (np.max(np.abs(x)) + 1e-12) * 10 ** (peak_db / 20)
    fade = min(len(x), int(.004 * SR))
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
    f = f0 * (f1 / f0) ** u
    return np.sin(2 * np.pi * np.cumsum(f) / SR)


def chirp_voice(f0, f1, dur, formant=1400, rough=0.0):
    """A small animal voice: a pulse-ish tone gliding f0 -> f1 through one bright formant."""
    n = int(dur * SR)
    t = tarr(n)
    f = f0 * (f1 / f0) ** (t / dur)
    ph = 2 * np.pi * np.cumsum(f * (1 + rough * rng.normal(0, 1, n) * .02)) / SR
    y = np.sign(np.sin(ph)) * .4 + np.sin(ph) * .6
    y = fft_filter(y, formant * .5, formant * 2.2, 2)
    return y * np.sin(np.pi * np.clip(t / dur, 0, 1)) ** .6


def main():
    os.makedirs(OUT, exist_ok=True)
    # steps on soft soil
    for i in range(3):
        n = int(.12 * SR)
        t = tarr(n)
        thud = np.sin(2 * np.pi * (90 + 30 * i) * t) * np.exp(-t / .03)
        grit = fft_filter(noise(n), 600, 4000) * np.exp(-t / .02) * .5
        write(f'ct_step_{i + 1:02d}', thud + grit, -20)
    # pushing a pumpkin: a scrape across the soil and a small "hup"
    for i in range(3):
        n = int(.32 * SR)
        t = tarr(n)
        scrape = fft_filter(noise(n), 300 + 80 * i, 2200) * np.sin(np.pi * np.clip(t / .3, 0, 1)) * .6
        hup = chirp_voice(240 + 25 * i, 200 + 20 * i, .12, 900) * .7
        y = scrape.copy()
        y[:len(hup)] += hup
        write(f'ct_push_{i + 1:02d}', y, -15)
    # a pumpkin drops onto a burrow: a hollow thunk, a puff of soil, a tiny "eep"
    n = int(.6 * SR)
    t = tarr(n)
    thunk = np.sin(2 * np.pi * (70 + 80 * np.exp(-t / .03)) * t) * np.exp(-t / .12) + .5 * np.sin(2 * np.pi * 180 * t) * np.exp(-t / .05)
    puff = fft_filter(noise(n), 400, 3000) * np.exp(-t / .06) * .4
    eep = np.zeros(n)
    e = chirp_voice(900, 1300, .1, 2200)
    s = int(.12 * SR)
    eep[s:s + len(e)] = e * .35
    write('ct_trap', thunk + puff + eep, -10)
    # a raccoon pops up, and ducks back down
    write('ct_pop', np.concatenate([sweep(300, 900, .09, .6) * np.exp(-tarr(int(.09 * SR)) / .05), np.zeros(int(.02 * SR))]), -16)
    write('ct_duck', sweep(900, 260, .12, 1.4) * np.exp(-tarr(int(.12 * SR)) / .06), -18)
    # muffled grumbling from under a pumpkin
    for i in range(2):
        parts = []
        for k in range(3 + i):
            parts.append(chirp_voice(rng.uniform(200, 320), rng.uniform(160, 280), rng.uniform(.08, .16), 500, rough=1))
            parts.append(np.zeros(int(rng.uniform(.02, .06) * SR)))
        y = fft_filter(np.concatenate(parts), 80, 900, 2)  # under a pumpkin: everything dull
        write(f'ct_muffle_{i + 1:02d}', y, -17)
    # a raspberry: a buzzing tongue
    n = int(.55 * SR)
    t = tarr(n)
    f = 95 + 25 * np.sin(2 * np.pi * 7 * t)
    buzz = np.sign(np.sin(2 * np.pi * np.cumsum(f) / SR)) * (1 + .4 * np.sin(2 * np.pi * 31 * t))
    buzz = fft_filter(buzz, 120, 2400, 2) + fft_filter(noise(n), 800, 4000) * .2
    write('ct_raspberry', buzz * np.minimum(1, t / .03) * np.exp(-np.maximum(0, t - .35) / .06), -14)
    # a carrot crunch
    n = int(.18 * SR)
    t = tarr(n)
    crunch = fft_filter(noise(n), 1500, 9000) * (np.exp(-t / .02) + .6 * np.exp(-np.maximum(0, t - .05) / .015) * (t > .05) + .4 * np.exp(-np.maximum(0, t - .1) / .015) * (t > .1))
    write('ct_crunch', crunch, -18)
    # the raccoons' laugh: a quick run of chittering "hee"s, falling
    parts = []
    for k in range(6):
        f0 = 1200 - k * 70 + rng.uniform(-40, 40)
        parts.append(chirp_voice(f0, f0 * .82, .075, 2600))
        parts.append(np.zeros(int(.035 * SR)))
    write('ct_laugh', np.concatenate(parts), -15)
    # walking into a hedge: a leafy rustle and a soft bonk
    n = int(.25 * SR)
    t = tarr(n)
    write('ct_bump', fft_filter(noise(n), 1500, 7000) * np.exp(-t / .06) * .5 + np.sin(2 * np.pi * 150 * t) * np.exp(-t / .04), -20)
    # undo: a soft backwards swish; start over: a quick shuffle of soil
    write('ct_undo', sweep(1400, 700, .12, 1) * np.exp(-tarr(int(.12 * SR)) / .05) * .6 + fft_filter(noise(int(.12 * SR)), 2000, 6000) * .2, -22)
    n = int(.4 * SR)
    t = tarr(n)
    write('ct_restart', fft_filter(noise(n), 300, 3000) * np.sin(np.pi * t / .4) ** 2 + .4 * sweep(500, 250, .4, 1), -17)
    # a hint: a little chime; a click
    n = int(.6 * SR)
    t = tarr(n)
    chime = sum(a * np.sin(2 * np.pi * f * t) * np.exp(-t / tau) for f, a, tau in ((1568, 1, .25), (2349, .5, .2), (3136, .25, .12)))
    write('ct_hint', chime, -16)
    n = int(.04 * SR)
    t = tarr(n)
    write('ct_click', np.sin(2 * np.pi * 1800 * t) * np.exp(-t / .006), -22)
    print('ok')


if __name__ == '__main__':
    main()
