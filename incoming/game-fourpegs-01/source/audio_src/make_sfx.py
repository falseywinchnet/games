"""Four Pegs effects and the butler's voice. Synthesized, no samples.

His voice is a gravelly baritone murmur: a jittery glottal pulse train with a
touch of vocal fry, shaped by vowel formants, in short syllables the game
plays as his words type out. Writes 48 kHz 16-bit mono WAVs into ../assets/audio.
"""
import os
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'engine'))
from synth import SR, tarr, noise, fft_filter  # noqa: E402

OUT = os.path.join(os.path.dirname(__file__), '..', 'assets', 'audio')
rng = np.random.default_rng(1888)

VOWELS = {'a': (730, 1090, 2440), 'o': (570, 840, 2410), 'e': (530, 1840, 2480), 'u': (440, 1020, 2240), 'i': (390, 1990, 2550), 'uh': (520, 1190, 2390)}


def write(name, x, peak_db):
    x = x / (np.max(np.abs(x)) + 1e-12) * 10 ** (peak_db / 20)
    fade = min(len(x), int(.004 * SR))
    x[-fade:] *= np.linspace(1, 0, fade)
    with wave.open(os.path.join(OUT, name + '.wav'), 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes((x * 32767).astype('<i2').tobytes())


def glottal(f0_t, fry=.15):
    """A pulse train following f0_t with jitter, shimmer, and occasional fry pulses."""
    n = len(f0_t)
    y = np.zeros(n)
    t = 0.0
    while t < n - 1:
        i = int(t)
        f = f0_t[i] * (1 + rng.normal(0, .02))
        period = SR / max(40, f)
        if rng.random() < fry:
            period *= rng.uniform(1.6, 2.4)  # a creaky, gravelly skip
        amp = 1 + rng.normal(0, .12)
        k = np.arange(int(period * .6))
        pulse = np.sin(np.pi * k / max(1, len(k))) ** 3 * amp
        e = min(n, i + len(pulse))
        y[i:e] += pulse[:e - i]
        t += period
    return np.diff(y, prepend=0)  # the derivative brightens it like a real glottal source


def formants(src, vowel, bw=(90, 110, 160), gains=(1, .55, .25)):
    F = VOWELS[vowel]
    n = len(src)
    X = np.fft.rfft(src)
    f = np.fft.rfftfreq(n, 1 / SR)
    H = sum(g * np.exp(-.5 * ((f - Fi) / b) ** 2) for Fi, b, g in zip(F, bw, gains)) + .02
    return np.fft.irfft(X * H, n)


def syllable(vowel, f0, contour, dur, fry=.18, breath=.05):
    n = int(dur * SR)
    t = tarr(n)
    f0_t = f0 * (1 + contour * (t / dur) - .6 * contour * (t / dur) ** 2)
    v = formants(glottal(f0_t, fry), vowel)
    v += breath * fft_filter(noise(n), 400, 3000)
    env = np.minimum(1, t / .018) * np.minimum(1, (dur - t) / .05)
    return v * np.clip(env, 0, 1)


def hum(f0, dur):  # a closed-mouth "hm", nasal and low
    n = int(dur * SR)
    t = tarr(n)
    f0_t = f0 * (1 + .12 * np.sin(np.pi * t / dur))
    v = formants(glottal(f0_t, .1), 'u', bw=(60, 80, 120), gains=(1, .15, .05))
    return v * np.minimum(1, t / .03) * np.clip((dur - t) / .1, 0, 1)


def chuckle():
    out = np.zeros(int(.75 * SR))
    for i, (f0, d) in enumerate([(118, .11), (110, .11), (102, .14)]):
        s = syllable('uh', f0, -.1, d, fry=.3, breath=.25)
        st = int(i * .2 * SR)
        out[st:st + len(s)] += s * (1 - .15 * i)
    return out


def tone(f, dur, decay, partials=((1, 1),)):
    n = int(dur * SR)
    t = tarr(n)
    return sum(a * np.sin(2 * np.pi * f * r * t) for r, a in partials) * np.exp(-t / decay)


def main():
    os.makedirs(OUT, exist_ok=True)
    # his voice: eight syllables, played as text types out
    for i, (v, f0, c) in enumerate([('a', 104, -.08), ('o', 98, -.12), ('e', 112, -.05), ('u', 96, -.1),
                                    ('i', 116, -.15), ('uh', 100, .05), ('o', 108, -.2), ('a', 94, -.04)]):
        write(f'fp_voice_{i + 1:02d}', syllable(v, f0, c, .13 + .03 * (i % 3)), -13)
    write('fp_voice_hm', hum(96, .5), -14)
    write('fp_chuckle', chuckle(), -12)
    # his maniacal laugh: slow, then faster and higher, then a falling cackle
    beats = [(.0, 108, .2), (.42, 112, .2), (.8, 120, .16), (1.08, 130, .14), (1.3, 142, .13), (1.5, 152, .13), (1.69, 160, .13),
             (1.88, 168, .14), (2.08, 172, .15), (2.3, 166, .15), (2.52, 154, .15), (2.76, 140, .16), (3.02, 124, .2), (3.34, 110, .3)]
    laugh = np.zeros(int(3.9 * SR))
    for st, f0, d in beats:
        s = syllable('a', f0, -.08, d, fry=.25, breath=.18)
        i = int(st * SR)
        laugh[i:i + len(s)] += s * (.7 + .3 * min(1, st / 1.5))
    write('fp_laugh', laugh, -8)
    # the lair coming down: a long swelling rumble, and crashes of falling masonry
    n = int(7.5 * SR)
    t = tarr(n)
    rumble = fft_filter(noise(n), 25, 160) * np.minimum(1, t / 3.0) * (1 + .3 * np.sin(2 * np.pi * 3.1 * t))
    rumble += .4 * fft_filter(noise(n), 200, 900) * np.minimum(1, t / 5.0) * (.5 + .5 * np.sin(2 * np.pi * .7 * t)) ** 2
    rumble *= np.clip((7.5 - t) / 1.0, 0, 1)
    write('fp_rumble', rumble, -7)
    m = int(1.4 * SR)
    tt = tarr(m)
    crash = np.sin(2 * np.pi * (55 + 30 * np.exp(-tt / .05)) * tt) * np.exp(-tt / .25)
    crash += .8 * fft_filter(noise(m), 300, 5000) * np.exp(-tt / .12)
    for k in range(9):  # rattling rubble
        i = int((.08 + .1 * k + .03 * rng.random()) * SR)
        l = int(.05 * SR)
        crash[i:i + l] += .25 * fft_filter(noise(l), 1500, 7000) * np.exp(-tarr(l) / .01) * (1 - k / 10)
    write('fp_crash', crash, -8)
    # the console
    n = int(.9 * SR)
    t = tarr(n)
    sweep = np.sin(2 * np.pi * np.cumsum(200 + 1600 * (t / .9) ** 2) / SR) * np.exp(-((t - .45) / .3) ** 2)
    chirps = sum(tone(1800 + 300 * k, .9, .03) * (t > .45 + .08 * k) for k in range(4))
    write('fp_console_boot', sweep * .6 + .2 * chirps + .05 * fft_filter(noise(n), 2000, 8000) * np.exp(-t / .3), -12)
    write('fp_pick', tone(2600, .12, .02, ((1, 1), (2.7, .3))) + .05 * fft_filter(noise(int(.12 * SR)), 3000, 9000) * np.exp(-tarr(int(.12 * SR)) / .01), -18)
    for i in range(3):
        m = int(.28 * SR)
        tt = tarr(m)
        thunk = np.sin(2 * np.pi * (150 - 60 * tt / .28) * tt) * np.exp(-tt / .04)
        zing = np.sin(2 * np.pi * np.cumsum(1200 + 900 * tt / .28 + 80 * i) / SR) * np.exp(-tt / .09) * .35
        write(f'fp_place_{i + 1:02d}', thunk + zing, -15)
    m = int(.25 * SR)
    tt = tarr(m)
    write('fp_remove', np.sin(2 * np.pi * np.cumsum(1500 - 1100 * tt / .25) / SR) * np.exp(-tt / .07), -18)
    m = int(.6 * SR)
    tt = tarr(m)
    press = np.sin(2 * np.pi * 110 * tt) * np.exp(-tt / .03)
    scan = np.sin(2 * np.pi * np.cumsum(600 + 2400 * tt / .6) / SR) * np.exp(-((tt - .3) / .15) ** 2) * .3
    write('fp_check', press + scan, -13)
    write('fp_pin_exact', tone(1760, .7, .25, ((1, 1), (2.76, .25), (5.4, .08))), -15)
    write('fp_pin_near', tone(1175, .45, .12, ((1, 1), (2.0, .15))), -18)
    write('fp_pin_none', tone(98, .35, .1, ((1, 1), (3, .4), (5, .2))) * (1 + .5 * np.sign(np.sin(2 * np.pi * 30 * tarr(int(.35 * SR))))), -20)
    write('fp_invalid', tone(220, .18, .06, ((1, 1), (1.5, .5))), -20)
    # the desk: a gloved palm on old wood, and restrained applause
    m = int(.4 * SR)
    tt = tarr(m)
    thud = np.sin(2 * np.pi * (85 + 40 * np.exp(-tt / .02)) * tt) * np.exp(-tt / .08) + .3 * fft_filter(noise(m), 200, 1800) * np.exp(-tt / .025)
    write('fp_desk', thud, -9)
    m = int(.14 * SR)
    tt = tarr(m)
    write('fp_clap', fft_filter(noise(m), 300, 2500) * np.exp(-tt / .018), -14)
    print('ok')


if __name__ == '__main__':
    main()
