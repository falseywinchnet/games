#!/usr/bin/env python3
"""Renders a page being turned, as 48 kHz stereo 16-bit WAV, with the standard library only.

    python3 make_page.py <out dir> [candidates]

A turned page is paper noise shaped by the page's motion: a few dry crackles as the leaf
is lifted, a swish of air as it crosses (moving across the stereo field, right to left,
as a page turned forward does), and a soft, low puff as it settles on the others. The
paper's character is its band (thin India paper is high and light, an old book's leaf
lower and fuller) and how much it crackles.

With "candidates", five labelled variations are written for listening (pl_page_a.wav ...
pl_page_e.wav); without, the chosen one is written as pl_page.wav, with its mirror,
pl_page_back.wav, for a page turned back. Deterministic: the same
script gives the same bytes on every machine.
"""
import math
import random
import struct
import sys
import wave
from pathlib import Path

RATE = 48000


class Band:
    """A two-pole band-pass filter (the RBJ cookbook's, constant skirt gain)."""

    def __init__(self, centre, q):
        w = 2 * math.pi * centre / RATE
        alpha = math.sin(w) / (2 * q)
        a0 = 1 + alpha
        self.b0, self.b2 = alpha / a0, -alpha / a0
        self.a1, self.a2 = -2 * math.cos(w) / a0, (1 - alpha) / a0
        self.x1 = self.x2 = self.y1 = self.y2 = 0.0

    def __call__(self, x):
        y = self.b0 * x + self.b2 * self.x2 - self.a1 * self.y1 - self.a2 * self.y2
        self.x2, self.x1, self.y2, self.y1 = self.x1, x, self.y1, y
        return y


class Lowpass:
    """A two-pole low-pass filter (RBJ cookbook)."""

    def __init__(self, corner, q=0.707):
        w = 2 * math.pi * corner / RATE
        alpha = math.sin(w) / (2 * q)
        a0 = 1 + alpha
        self.b0 = self.b2 = (1 - math.cos(w)) / 2 / a0
        self.b1 = (1 - math.cos(w)) / a0
        self.a1, self.a2 = -2 * math.cos(w) / a0, (1 - alpha) / a0
        self.x1 = self.x2 = self.y1 = self.y2 = 0.0

    def __call__(self, x):
        y = self.b0 * x + self.b1 * self.x1 + self.b2 * self.x2 - self.a1 * self.y1 - self.a2 * self.y2
        self.x2, self.x1, self.y2, self.y1 = self.x1, x, self.y1, y
        return y


def smooth(t, start, end):
    """0 before `start`, 1 after `end`, an S-curve between."""
    if t <= start:
        return 0.0
    if t >= end:
        return 1.0
    u = (t - start) / (end - start)
    return u * u * (3 - 2 * u)


