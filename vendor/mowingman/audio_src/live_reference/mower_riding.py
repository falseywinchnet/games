#!/usr/bin/env python3
"""Riding mower v4, fitted to the reference recording (90-degree V-twin, measured 3486 rpm).
usage: mower_riding.py <out.wav>
0 catch + idle, 3-4.5 throttle up, 6.5 PTO on, 9 short grass, 13 lull, 16-21 heavy grass with surges,
22-24 short, 24 clear, 25.5 PTO off, 27.5 throttle down, 30 key off."""
import sys
import numpy as np
import mower_synth as M
from mower_synth import SR, filt, bp, lp, hp, norm, wander, place, crossings, write

rng = M.rng
FULL = 3600.0
BLADE = 1.0125  # spindle revs per engine rev; keeps the v5 blade pitch at 3600 rpm

# exhaust transfer measured from the reference: harmonic levels with the V-twin firing comb divided out
EX_F = [10, 29, 58, 87, 116, 145, 174, 203, 232, 261, 290, 319, 350, 460, 640, 1000, 2000, 6000]
EX_DB = [-36, -14, -4, -6, -3, 0, -2, 0, -10, -9, -11, -17, -22, -22, -27, -36, -46, -60]


def exhaust_tf(f):
    return 10 ** (np.interp(np.log(np.maximum(f, 1)), np.log(EX_F), EX_DB) / 20)


def norm_on(x, mask):
    return x / (np.sqrt(np.mean(x[mask] ** 2)) + 1e-12)


