"""The Parrot's Table effects. Synthesized, no samples.

Parrot babble: six short squawky syllables (a buzzy glottal source through two
vowel formants, with a parrot's characteristic pitch hook) that the game strings
together, pitched per species, while a speech bubble types out. Then: the
host's little bell, marks (a bright "tink" for honest, a low "tonk" for a liar,
a soft untick), a contradiction (a ruffle of feathers and a disapproving
"hmm"), a question asked, the gavel, wings flapping off, the table cheering, a
tick and a click. Writes 48 kHz 16-bit mono WAVs into ../assets/audio.
"""
import os
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'engine'))
from synth import SR, tarr, noise, fft_filter  # noqa: E402

OUT = os.path.join(os.path.dirname(__file__), '..', 'assets', 'audio')
rng = np.random.default_rng(1803)


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
    """A two-pole resonant band-pass (one formant)."""
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


def syllable(f0, hook, vowel, dur, rasp=.25):
    """One squawky syllable: a buzzy pulse with a pitch hook, through two formants."""
    n = int(dur * SR)
    t = tarr(n)
    u = t / dur
    # pitch: a quick rise then a fall, the parrot "hook"
    f = f0 * (1 + hook * np.sin(np.pi * np.clip(u * 1.3, 0, 1)) - .25 * u) * (1 + .03 * rng.normal(0, 1, n).cumsum() / np.sqrt(n))
    ph = 2 * np.pi * np.cumsum(f) / SR
    src = (np.mod(ph / (2 * np.pi), 1) < .3).astype(float) - .3  # narrow pulse: nasal and bright
    src += rasp * fft_filter(noise(n), 1500, 6000) * (1 + np.sin(ph))  # grit, modulated with the voice
    f1, f2 = vowel
    y = resonator(src, f1, 160) * 1.0 + resonator(src, f2, 220) * .7 + resonator(src, f2 * 1.5, 400) * .25
    env = np.minimum(1, t / .012) * np.exp(-np.maximum(0, t - dur * .55) / (dur * .18))
    return y * env


def main():
    os.makedirs(OUT, exist_ok=True)
    # babble: six vowels, varied pitch hooks
    vowels = [(900, 1500), (600, 2100), (520, 900), (380, 2500), (420, 1000), (780, 1250)]
    for i, v in enumerate(vowels):
        y = syllable(520 + 40 * (i % 3), .35 + .1 * (i % 2), v, .095 + .015 * (i % 3), .2 + .05 * i)
        write(f'pt_voice_{i + 1:02d}', fft_filter(y, 250, 7000, 2), -16)
    # the host's bell: a small brass table bell
    n = int(1.6 * SR)
    t = tarr(n)
    bell = sum(a * np.sin(2 * np.pi * f * t) * np.exp(-t / tau) for f, a, tau in ((1320, 1, .7), (3170, .5, .35), (4900, .3, .2), (2640, .25, .5)))
    write('pt_bell', bell * np.minimum(1, t / .002), -12)
    # marks: honest a bright tink up, liar a low wooden tonk, unmark a soft tick down
    n = int(.25 * SR)
    t = tarr(n)
    write('pt_mark_honest', (np.sin(2 * np.pi * 1760 * t) + .4 * np.sin(2 * np.pi * 2640 * t)) * np.exp(-t / .07), -16)
    write('pt_mark_liar', (np.sin(2 * np.pi * (330 + 60 * np.exp(-t / .01)) * t) + .3 * fft_filter(noise(n), 400, 2000)) * np.exp(-t / .06), -14)
    n = int(.08 * SR)
    t = tarr(n)
    write('pt_unmark', np.sin(2 * np.pi * (900 - 2000 * t) * t) * np.exp(-t / .02), -22)
    # a contradiction: a feather ruffle and a disapproving low "hmm"
    n = int(.5 * SR)
    t = tarr(n)
    ruffle = fft_filter(noise(n), 2000, 8000) * (.5 + .5 * np.sin(2 * np.pi * 28 * t)) * np.exp(-t / .1) * .6
    hmm = syllable(240, -.15, (350, 900), .38, .05)
    y = ruffle.copy()
    s = int(.08 * SR)
    y[s:s + len(hmm)] += hmm[:n - s] * .8
    write('pt_contradiction', y, -14)
    # a question asked: a rising "hm?" chirp with a little pen scratch
    q = syllable(560, .6, (600, 2000), .16, .1)
    scratch = fft_filter(noise(int(.12 * SR)), 3000, 9000) * .3
    write('pt_ask', np.concatenate([scratch, np.zeros(int(.03 * SR)), q]), -15)
    # the gavel: two raps on wood
    def rap(n):
        t = tarr(n)
        return (np.sin(2 * np.pi * 220 * t) * np.exp(-t / .03) + np.sin(2 * np.pi * 610 * t) * np.exp(-t / .015) * .6 + fft_filter(noise(n), 800, 4000) * np.exp(-t / .008) * .6)
    write('pt_gavel', np.concatenate([rap(int(.18 * SR)) * .7, rap(int(.5 * SR))]), -9)
    # wings flapping off: a run of feathery whumps, receding
    parts = []
    for k in range(9):
        m = int(.09 * SR)
        t = tarr(m)
        w = fft_filter(noise(m), 300, 3000) * np.sin(np.pi * t / (.09)) ** 2 * (1 - k / 11)
        parts.append(w)
    write('pt_flap', fft_filter(np.concatenate(parts), 200, 5000), -13)
    # the table cheers: a cluster of happy squawks
    n = int(1.1 * SR)
    y = np.zeros(n)
    for k in range(10):
        s = syllable(rng.uniform(500, 900), rng.uniform(.4, .7), vowels[int(rng.integers(0, 6))], rng.uniform(.09, .16), .25)
        at = int(rng.uniform(0, .85) * SR)
        e = min(n, at + len(s))
        y[at:e] += s[:e - at] * rng.uniform(.5, 1)
    write('pt_cheer', fft_filter(y, 250, 7000), -12)
    # a tick and a click
    n = int(.05 * SR)
    t = tarr(n)
    write('pt_tick', np.sin(2 * np.pi * 2400 * t) * np.exp(-t / .008), -20)
    n = int(.04 * SR)
    t = tarr(n)
    write('pt_click', np.sin(2 * np.pi * 1800 * t) * np.exp(-t / .006), -22)
    print('ok')


if __name__ == '__main__':
    main()