def page(seed, seconds, low, high, crackle, swish_at, settle_at, thump, level):
    """One turn. `low` and `high` bound the paper's band (Hz); `crackle` is crackles a
    second at the lift; the swish peaks at `swish_at` and the leaf settles at `settle_at`
    (seconds); `thump` is how much of the low puff there is."""
    rng = random.Random(seed)
    count = int(seconds * RATE)
    body = Band(math.sqrt(low * high), 0.9)
    edge = Band(high * 0.7, 1.4)
    air = Band(low * 0.7, 0.8)
    puff = Band(150, 1.0)
    puff_top = Lowpass(450)
    tops = [Lowpass(high * 1.3), Lowpass(high * 1.3)]  # nothing of paper above its band
    left, right = [], []
    lift_end = swish_at * 0.7
    crackles = []  # (start sample, length, gain): only while the leaf is lifted
    for _ in range(int(crackle * lift_end * 1.5)):
        at = rng.uniform(0.01, lift_end)
        crackles.append((int(at * RATE), int(rng.uniform(0.002, 0.008) * RATE), rng.uniform(0.25, 1.0)))
    crackles.sort()
    active = []
    next_crackle = 0
    for index in range(count):
        t = index / RATE
        noise = rng.uniform(-1, 1)
        # The motion: the leaf eased up, the swish as it crosses, nothing once it lands.
        lift = smooth(t, 0.0, lift_end) * (1 - smooth(t, settle_at - 0.02, settle_at + 0.02))
        swish = math.exp(-((t - swish_at) / (swish_at * 0.5)) ** 2)
        landing = math.exp(-max(0.0, t - settle_at) * 35) * smooth(t, settle_at - 0.025, settle_at)
        paper = (body(noise) * 0.9 + edge(noise) * 0.3) * (0.12 * lift + swish)
        wind = air(noise) * swish * 0.45
        while next_crackle < len(crackles) and crackles[next_crackle][0] <= index:
            active.append(list(crackles[next_crackle]) + [0])
            next_crackle += 1
        crack = 0.0
        for burst in active:
            burst[3] += 1
            crack += rng.uniform(-1, 1) * burst[2] * math.exp(-burst[3] / max(1, burst[1]) * 3)
        active = [burst for burst in active if burst[3] < burst[1] * 2]
        crack = edge(crack) * 0.9
        low_puff = puff_top(puff(noise)) * landing * thump * 4.0
        # Once the leaf has landed, nothing is left moving.
        settled = 1 - smooth(t, settle_at + 0.02, seconds)
        sample = (tops[1](tops[0](paper + wind + crack)) + low_puff) * settled
        # The leaf crosses from right to left as it turns.
        pan = smooth(t, swish_at * 0.4, settle_at)
        left.append(sample * (0.55 + 0.45 * pan))
        right.append(sample * (1.0 - 0.45 * pan))
    # Ends without a click; peaks set to `level`.
    fade = int(0.006 * RATE)
    for index in range(fade):
        g = index / fade
        left[index] *= g
        right[index] *= g
        left[-1 - index] *= g
        right[-1 - index] *= g
    peak = max(max(abs(v) for v in left), max(abs(v) for v in right), 1e-9)
    scale = level / peak
    return [v * scale for v in left], [v * scale for v in right]


def write(path, channels):
    left, right = channels
    with wave.open(str(path), "wb") as output:
        output.setnchannels(2)
        output.setsampwidth(2)
        output.setframerate(RATE)
        frames = bytearray()
        for l, r in zip(left, right):
            frames += struct.pack("<hh", int(round(l * 32767)), int(round(r * 32767)))
        output.writeframes(bytes(frames))
    print("wrote", path, f"{len(left) / RATE:.2f} s")


CANDIDATES = {
    # A: whisper, air only: a low soft swish, no crackle.
    "a": dict(seed=11, seconds=0.55, low=500, high=2600, crackle=0, swish_at=0.22, settle_at=0.40, thump=0.25,
              level=0.30),
    # B: India paper, thin and crisp: high, light, a fine crackle, quick.
    "b": dict(seed=12, seconds=0.50, low=1800, high=7000, crackle=40, swish_at=0.18, settle_at=0.34, thump=0.15,
              level=0.30),
    # C: lift and settle: a crackle as it is lifted, the swish, a soft landing.
    "c": dict(seed=13, seconds=0.65, low=1000, high=5000, crackle=55, swish_at=0.26, settle_at=0.48, thump=0.5,
              level=0.32),
    # D: an old library book: a fuller, lower leaf, more crackle, slower.
    "d": dict(seed=14, seconds=0.75, low=600, high=3500, crackle=70, swish_at=0.30, settle_at=0.56, thump=0.7,
              level=0.34),
    # E: feather: the shortest and quietest, for reading quickly.
    "e": dict(seed=15, seconds=0.38, low=1200, high=5200, crackle=18, swish_at=0.14, settle_at=0.26, thump=0.2,
              level=0.24),
}
CHOSEN = "c"


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "assets" / "audio"
    out.mkdir(parents=True, exist_ok=True)
    if len(sys.argv) > 2 and sys.argv[2] == "candidates":
        for name, settings in CANDIDATES.items():
            write(out / f"pl_page_{name}.wav", page(**settings))
    else:
        # A page turned forward crosses right to left; one turned back, the mirror.
        left, right = page(**CANDIDATES[CHOSEN])
        write(out / "pl_page.wav", (left, right))
        write(out / "pl_page_back.wav", (right, left))


if __name__ == "__main__":
    main()
