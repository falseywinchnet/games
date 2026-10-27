"""Zen Construction's sounds. Synthesized, no samples.

Effects: the slings clipped on and let go, stones knocking, a collapse, a
rock whooshing back to the bowl and landing in its wood, rocks poured into
the bowl for a new site, the relay of the motor engaging, a stalled lift,
the new-best chime on stone bars, the duck's squeak, a UI tick, birds in the
trees. Beds (looped): the brook babbling, the crane's small electric motor.

Everything is modal synthesis (sums of decaying sines) and shaped noise.
Writes 48 kHz 16-bit WAVs into ../assets/audio; the brook goes out as a WAV
master into masters/ to be encoded to AAC.
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'engine'))
from synth import SR, tarr, fft_filter, make_ir, conv_reverb, LoopMix, write_wav, mtof  # noqa: E402

HERE = os.path.dirname(__file__)
OUT = os.path.join(HERE, '..', 'assets', 'audio')
MASTERS = os.path.join(HERE, 'masters')
rng = np.random.default_rng(2604)


def secs(n):
    return int(n * SR)


def modes(n, freqs, amps, taus, phases=None):
    """Decaying sines: a struck object's modes."""
    t = tarr(n)
    y = np.zeros(n)
    for i, (f, a, tau) in enumerate(zip(freqs, amps, taus)):
        if f > 20000:
            continue
        ph = rng.uniform(0, 6.28) if phases is None else phases[i]
        y += a * np.exp(-t / tau) * np.sin(2 * np.pi * f * t + ph)
    return y


def burst(n, lo, hi, tau, seed=None):
    """A breath of band-limited noise, decaying."""
    r = np.random.default_rng(seed)
    x = fft_filter(r.standard_normal(n), lo, hi, 2)
    return x * np.exp(-tarr(n) / tau)


def place(dst, src, at):
    s = secs(at)
    e = min(len(dst), s + len(src))
    if e > s:
        dst[s:e] += src[:e - s]


def finish(x, peak_db, fade=0.004):
    x = np.asarray(x, dtype=float)
    x = x / (np.max(np.abs(x)) + 1e-12) * 10 ** (peak_db / 20)
    k = min(x.shape[-1], secs(fade))
    ramp = np.linspace(1, 0, k)
    if x.ndim == 1:
        x[-k:] *= ramp
        x[:16] *= np.linspace(0, 1, 16)
    else:
        x[:, -k:] *= ramp
        x[:, :16] *= np.linspace(0, 1, 16)
    return x


def save(name, x, peak_db=-3.0):
    write_wav(os.path.join(OUT, name + '.wav'), finish(x, peak_db), bits=16)


def roomy(x, wet=0.18, length=0.9, rt=0.5):
    """A little outdoor air round a mono sound; returns stereo."""
    ir = make_ir(length, rt * 1.2, rt, rt * 0.5, 0.006, seed=11)
    n = len(x) + ir.shape[1]
    dry = np.zeros((2, n))
    dry[0, :len(x)] = x
    dry[1, :len(x)] = x
    wetx = conv_reverb(dry, ir, circular=False)[:, :n]
    return dry + wet * wetx / (np.max(np.abs(wetx)) + 1e-12) * np.max(np.abs(x))


# ---------------------------------------------------------------- stone and steel

def stone_knock(f0, vel=1.0, seed=None):
    """Two stones meeting: a click of contact, the stones' few inharmonic modes, a dull body."""
    r = np.random.default_rng(seed)
    n = secs(0.16)
    ratios = [1.0, 1.58 * r.uniform(.97, 1.03), 2.31 * r.uniform(.96, 1.04), 3.9 * r.uniform(.95, 1.05), 5.2]
    y = modes(n, [f0 * k for k in ratios], [1, .7, .45, .25, .12], [.016, .011, .008, .005, .003])
    y += 0.9 * burst(n, 1800, 9000, 0.0016, seed)
    y += 0.8 * modes(n, [r.uniform(140, 210)], [1], [.022])
    return y * vel


