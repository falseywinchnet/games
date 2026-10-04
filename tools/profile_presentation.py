#!/usr/bin/env python3
"""Collect native presentation counters without machine-speed pass thresholds.

The app warms for five seconds and samples for ten. These are renderer timings,
not process CPU percentages. Use an isolated save directory. Preserve a copy of
its fixtures before comparing builds: timed gameplay can change saved positions.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--assets", required=True, type=Path)
    parser.add_argument("--state", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--entries", nargs="+", type=int, default=[0, 5, 7])
    args = parser.parse_args()
    args.state.mkdir(parents=True, exist_ok=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    environment["GAMES_STATE_DIR"] = str(args.state.resolve())
    environment["GAMES_ASSET_DIR"] = str(args.assets.resolve())
    reports = []
    for entry in args.entries:
        result = subprocess.run(
            [str(args.executable.resolve()), "--profile-idle", str(entry)],
            env=environment, capture_output=True, text=True, encoding="utf-8", timeout=90)
        if result.returncode or "PROFILE_BEGIN" not in result.stdout or "PROFILE_END" not in result.stdout:
            raise RuntimeError(f"Native profile {entry} failed:\n{result.stdout}\n{result.stderr}")
        prefix = "PROFILE_METRICS "
        records = [json.loads(line[len(prefix):]) for line in result.stdout.splitlines()
                   if line.startswith(prefix)]
        if len(records) != 1 or "window" not in records[0]:
            raise RuntimeError(f"Native profile {entry} did not return sampled window metrics")
        metrics = records[0]["window"]
        frames = metrics["frames_presented"]
        report = {"entry": entry, "frames": frames, "sample_seconds": records[0]["seconds"],
                  "average_present_ms": metrics["present_duration_nanoseconds"] / max(1, frames) / 1e6,
                  "worst_present_ms": metrics["worst_present_duration_nanoseconds"] / 1e6,
                  "metrics": metrics, "stderr": result.stderr}
        reports.append(report)
        print(json.dumps({key: report[key] for key in (
            "entry", "frames", "sample_seconds", "average_present_ms", "worst_present_ms")}), flush=True)
        args.output.write_text(json.dumps(reports, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
