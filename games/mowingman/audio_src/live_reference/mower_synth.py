#!/usr/bin/env python3
"""Mower sound prototypes: physically motivated, fully procedural.
usage: mower_synth.py <outdir>   -> mower_gas_v1.wav, mower_robot_v1.wav"""
import sys, wave
import numpy as np

SR = 48000
rng = np.random.default_rng(11)


def filt(x, H):
    n = len(x)
    f = np.fft.rfftfreq(n, 1 / SR)
    return np.fft.irfft(np.fft.rfft(x) * H(f), n)


def bp(f, f0, q):
    s = 1j * f / f0
    return (s / q) / (1 + s / q + s * s)


def lp(f, fc, n=1):
    return (1 / (1 + 1j * f / fc)) ** n


def hp(f, fc, n=1):
    s = 1j * f / fc
    return (s / (1 + s)) ** n


def norm(x):
    return x / (np.sqrt(np.mean(x * x)) + 1e-12)


def wander(n, rate):
    """smooth random control signal, roughly unit variance, bandwidth ~rate Hz"""
    k = int(n / SR * rate) + 3
    return np.interp(np.arange(n) / SR * rate, np.arange(k), rng.standard_normal(k))


def place(times, amps, n):
    idx = times * SR
    i = np.floor(idx).astype(int)
    fr = idx - i
    ok = (i >= 0) & (i < n - 1)
    out = np.zeros(n)
    np.add.at(out, i[ok], amps[ok] * (1 - fr[ok]))
    np.add.at(out, i[ok] + 1, amps[ok] * fr[ok])
    return out


def crossings(phase, t, offset=0.0):
    k = np.arange(1, int(phase[-1] - offset) + 1) + offset
    return np.interp(k, phase, t)


def write(path, left, right):
    st = np.stack([left, right], 1)
    st = np.tanh(st * 0.9 / np.percentile(np.abs(st), 99.7))
    st *= 0.7 / np.max(np.abs(st))
    fade = int(0.03 * SR)
    st[:fade] *= np.linspace(0, 1, fade)[:, None]
    st[-fade:] *= np.linspace(1, 0, fade)[:, None]
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes((st * 32767).astype("<i2").tobytes())


