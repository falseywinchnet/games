#!/usr/bin/env python3
"""Measure all three tanks at fixed sizes/times, preserving images and tool output."""
import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("preview", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for scene in ("riverscape", "reef", "pool"):
        for width, height in ((300, 210), (590, 380), (880, 576)):
            for repeat in range(3):
                stem = f"{scene}-{width}x{height}-{repeat}"
                command = [str(args.preview.resolve()), f"games/stillwater/assets/scene/{scene}.ambient",
                           str(args.output / (stem + ".png")), str(width), str(height), "12", "2", "120"]
                result = subprocess.run(command, check=True, capture_output=True, text=True)
                (args.output / (stem + ".log")).write_text(result.stdout + result.stderr)
                print(stem + " " + next(line for line in result.stdout.splitlines() if line.startswith("steady:")), flush=True)


if __name__ == "__main__":
    main()
