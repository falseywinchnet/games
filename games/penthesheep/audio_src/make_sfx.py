"""Pen the Sheep effects. Synthesized, no samples.

A stone set down in the grass, the sheep's little hop, four bleats (baa!),
munching clover, a hint chime, undo, a click, a refused patch. The win and
the escape add the music's little tunes in the game (sh_tune_*), with a
contented or cheeky bleat here. Writes 48 kHz 16-bit mono WAVs into
../assets/audio.
"""
import os
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'engine'))
from synth import SR, tarr, noise, fft_filter  # noqa: E402

OUT = os.path.join(os.path.dirname(__file__), '..', 'assets', 'audio')
rng = np.random.default_rng(1977)


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


def resonator(x, f, bw):
    r = np.exp(-np.pi * bw / SR)
    c = 2 * r * np.cos(2 * np.pi * f / SR)
    y = np.zeros_like(x)
    y1 = y2 = 0.0
    g = 1 - r
    for i in range(len(x)):
        y0 = g * x[i] + c * y1 - r * r * y2
        y[i] = y0
        y2, y1 = y1, y0
    return y


def bleat(f0, dur, wobble=7.0, depth=.06, vowel=(700, 1200), bright=1.0):
    """Baa: a nasal pulse with the sheep's quavering vibrato, through an 'aa' vowel."""
    n = int(dur * SR)
    t = tarr(n)
    u = t / dur
    f = f0 * (1 + .12 * np.sin(np.pi * np.clip(u * 1.5, 0, 1)) - .1 * u) * (1 + depth * np.sin(2 * np.pi * wobble * t) * np.minimum(1, t / .08))
    ph = 2 * np.pi * np.cumsum(f) / SR
    src = ((np.mod(ph / (2 * np.pi), 1) < .3).astype(float) - .3) + .2 * fft_filter(noise(n), 1000, 4000)
    y = resonator(src, vowel[0], 120) + .8 * resonator(src, vowel[1], 160) + .3 * bright * resonator(src, 2600, 300)
    env = np.minimum(1, t / .03) * np.exp(-np.maximum(0, t - dur * .6) / (dur * .2))
    return fft_filter(y * env, 150, 5000)


def main():
    os.makedirs(OUT, exist_ok=True)
    # a stone set in the grass: a soft, earthy thunk and a rustle
    n = int(.4 * SR)
    t = tarr(n)
    thunk = np.sin(2 * np.pi * (110 + 80 * np.exp(-t / .02)) * t) * np.exp(-t / .07)
    rustle = fft_filter(noise(n), 1500, 6000) * np.exp(-t / .05) * .35
    clack = np.sin(2 * np.pi * 900 * t) * np.exp(-t / .01) * .2
    write('sh_place', thunk + rustle + clack, -12)
    # a hop: a soft double patter on turf
    n = int(.25 * SR)
    y = np.zeros(n)
    for at in (0, .07):
        s = int(at * SR)
        m = int(.08 * SR)
        tt = tarr(m)
        y[s:s + m] += (np.sin(2 * np.pi * 160 * tt) * np.exp(-tt / .02) + fft_filter(noise(m), 500, 3000) * np.exp(-tt / .015) * .4)
    write('sh_hop', y, -20)
    # bleats: four sheep voices, from a lamb to an old ewe
    for k, (f0, dur, wob) in enumerate(((420, .6, 7.5), (330, .75, 6.5), (260, .8, 6.0), (520, .45, 8.5))):
        write(f'sh_baa_{k + 1:02d}', bleat(f0, dur, wob), -13)
    # munching clover: crisp tearing and chewing
    n = int(.9 * SR)
    y = np.zeros(n)
    for j in range(6):
        at = int((.05 + j * .13 + rng.uniform(0, .03)) * SR)
        m = int(.07 * SR)
        tt = tarr(m)
        y[at:at + m] += fft_filter(noise(m), 1200, 7000) * np.exp(-tt / .02) * rng.uniform(.6, 1)
    write('sh_munch', y, -17)
    # the hint: a little chime on the pentatonic
    n = int(.9 * SR)
    t = tarr(n)
    write('sh_hint', sum(a * np.sin(2 * np.pi * f * t) * np.exp(-t / d) for f, a, d in ((1046.5, 1, .35), (1318.5, .6, .3), (1568, .4, .25))), -17)
    # undo: a soft backwards swish
    n = int(.18 * SR)
    t = tarr(n)
    write('sh_undo', fft_filter(noise(n), 800, 4000) * np.sin(np.pi * t / t[-1]) ** 2, -22)
    # a click; a refused patch
    n = int(.04 * SR)
    t = tarr(n)
    write('sh_click', np.sin(2 * np.pi * 1500 * t) * np.exp(-t / .006), -23)
    n = int(.14 * SR)
    t = tarr(n)
    write('sh_nope', np.sin(2 * np.pi * 180 * t) * np.exp(-t / .04), -20)
    # penned: a contented little bleat (the tune plays with it); got away: a cheeky one, rising
    write('sh_penned', np.concatenate([np.zeros(int(.4 * SR)), bleat(300, .9, 5.5, .08)]), -14)
    write('sh_escape', bleat(480, .5, 9, .05) * 1.0, -13)
    print('ok')


if __name__ == '__main__':
    main()
