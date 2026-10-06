#!/usr/bin/env python3
"""Mowing Man's sounds, synthesized from a physical description. NumPy only.

See README.md for the acoustics. Writes 48 kHz masters to masters/, short effects
as WAV and the looping beds as AAC to ../assets/audio/, and the loop manifest.
Deterministic: the same script makes the same files.
"""
from __future__ import annotations

import json
import shutil
import subprocess
import wave
from pathlib import Path

import numpy as np

SR = 48000
HERE = Path(__file__).resolve().parent
OUT = HERE.parent / "assets" / "audio"
MASTERS = HERE / "masters"
TAU = 2 * np.pi
PULLEY = 5 / 6        # blade spindle speed / engine speed
BLADE_ENDS = 2        # passes per spindle revolution
LOOP_SECONDS = 4.0

ENGINES = {
    # livery: (rated rpm, firing offsets within a 720-degree cycle as fractions, voice)
    "h": (3300, [0.0], "single"),
    "t": (3400, [0.0, 0.5], "twin"),
    "jd": (3600, [0.0, 270 / 720], "vtwin"),
}


# ---------------------------------------------------------------- tools

def write_wav(path: Path, stereo: np.ndarray) -> None:
    pcm = (np.clip(stereo, -1, 1) * 32767).astype("<i2")
    with wave.open(str(path), "wb") as out:
        out.setnchannels(2)
        out.setsampwidth(2)
        out.setframerate(SR)
        out.writeframes(pcm.tobytes())


def encode(master: Path, destination: Path) -> None:
    if shutil.which("afconvert"):
        subprocess.run(["afconvert", "-f", "m4af", "-d", "aac", "-b", "160000", str(master), str(destination)], check=True)
    elif shutil.which("ffmpeg"):
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", str(master), "-c:a", "aac", "-b:a", "160k",
                        str(destination)], check=True)
    else:
        raise SystemExit("afconvert or ffmpeg is needed to encode the beds")


def resonances(n: int, modes, lowpass: float | None = None, highpass: float | None = None) -> np.ndarray:
    """Frequency response (rfft bins) of parallel resonators: (frequency, Q, gain)."""
    f = np.fft.rfftfreq(n, 1 / SR)
    f[0] = 1e-3
    h = np.zeros_like(f, dtype=complex)
    for frequency, q, gain in modes:
        h += gain / (1 + 1j * q * (f / frequency - frequency / f))
    if lowpass:
        h /= (1 + (f / lowpass) ** 4) ** 0.5
    if highpass:
        h *= 1 / (1 + (highpass / f) ** 4) ** 0.5
    return h


def filtered(signal: np.ndarray, response: np.ndarray) -> np.ndarray:
    """Circular filtering: seamless for loops; pad one-shots before calling."""
    return np.fft.irfft(np.fft.rfft(signal) * response, n=len(signal))


def band(n: int, low: float, high: float) -> np.ndarray:
    f = np.fft.rfftfreq(n, 1 / SR)
    f[0] = 1e-3
    return 1 / np.sqrt(1 + (f / high) ** 4) / np.sqrt(1 + (low / f) ** 4)


def rms(x: np.ndarray) -> float:
    return float(np.sqrt(np.mean(x ** 2)) + 1e-12)


def normalize(stereo: np.ndarray, dbfs: float) -> np.ndarray:
    return stereo * (10 ** (dbfs / 20) / rms(stereo))


def garden_space(mono: np.ndarray, loop: bool) -> np.ndarray:
    """Early reflections from a house wall and fences, a little different each side."""
    left = mono.copy()
    right = mono.copy()
    soft = band(len(mono), 60, 3500)
    for delay_ms, gain, side in ((9.5, 0.22, 0), (13.0, 0.2, 1), (21.0, 0.13, 0), (27.5, 0.12, 1), (41.0, 0.07, 0)):
        shift = int(SR * delay_ms / 1000)
        echo = filtered(np.roll(mono, shift) if loop else np.concatenate([np.zeros(shift), mono[:-shift]]), soft) * gain
        if side == 0:
            left += echo
            right += echo * 0.6
        else:
            right += echo
            left += echo * 0.6
    return np.stack([left, right], axis=1)


