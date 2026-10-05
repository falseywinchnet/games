#!/usr/bin/env python3
"""Stillwater's sounds, synthesized (no samples). NumPy only.

  sw_ambience  a 64-second seamless loop: the filter's low pump hum, moving water
               (smoothed noise) and an air stone's small bubble resonances, spaced
               irregularly so the loop does not tick like a clock
  sw_tap       a fingertip on the glass: damped glass resonances and a soft knock

Ported from Stillwater's sound.cpp (make_ambience, make_glass_tap), lengthened and
made seamless. Writes 48 kHz 16-bit WAV: the tap into ../assets/audio, the
ambience master into masters/, then encodes the ambience as AAC with afconvert
(macOS) or ffmpeg, and writes the loop manifest.
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
LOOP_SECONDS = 64
TAU = 2 * np.pi


def write_wav(path: Path, stereo: np.ndarray) -> None:
    data = np.clip(stereo, -1, 1)
    pcm = (data * 32767).astype("<i2")
    with wave.open(str(path), "wb") as out:
        out.setnchannels(2)
        out.setsampwidth(2)
        out.setframerate(SR)
        out.writeframes(pcm.tobytes())


def one_pole(signal: np.ndarray, keep: float) -> np.ndarray:
    """The one-pole low-pass y[n] = keep y[n-1] + (1 - keep) x[n], applied circularly
    (in the frequency domain), so the filtered loop joins without a step."""
    spectrum = np.fft.rfft(signal)
    omega = TAU * np.arange(len(spectrum)) / len(signal)
    response = (1 - keep) / (1 - keep * np.exp(-1j * omega))
    return np.fft.irfft(spectrum * response, n=len(signal))


def ambience() -> np.ndarray:
    frames = SR * LOOP_SECONDS
    t = np.arange(frames) / SR
    rng = np.random.default_rng(71137)
    # The pump: 55 and 110 Hz complete whole cycles in 64 s, so they loop exactly.
    pump = 0.012 * np.sin(TAU * 55 * t) + 0.005 * np.sin(TAU * 110 * t)
    # Moving water: smoothed noise, slowly swelling (periods divide the loop).
    swell = 0.8 + 0.2 * np.sin(TAU * t / 16) * np.sin(TAU * t / 64 + 1.3)
    water_l = 0.17 * one_pole(rng.uniform(-0.5, 0.5, frames), 0.965) * swell
    water_r = 0.16 * one_pole(rng.uniform(-0.5, 0.5, frames), 0.965) * swell
    # Bubbles: short rising resonances at irregular intervals, wrapped at the loop end.
    left = np.zeros(frames)
    right = np.zeros(frames)
    start = 0.13
    length = int(0.16 * SR)
    age = np.arange(length) / SR
    while start < LOOP_SECONDS:
        frequency = 440 + rng.uniform(0, 900)
        gain = 0.022 + rng.uniform(0, 0.012)
        bubble = gain * (1 - np.exp(-age * 700)) * np.exp(-age * 36) * np.sin(TAU * (frequency * age + 800 * age * age))
        first = int(start * SR)
        index = (first + np.arange(length)) % frames
        pan = rng.uniform(0.25, 0.75)
        left[index] += bubble * np.sqrt(1 - pan)
        right[index] += bubble * np.sqrt(pan)
        # Mostly a steady trickle, now and then a little cluster.
        start += rng.uniform(0.07, 0.18) if rng.uniform() < 0.25 else rng.uniform(0.35, 0.8)
    stereo = np.stack([pump + water_l + left, pump + water_r + right], axis=1)
    # Soft, but audible beside the collection's music: peak at 0.3.
    return stereo * (0.3 / np.max(np.abs(stereo)))


def glass_tap() -> np.ndarray:
    frames = SR * 3 // 5
    t = np.arange(frames) / SR
    attack = 1 - np.exp(-t * 6000)
    body = 0.24 * np.exp(-t * 19) * np.sin(TAU * 730 * t)
    glass = 0.11 * np.exp(-t * 29) * np.sin(TAU * 1931 * t) + 0.065 * np.exp(-t * 40) * np.sin(TAU * 3173 * t)
    knock = 0.16 * np.exp(-t * 90) * np.sin(TAU * 157 * t)
    tail = np.minimum(1, (frames - 1 - np.arange(frames)) / 256)
    value = (body + glass + knock) * attack * tail
    return np.stack([value, value], axis=1) * 0.9


def encode(master: Path, destination: Path) -> None:
    if shutil.which("afconvert"):
        subprocess.run(["afconvert", "-f", "m4af", "-d", "aac", "-b", "160000", str(master), str(destination)],
                       check=True)
    elif shutil.which("ffmpeg"):
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", str(master), "-c:a", "aac", "-b:a", "160k",
                        str(destination)], check=True)
    else:
        raise SystemExit("afconvert or ffmpeg is needed to encode the ambience")


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    MASTERS.mkdir(parents=True, exist_ok=True)
    write_wav(OUT / "sw_tap.wav", glass_tap())
    loop = ambience()
    master = MASTERS / "sw_ambience.wav"
    write_wav(master, loop)
    encode(master, OUT / "sw_ambience.m4a")
    manifest = {
        "music": [{"id": "sw_ambience", "loop_end_sample_exclusive": len(loop), "seconds": len(loop) / SR}],
        "stingers": [],
    }
    (OUT / "stillwater_audio_manifest.json").write_text(json.dumps(manifest, indent=1) + "\n", encoding="utf-8")
    peak = float(np.max(np.abs(loop)))
    print(f"sw_ambience: {len(loop)} frames, peak {peak:.3f}; sw_tap written")


if __name__ == "__main__":
    main()
