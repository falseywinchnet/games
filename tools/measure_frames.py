#!/usr/bin/env python3
"""What a steady second of each live scene costs: processor, frames, painted area, copies.

Made to judge a change to how GUI.Forms gathers, composes and presents a frame (its single
frame sequence, partial present): run it on a build before and after, on each host, and
compare. Each scene is run twice through the native development build's --script, once
with a short wait and once ten seconds longer; the counters of the two runs are
subtracted, so starting and loading cancel out and what remains is the steady scene.

    python3 tools/measure_frames.py --app .build/app-dogfood/games.app/Contents/MacOS/games
    python3 tools/measure_frames.py --app ... --scene stillwater --repeat 3 --out results.json

The window's own counters come from the JSON the build prints when the script quits; any
host counter that is a number is subtracted too, so a counter GUI.Forms adds later (bytes
copied to the screen, say) shows up without changing this file. Processor time is the
child's user plus system time (POSIX only; elsewhere it is left out). Each run uses its
own empty GAMES_STATE_DIR, so personal saves are never touched.

Two cautions. The window's frames_presented counts retained repaints, not live frames: a
live-surface game shows few or none while its surface publishes thirty a second, so its
per-frame ratios are left out. And the two runs are separate games (a mower, a cat or a
walk need not repeat), so a counter can differ by a little either way: run --repeat and
read the spread.
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

# A scene: the game to open, and how long to let it settle before the measured span
# (Maze 95 starts walking by itself only after 20 seconds untouched).
SCENES = {
    "stillwater": ("stillwater", 8.0),   # the tank: water, fish and plants, every frame whole
    "mowingman": ("mowingman", 10.0),    # the yard as the mower works it
    "maze": ("maze", 26.0),              # walking by itself through the maze
    "gems": ("gems", 6.0),               # a resting board: breathing and sparkle
    "parrots": ("parrots", 12.0),        # the aviary at rest: birds settled, breathing and blinking
    "untangle": ("untangle", 6.0),       # a resting board: the cat and frosted pegs
    "eggy": ("eggy", 8.0),               # Eggy climbing by himself, the camera following
}
MEASURED = 10.0  # seconds between the short and the long run

try:
    import resource
except ImportError:  # Windows
    resource = None


def run_once(app: Path, game: str, seconds: float, size: tuple[int, int]) -> dict:
    """Runs one script; returns the window's counters, the host's numbers and processor time."""
    with tempfile.TemporaryDirectory() as state, tempfile.TemporaryDirectory() as scratch:
        script = Path(scratch) / "scene.txt"
        open_at = 1000
        quit_at = open_at + int(seconds * 1000)
        script.write_text(f"300 resize {size[0]} {size[1]}\n{open_at} open {game}\n{quit_at} quit\n", encoding="utf-8")
        environment = dict(os.environ, GAMES_STATE_DIR=state)
        before = resource.getrusage(resource.RUSAGE_CHILDREN) if resource else None
        started = time.monotonic()
        finished = subprocess.run([str(app), "--dev", "--script", str(script)], env=environment,
                                  capture_output=True, text=True, timeout=seconds + 120)
        wall = time.monotonic() - started
        after = resource.getrusage(resource.RUSAGE_CHILDREN) if resource else None
    output = finished.stdout + finished.stderr
    # The statistics are one JSON line holding "window" (macOS prints a "stable_id" first).
    lines = [line for line in output.splitlines() if line.startswith("{") and '"window"' in line]
    if not lines:
        raise RuntimeError(f"{game}: no window statistics in the output (exit {finished.returncode})")
    stats = json.loads(lines[-1])
    numbers = {}

    def collect(prefix: str, node) -> None:
        for key, value in node.items():
            if isinstance(value, dict):
                collect(prefix + key + ".", value)
            elif isinstance(value, (int, float)) and not isinstance(value, bool):
                numbers[prefix + key] = float(value)

    collect("window.", stats.get("window", {}))
    collect("host.", stats.get("host", {}))
    if before is not None and after is not None:
        numbers["process.cpu_seconds"] = (after.ru_utime - before.ru_utime) + (after.ru_stime - before.ru_stime)
    numbers["process.wall_seconds"] = wall
    return numbers


def measure(app: Path, scene: str, size: tuple[int, int]) -> dict:
    game, settle = SCENES[scene]
    short = run_once(app, game, settle, size)
    long = run_once(app, game, settle + MEASURED, size)
    # Counters that only grow subtract; "last_" and "maximum_" values describe one moment.
    steady = {}
    for key, value in long.items():
        name = key.rsplit(".", 1)[-1]
        if key in short and not name.startswith(("last_", "maximum_", "worst_")) and key != "process.wall_seconds":
            steady[key] = (value - short[key]) / MEASURED
        else:
            steady[key] = value
    return steady


def summary(scene: str, steady: dict) -> str:
    def get(key: str, scale: float = 1.0) -> str:
        return f"{steady[key] * scale:,.1f}" if key in steady else "-"

    frames = steady.get("window.frames_presented", 0.0)
    rectangles = steady.get("window.damage_rectangles_painted", 0.0)
    painted = steady.get("window.painted_damage_area", 0.0)
    bounding = steady.get("window.paint_bounding_area", 0.0)
    line = (f"{scene:11} cpu {get('process.cpu_seconds', 100)} %  frames/s {get('window.frames_presented')}  "
            f"painted {painted / 1e6:,.2f} Mpt/s of {bounding / 1e6:,.2f} bounding  "
            f"rects/frame {f'{rectangles / frames:.2f}' if frames >= 1 else '-'}  "
            f"retained ms/s {get('host.paint_retained_nanoseconds', 1e-6)}  "
            f"present ms/s {get('window.present_duration_nanoseconds', 1e-6)}")
    # What reaches the screen, in device pixels (GUI.Forms #71); absent before it.
    if "window.live_frames_presented" in steady:
        line += (f"\n{'':11} to screen: live {get('window.live_frames_presented')} frames/s, "
                 f"{get('window.live_pixels_presented', 1e-6)} Mpx/s in "
                 f"{get('window.live_rectangles_presented')} rects/s, {get('window.live_present_duration_nanoseconds', 1e-6)} ms/s; "
                 f"retained {get('window.retained_pixels_presented', 1e-6)} Mpx/s in "
                 f"{get('window.retained_rectangles_presented')} rects/s")
    # The copy to the screen alone, without the paint and compose (GUI.Forms #77).
    copy = next((key for key in steady if key.endswith("screen_copy_nanoseconds")), None)
    if copy is not None:
        line += f"; copy alone {get(copy, 1e-6)} ms/s"
    return line


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--app", type=Path, required=True, help="the native development build's executable")
    parser.add_argument("--scene", action="append", choices=sorted(SCENES), help="scenes to run (default: all)")
    parser.add_argument("--repeat", type=int, default=1, help="runs of each scene, each reported")
    parser.add_argument("--size", default="1100x720", help="window size in points, WIDTHxHEIGHT")
    parser.add_argument("--out", type=Path, help="also write every run's steady counters here as JSON")
    options = parser.parse_args()
    width, height = (int(part) for part in options.size.lower().split("x"))
    results = {}
    for scene in options.scene or list(SCENES):
        results[scene] = []
        for _ in range(max(1, options.repeat)):
            steady = measure(options.app, scene, (width, height))
            results[scene].append(steady)
            print(summary(scene, steady), flush=True)
    if options.out:
        options.out.write_text(json.dumps({"size": [width, height], "seconds": MEASURED, "scenes": results}, indent=1),
                               encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