def loop_length(rpm: float) -> tuple[int, float]:
    """A whole number of engine cycles and blade passes, in whole samples."""
    # 3 engine cycles (2 revolutions each) = 10 blade passes at a 5:6 pulley.
    period = round(SR * 3 * 120 / rpm)
    exact_rpm = SR * 3 * 120 / period
    periods = max(1, round(LOOP_SECONDS * SR / period))
    return periods * period, exact_rpm


# ---------------------------------------------------------------- engine and deck

def engine(livery: str, heavy: bool, rng: np.random.Generator) -> np.ndarray:
    rated, offsets, voice = ENGINES[livery]
    n, rpm = loop_length(rated)
    cycle = 120 / rpm * SR  # samples per four-stroke cycle
    cycles = int(round(n / cycle))
    impulses = np.zeros(n)
    ticks = np.zeros(n)
    for k in range(cycles):
        for cylinder, offset in enumerate(offsets):
            jitter = rng.normal(0, 0.003) * cycle
            position = int(round((k + offset) * cycle + jitter)) % n
            impulses[position] += 1.0 + rng.normal(0, 0.06 if heavy else 0.1)
            # Valve gear: an exhaust valve and an intake valve closing.
            for valve in (0.27, 0.62):
                ticks[int(round((k + offset + valve) * cycle)) % n] += rng.uniform(0.4, 1.0)
    # The blow-down pulse: fast rise, decay of a few milliseconds (longer and harder under load).
    t = np.arange(int(0.03 * SR)) / SR
    tau = 0.0032 if heavy else 0.0024
    pulse = np.exp(-t / tau) - np.exp(-t / 0.00035)
    pulse /= np.max(pulse)
    shaped = np.real(np.fft.irfft(np.fft.rfft(impulses) * np.fft.rfft(np.pad(pulse, (0, n - len(pulse))))))
    # The muffler: Helmholtz and chamber modes, rolled off above a few kilohertz.
    muffler = [(95, 2.5, 0.7), (180, 3.0, 0.8), (380, 4.0, 0.9), (720, 4.0, 0.75), (1550, 5.0, 0.45), (2800, 6.0, 0.2)]
    if voice == "single":
        muffler = [(78, 2.2, 0.8), (160, 2.8, 0.85), (340, 4.0, 0.9), (650, 4.0, 0.7), (1400, 5.0, 0.4), (2600, 6.0, 0.18)]
    if voice == "vtwin":
        muffler = [(88, 2.4, 0.75), (210, 3.0, 0.85), (420, 4.0, 0.9), (840, 4.5, 0.7), (1700, 5.0, 0.42), (3000, 6.0, 0.2)]
    exhaust = filtered(shaped, resonances(n, muffler, lowpass=5500 if heavy else 3600))
    # Turbulent gas leaving the muffler: a burst of noise with every firing.
    pop_t = np.arange(int(0.012 * SR)) / SR
    pop = rng.normal(0, 1, len(pop_t)) * np.exp(-pop_t / (0.0035 if heavy else 0.0025))
    turbulence = np.fft.irfft(np.fft.rfft(impulses) * np.fft.rfft(np.pad(pop, (0, n - len(pop)))), n=n)
    turbulence = filtered(turbulence, band(n, 450, 4500 if heavy else 3200))
    mechanical = filtered(ticks, resonances(n, [(5200, 4, 1.0), (7800, 5, 0.6)], highpass=2500))
    # The flywheel fan: a breathy band modulated by each revolution.
    revolution = rpm / 60
    time = np.arange(n) / SR
    fan = filtered(rng.normal(0, 1, n), band(n, 700, 2600)) * (1 + 0.3 * np.sin(TAU * revolution * time)) * 0.02
    # The block's own rumble at twice crank speed.
    rumble = 0.012 * np.sin(TAU * 2 * revolution * time) * (1.4 if heavy else 1.0)
    mono = (exhaust / rms(exhaust) * 0.2 + turbulence / rms(turbulence) * (0.09 if heavy else 0.06)
            + mechanical / rms(mechanical) * 0.015 + fan + rumble)
    stereo = garden_space(mono, loop=True)
    return normalize(stereo, -17 if heavy else -20)