def wood_thock(f0=210, seed=None):
    """A stone dropped in the wooden bowl: a hollow, short ring."""
    n = secs(0.35)
    y = modes(n, [f0, f0 * 2.03, f0 * 3.4, f0 * 5.6], [1, .45, .2, .08], [.09, .05, .025, .012])
    y += 0.4 * burst(n, 300, 3000, 0.004, seed)
    return y


def clip():
    """The sling's hook snapping onto its ring: a spring gate's two clicks and a ring of steel."""
    n = secs(0.32)
    y = np.zeros(n)
    place(y, modes(secs(.2), [2950, 4720, 6910, 8840], [1, .7, .45, .25], [.028, .018, .011, .007]) + burst(secs(.2), 3000, 12000, .0012, 1), 0.0)
    place(y, 0.55 * modes(secs(.15), [3320, 5600, 7700], [1, .5, .3], [.02, .012, .007]) + 0.5 * burst(secs(.15), 3000, 12000, .001, 2), 0.045)
    place(y, 0.5 * modes(secs(.12), [175, 410], [1, .4], [.035, .02]), 0.0)
    return roomy(y, 0.12)


def unclip():
    """Let go: the gate clicks open, the strap slaps free, the ring jingles."""
    n = secs(0.45)
    y = np.zeros(n)
    place(y, 0.8 * modes(secs(.2), [2700, 4400, 6300], [1, .6, .3], [.022, .014, .008]) + 0.6 * burst(secs(.2), 3000, 11000, .001, 3), 0.0)
    place(y, 0.6 * burst(secs(.12), 250, 1800, .025, 4), 0.03)
    place(y, 0.35 * modes(secs(.3), [3900, 5150, 6600], [1, .8, .5], [.06, .045, .03]), 0.07)
    return roomy(y, 0.14)


def knock(variant):
    r = np.random.default_rng(100 + variant)
    f0 = [1250, 1520, 1080][variant]
    y = stone_knock(f0, 1.0, 200 + variant)
    if variant == 2:
        # a double tap: the rock rocks once and settles
        y2 = np.zeros(secs(.25))
        place(y2, y, 0)
        place(y2, 0.45 * stone_knock(f0 * r.uniform(.97, 1.03), 1, 300), 0.075)
        y = y2
    return roomy(y, 0.1, 0.6, 0.35)


def tumble():
    """The stack giving way: stones clattering down, gathering then spent, a low rumble under it."""
    n = secs(2.2)
    y = np.zeros(n)
    t = 0.0
    k = 0
    while t < 1.7:
        vel = (0.5 + 0.5 * np.sin(np.pi * min(1.0, t / 1.2))) * rng.uniform(.4, 1)
        place(y, vel * stone_knock(rng.uniform(800, 1700), 1, 400 + k), t)
        if rng.random() < .35:
            # a heavier one hitting the ledge: a dull thud under the click
            place(y, 0.4 * vel * modes(secs(.2), [rng.uniform(90, 150)], [1], [.05]), t)
        t += rng.uniform(.03, .16) * (1 + t)
        k += 1
    rumble = fft_filter(rng.standard_normal(n), 30, 260, 2)
    env = np.clip(tarr(n) / 0.15, 0, 1) * np.exp(-tarr(n) / 0.7)
    y += 0.6 * rumble / np.max(np.abs(rumble)) * env * np.max(np.abs(y)) * 0.5
    # grit: gravel stirred where they land
    for _ in range(90):
        at = rng.uniform(0.1, 1.9)
        place(y, 0.08 * modes(secs(.02), [rng.uniform(3500, 8000)], [1], [.002]), at)
    return roomy(y, 0.2, 1.0, 0.5)


