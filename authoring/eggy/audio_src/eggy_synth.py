"""Extra instruments for Eggy's music (registered into the shared synth engine)."""
import os, sys
_here = os.path.dirname(os.path.abspath(__file__))
_engine = os.path.join(_here, 'engine')  # packaged copy of the shared synthesis engine
sys.path.insert(0, _engine if os.path.isdir(_engine) else os.path.join(_here, '..', '..', 'work', 'src'))
import numpy as np
import synth as S
from synth import SR, mtof, tarr, gate, fft_filter, noise, _partials, wt


def snare(vel=.6, seed=None):
    """Field snare: crisp head tone + wire rattle."""
    n = int(.25 * SR)
    t = tarr(n)
    head = np.sin(2 * np.pi * 190 * t) * np.exp(-t / .035) * .5 + np.sin(2 * np.pi * 330 * t) * np.exp(-t / .02) * .25
    wires = fft_filter(noise(n, seed), 1800, 9000) * np.exp(-t / .07)
    wires /= np.abs(wires).max() + 1e-9
    return (head + wires * .7) * gate(n, 10, .0008) * vel


def snare_ghost(vel=.25, seed=None):
    return snare(vel, seed) * np.exp(-tarr(int(.25 * SR)) / .03)


def anvil(vel=.5, seed=None):
    n = int(1.2 * SR)
    y = _partials(1, n, [1580, 2410, 3920, 5230, 7100], [1, .6, .5, .3, .2], [.5, .35, .25, .15, .1])
    k = int(.004 * SR)
    y[:k] += .6 * fft_filter(noise(k, seed), 3000, 12000)
    return y * gate(n, 10, .0003) * vel


def frame_drum(vel=.6, seed=None):
    n = int(.6 * SR)
    t = tarr(n)
    f = 70 * (1 + .25 * np.exp(-t / .03))
    y = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t / .25) + .2 * fft_filter(noise(n, seed), 100, 900) * np.exp(-t / .05)
    return y * gate(n, 10, .002) * vel


def washboard(vel=.3, seed=None):
    n = int(.06 * SR)
    t = tarr(n)
    y = fft_filter(noise(n, seed), 2500, 9000) * (np.sin(2 * np.pi * 180 * t) > 0) * np.exp(-t / .02)
    return y / (np.abs(y).max() + 1e-9) * vel


def bowl(m, dur, vel=.6):
    """Singing bowl: inharmonic, very long, slow beating."""
    f = mtof(m)
    n = int((min(dur, 6) + 2.5) * SR)
    t = tarr(n)
    y = np.zeros(n)
    for r, a, tau, beat in ((1, 1, 4.0, .9), (2.71, .45, 2.5, 1.7), (5.1, .2, 1.4, 2.3), (8.3, .08, .8, 3.1)):
        if f * r > 16000:
            continue
        y += a * np.exp(-t / tau) * np.sin(2 * np.pi * f * r * t) * (1 + .35 * np.sin(2 * np.pi * beat * t))
    return y * gate(n, 10, .004) * (.3 + .7 * vel) * .6


def whistle(m, dur, vel=.6):
    """Lonesome whistle: near sine, slow vibrato, slight breath."""
    f = mtof(m)
    rel = .12
    n = int((dur + rel) * SR)
    t = tarr(n)
    vib = 1 + .006 * np.clip((t - .2) / .3, 0, 1) * np.sin(2 * np.pi * 5.5 * t)
    scoop = 1 - .02 * np.exp(-t / .05)
    y = np.sin(2 * np.pi * np.cumsum(f * vib * scoop) / SR) + .06 * np.sin(2 * 2 * np.pi * np.cumsum(f * vib) / SR)
    br = fft_filter(noise(n), f * .8, f * 2.5, 1)
    y += .03 * br / (np.std(br) + 1e-9)
    return y * gate(n, dur, .05, rel) * (.35 + .65 * vel)


def banjo(m, dur, vel=.6):
    return S.pluck(m, min(dur, .5), vel, pos=.08, decay=.45, bright=1.8)


def twang(m, dur, vel=.6):
    """Reverb-drenched twangy guitar with tremolo."""
    y = S.pluck(m, max(dur, .8), vel, pos=.12, decay=1.6, bright=1.5)
    t = tarr(len(y))
    return y * (1 + .3 * np.sin(2 * np.pi * 6 * t))


def brass(m, dur, vel=.6):
    """Soft cute trumpet-ish (filtered saw with a little blat)."""
    f = mtof(m)
    rel = .08
    n = int((dur + rel) * SR)
    t = tarr(n)
    fr = f * (1 - .015 * np.exp(-t / .04)) * (1 + .004 * np.sin(2 * np.pi * 5 * t) * np.clip(t - .25, 0, 1))
    y = wt('saw', fr, 30)
    y = fft_filter(y, 150, 2500 + 2000 * vel, 1)
    return y * gate(n, dur, .03, rel) * (.35 + .65 * vel) * .5


def drone(m, dur, vel=.5):
    """Low monastery drone with a vowel-ish formant."""
    f = mtof(m)
    n = int((dur + 1.0) * SR)
    t = tarr(n)
    y = wt('saw', f * np.ones(n), 40) + wt('saw', f * 1.003 * np.ones(n), 40, .3)
    y = fft_filter(y, 40, 900, 1) + .3 * fft_filter(y, 600, 800, 3)
    return y * gate(n, dur, .8, 1.0) * vel * .25


S.INSTR.update(bowl=bowl, whistle=whistle, banjo=banjo, twang=twang, brass=brass, drone=drone)
S.DRUMS.update(snare=snare, ghost=snare_ghost, anvil=anvil, frame=frame_drum, washboard=washboard)
