#!/usr/bin/env python3
"""Renders a page being turned, as 48 kHz stereo 16-bit WAV, with the standard library only.

    python3 make_page.py [out dir]

A page turned is the leaf's edge drawn over the page beneath: a soft hiss with little
below 1 kHz and its top flat to 12 kHz, eased in and out, wavering as the hand moves.
Its band and its shape were measured from recordings of pages being turned; the owner
chose it by ear (no landing: a thud at the end sounded like a tap on the desk).

Ten variations are written (pl_page_0.wav ... pl_page_9.wav), each a little longer or
shorter and wavering differently, so turning many pages does not repeat one sound; each
has a mirror (pl_page_back_0.wav ...) for a page turned back, as a page turned forward
crosses right to left. Deterministic: the same script gives the same bytes on every
machine. Writes into this game's assets/audio/ unless told otherwise.
"""
import math
import random
import struct
import sys
import wave
from pathlib import Path

RATE = 48000
VARIATIONS = 10
LEVEL = 0.06  # peak: half the chosen candidate's, so the page is felt more than heard


class Filter:
    """A two-pole filter (RBJ cookbook), high- or low-pass."""

    def __init__(self, kind, corner, q=0.707):
        w = 2 * math.pi * corner / RATE
        alpha = math.sin(w) / (2 * q)
        a0 = 1 + alpha
        if kind == "high":
            self.b0 = self.b2 = (1 + math.cos(w)) / 2 / a0
            self.b1 = -(1 + math.cos(w)) / a0
        else:
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


def page(seed, seconds, waver):
    """One turn: `seconds` long, wavering by `waver` (how quickly the hand's pressure moves)."""
    rng = random.Random(seed)
    count = int(seconds * RATE)
    cut = [Filter("high", 1000), Filter("high", 700)]
    top = Filter("low", 12000)
    drift = 0.0
    left, right = [], []
    for index in range(count):
        t = index / RATE
        noise = rng.uniform(-1, 1)
        drift += (rng.uniform(-1, 1) - drift) * (waver * 40 / RATE)
        rise = smooth(t, 0.0, seconds * 0.6) * (1 - smooth(t, seconds - 0.015, seconds))
        sample = top(cut[1](cut[0](noise))) * rise * (1 + 2.5 * drift)
        pan = smooth(t, 0.0, seconds)
        left.append(sample * (0.6 + 0.4 * pan))
        right.append(sample * (1.0 - 0.4 * pan))
    fade = int(0.004 * RATE)
    for index in range(fade):
        g = index / fade
        left[-1 - index] *= g
        right[-1 - index] *= g
    peak = max(max(abs(v) for v in left), max(abs(v) for v in right), 1e-9)
    return [v * LEVEL / peak for v in left], [v * LEVEL / peak for v in right]


def write(path, left, right):
    with wave.open(str(path), "wb") as output:
        output.setnchannels(2)
        output.setsampwidth(2)
        output.setframerate(RATE)
        frames = bytearray()
        for l, r in zip(left, right):
            frames += struct.pack("<hh", int(round(l * 32767)), int(round(r * 32767)))
        output.writeframes(bytes(frames))


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "assets" / "audio"
    out.mkdir(parents=True, exist_ok=True)
    for number in range(VARIATIONS):
        # The first is the one chosen; the others a little longer or shorter, wavering
        # more or less.
        seconds = 0.20 + (0.0 if number == 0 else ((number * 7) % 9 - 4) * 0.0075)
        waver = 1.0 if number == 0 else 0.7 + ((number * 3) % 7) * 0.1
        left, right = page(36 + number, seconds, waver)
        write(out / f"pl_page_{number}.wav", left, right)
        write(out / f"pl_page_back_{number}.wav", right, left)
        print(f"pl_page_{number}: {seconds:.3f} s, waver {waver:.1f}")


if __name__ == "__main__":
    main()