def whoosh():
    """A rock swung back over to the bowl: air past it, rising and falling."""
    n = secs(0.9)
    t = tarr(n)
    x = rng.standard_normal(n)
    y = np.zeros(n)
    # a band sweeping up and back down, built from a few narrow bands crossfaded
    centres = [350, 550, 850, 1300, 1900]
    pos = np.sin(np.pi * np.clip(t / 0.8, 0, 1))
    for i, c in enumerate(centres):
        band = fft_filter(x, c * .75, c * 1.33, 2)
        w = np.exp(-((pos * (len(centres) - 1) - i) ** 2) / 0.8)
        y += band * w
    env = np.sin(np.pi * np.clip(t / 0.85, 0, 1)) ** 1.5
    return roomy(y * env, 0.15)


def landed():
    """The flown rock coming down in the bowl: wood and a knock."""
    n = secs(0.5)
    y = np.zeros(n)
    place(y, wood_thock(196, 7), 0)
    place(y, 0.6 * stone_knock(1180, 1, 8), 0.0)
    place(y, 0.25 * stone_knock(1400, 1, 9), 0.09)
    return roomy(y, 0.14, 0.8, 0.45)


def pour():
    """A new site: a heap of stones poured into the wooden bowl."""
    n = secs(2.4)
    y = np.zeros(n)
    t = 0.02
    k = 0
    while t < 1.9:
        density = np.exp(-((t - 0.45) / 0.55) ** 2)
        vel = rng.uniform(.25, 1) * (0.4 + 0.6 * density)
        place(y, vel * stone_knock(rng.uniform(850, 1800), 1, 600 + k), t)
        if rng.random() < 0.3:
            place(y, 0.5 * vel * wood_thock(rng.uniform(170, 230), 700 + k), t)
        t += rng.uniform(.012, .05) / (0.25 + density)
        k += 1
    return roomy(y, 0.18, 1.0, 0.5)


def tick():
    """A UI tick: a small wooden tap."""
    n = secs(0.08)
    y = modes(n, [1750, 2900, 4300], [1, .4, .15], [.009, .005, .003]) + 0.6 * modes(n, [420], [1], [.012])
    y += 0.3 * burst(n, 2000, 9000, .0008, 12)
    return y


def relay():
    """The motor engaging: a relay's clack and the first turn of the gears."""
    n = secs(0.3)
    y = np.zeros(n)
    place(y, modes(secs(.08), [3400, 1250, 5600], [1, .6, .3], [.01, .014, .005]) + 0.6 * burst(secs(.08), 2500, 10000, .001, 13), 0)
    place(y, 0.6 * modes(secs(.08), [3000, 1100], [1, .5], [.008, .012]), 0.014)
    t = tarr(secs(.22))
    spin = np.sin(2 * np.pi * np.cumsum(180 + 260 * np.clip(t / .2, 0, 1)) / SR)
    place(y, 0.18 * spin * np.sin(np.pi * np.clip(t / .22, 0, 1)), 0.03)
    return roomy(y, 0.08)


def clunk():
    """A lift the crane can't make: the brake's clunk and the motor sagging."""
    n = secs(0.9)
    y = np.zeros(n)
    place(y, modes(secs(.4), [92, 238, 410, 1300], [1, .6, .3, .2], [.12, .06, .03, .01]) + 0.5 * burst(secs(.4), 200, 3000, .006, 14), 0)
    t = tarr(secs(.7))
    f = 620 - 280 * np.clip(t / .6, 0, 1)
    ph = 2 * np.pi * np.cumsum(f) / SR
    sag = (np.sin(ph) + .5 * np.sin(2 * ph) + .2 * np.sin(3 * ph)) * np.exp(-t / .3) * 0.22
    place(y, sag, 0.03)
    return roomy(y, 0.12)