def blade_passes(livery: str) -> tuple[int, float, float]:
    rated, _, _ = ENGINES[livery]
    n, rpm = loop_length(rated)
    bpf = rpm * PULLEY * BLADE_ENDS / 60
    return n, rpm, bpf


def blades(livery: str, rng: np.random.Generator) -> np.ndarray:
    n, rpm, bpf = blade_passes(livery)
    period = SR / bpf
    passes = int(round(n / period))
    time = np.arange(n) / SR
    train = np.zeros(n)
    # Two spindles, belted together, their blades out of step with each other.
    for spindle, phase in ((0, 0.0), (1, 0.31)):
        for j in range(passes):
            train[int(round((j + phase) * period)) % n] += 1.0 if spindle == 0 else 0.8
    whump_t = np.arange(int(0.012 * SR)) / SR
    whump = np.exp(-whump_t / 0.0025) * np.sin(TAU * 160 * whump_t)
    tonal = np.fft.irfft(np.fft.rfft(train) * np.fft.rfft(np.pad(whump, (0, n - len(whump)))), n=n)
    # The deck: structural modes roughly fifty hertz apart, up to five hundred.
    deck = filtered(tonal, resonances(n, [(f, 6.0, 1.0 / (1 + i * 0.35)) for i, f in enumerate(range(150, 501, 50))]))
    # Tip vortex hiss, swelling at every pass ("whoosh, whoosh").
    envelope = 0.45 + 0.55 * (0.5 + 0.5 * np.cos(TAU * bpf * time)) ** 3
    hiss = filtered(rng.normal(0, 1, n), band(n, 600, 6500)) * envelope
    tone = 0.25 * np.sin(TAU * bpf * time) + 0.08 * np.sin(TAU * 2 * bpf * time + 0.7)
    mono = deck / rms(deck) * 0.22 + hiss / rms(hiss) * 0.16 + tone * 0.25
    return normalize(garden_space(mono, loop=True), -22)


def cutting(livery: str, rng: np.random.Generator) -> np.ndarray:
    n, rpm, bpf = blade_passes(livery)
    period = SR / bpf
    passes = int(round(n / period))
    snips = np.zeros(n)
    thumps = np.zeros(n)
    for spindle_phase in (0.0, 0.31):
        for j in range(passes):
            at = int(round((j + spindle_phase) * period)) % n
            if rng.uniform() < 0.85:
                snips[at] += rng.uniform(0.5, 1.0)
            thumps[at] += rng.uniform(0.6, 1.0)
    # Each pass shears a tuft: a short burst of high noise.
    burst_t = np.arange(int(0.009 * SR)) / SR
    burst = rng.normal(0, 1, len(burst_t)) * np.exp(-burst_t / 0.0022)
    shear = np.fft.irfft(np.fft.rfft(snips) * np.fft.rfft(np.pad(burst, (0, n - len(burst)))), n=n)
    shear = filtered(shear, band(n, 1800, 9000))
    # The mass of grass taken: a low chuff with every pass.
    chuff_t = np.arange(int(0.03 * SR)) / SR
    chuff = np.exp(-chuff_t / 0.008) * np.sin(TAU * 210 * chuff_t)
    mass = np.fft.irfft(np.fft.rfft(thumps) * np.fft.rfft(np.pad(chuff, (0, n - len(chuff)))), n=n)
    # Clippings rattling about the deck: sparse metallic pings.
    rattle = np.zeros(n)
    count = int(n / SR * 260)
    rattle[rng.integers(0, n, count)] = rng.uniform(0.2, 1.0, count)
    rattle = filtered(rattle, resonances(n, [(2600, 18, 1.0), (3900, 22, 0.7), (5300, 25, 0.5)], highpass=1500))
    mono = shear / rms(shear) * 0.2 + mass / rms(mass) * 0.18 + rattle / rms(rattle) * 0.07
    return normalize(garden_space(mono, loop=True), -21)


# ---------------------------------------------------------------- one-shots

def one_shot(seconds: float) -> tuple[int, np.ndarray]:
    n = int(seconds * SR)
    return n, np.arange(n) / SR