def riding(path):
    D, CR, CRANK, OFF = 32.0, 1000, 0.35, 30.0
    nc = int(D * CR)
    tc = np.arange(nc) / CR
    tgt = np.interp(tc, [0, 2.6, 5.2, 27.3, 29, D], [1500, 1500, FULL, FULL, 1500, 1500])
    pto_c = ((tc > 6.5) & (tc < 25.5)).astype(float)
    base = np.interp(tc, [0, 8.6, 9.2, 12.6, 13.2, 14.2, 15, 16, 16.6, 18.4, 18.9, 19.5, 21, 21.6, 23.6, 24.3, D],
                     [0, 0, .3, .32, .1, .1, .3, .35, .7, .7, .3, .75, .75, .3, .3, 0, 0])

    def cw(rate):
        k = int(D * rate) + 3
        return np.interp(tc * rate, np.arange(k), rng.standard_normal(k))

    gl_c = np.clip(base * (1 + 0.3 * cw(0.6) + 0.25 * cw(2.5) + 0.12 * cw(7)), 0, 1.1)
    rpm_c, thr_c, bl_c, slip_c = (np.zeros(nc) for _ in range(4))
    r, th, b = 350.0, 0.6, 0.0
    for i in range(nc):
        t = tc[i]
        ign = CRANK < t < OFF
        want = np.clip(0.1 + 0.13 * tgt[i] / 3400 + 0.0022 * (tgt[i] - r), 0.05, 1.0)
        th += (want - th) / (0.15 * CR)
        want_b = r / 60 * BLADE
        db = (want_b - b) / 0.45 if pto_c[i] else -b / 0.9 - 4
        slip_c[i] = pto_c[i] * max(want_b - b, 0)
        b = max(b + db / CR, 0.0)
        acc = (5800 * th if ign else (0 if t < CRANK else -400)) - (1500 + 3600 * gl_c[i]) * r / 3400 * (t > CRANK) \
            - pto_c[i] * (1200 * (b / 50) ** 2 + 22 * max(db, 0))
        r = max(r + acc / CR, 1.0)
        rpm_c[i], thr_c[i], bl_c[i] = r, th, b
    n = int(D * SR)
    t = np.arange(n) / SR
    rpm, thr, bl, gl, slip = (np.interp(t, tc, c) for c in (rpm_c, thr_c, bl_c, gl_c, slip_c))
    rev = np.cumsum(rpm / 60) / SR
    cyc = rev / 2
    spin = rpm / FULL
    bs = bl / (FULL / 60 * BLADE)
    glc = np.clip(gl, 0, 1.1)
    full = (t > 5.6) & (t < 6.4)

    # V-twin, 270/450 degree firing; steady, because the reference harmonics are very clean
    imp = np.zeros(n)
    for off, gain in ((0.0, 1.0), (0.375, 0.95)):
        ft = crossings(cyc, t, off)
        fth = np.interp(ft, t, thr)
        amp = gain * (0.45 + 0.55 * fth) * (1 + 0.05 * rng.standard_normal(len(ft)))
        amp[(ft > OFF) | (np.interp(ft, t, rpm) < 150)] = 0
        imp += place(ft, amp, n)
    exhaust = norm_on(filt(imp, exhaust_tf), full)

    # steady broadband: cooling fan, intake, shroud. Nearly unmodulated in the reference.
    c2 = 2 * np.pi * cyc
    air = filt(rng.standard_normal(n), lambda f: hp(f, 80) * lp(f, 900) * lp(f, 6000, 2))
    air = norm_on(air * spin ** 2 * (1 + 0.06 * np.cos(2 * c2) + 0.06 * np.cos(3 * c2 + 1)), full)
    # intake: breathing through the air filter, one soft gulp per intake stroke, no sharp edges
    gulp = 1 + 0.35 * np.cos(c2 + 2.0) + 0.35 * np.cos(c2 + 2.0 - 2 * np.pi * 0.375)
    intake = norm_on(filt(rng.standard_normal(n), lambda f: bp(f, 330, 1.1) * lp(f, 900, 2))
                     * gulp * spin ** 1.5 * (0.5 + 0.5 * thr), full)
    whine = norm_on((np.sin(16 * c2) + 0.7 * np.sin(19 * c2 + 1) + 0.7 * np.sin(22 * c2 + 2)) * spin ** 2, full)
    vt = np.concatenate([crossings(cyc, t, o) for o in (0.1, 0.47, 0.62, 0.99)])
    venv = np.maximum(filt(place(vt, np.interp(vt, t, spin), n), lambda f: lp(f, 500, 2)), 0)
    clatter = norm_on(filt(rng.standard_normal(n), lambda f: hp(f, 2500, 2) * (0.3 + bp(f, 3400, 2))) * venv, full)

    # deck: three propellers. Blade-pass harmonics, slightly detuned so they beat like a multi-engine aircraft.
    bend = 1 + 0.012 * wander(n, 0.8)
    rough = np.clip(1 + 0.45 * norm(filt(rng.standard_normal(n), lambda f: bp(f, 31, 1.5))), 0.1, None)
    drone = np.zeros(n)
    for ratio in (1.0, 0.9955, 1.006):
        a = 2 * np.pi * np.cumsum(2 * bl * ratio * bend) / SR
        for k in range(1, 9):
            drone += 10 ** (-4 * (k - 1) / 20) * np.sin(k * a + rng.random() * 6.28)
    on = bs > 0.6
    drone = filt(drone * bs ** 2, lambda f: 0.5 + bp(f, 260, 2) + 0.7 * bp(f, 520, 2.5)) * rough
    drone = norm_on(drone, on)
    rough2 = np.clip(1 + 0.4 * norm(filt(rng.standard_normal(n), lambda f: bp(f, 26, 1.2))), 0.1, None)
    wind = norm(filt(rng.standard_normal(n), lambda f: hp(f, 150, 2) * lp(f, 420) * lp(f, 2200, 2))) * bs ** 2.5
    squeal = norm(filt(rng.standard_normal(n), lambda f: bp(f, 2900, 9))) * np.clip(slip / 30, 0, 1) ** 1.5
    clunk = filt(place(np.array([6.5, 25.5]), np.array([1.0, 0.5]), n),
                 lambda f: (bp(f, 95, 6) + bp(f, 140, 7) + 0.8 * bp(f, 230, 8)) * lp(f, 800))
    clunk /= np.max(np.abs(clunk))

    mono = (1.0 * exhaust + 0.22 * air + 0.22 * intake + 0.09 * whine + 0.05 * clatter
            + drone * (0.25 + 1.7 * glc) + wind * rough2 * (0.2 + 1.2 * glc)
            + 0.08 * squeal + 1.5 * clunk)
    d = int(0.0016 * SR)
    mono[d:] += 0.25 * filt(mono, lambda f: lp(f, 3000))[:-d]
    side = norm(filt(rng.standard_normal(n), lambda f: hp(f, 500) * lp(f, 4000))) * 0.04 * (spin ** 2 + glc)
    write(path, mono + side, mono - side)


if __name__ == "__main__":
    riding(sys.argv[1])
    print("ok")