def best():
    """A new best: four stone bars up the pentatonic, and a high shimmer."""
    n = secs(3.2)
    L = np.zeros(n)
    R = np.zeros(n)
    notes = [74, 78, 81, 86]
    for i, m in enumerate(notes):
        f = mtof(m)
        y = modes(secs(2.4), [f, f * 2.76, f * 5.40, f * 8.93], [1, .38, .16, .06], [.55, .22, .08, .03])
        y += 0.25 * burst(secs(2.4), 2000, 9000, .0015, 20 + i)
        p = -0.5 + i / 3
        place(L, y * np.cos((p + 1) * np.pi / 4), 0.11 * i)
        place(R, y * np.sin((p + 1) * np.pi / 4), 0.11 * i)
    for i, m in enumerate([93, 98]):
        f = mtof(m)
        y = 0.25 * modes(secs(1.6), [f, f * 5.9], [1, .1], [.5, .05])
        place(L, y * (0.8 if i == 0 else 0.4), 0.5 + 0.16 * i)
        place(R, y * (0.4 if i == 0 else 0.8), 0.5 + 0.16 * i)
    x = np.stack([L, R])
    ir = make_ir(2.2, 1.8, 1.4, 0.7, 0.02, seed=21)
    wet = conv_reverb(x, ir, circular=False)[:, :n]
    return x + 0.35 * wet / np.max(np.abs(wet)) * np.max(np.abs(x))


def squeak():
    """The rubber duck, squeezed twice: a reedy squeak through a little rubber body."""
    n = secs(0.6)
    y = np.zeros(n)
    for k, (at, length, f0, f1) in enumerate([(0.0, 0.17, 1050, 1420), (0.24, 0.20, 1120, 1520)]):
        m = secs(length)
        t = tarr(m)
        f = f0 + (f1 - f0) * np.sin(np.pi / 2 * np.clip(t / (length * 0.6), 0, 1))
        f = f * (1 + 0.012 * np.sin(2 * np.pi * 31 * t))
        ph = 2 * np.pi * np.cumsum(f) / SR
        # a pulse-like reed: odd and even harmonics falling away
        tone = sum((0.85 ** h) * np.sin(h * ph) for h in range(1, 9))
        body = fft_filter(tone, 700, 3600, 2) + 0.3 * fft_filter(tone, 2400, 3200, 4)
        air = 0.15 * fft_filter(np.random.default_rng(30 + k).standard_normal(m), 3000, 9000, 2)
        env = np.clip(t / 0.015, 0, 1) * np.clip((length - t) / 0.03, 0, 1)
        place(y, (body + air) * env, at)
    return roomy(y, 0.1)


def bird(kind):
    """A small bird in the trees across the brook: three kinds of call."""
    if kind == 0:
        # tee-oo, tee-oo: a falling two-note whistle
        n = secs(1.6)
        y = np.zeros(n)
        for at in (0.0, 0.62):
            for j, (length, fa, fb) in enumerate([(0.16, 3600, 3450), (0.24, 2950, 2700)]):
                m = secs(length)
                t = tarr(m)
                f = fa + (fb - fa) * t / length
                ph = 2 * np.pi * np.cumsum(f) / SR
                env = np.sin(np.pi * np.clip(t / length, 0, 1)) ** 1.5
                place(y, (np.sin(ph) + 0.08 * np.sin(2 * ph)) * env, at + 0.2 * j)
    elif kind == 1:
        # a quick trill
        n = secs(1.2)
        y = np.zeros(n)
        for i in range(11):
            m = secs(0.045)
            t = tarr(m)
            f = 4300 + 900 * (t / 0.045) - 40 * i
            ph = 2 * np.pi * np.cumsum(f) / SR
            place(y, np.sin(ph) * np.sin(np.pi * t / 0.045) ** 2 * (0.6 + 0.4 * np.sin(np.pi * i / 10)), 0.07 * i)
    else:
        # a warble: up, wavering, down
        n = secs(1.3)
        m = secs(0.8)
        t = tarr(m)
        f = 2600 + 900 * np.sin(np.pi * t / 0.8) + 260 * np.sin(2 * np.pi * 14 * t)
        ph = 2 * np.pi * np.cumsum(f) / SR
        y = np.zeros(n)
        place(y, np.sin(ph) * np.sin(np.pi * t / 0.8) ** 2, 0.05)
    y = fft_filter(y, 1500, 9000, 1)
    return roomy(y, 0.35, 1.4, 0.9)


