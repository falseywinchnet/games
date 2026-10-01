"""Synthesized Switchbox effects added for the new game: a soft clap and her
babble syllables (non-vocal, Animal-Crossing-style gibberish tones).
No samples. Writes 48 kHz 16-bit mono WAVs into ../assets/audio."""
import os
import wave

import numpy as np

SR = 48000
OUT = os.path.join(os.path.dirname(__file__), '..', 'assets', 'audio')
rng = np.random.default_rng(7)


def write(name, x, peak_db):
    x = x / (np.max(np.abs(x)) + 1e-12) * 10 ** (peak_db / 20)
    fade = min(len(x), int(.004 * SR))
    x[-fade:] *= np.linspace(1, 0, fade)
    with wave.open(os.path.join(OUT, name + '.wav'), 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes((x * 32767).astype('<i2').tobytes())


def onepole_lp(x, fc):
    a = np.exp(-2 * np.pi * fc / SR)
    y = np.zeros_like(x)
    s = 0.0
    for i, v in enumerate(x):
        s = (1 - a) * v + a * s
        y[i] = s
    return y


def clap(seed):
    r = np.random.default_rng(seed)
    n = int(.16 * SR)
    t = np.arange(n) / SR
    x = np.zeros(n)
    # three quick bursts make the slap of two palms, then a short body
    for k, (off, g) in enumerate([(0, .6), (.006, .8), (.013, 1.0)]):
        i = int(off * SR)
        m = n - i
        env = np.exp(-np.arange(m) / SR / (.004 if k < 2 else .03))
        x[i:] += g * r.standard_normal(m) * env
    x = onepole_lp(x, 3200) - .6 * onepole_lp(x, 500)  # band-limited: soft, not sharp
    return x * (1 - np.exp(-t / .0006))


def syllable(vowel, f0, glide, dur):
    formants = {'a': (850, 1250), 'i': (320, 2400), 'u': (350, 900), 'e': (500, 2000), 'o': (520, 950), 'y': (420, 1900)}[vowel]
    n = int(dur * SR)
    t = np.arange(n) / SR
    f = f0 * (1 + glide * (t / dur) - .5 * glide * (t / dur) ** 2)
    ph = 2 * np.pi * np.cumsum(f) / SR
    x = np.zeros(n)
    for h in range(1, 14):
        fh = f0 * h
        if fh > 5000:
            break
        a = sum(np.exp(-((fh - F) / (F * .35)) ** 2) for F in formants) + .08 / h
        x += a * np.sin(h * ph)
    env = np.minimum(1, t / .012) * np.exp(-np.maximum(0, t - dur * .45) / (dur * .25))
    vib = 1 + .03 * np.sin(2 * np.pi * 11 * t)
    return x * env * vib


def main():
    os.makedirs(OUT, exist_ok=True)
    write('switchbox_clap', clap(20), -9)
    for i, (v, f0, g) in enumerate([('a', 560, .25), ('i', 640, -.15), ('u', 520, .1), ('e', 600, .3), ('o', 540, -.2), ('y', 680, .15)]):
        write(f'switchbox_babble_0{i + 1}', syllable(v, f0, g, .11 + .02 * (i % 3)), -12)
    print('ok')


if __name__ == '__main__':
    main()
