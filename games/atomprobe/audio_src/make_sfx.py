"""Atom Probe effects. Synthesized, no samples.

The emitters whine as they charge and zap as they fire; the fog hums when a
beam is inside; an exit sings, a reflection pings, an absorption thumps. The
lever clunks, the vents hiss, the atoms shimmer into view, and each marker
locks on (or doesn't). Writes 48 kHz 16-bit mono WAVs into ../assets/audio.
"""
import os
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'engine'))
from synth import SR, tarr, noise, fft_filter, mtof  # noqa: E402

OUT = os.path.join(os.path.dirname(__file__), '..', 'assets', 'audio')
rng = np.random.default_rng(2026)


def write(name, x, peak_db):
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
    return np.sin(2 * np.pi * np.cumsum(f) / SR), f


def env(n, att, tau):
    t = tarr(n)
    return np.minimum(1, t / max(att, 1e-6)) * np.exp(-t / tau)


def bell(f, dur, tau, partials=((1, 1), (2.76, .3), (5.4, .12), (8.9, .05))):
    n = int(dur * SR)
    t = tarr(n)
    y = sum(a * np.sin(2 * np.pi * f * r * t) * np.exp(-t / (tau / (1 + .6 * k))) for k, (r, a) in enumerate(partials))
    return y * np.minimum(1, t / .001)