# ---------------------------------------------------------------- beds

def motor():
    """The crane's little electric motor, a seamless 2-second loop: a soft whine
    at the commutator's rate with its harmonics, the gears' mesh above it, a
    whisper of brush noise ticking at the rotation. Every frequency repeats a
    whole number of times in the loop. The game varies its pitch with speed."""
    L = secs(2.0)
    t = tarr(L)
    f1 = 280.0
    ph = 2 * np.pi * f1 * t - f1 * 0.004 * np.cos(2 * np.pi * 8.0 * t) / 8.0
    tone = (0.55 * np.sin(ph) + 1.0 * np.sin(2 * ph + 1.0) + 0.32 * np.sin(3 * ph + 2.0) + 0.22 * np.sin(4 * ph + 0.4)
            + 0.10 * np.sin(6 * ph + 1.7) + 0.05 * np.sin(9 * ph + 0.3))
    tone *= 1 + 0.12 * np.sin(2 * np.pi * 140.0 * t)
    gears = 0.07 * np.sin(2 * np.pi * 1690.0 * t) * (1 + 0.5 * np.sin(2 * np.pi * 56.0 * t))
    hum = 0.25 * np.sin(2 * np.pi * 70.0 * t)
    rnd = np.random.default_rng(50)
    brush = fft_filter(rnd.standard_normal(L), 2500, 7000, 2, circular=True)
    brush *= (0.5 + 0.5 * np.cos(2 * np.pi * 140.0 * t)) ** 4
    y = tone + gears + hum + 0.10 * brush / np.max(np.abs(brush)) * 3
    y = fft_filter(y, 50, 4200, 2, circular=True)
    stereo = np.stack([y, np.roll(y, 37)])
    return stereo


