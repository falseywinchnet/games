"""Liar's Dice effects. Synthesized, no samples.

Dice rattling in a leather cup (three takes), the cup slapped down on the
baize, a cup lifted, a knuckle tap, a die counted (a glassy tick), a die
dropped into the Keeper's jar (a clink and a gurgle), a player going out (a
sigh of bubbles), a refused bid, a click. Voices: three gibberish syllables
for each kind of drowned gambler (crab chitter, octopus burble, skeleton
clack, grouper gloop, eel hiss, turtle drawl, shark growl, ghost wail) and
three for the Keeper (deep and wet). Writes 48 kHz 16-bit mono WAVs into
../assets/audio.
"""
import os
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'engine'))
from synth import SR, tarr, noise, fft_filter  # noqa: E402

OUT = os.path.join(os.path.dirname(__file__), '..', 'assets', 'audio')
rng = np.random.default_rng(1809)


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


def click(n_len, f, decay, amp=1.0):
    t = tarr(n_len)
    return amp * np.sin(2 * np.pi * f * t) * np.exp(-t / decay)


def voice(f0, vowel, dur, buzz=.5, rasp=.2, wobble=0.0, breath=0.0, hook=.2):
    """A gibberish syllable: a pulse source through two formants, with optional grit, wobble and breath."""
    n = int(dur * SR)
    t = tarr(n)
    u = t / dur
    f = f0 * (1 + hook * np.sin(np.pi * np.clip(u * 1.2, 0, 1)) - .15 * u) * (1 + wobble * np.sin(2 * np.pi * 7 * t))
    ph = 2 * np.pi * np.cumsum(f) / SR
    src = buzz * ((np.mod(ph / (2 * np.pi), 1) < .35).astype(float) - .35) + (1 - buzz) * np.sin(ph)
    src = src + rasp * fft_filter(noise(n), 1200, 6000) * (1 + np.sin(ph)) + breath * fft_filter(noise(n), 800, 5000)
    f1, f2 = vowel
    y = resonator(src, f1, 140) + .7 * resonator(src, f2, 200)
    env = np.minimum(1, t / .015) * np.exp(-np.maximum(0, t - dur * .55) / (dur * .2))
    return y * env


