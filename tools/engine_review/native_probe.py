#!/usr/bin/env python3
"""M4 native sampling; run from the repository root, with no build in progress.

python3 tools/engine_review/native_probe.py .build/app-dogfood/games.app stillwater
Each run has isolated saves, logs and a six-second sample. Never kills other apps.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    parser.add_argument("game", choices=["stillwater", "zenconstruction"])
    parser.add_argument("--scene", choices=["planted", "reef", "pool"], default="planted")
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--width", type=int, default=1100)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--seed-state", type=Path, help="Copy this isolated save snapshot into each fresh run")
    parser.add_argument("--output", type=Path, default=Path("astra/engine-review/native"))
    args = parser.parse_args()
    executable = args.bundle.resolve() / "Contents/MacOS/games"
    root = args.output / f"{args.game}-{args.scene}-{args.width}x{args.height}-{time.time_ns()}"
    root.mkdir(parents=True)
    results = []
    for run in range(args.runs):
        directory = root / str(run + 1)
        directory.mkdir()
        state = directory / "state"
        if args.seed_state:
            shutil.copytree(args.seed_state, state)
        else:
            state.mkdir()
        if args.game == "stillwater":
            (state / "stillwater-v1-dev.txt").write_text(
                f"ambient-settings 1\npaused 0\ndetail balanced\nscene {args.scene}\n")
        script = directory / "script.txt"
        commands = f"300 resize {args.width} {args.height}\n600 open {args.game}\n"
        if args.game == "zenconstruction" and not args.seed_state:
            commands += "3000 key enter\n"
        script.write_text(commands + f"24000 capture {(directory / 'window.ppm').resolve()}\n26000 quit\n")
        env = dict(os.environ, GAMES_STATE_DIR=str(state.resolve()), GAMES_SCENE_TRACE="1")
        with (directory / "app.log").open("w") as log:
            process = subprocess.Popen([str(executable), "--dev", "--script", str(script.resolve())],
                                       env=env, stdout=log, stderr=subprocess.STDOUT)
            try:
                time.sleep(16)
                subprocess.run(["/usr/bin/sample", str(process.pid), "6", "-file",
                                str(directory / "sample.txt")], check=True, capture_output=True)
                code = process.wait(timeout=25)
                if code:
                    raise RuntimeError(f"app exited {code}; see {directory}")
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=10)
        sample = (directory / "sample.txt").read_text()
        top = sample.split("Sort by top of stack", 1)[1].split("\n\n", 1)[0]
        busy = 0
        for line in top.splitlines():
            if re.search(r"__psynch|semaphore|__workq|mach_msg|kevent|start_wqthread|__select|__ulock", line):
                continue
            match = re.search(r"\s(\d+)\s*$", line)
            if match:
                busy += int(match[1])
        log = (directory / "app.log").read_text()
        metrics = dict(re.findall(r'"(native_draw_count|paint_retained_calls|paint_retained_nanoseconds)":(\d+)', log))
        result = dict(run=run + 1, busy_samples=busy, metrics=metrics,
                      trace=[line for line in log.splitlines() if "SCENE_TRACE" in line])
        results.append(result)
        print(json.dumps(result), flush=True)
    (root / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    print(root, flush=True)


if __name__ == "__main__":
    main()