def brook():
    """The brook, a seamless 40-second loop: a wash of water, and over it the
    babble: thousands of small bubbles ringing (each a sine whose pitch rises
    as it rings, Minnaert's bubble), coming in gurgling runs, the odd deep
    gulp; a little air round it all."""
    LOOP = 40.0
    L = secs(LOOP)
    mix = LoopMix(L, circular=True)
    rnd = np.random.default_rng(77)
    t = tarr(L)
    # the wash: pink, band-limited, swelling slowly (every period divides the loop)
    for ch, pan in ((0, -0.6), (1, 0.6)):
        spec = np.fft.rfft(rnd.standard_normal(L))
        f = np.fft.rfftfreq(L, 1 / SR)
        shape = 1 / np.sqrt(np.maximum(f, 20)) / np.sqrt(1 + (f / 1800) ** 4) / np.sqrt(1 + (160 / np.maximum(f, 1)) ** 4)
        wash = np.fft.irfft(spec * shape, L)
        wash /= np.max(np.abs(wash))
        swell = 1 + 0.22 * np.sin(2 * np.pi * 3 * t / LOOP + ch) + 0.15 * np.sin(2 * np.pi * 11 * t / LOOP + 2 * ch) + 0.1 * np.sin(2 * np.pi * 29 * t / LOOP + 1)
        mix.add('wash', wash * swell, 0, pan, 0.12)
    # the babble's pace: busy runs and quieter spells
    pace = np.zeros(L)
    for k in range(1, 40):
        pace += rnd.uniform(0, 1) / k ** 0.7 * np.sin(2 * np.pi * k * t / LOOP + rnd.uniform(0, 6.28))
    pace = (pace - pace.min()) / (pace.max() - pace.min())
    pace = 0.25 + 0.75 * pace ** 1.5
    time = 0.0
    count = 0
    while time < LOOP:
        rate = 120 * pace[min(L - 1, secs(time))]
        time += rnd.exponential(1 / rate)
        f0 = float(np.exp(rnd.uniform(np.log(420), np.log(2600))))
        q = rnd.uniform(12, 30)
        tau = q / (np.pi * f0)
        m = secs(min(0.12, 5 * tau))
        tt = tarr(m)
        rise = rnd.uniform(0.04, 0.16)
        f = f0 * (1 + rise * tt / tau)
        ph = 2 * np.pi * np.cumsum(f) / SR
        amp = rnd.uniform(0.2, 1.0) * (f0 / 1000) ** -0.4
        bubble = amp * np.sin(ph) * np.exp(-tt / tau) * np.clip(tt / 0.0006, 0, 1)
        mix.add('babble', bubble, secs(time), rnd.uniform(-0.75, 0.75), 0.13)
        count += 1
    # the odd deep gulp, in twos and threes
    time = 1.0
    while time < LOOP:
        for j in range(rnd.integers(2, 4)):
            f0 = rnd.uniform(160, 340)
            tau = rnd.uniform(18, 30) / (np.pi * f0)
            m = secs(6 * tau)
            tt = tarr(m)
            ph = 2 * np.pi * np.cumsum(f0 * (1 + 0.1 * tt / tau)) / SR
            mix.add('babble', np.sin(ph) * np.exp(-tt / tau) * np.clip(tt / .002, 0, 1), secs(time + 0.07 * j), rnd.uniform(-.4, .4), 0.16)
        time += rnd.uniform(2.0, 5.0)
    x = mix.bus('wash') + mix.bus('babble')
    x[0] = fft_filter(x[0], 70, None, 2, circular=True)
    x[1] = fft_filter(x[1], 70, None, 2, circular=True)
    ir = make_ir(1.2, 0.9, 0.7, 0.35, 0.01, seed=31)
    wet = conv_reverb(x, ir, circular=True)
    x = x + 0.25 * wet * np.max(np.abs(x)) / np.max(np.abs(wet))
    print(f'brook: {count} bubbles')
    return x


# ---------------------------------------------------------------- the creek's mix

def rush():
    """The stream's steady rush, a seamless 37-second loop: band-limited noise
    with its body in the low mids, swelling and easing as the flow does,
    a few narrow gurgling bands wandering through it."""
    LOOP = 37.0
    L = secs(LOOP)
    t = tarr(L)
    rnd = np.random.default_rng(91)
    out = np.zeros((2, L))
    f = np.fft.rfftfreq(L, 1 / SR)
    for ch in range(2):
        spec = np.fft.rfft(rnd.standard_normal(L))
        shape = 1 / np.maximum(f, 30) ** 0.35 / np.sqrt(1 + (f / 2600) ** 4) / np.sqrt(1 + (110 / np.maximum(f, 1)) ** 4)
        body = np.fft.irfft(spec * shape, L)
        body /= np.max(np.abs(body))
        swell = np.ones(L)
        for k in (2, 5, 9, 17, 31):
            swell += rnd.uniform(0.04, 0.12) * np.sin(2 * np.pi * k * t / LOOP + rnd.uniform(0, 6.28))
        out[ch] = body * swell
    # gurgles: narrow bands that come and go
    for g in range(6):
        centre = rnd.uniform(300, 1400)
        band = fft_filter(rnd.standard_normal(L), centre * 0.85, centre * 1.18, 3, circular=True)
        band /= np.max(np.abs(band))
        k = int(rnd.integers(3, 12))
        env = np.maximum(0, np.sin(2 * np.pi * k * t / LOOP + rnd.uniform(0, 6.28))) ** 3
        pan = rnd.uniform(-0.7, 0.7)
        out[0] += 0.35 * band * env * np.cos((pan + 1) * np.pi / 4)
        out[1] += 0.35 * band * env * np.sin((pan + 1) * np.pi / 4)
    return out