def main():
    os.makedirs(OUT, exist_ok=True)
    # dice in a leather cup: bursts of little clacks, dulled by leather, with a shuffle of the cup itself
    for k in range(3):
        n = int(.9 * SR)
        y = np.zeros(n)
        for j in range(22 + 4 * k):
            at = int(abs(rng.normal(.4, .2)) * SR) % (n - 3000)
            c = click(2400, rng.uniform(2200, 4200), .004, rng.uniform(.3, 1))
            y[at:at + len(c)] += c
        shuffle = fft_filter(noise(n), 200, 1500) * (.5 + .5 * np.sin(2 * np.pi * 7 * tarr(n))) * .25
        y = fft_filter(y, 300, 6000) + shuffle
        y *= np.sin(np.pi * np.clip(tarr(n) / .9, 0, 1)) ** .5
        write(f'ld_rattle_{k + 1:02d}', y, -13)
    # the cup slapped down on baize: a soft thump, then the dice settling
    n = int(.35 * SR)
    t = tarr(n)
    thump = np.sin(2 * np.pi * (90 + 60 * np.exp(-t / .02)) * t) * np.exp(-t / .06) + fft_filter(noise(n), 300, 2000) * np.exp(-t / .02) * .5
    for j in range(4):
        at = int((.03 + .025 * j) * SR)
        c = click(1500, 3000 + 300 * j, .003, .25)
        thump[at:at + len(c)] += c
    write('ld_cup_down', thump, -12)
    # a cup lifted: a leathery swish
    n = int(.3 * SR)
    t = tarr(n)
    write('ld_cup_lift', fft_filter(noise(n), 400, 3500) * np.sin(np.pi * t / .3) ** 2, -18)
    # a knuckle tap on the table (bidding)
    n = int(.12 * SR)
    t = tarr(n)
    write('ld_tap', np.sin(2 * np.pi * 180 * t) * np.exp(-t / .025) + .3 * fft_filter(noise(n), 800, 4000) * np.exp(-t / .006), -16)
    # a die counted: a glassy tick
    n = int(.25 * SR)
    t = tarr(n)
    write('ld_count', (np.sin(2 * np.pi * 1760 * t) + .3 * np.sin(2 * np.pi * 4400 * t)) * np.exp(-t / .05), -17)
    # a die into the jar: a glass clink, a plop and a gurgle of bubbles
    n = int(1.0 * SR)
    t = tarr(n)
    clink = sum(a * np.sin(2 * np.pi * f * t) * np.exp(-t / d) for f, a, d in ((2100, 1, .15), (3350, .6, .1), (5200, .3, .06)))
    plop = np.zeros(n)
    s = int(.12 * SR)
    m = n - s
    tt = tarr(m)
    plop[s:] = np.sin(2 * np.pi * np.cumsum(300 + 900 * np.exp(-tt / .03)) / SR) * np.exp(-tt / .08) * .6
    gurgle = np.zeros(n)
    for j in range(8):
        at = int((.25 + j * .07 + rng.uniform(0, .03)) * SR)
        b = np.sin(2 * np.pi * np.cumsum(np.linspace(500 + 80 * j, 1100 + 60 * j, int(.05 * SR))) / SR) * np.hanning(int(.05 * SR)) * .3
        gurgle[at:at + len(b)] += b[:max(0, n - at)]
    write('ld_plunk', clink * .6 + plop + gurgle, -11)
    # going out: a long sigh of bubbles
    n = int(1.3 * SR)
    y = np.zeros(n)
    for j in range(14):
        at = int(j * .08 * SR + rng.uniform(0, .03) * SR)
        L = int(.06 * SR)
        b = np.sin(2 * np.pi * np.cumsum(np.linspace(300 + 30 * j, 700 + 40 * j, L)) / SR) * np.hanning(L) * (1 - j / 16)
        y[at:at + L] += b[:max(0, n - at)]
    write('ld_out', y, -15)
    # a refused bid; a click
    n = int(.15 * SR)
    t = tarr(n)
    write('ld_nope', np.sin(2 * np.pi * 110 * t) * np.exp(-t / .05), -18)
    n = int(.04 * SR)
    t = tarr(n)
    write('ld_click', np.sin(2 * np.pi * 1600 * t) * np.exp(-t / .006), -22)
    # voices, by kind (the species order of the game: crab, octopus, skeleton, grouper, eel, turtle, shark, ghost)
    vowels = [(800, 1400), (500, 1900), (400, 900)]
    kinds = [
        dict(f0=420, buzz=.9, rasp=.35, hook=.4, wobble=.0, breath=0, dur=.07),    # crab: a quick clicky chitter
        dict(f0=230, buzz=.3, rasp=.05, hook=.3, wobble=.08, breath=.1, dur=.11),  # octopus: a soft burble
        dict(f0=180, buzz=1., rasp=.5, hook=.1, wobble=0, breath=0, dur=.08),      # skeleton: hollow clack
        dict(f0=120, buzz=.4, rasp=.1, hook=.2, wobble=.05, breath=.05, dur=.13),  # grouper: low gloop
        dict(f0=300, buzz=.5, rasp=.15, hook=.5, wobble=0, breath=.5, dur=.09),    # eel: hissy
        dict(f0=110, buzz=.5, rasp=.15, hook=.05, wobble=0, breath=.1, dur=.16),   # turtle: slow drawl
        dict(f0=140, buzz=.9, rasp=.6, hook=.15, wobble=.02, breath=.05, dur=.1),  # shark: gruff growl
        dict(f0=380, buzz=.1, rasp=.0, hook=.6, wobble=.12, breath=.4, dur=.15),   # ghost: airy wail
    ]
    for k, kd in enumerate(kinds):
        for j, v in enumerate(vowels):
            y = voice(kd['f0'] * (1 + .06 * j), v, kd['dur'], kd['buzz'], kd['rasp'], kd['wobble'], kd['breath'], kd['hook'])
            if k == 2:  # the skeleton's jaw clacks at each syllable
                y[:len(click(600, 1200, .003))] += click(600, 1200, .003, 2)
            write(f'ld_voice_{k}_{j + 1:02d}', fft_filter(y, 70, 7000, 2), -17)
    # the Keeper: deep, slow and wet
    for j, v in enumerate(vowels):
        y = voice(70 + 6 * j, v, .2, .6, .2, .04, .15, .1)
        y = y + .3 * fft_filter(noise(len(y)), 100, 600) * np.hanning(len(y))
        write(f'ld_keeper_{j + 1:02d}', fft_filter(y, 40, 3000, 2), -14)
    print('ok')


if __name__ == '__main__':
    main()