def gas(path):
    """Walk-behind single-cylinder four-stroke with a mechanical governor.
    0-1 start, 1-7 no load, 7-13 short grass, 13-18 tall grass, 18-20.5 no load, then cut-off."""
    D, CR, OFF = 24.0, 1000, 20.5
    nc = int(D * CR)
    tc = np.arange(nc) / CR
    base = np.zeros(nc)
    base[(tc > 7) & (tc < 13)] = 0.35
    base[(tc >= 13) & (tc < 18)] = 0.85
    win = np.hanning(500)
    base = np.convolve(base, win / win.sum(), "same")
    k = int(D * 4) + 3
    wob = np.interp(tc * 4, np.arange(k), rng.standard_normal(k))
    load_c = np.clip(base * (1 + 0.35 * wob), 0, 1.2)
    rpm_c, thr_c = np.zeros(nc), np.zeros(nc)
    r, th = 500.0, 1.0
    for i in range(nc):
        ign = tc[i] < OFF
        want = np.clip(0.22 + 0.0016 * (3050 - r), 0.06, 1.0)
        th += (want - th) / (0.12 * CR)
        acc = (7000 * th if ign else -500) - (4200 * load_c[i] + 1700) * r / 3000
        r = max(r + acc / CR, 1.0)
        rpm_c[i], thr_c[i] = r, th
    n = int(D * SR)
    t = np.arange(n) / SR
    rpm = np.interp(t, tc, rpm_c)
    thr = np.interp(t, tc, thr_c)
    load = np.interp(t, tc, load_c)
    rev = np.cumsum(rpm / 60) / SR          # crank revolutions
    cyc = rev / 2                           # four-stroke cycles
    spin = (rpm / 3000)

    # combustion: one firing per cycle, cycle-to-cycle variation, light-load weak fires
    ft = crossings(cyc, t)
    fth = np.interp(ft, t, thr)
    amp = (0.3 + 0.7 * fth) * (1 + 0.13 * rng.standard_normal(len(ft)))
    weak = (fth < 0.35) & (rng.random(len(ft)) < 0.10)
    amp[weak] *= 0.35
    amp[np.roll(weak, 1)] *= 1.2
    amp[(ft > OFF) | (np.interp(ft, t, rpm) < 300)] = 0
    imp = place(ft, amp, n)
    exhaust = norm(filt(imp, lambda f: hp(f, 45, 2) * lp(f, 2600) * (
        2.0 * lp(f, 300, 2) + 1.0 * bp(f, 95, 1.5) + 1.0 * bp(f, 210, 3)
        + 0.7 * bp(f, 430, 4) + 0.4 * bp(f, 880, 5) + 0.15 * bp(f, 1700, 4))))
    env = np.maximum(filt(imp, lambda f: lp(f, 60, 2)), 0)
    exnoise = norm(filt(rng.standard_normal(n), lambda f: hp(f, 300, 2) * lp(f, 3000, 2)) * env)

    # steel deck and shroud ringing, struck once per revolution
    rt = crossings(rev, t)
    rimp = place(rt, (1 + 0.2 * rng.standard_normal(len(rt))) * np.interp(rt, t, spin) ** 2, n)
    deck = norm(filt(rimp + 0.6 * imp, lambda f: sum(
        g * bp(f, f0, q) for f0, q, g in
        [(180, 10, 1.0), (310, 14, 0.9), (545, 18, 0.7), (870, 20, 0.5), (1320, 22, 0.35), (2100, 25, 0.2)])))

    # valve train clatter, two events per cycle
    vt = np.concatenate([crossings(cyc, t, 0.1), crossings(cyc, t, 0.62)])
    venv = np.maximum(filt(place(vt, np.interp(vt, t, spin) * (1 + 0.3 * rng.standard_normal(len(vt))), n),
                           lambda f: lp(f, 500, 2)), 0)
    clatter = norm(filt(rng.standard_normal(n), lambda f: hp(f, 2500, 2) * (0.3 + bp(f, 3600, 2))) * venv)

    # blade: two tips, broadband air noise chopped at blade-pass rate, plus imbalance tones
    th2 = 2 * np.pi * rev
    chop = 1 + 0.55 * np.cos(2 * th2) + 0.2 * np.cos(4 * th2 + 0.7)
    air = filt(rng.standard_normal(n), lambda f: hp(f, 120, 2) * lp(f, 420) * lp(f, 5000))
    blade = norm(air * chop * spin ** 2.5 * (1 - 0.3 * np.clip(load, 0, 1)))
    tones = norm((np.sin(th2) + 0.6 * np.sin(2 * th2 + 0.4) + 0.25 * np.sin(4 * th2)) * spin ** 2)

    # cutting: tip-synchronous shredding, clumps thumping the deck in tall grass
    grain = np.clip(0.6 + 0.5 * wander(n, 30), 0, None) ** 2
    shred = filt(rng.standard_normal(n), lambda f: hp(f, 1200, 2) * lp(f, 6000, 2)) * chop * grain
    clump = np.clip(wander(n, 7), 0, None) ** 2
    chuff = norm(filt(rng.standard_normal(n), lambda f: bp(f, 520, 1.2)) * clump) * np.clip(load - 0.4, 0, None) * 2
    cut = (norm(shred) + 0.9 * chuff) * load * spin

    mono = (1.0 * exhaust + 0.25 * exnoise + 0.30 * deck + 0.12 * clatter
            + 0.45 * blade + 0.20 * tones + 0.9 * cut)
    # ground bounce for a listener standing behind the handle
    d = int(0.0021 * SR)
    mono[d:] += 0.35 * filt(mono, lambda f: lp(f, 3500))[:-d]
    side = filt(rng.standard_normal(n), lambda f: hp(f, 800) * lp(f, 4000)) * 0.02 * (blade.std() + np.abs(cut))
    write(path, mono + side, mono - side)