def reeds():
    """Wind in the reeds and grass, a seamless 53-second loop: a soft high
    rustle that comes in gusts, the odd dry tick of stems."""
    LOOP = 53.0
    L = secs(LOOP)
    t = tarr(L)
    rnd = np.random.default_rng(92)
    out = np.zeros((2, L))
    gust = np.zeros(L)
    for k in (1, 2, 3, 5, 8, 13):
        gust += rnd.uniform(0.3, 1.0) / k * np.sin(2 * np.pi * k * t / LOOP + rnd.uniform(0, 6.28))
    gust = (gust - gust.min()) / (gust.max() - gust.min())
    gust = 0.15 + 0.85 * gust ** 2
    for ch in range(2):
        hiss = fft_filter(rnd.standard_normal(L), 1400, 7500, 2, circular=True)
        flutter = 1 + 0.4 * fft_filter(rnd.standard_normal(L), 2, 14, 1, circular=True) * 8
        out[ch] = hiss / np.max(np.abs(hiss)) * gust * np.clip(flutter, 0.2, 2.0)
    mix = LoopMix(L, circular=True)
    for _ in range(140):
        at = rnd.uniform(0, LOOP)
        g = gust[min(L - 1, secs(at))]
        tick = modes(secs(0.02), [rnd.uniform(2500, 6000)], [1], [0.002])
        mix.add('ticks', tick * g, secs(at), rnd.uniform(-0.9, 0.9), 0.25)
    return out + mix.bus('ticks')


def splash(variant):
    """Water striking a stone: a slap of spray, then the bubbles it leaves."""
    rnd = np.random.default_rng(300 + variant)
    n = secs(0.6)
    y = np.zeros(n)
    slap = burst(secs(0.12), 500 + 300 * variant, 4500, 0.018 + 0.01 * rnd.random(), 310 + variant)
    place(y, slap, 0.0)
    for b in range(int(rnd.integers(4, 10))):
        f0 = float(np.exp(rnd.uniform(np.log(500), np.log(2600))))
        tau = rnd.uniform(14, 26) / (np.pi * f0)
        m = secs(min(0.1, 6 * tau))
        tt = tarr(m)
        ph = 2 * np.pi * np.cumsum(f0 * (1 + rnd.uniform(0.05, 0.14) * tt / tau)) / SR
        place(y, rnd.uniform(0.2, 0.7) * np.sin(ph) * np.exp(-tt / tau) * np.clip(tt / 0.0005, 0, 1), rnd.uniform(0.01, 0.3))
    if variant == 3:
        # a plop: a deeper bubble, something heavier dropped in
        f0 = 340.0
        tau = 22 / (np.pi * f0)
        m = secs(6 * tau)
        tt = tarr(m)
        ph = 2 * np.pi * np.cumsum(f0 * (1 + 0.12 * tt / tau)) / SR
        place(y, 1.2 * np.sin(ph) * np.exp(-tt / tau), 0.0)
    return roomy(y, 0.2, 0.8, 0.4)


def bed_knock(variant):
    """Pebbles knocking along the stream bed: stone clicks muffled by the water, in a little cluster."""
    rnd = np.random.default_rng(400 + variant)
    n = secs(0.9)
    y = np.zeros(n)
    at = 0.0
    for k in range(int(rnd.integers(2, 5))):
        knock = stone_knock(rnd.uniform(900, 1600), rnd.uniform(0.4, 1.0), 410 + variant * 7 + k)
        place(y, knock, at)
        at += rnd.uniform(0.05, 0.22)
    y = fft_filter(y, 120, 1300, 2)
    return roomy(y, 0.15, 0.7, 0.35)