def pad_filter(signal: np.ndarray, response_maker) -> np.ndarray:
    padded = np.concatenate([signal, np.zeros(SR)])
    out = filtered(padded, response_maker(len(padded)))
    return out[:len(signal)]


def finish(mono: np.ndarray, peak: float, space: bool = True) -> np.ndarray:
    fade = np.minimum(1, (len(mono) - 1 - np.arange(len(mono))) / 400)
    mono = mono * fade
    stereo = garden_space(mono, loop=False) if space else np.stack([mono, mono], axis=1)
    return stereo * (peak / np.max(np.abs(stereo)))


def formant_voice(f0: np.ndarray, formants, breath: float, rng: np.random.Generator) -> np.ndarray:
    """A glottal pulse train (harmonics falling at about 12 dB per octave) through
    parallel formant resonators, with breath noise."""
    n = len(f0)
    phase = np.cumsum(f0 / SR)
    source = np.zeros(n)
    for k in range(1, 30):
        amplitude = 1.0 / k ** 1.9
        harmonic = k * f0
        source += amplitude * np.sin(TAU * k * phase) * (harmonic < 9000)
    source += rng.normal(0, breath, n)
    return pad_filter(source, lambda m: resonances(m, formants))


def gnome_hoo(rng: np.random.Generator, double: bool) -> np.ndarray:
    syllables = [(0.0, 0.42, 430, 560, 400)]
    if double:
        syllables = [(0.0, 0.24, 440, 520, 470), (0.3, 0.36, 470, 620, 430)]
    total = (syllables[-1][0] + syllables[-1][1]) + 0.2
    n, t = one_shot(total)
    out = np.zeros(n)
    for start, length, low, high, end in syllables:
        m = int(length * SR)
        s = np.arange(m) / m
        # Cheeky contour: a quick rise, then a fall.
        f0 = np.where(s < 0.3, low + (high - low) * (s / 0.3), high + (end - high) * ((s - 0.3) / 0.7))
        f0 = f0 * (1 + 0.012 * np.sin(TAU * 6 * s * length))
        voiced = formant_voice(f0, [(330, 4.5, 1.0), (830, 7.0, 0.5), (2400, 14.0, 0.12)], 0.04, rng)
        envelope = np.minimum(1, s * length / 0.03) * np.minimum(1, (1 - s) * length / 0.12)
        # The "h": breath before the voice.
        h = int(0.05 * SR)
        breath = pad_filter(rng.normal(0, 1, h), lambda q: band(q, 700, 3000)) * np.linspace(0, 1, h) * 0.25
        at = int(start * SR)
        out[at:at + h] += breath / (np.max(np.abs(breath)) + 1e-9) * 0.15
        out[at + h // 2:at + h // 2 + m] += voiced * envelope / (np.max(np.abs(voiced)) + 1e-9)
    return finish(out, 0.55)


def freeze(rng: np.random.Generator) -> np.ndarray:
    n, t = one_shot(0.6)
    chirp = np.sin(TAU * np.cumsum(2600 * np.exp(-t * 3.2) + 700) / SR) * np.exp(-t * 4)
    sparkle = np.zeros(n)
    for _ in range(18):
        at = rng.uniform(0, 0.45)
        f = rng.uniform(4000, 8000)
        s = t - at
        sparkle += np.where(s > 0, np.sin(TAU * f * s) * np.exp(-np.maximum(s, 0) * 60), 0) * rng.uniform(0.2, 0.5)
    ice = pad_filter(rng.normal(0, 1, n), lambda m: band(m, 4000, 11000)) * np.exp(-t * 6) * 0.15
    return finish(chirp * 0.6 + sparkle * 0.4 + ice, 0.5)


def shatter(rng: np.random.Generator) -> np.ndarray:
    n, t = one_shot(1.2)
    out = np.zeros(n)
    # The crack and the thud of the body giving way.
    crack = rng.normal(0, 1, int(0.004 * SR)) * np.linspace(1, 0, int(0.004 * SR))
    out[:len(crack)] += crack * 0.9
    out += np.sin(TAU * 120 * t) * np.exp(-t * 35) * 0.5
    # Porcelain fragments: high, short ringing modes, thinning out as they settle.
    for k in range(70):
        at = 0.003 + rng.exponential(0.16)
        if at > 1.0:
            continue
        big = rng.uniform() < 0.2
        s = t - at
        alive = s > 0
        piece = np.zeros(n)
        for _ in range(3):
            f = rng.uniform(1100, 2600) if big else rng.uniform(2500, 9500)
            decay = rng.uniform(40, 90) if big else rng.uniform(70, 220)
            piece += np.where(alive, np.sin(TAU * f * s + rng.uniform(0, TAU)) * np.exp(-np.maximum(s, 0) * decay), 0)
        out += piece * rng.uniform(0.15, 0.5) * np.exp(-at * 1.5)
    return finish(out, 0.75)


def scream(rng: np.random.Generator) -> np.ndarray:
    n, t = one_shot(0.85)
    s = t / t[-1]
    f0 = np.where(s < 0.25, 1050 + 450 * s / 0.25, 1500 - 700 * (s - 0.25) / 0.75)
    f0 = f0 * (1 + 0.04 * np.sin(TAU * 7 * t))
    voice = formant_voice(f0, [(1000, 6, 0.9), (2500, 9, 0.7), (3400, 12, 0.35)], 0.08, rng)
    envelope = np.minimum(1, t / 0.04) * np.minimum(1, (t[-1] - t) / 0.25)
    # Tiny and far away: rolled off, set back in the space.
    distant = pad_filter(voice * envelope, lambda m: band(m, 500, 3800))
    return finish(distant, 0.35)


def puff(rng: np.random.Generator) -> np.ndarray:
    n, t = one_shot(0.45)
    breath = pad_filter(rng.normal(0, 1, n), lambda m: band(m, 700, 3200))
    return finish(breath * np.minimum(1, t / 0.05) * np.exp(-t * 7), 0.3)


def mushroom(rng: np.random.Generator) -> np.ndarray:
    n, t = one_shot(0.25)
    thock = np.sin(TAU * 185 * t) * np.exp(-t * 40)
    squelch = pad_filter(rng.normal(0, 1, n), lambda m: resonances(m, [(620, 4, 1.0), (1300, 6, 0.4)])) * np.exp(-t * 25)
    return finish(thock * 0.7 + squelch / (np.max(np.abs(squelch)) + 1e-9) * 0.35, 0.45)


def bump(rng: np.random.Generator) -> np.ndarray:
    n, t = one_shot(0.45)
    thud = np.sin(TAU * 88 * t) * np.exp(-t * 18)
    rattle = np.zeros(n)
    for f in (640, 930, 1350, 1780):
        rattle += np.sin(TAU * f * t + rng.uniform(0, TAU)) * np.exp(-t * rng.uniform(14, 30))
    return finish(thud * 0.8 + rattle * 0.25, 0.5)


# ---------------------------------------------------------------- output

def granny_bonk(rng: np.random.Generator) -> np.ndarray:
    """Caught: a cartoon beating. Three quick whacks of a rolling pin on the mower (a dry
    wooden crack into the clang of a steel hood), a big sproingy boing, and the dizzy
    tweeting of birds going round a head."""
    n, t = one_shot(2.6)
    out = np.zeros(n)

    def whack(at: float, size: float) -> None:
        i = int(at * SR)
        m = int(0.5 * SR)
        s = np.arange(m) / SR
        crack = rng.normal(0, 1, m) * np.exp(-s * 160)                       # the wood's crack
        wood = sum(np.sin(TAU * f * s) * np.exp(-s * d) for f, d in ((520, 60), (1130, 80), (1870, 110)))
        clang = sum(np.sin(TAU * f * s + rng.uniform(0, TAU)) * np.exp(-s * d) * a
                    for f, d, a in ((310, 9, 0.6), (742, 7, 0.5), (1385, 6, 0.35), (2290, 8, 0.25), (3510, 11, 0.15)))
        thud = np.sin(TAU * 95 * s) * np.exp(-s * 30)
        hit = (crack * 0.5 + wood * 0.5 + clang * 0.55 + thud * 0.8) * size
        out[i:i + m] += hit[:max(0, min(m, n - i))]

    whack(0.02, 1.0)
    whack(0.2, 0.8)
    whack(0.36, 1.1)
    # the boing: a spring's tone bent hard by its own wobble, dying away
    i = int(0.5 * SR)
    m = int(1.1 * SR)
    s = np.arange(m) / SR
    f = 150 * (1 + 0.35 * np.exp(-s * 3) * np.sin(TAU * 11 * s)) * (1 + 0.6 * np.exp(-s * 12))
    boing = np.sin(TAU * np.cumsum(f) / SR) + 0.35 * np.sin(2 * TAU * np.cumsum(f) / SR)
    out[i:i + m] += boing * np.exp(-s * 2.6) * 0.7
    # birdies circling: little rising chirps, panned about later
    for k in range(7):
        at = 1.25 + 0.17 * k
        j = int(at * SR)
        q = int(0.09 * SR)
        u = np.arange(q) / SR
        chirp = np.sin(TAU * np.cumsum(3200 + 1800 * u / 0.09 + 300 * np.sin(TAU * 40 * u)) / SR) * np.sin(np.pi * u / 0.09)
        out[j:j + q] += chirp * 0.18 * (1 - k / 9)
    return finish(out, 0.75)


def mowing_music(rng: np.random.Generator) -> np.ndarray:
    """An easy summer tune to mow to: G major, 116 to the minute, 16 bars that loop.
    Ukulele strummed on the off-beats, a plucked bass, a whistled tune and its answer,
    a glockenspiel taking the tune in the second half, soft kick, brushes and shaker.
    Kept light and out of the mower's way: little below 120 Hz, nothing harsh on top."""
    bpm = 116.0
    beat = 60.0 / bpm
    bars = 16
    total = bars * 4 * beat
    n = int(round(total * SR))
    tail = int(2.0 * SR)
    left = np.zeros(n + tail)
    right = np.zeros(n + tail)

    def hz(midi: float) -> float:
        return 440.0 * 2 ** ((midi - 69) / 12)

    def put(sound: np.ndarray, at: float, pan: float, gain: float) -> None:
        i = int(round(at * SR))
        j = min(len(left), i + len(sound))
        left[i:j] += sound[:j - i] * gain * np.sqrt(0.5 * (1 - pan))
        right[i:j] += sound[:j - i] * gain * np.sqrt(0.5 * (1 + pan))

    plucked: dict = {}

    def pluck(freq: float, seconds: float, bright: float, decay: float) -> np.ndarray:
        """Karplus-Strong: a burst of noise in a tuned, damped delay line. Three takes of
        each string are made and reused, so a strum is never quite the same twice."""
        key = (round(freq, 3), seconds, bright, decay, int(rng.integers(3)))
        if key in plucked:
            return plucked[key]
        plucked[key] = pluck_once(freq, seconds, bright, decay)
        return plucked[key]

    def pluck_once(freq: float, seconds: float, bright: float, decay: float) -> np.ndarray:
        m = int(seconds * SR)
        period = SR / freq
        d = int(period)
        frac = period - d
        buf = rng.uniform(-1, 1, d + 2)
        # soften the burst for a nylon / ukulele attack
        for _ in range(int(3 * (1 - bright)) + 1):
            buf = 0.5 * (buf + np.roll(buf, 1))
        out = np.zeros(m)
        line = np.concatenate([buf, np.zeros(m)])
        for i in range(d + 1, m + d + 1):
            a = line[i - d - 1] * frac + line[i - d] * (1 - frac)
            b = line[i - d] * frac + line[i - d + 1] * (1 - frac) if i - d + 1 < len(line) else a
            line[i] = decay * 0.5 * (a + b)
        out = line[d + 1:d + 1 + m]
        env = np.minimum(1, np.arange(m) / (0.002 * SR))
        return out * env

    # chords: G C D G | Em C D G, twice over
    G, C, D, Em, D7 = [55, 59, 62, 67], [55, 60, 64, 67], [57, 62, 66, 69], [55, 59, 64, 67], [57, 60, 62, 66]
    chords = [G, C, D, G, Em, C, D7, G] * 2
    roots = [43, 48, 50, 43, 40, 48, 50, 43] * 2
    for bar in range(bars):
        start = bar * 4 * beat
        chord = chords[bar]
        # ukulele: down on 1, then the island strum: down-up up-down-up
        strokes = [(0.0, 1, 1.0), (1.0, 1, 0.7), (1.5, -1, 0.55), (2.5, -1, 0.6), (3.0, 1, 0.75), (3.5, -1, 0.5)]
        for at, way, accent in strokes:
            notes = chord if way > 0 else list(reversed(chord))
            for k, note in enumerate(notes):
                s = pluck(hz(note + 12), 0.9, 0.55, 0.996)
                put(s * 0.35 * accent, start + at * beat + k * 0.011, 0.25, 0.5)
        # bass: root on 1, fifth on 3, a walk up into the next bar
        r = roots[bar]
        for at, step in ((0.0, 0), (2.0, 7), (3.5, 5 if bar % 2 == 1 else 7)):
            s = pluck(hz(r + step), 0.7, 0.2, 0.997)
            s = np.convolve(s, np.ones(6) / 6, mode="same")
            put(s * 0.9, start + at * beat, -0.1, 0.6)
        # drums: a soft kick on 1 and 3, brushes on 2 and 4, a shaker in eighths
        for at in (0.0, 2.0):
            m = int(0.25 * SR)
            u = np.arange(m) / SR
            kick = np.sin(TAU * np.cumsum(55 + 70 * np.exp(-u * 40)) / SR) * np.exp(-u * 14)
            put(kick, start + at * beat, 0.0, 0.32)
        for at in (1.0, 3.0):
            m = int(0.22 * SR)
            u = np.arange(m) / SR
            brush = pad_filter(rng.normal(0, 1, m), lambda q: band(q, 1500, 7000)) * np.exp(-u * 18)
            put(brush / (np.max(np.abs(brush)) + 1e-9), start + at * beat, 0.15, 0.12)
        for e in range(8):
            m = int(0.07 * SR)
            u = np.arange(m) / SR
            shake = pad_filter(rng.normal(0, 1, m), lambda q: band(q, 5000, 11000)) * np.exp(-u * 60)
            put(shake / (np.max(np.abs(shake)) + 1e-9), start + e * 0.5 * beat, -0.35, 0.05 if e % 2 else 0.08)

    def whistle(note: float, at: float, length: float, gain: float, pan: float) -> None:
        m = int((length * beat + 0.08) * SR)
        u = np.arange(m) / SR
        f = hz(note) * (1 + 0.012 * np.sin(TAU * 5.5 * u) * np.minimum(1, u / 0.25))
        f = f * (1 - 0.03 * np.exp(-u * 30))  # a little scoop up into the note
        tone = np.sin(TAU * np.cumsum(f) / SR) + 0.04 * np.sin(2 * TAU * np.cumsum(f) / SR)
        breath = pad_filter(rng.normal(0, 1, m), lambda q: band(q, 1800, 4500)) * 0.04
        env = np.minimum(1, u / 0.025) * np.minimum(1, (length * beat + 0.05 - u) / 0.06).clip(0, 1)
        put((tone + breath) * env, at, pan, gain)

    def glock(note: float, at: float, gain: float, pan: float) -> None:
        m = int(1.6 * SR)
        u = np.arange(m) / SR
        f = hz(note)
        bell = (np.sin(TAU * f * u) * np.exp(-u * 3.0) + 0.35 * np.sin(TAU * f * 2.76 * u) * np.exp(-u * 7)
                + 0.15 * np.sin(TAU * f * 5.4 * u) * np.exp(-u * 12))
        put(bell * np.minimum(1, u / 0.001), at, pan, gain)

    # the tune: a call (bars 1-4) and its answer (5-8), whistled
    tune = [
        (0, 0, 1, 71), (0, 1, 0.5, 74), (0, 1.5, 0.5, 79), (0, 2, 1, 78), (0, 3, 1, 74),
        (1, 0, 1.5, 76), (1, 1.5, 0.5, 72), (1, 2, 0.5, 76), (1, 2.5, 0.5, 79), (1, 3, 1, 76),
        (2, 0, 1, 78), (2, 1, 0.5, 81), (2, 1.5, 0.5, 78), (2, 2, 1, 74), (2, 3, 0.5, 69), (2, 3.5, 0.5, 72),
        (3, 0, 2, 71), (3, 2, 0.5, 67), (3, 2.5, 0.5, 69), (3, 3, 1, 71),
        (4, 0, 1, 79), (4, 1, 0.5, 76), (4, 1.5, 0.5, 71), (4, 2, 1, 76), (4, 3, 1, 79),
        (5, 0, 0.5, 76), (5, 0.5, 0.5, 74), (5, 1, 1, 72), (5, 2, 0.5, 76), (5, 2.5, 0.5, 72), (5, 3, 1, 69),
        (6, 0, 1.5, 74), (6, 1.5, 0.5, 78), (6, 2, 1, 81), (6, 3, 1, 84),
        (7, 0, 2, 83), (7, 2, 1.5, 79),
    ]
    for bar, at, length, note in tune:
        whistle(note, (bar * 4 + at) * beat, length, 0.16, 0.1)
    # second half: the glockenspiel has the tune, the whistle comes back for the last four bars a third above
    for bar, at, length, note in tune:
        if bar < 4:
            glock(note + 12, ((bar + 8) * 4 + at) * beat, 0.11, -0.3)
        else:
            whistle(note, ((bar + 8) * 4 + at) * beat, length, 0.14, 0.15)
            glock(note + 12 + (4 if note % 12 in (7, 11, 2) else 3), ((bar + 8) * 4 + at) * beat, 0.05, -0.3)
    # make the loop seamless: what rings past the end comes back in at the start
    left[:tail] += left[n:n + tail]
    right[:tail] += right[n:n + tail]
    stereo = np.stack([left[:n], right[:n]], axis=1)
    # keep out of the mower's lows
    for c in range(2):
        stereo[:, c] = pad_filter(stereo[:, c], lambda q: band(q, 90, 16000))
    return stereo * (0.5 / np.max(np.abs(stereo)))


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    MASTERS.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(20261005)
    manifest = {"music": [], "stingers": []}
    # The mower itself is rendered by the live voice: build mm_mower_loops (tools/mower_loops.cpp),
    # run it into masters/, and this script encodes what it wrote.
    loop = MASTERS / "mm_mower_run.wav"
    if not loop.exists():
        raise SystemExit("run mm_mower_loops into audio_src/masters first")
    encode(loop, OUT / "mm_mower_run.m4a")
    with wave.open(str(loop)) as reader:
        frames = reader.getnframes()
    manifest["music"].append({"id": "mm_mower_run", "loop_end_sample_exclusive": frames, "seconds": frames / SR})
    for name in ("mm_mower_start", "mm_mower_stop", "mm_chipper", "mm_granny_shout", "mm_granny_scold", "mm_granny_shriek", "mm_granny_wail"):
        shutil.copyfile(MASTERS / f"{name}.wav", OUT / f"{name}.wav")
    shots = {
        "mm_gnome_hoo": gnome_hoo(rng, False),
        "mm_gnome_hoo2": gnome_hoo(rng, True),
        "mm_freeze": freeze(rng),
        "mm_shatter": shatter(rng),
        "mm_scream": scream(rng),
        "mm_puff": puff(rng),
        "mm_mushroom": mushroom(rng),
        "mm_bump": bump(rng),
        # added after the rest, so their draws from the generator leave the others unchanged
        "mm_granny_bonk": granny_bonk(rng),
    }
    # the tune to mow to: its own generator, so nothing else moves
    tune = mowing_music(np.random.default_rng(116))
    write_wav(OUT / "mm_music_mowing.wav", tune)
    manifest["music"].append({"id": "mm_music_mowing", "loop_end_sample_exclusive": len(tune), "seconds": len(tune) / SR})
    for name, sound in shots.items():
        if name.startswith("mm_gnome_"):
            continue  # the gnome's voice is performed by gnome_voice.mjs; his entries stay so the others' draws are unchanged
        write_wav(OUT / f"{name}.wav", sound)
    (OUT / "mowingman_audio_manifest.json").write_text(json.dumps(manifest, indent=1) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