def robot(path):
    """Electric robotic mower: brushless disc motor, three pivoting razor blades, two geared wheel motors.
    0.5 spin-up, 2.5-7.5 cutting, 8-9.5 pivot turn, 10-13.5 cutting, 14 spin-down."""
    D = 16.0
    n = int(D * SR)
    t = np.arange(n) / SR

    def ramp(points):
        xs, ys = zip(*points)
        return filt(np.interp(t, xs, ys), lambda f: lp(f, 3, 2))

    cutting = ramp([(0, 0), (2.5, 0), (2.7, 1), (7.5, 1), (7.7, 0), (10, 0), (10.2, 1), (13.5, 1), (13.7, 0), (D, 0)])
    dens = np.clip(cutting * (1 + 0.5 * wander(n, 2)), 0, None)
    disc = ramp([(0, 0), (0.5, 0), (1.7, 40), (14, 40), (15.6, 0), (D, 0)]) * (1 - 0.025 * dens)
    vl = ramp([(0, 0), (1.8, 0), (2.4, 1), (7.8, 1), (8.1, 1), (9.4, 1), (9.7, 1), (13.8, 1), (14.4, 0), (D, 0)])
    vr = ramp([(0, 0), (1.8, 0), (2.4, 1), (7.8, 1), (8.1, -0.6), (9.4, -0.6), (9.7, 1), (13.8, 1), (14.4, 0), (D, 0)])

    drev = np.cumsum(disc) / SR
    el = 2 * np.pi * 7 * drev * (1 + 0.0)                       # 7 pole pairs
    jit = 1 + 0.03 * wander(n, 40)
    s = disc / 40
    motor = (np.sin(el) + 0.45 * np.sin(2 * el + 1) + 0.2 * np.sin(3 * el) + 0.3 * np.sin(6 * el + 2)) * jit * s ** 1.5
    whoosh = filt(rng.standard_normal(n), lambda f: hp(f, 300, 2) * lp(f, 2500, 2)) \
        * (1 + 0.25 * np.cos(2 * np.pi * 3 * drev)) * s ** 2.5

    def wheel(v):
        a = np.abs(v)
        ph = 2 * np.pi * np.cumsum(520 * a) / SR
        rough = 1 + 0.35 * np.sin(ph / 13) + 0.15 * wander(n, 60)
        tone = (np.sin(ph) + 0.5 * np.sin(2 * ph + 0.5) + 0.2 * np.sin(3.02 * ph)) * rough
        brush = filt(rng.standard_normal(n), lambda f: bp(f, 3200, 1.5)) * (1 + 0.5 * np.sin(ph / 13))
        return (tone + 0.5 * brush / (brush.std() + 1e-9)) * a ** 0.7

    # razor snips: sparse ticks, rate follows grass density
    slots = rng.random(n) < dens * 150 / SR
    ticks = filt(slots * rng.lognormal(0, 0.6, n), lambda f: hp(f, 2500, 2) * (0.4 + bp(f, 5200, 1.2)))
    hiss = filt(rng.standard_normal(n), lambda f: hp(f, 3000, 2) * lp(f, 9000)) * dens

    centre = 0.5 * norm(motor) + 0.45 * norm(whoosh) + 0.3 * norm(ticks) + 0.25 * norm(hiss)
    wl, wr = wheel(vl), wheel(vr)
    sc = 0.35 / (wl.std() + 1e-9)
    left = centre + sc * (0.75 * wl + 0.25 * wr)
    right = centre + sc * (0.25 * wl + 0.75 * wr)
    write(path, left, right)


if __name__ == "__main__":
    out = sys.argv[1]
    gas(out + "/mower_gas_v1.wav")
    robot(out + "/mower_robot_v1.wav")
    print("ok")