def frog(kind):
    """Frogs along the bank, a little way off: a two-note ribbit, a green
    frog's twanging gunk, a deep croaking rattle."""
    rnd = np.random.default_rng(500 + kind)
    if kind == 0:
        n = secs(0.6)
        y = np.zeros(n)
        for j, (length, f0) in enumerate([(0.11, 420.0), (0.13, 470.0)]):
            m = secs(length)
            t = tarr(m)
            pulses = (0.5 + 0.5 * np.sin(2 * np.pi * 70 * t)) ** 3
            ph = 2 * np.pi * np.cumsum(f0 * (1 - 0.08 * t / length)) / SR
            tone = sum((0.75 ** h) * np.sin(h * ph) for h in range(1, 10))
            body = fft_filter(tone, 600, 2600, 2) + 0.5 * fft_filter(tone, 1100, 1500, 3)
            env = np.sin(np.pi * np.clip(t / length, 0, 1))
            place(y, body * pulses * env, 0.17 * j)
    elif kind == 1:
        n = secs(0.6)
        m = secs(0.35)
        t = tarr(m)
        f0 = 175 * (1 - 0.06 * t / 0.35)
        ph = 2 * np.pi * np.cumsum(f0) / SR
        tone = sum((0.7 ** h) * np.sin(h * ph + h) for h in range(1, 12)) * np.exp(-t / 0.09)
        y = np.zeros(n)
        place(y, fft_filter(tone, 150, 1800, 2) + 0.6 * fft_filter(tone, 520, 720, 3), 0.0)
    else:
        n = secs(0.9)
        m = secs(0.55)
        t = tarr(m)
        pulses = (0.5 + 0.5 * np.sin(2 * np.pi * 28 * t)) ** 6
        ph = 2 * np.pi * np.cumsum(118 * (1 + 0.04 * np.sin(np.pi * t / 0.55))) / SR
        tone = sum((0.8 ** h) * np.sin(h * ph) for h in range(1, 14))
        body = fft_filter(tone, 90, 1200, 2) + 0.5 * fft_filter(tone, 280, 380, 3)
        y = np.zeros(n)
        place(y, body * pulses * np.sin(np.pi * np.clip(t / 0.55, 0, 1)) ** 0.5, 0.0)
    _ = rnd
    return roomy(y, 0.3, 1.2, 0.7)


def main():
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(MASTERS, exist_ok=True)
    save('zc_clip', clip(), -5)
    save('zc_unclip', unclip(), -6)
    for v in range(3):
        save(f'zc_knock_{v + 1}', knock(v), -4)
    save('zc_tumble', tumble(), -4)
    save('zc_whoosh', whoosh(), -8)
    save('zc_land', landed(), -5)
    save('zc_reset', pour(), -5)
    save('zc_click', tick(), -10)
    save('zc_relay', relay(), -9)
    save('zc_clunk', clunk(), -6)
    save('zc_best', best(), -4)
    save('zc_squeak', squeak(), -7)
    for k in range(3):
        save(f'zc_bird_{k + 1}', bird(k), -6)
    m = motor()
    write_wav(os.path.join(OUT, 'zc_motor.wav'), m / np.max(np.abs(m)) * 10 ** (-3 / 20), bits=16)
    b = brook()
    write_wav(os.path.join(MASTERS, 'zc_brook.wav'), b / np.max(np.abs(b)) * 10 ** (-3 / 20), bits=16)
    for name, bed in (('zc_rush', rush()), ('zc_reeds', reeds())):
        write_wav(os.path.join(MASTERS, name + '.wav'), bed / np.max(np.abs(bed)) * 10 ** (-3 / 20), bits=16)
    for k in range(4):
        save(f'zc_splash_{k + 1}', splash(k), -5)
    for k in range(3):
        save(f'zc_bed_{k + 1}', bed_knock(k), -6)
    for k in range(3):
        save(f'zc_frog_{k + 1}', frog(k), -6)


if __name__ == '__main__':
    main()