def main():
    os.makedirs(OUT, exist_ok=True)
    # hovering an emitter: a tiny relay tick
    n = int(.03 * SR)
    t = tarr(n)
    write('ap_hover', np.sin(2 * np.pi * 2600 * t) * np.exp(-t / .006) + .3 * fft_filter(noise(n), 3000, 9000) * np.exp(-t / .003), -24)
    # charging: a rising whine with a capacitor flutter, ending on a click
    y, f = sweep(320, 1500, .3, 1.6)
    t = tarr(len(y))
    y = (y + .35 * np.sin(2 * np.pi * np.cumsum(f * 2.01) / SR)) * (.3 + .7 * t / .3) * (1 + .25 * np.sin(2 * np.pi * 38 * t))
    y = np.concatenate([y, np.zeros(int(.04 * SR))])
    write('ap_charge', y, -17)
    # firing: a laser zap, a fast falling chirp over a crack of noise
    y, f = sweep(2600, 260, .26, .45)
    t = tarr(len(y))
    zap = (y + .4 * np.sign(y) * .5) * np.exp(-t / .09)
    crack = fft_filter(noise(len(y)), 1500, 12000) * np.exp(-t / .012) * .5
    write('ap_fire', zap + crack, -12)
    # the fog taking the beam: a low resonant hum swelling and fading, with a shimmer
    n = int(.75 * SR)
    t = tarr(n)
    e = np.sin(np.pi * np.clip(t / .75, 0, 1)) ** 1.5
    hum = (np.sin(2 * np.pi * 82 * t) + .6 * np.sin(2 * np.pi * 83.5 * t) + .3 * np.sin(2 * np.pi * 164 * t)) * e
    air = fft_filter(noise(n), 600, 3800) * e * .25
    shimmer = sum(np.sin(2 * np.pi * fr * t + rng.uniform(0, 6)) for fr in (1318, 1661, 1975)) * e * np.exp(-t / .3) * .08
    write('ap_hum', hum + air + shimmer, -15)
    # an exit: the beam bursts out the far side singing, a rising zing into a bright bell
    y, f = sweep(700, 2400, .12, .7)
    t = tarr(len(y))
    zing = y * np.exp(-t / .06)
    b = bell(1760, .6, .25)
    out = np.zeros(len(b) + len(y))
    out[:len(y)] += zing * .8
    out[int(.06 * SR):int(.06 * SR) + len(b)] += b
    write('ap_exit', out, -13)
    # a reflection: a metallic ping and its quick echo back
    p1, p2 = bell(1320, .5, .16, ((1, 1), (2.32, .4), (4.1, .2))), bell(990, .5, .2, ((1, 1), (2.32, .4), (4.1, .2)))
    out = np.zeros(int(.65 * SR))
    out[:len(p1)] += p1
    out[int(.09 * SR):int(.09 * SR) + len(p2)] += p2 * .8
    write('ap_reflect', out, -13)
    # an absorption: a deep thump that swallows the light
    n = int(.9 * SR)
    t = tarr(n)
    f = 38 + 70 * np.exp(-t / .05)
    boom = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t / .3)
    mud = fft_filter(noise(n), 60, 500) * np.exp(-t / .12) * .4
    suck, _ = sweep(900, 120, .35, .5)
    suck = np.concatenate([suck * np.exp(-tarr(len(suck)) / .12) * .25, np.zeros(n - len(suck))])
    write('ap_hit', boom + mud + suck, -9)
    # already known: a soft double blip
    n = int(.16 * SR)
    t = tarr(n)
    blip = np.sin(2 * np.pi * 1500 * t) * np.exp(-t / .02)
    out = np.zeros(int(.25 * SR))
    out[:n] += blip
    out[int(.07 * SR):int(.07 * SR) + n] += blip * .7
    write('ap_blip', out, -20)
    # markers: a glassy set-down, a lift, a scratch
    n = int(.35 * SR)
    t = tarr(n)
    tap = fft_filter(noise(n), 2000, 9000) * np.exp(-t / .004) * .5
    write('ap_mark', tap + bell(2093, .35, .12, ((1, 1), (2.0, .3), (3.0, .1))), -15)
    y, f = sweep(1800, 900, .12, 1)
    write('ap_unmark', y * np.exp(-tarr(len(y)) / .04), -20)
    n = int(.1 * SR)
    t = tarr(n)
    write('ap_cross', fft_filter(noise(n), 2500, 8000) * (np.exp(-t / .01) + .6 * np.exp(-np.maximum(0, t - .045) / .01) * (t > .045)), -21)
    # refused: a low double buzz
    n = int(.28 * SR)
    t = tarr(n)
    buzz = np.sign(np.sin(2 * np.pi * 110 * t)) * (np.exp(-t / .05) + np.exp(-np.maximum(0, t - .13) / .05) * (t > .13))
    write('ap_full', fft_filter(buzz, 80, 1500), -19)
    # the lever: a heavy ratcheting pull, then the clunk
    n = int(.6 * SR)
    t = tarr(n)
    y = np.zeros(n)
    for k in range(5):
        s = int((.02 + k * .045) * SR)
        m = int(.02 * SR)
        y[s:s + m] += fft_filter(noise(m), 1500, 6000) * np.exp(-tarr(m) / .004) * .5
    s = int(.27 * SR)
    m = n - s
    tt = tarr(m)
    y[s:] += np.sin(2 * np.pi * (70 + 60 * np.exp(-tt / .02)) * tt) * np.exp(-tt / .1) + .5 * fft_filter(noise(m), 200, 3000) * np.exp(-tt / .03)
    write('ap_lever', y, -9)
    # the vents: pressure released, a long hiss that thins out
    n = int(2.0 * SR)
    t = tarr(n)
    h = fft_filter(noise(n), 1500, 11000)
    e = np.minimum(1, t / .05) * np.exp(-t / .7)
    whoosh = fft_filter(noise(n), 200, 1200) * np.exp(-t / .3) * .6
    write('ap_vent', h * e + whoosh, -12)
    # the atoms appear: a shimmering chord swells in (E major add 9)
    n = int(2.0 * SR)
    t = tarr(n)
    e = np.minimum(1, t / .6) * np.exp(-np.maximum(0, t - .8) / .45)
    chord = sum(np.sin(2 * np.pi * mtof(m) * t * (1 + .002 * np.sin(2 * np.pi * 4.5 * t + k))) / (1 + .2 * k)
                for k, m in enumerate((64, 68, 71, 75, 78, 83)))
    sparkle = np.zeros(n)
    for _ in range(26):
        s = int(rng.uniform(.1, 1.5) * SR)
        b = bell(mtof(rng.choice([88, 92, 95, 99, 100])), .3, .08)
        e2 = min(n, s + len(b))
        sparkle[s:e2] += b[:e2 - s] * .15
    write('ap_atoms', chord * e + sparkle, -13)
    # a beam replayed along its true path: a soft travelling zap
    y, f = sweep(1800, 600, .35, .8)
    t = tarr(len(y))
    write('ap_replay', y * np.sin(np.pi * np.clip(t / .35, 0, 1)) ** .5 * np.exp(-t / .2) + fft_filter(noise(len(y)), 2000, 7000) * np.exp(-t / .03) * .2, -17)
    # a marker locked onto its atom, and one that found nothing
    a, b2 = bell(1568, .6, .25), bell(2349, .6, .3)
    out = np.zeros(int(.7 * SR))
    out[:len(a)] += a
    out[int(.08 * SR):int(.08 * SR) + len(b2)] += b2 * .9
    write('ap_lock', out, -13)
    n = int(.45 * SR)
    t = tarr(n)
    y, _ = sweep(240, 120, .45, 1)
    write('ap_wrong', fft_filter(np.sign(y), 60, 1200) * np.exp(-t / .12) + np.sin(2 * np.pi * 60 * t) * np.exp(-t / .1), -15)
    # a new box powering up: a rising sweep and the emitters coming online in a ripple of clicks
    n = int(1.2 * SR)
    t = tarr(n)
    y, f = sweep(90, 700, 1.0, 1.4)
    y = np.concatenate([y, np.zeros(n - len(y))])
    rise = fft_filter(y + .5 * np.sign(y), 40, 2500) * np.minimum(1, t / .2) * np.exp(-np.maximum(0, t - .9) / .08) * .5
    clicks = np.zeros(n)
    for k in range(16):
        s = int((.15 + k * .055) * SR)
        m = int(.012 * SR)
        clicks[s:s + m] += np.sin(2 * np.pi * (1800 + 60 * k) * tarr(m)) * np.exp(-tarr(m) / .003) * .4
    write('ap_boot', rise + clicks, -13)
    print('ok')


if __name__ == '__main__':
    main()
