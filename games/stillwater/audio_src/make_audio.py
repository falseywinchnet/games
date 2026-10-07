#!/usr/bin/env python3
"""Stillwater's fallback sound clips, rendered by the game's own live voices.

The game synthesizes the tank's water and taps (src/tank_voice.cpp) and its island
band (src/island_voice.cpp) as they play, where the toolkit plays generators. Where it
cannot, these clips stand in, rendered by the same code through sw_music_render:

  sw_ambience  a 64-second seamless loop of the planted tank (motor, return water and
               the air stone's bubbles), crossfaded over its own beginning
  sw_tap       one fingertip on the glass, centred (the effect is panned as it plays)
  sw_island    96 seconds of the band (sw_music_render loop ... 96)

    python3 make_audio.py --render <build>/sw_music_render

Writes 48 kHz 16-bit WAV: the tap into ../assets/audio, the masters into masters/
(not committed), then encodes the loops as AAC with afconvert (macOS) or ffmpeg, and
writes the loop manifest. Standard library only.
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import wave
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE.parent / "assets" / "audio"
MASTERS = HERE / "masters"
LOOP_SECONDS = 64
ISLAND_SECONDS = 96
SEED = "1720"


def encode(master: Path, destination: Path) -> None:
    if shutil.which("afconvert"):
        subprocess.run(["afconvert", "-f", "m4af", "-d", "aac", "-b", "160000", str(master), str(destination)],
                       check=True)
    elif shutil.which("ffmpeg"):
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", str(master), "-c:a", "aac", "-b:a", "160k",
                        str(destination)], check=True)
    else:
        raise SystemExit("afconvert or ffmpeg is needed to encode the loops")


def frames(path: Path) -> int:
    with wave.open(str(path), "rb") as clip:
        return clip.getnframes()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--render", type=Path, required=True, help="the built sw_music_render")
    parser.add_argument("--band", action="store_true", help="also re-render the band's loop (slow to encode)")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    MASTERS.mkdir(parents=True, exist_ok=True)
    render = str(args.render)
    subprocess.run([render, "tap", str(OUT / "sw_tap.wav")], check=True)
    ambience = MASTERS / "sw_ambience.wav"
    subprocess.run([render, "tankloop", str(ambience), str(LOOP_SECONDS), SEED], check=True)
    encode(ambience, OUT / "sw_ambience.m4a")
    if args.band:
        island = MASTERS / "sw_island.wav"
        subprocess.run([render, "loop", str(island), str(ISLAND_SECONDS), SEED], check=True)
        encode(island, OUT / "sw_island.m4a")
    manifest = {
        "music": [{"id": "sw_ambience", "loop_end_sample_exclusive": frames(ambience), "seconds": float(LOOP_SECONDS)},
                  {"id": "sw_island", "loop_end_sample_exclusive": ISLAND_SECONDS * 48000, "seconds": ISLAND_SECONDS}],
        "stingers": [],
    }
    (OUT / "stillwater_audio_manifest.json").write_text(json.dumps(manifest, indent=1) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
