"""Measure a settled native window with isolated saves, without third-party modules.

CPU percentages are fractions of one logical core, not total machine capacity.
The application warms for five seconds, then samples for ten. Keep the window
uncovered and do not interact with it during a run. Animated games keep animating.
"""
import argparse
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import subprocess
import shutil
import tempfile
import time
import threading


class Memory(ctypes.Structure):
    _fields_ = [("cb", wintypes.DWORD), ("faults", wintypes.DWORD)] + [
        (name, ctypes.c_size_t) for name in (
            "peak_working", "working", "peak_paged", "paged", "peak_nonpaged",
            "nonpaged", "pagefile", "peak_pagefile", "private")]


def sample(handle):
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    psapi = ctypes.WinDLL("psapi", use_last_error=True)
    stamps = [wintypes.FILETIME() for _ in range(4)]
    if not kernel.GetProcessTimes(wintypes.HANDLE(handle), *(ctypes.byref(s) for s in stamps)):
        raise ctypes.WinError(ctypes.get_last_error())
    cpu = sum((s.dwHighDateTime << 32) | s.dwLowDateTime for s in stamps[2:]) / 10_000_000
    memory = Memory()
    memory.cb = ctypes.sizeof(memory)
    if not psapi.GetProcessMemoryInfo(wintypes.HANDLE(handle), ctypes.byref(memory), memory.cb):
        raise ctypes.WinError(ctypes.get_last_error())
    return time.perf_counter(), cpu, memory.working, memory.private


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--seed", required=True, type=Path, help="Valid cabinet save copied to each isolated run")
    parser.add_argument("--assets", required=True, type=Path)
    parser.add_argument("--entries", type=int, nargs="+", default=[-1, 0, 13, 12])
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--game-save", action="append", type=Path, default=[], help="Copy an identical starting position into each run")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("This process-counter collector runs on Windows; --profile-idle itself is portable")
    # Preserve game data, change only master audio preferences in the copied save.
    payload = args.seed.read_text(encoding="utf-8").split("\n", 1)[1]
    first, rest = payload.split("\n", 1)
    fields = first.split()
    fields[3:5] = ["0", "0"]
    payload = " ".join(fields) + "\n" + rest
    checksum = 14695981039346656037
    for byte in payload.encode("utf-8"):
        checksum = ((checksum ^ byte) * 1099511628211) & ((1 << 64) - 1)
    cabinet = f"RAINSTAR_GAMES 2 {checksum}\n" + payload
    reports = []
    args.output.parent.mkdir(parents=True, exist_ok=True)
    for entry in args.entries:
        with tempfile.TemporaryDirectory(prefix="playsuite-profile-") as state:
            for save in args.game_save:
                shutil.copy2(save, Path(state, save.name))
            Path(state, "cabinet-v1.txt").write_text(cabinet, encoding="utf-8", newline="\n")
            environment = os.environ.copy()
            environment["GAMES_STATE_DIR"] = state
            environment["GAMES_ASSET_DIR"] = str(args.assets.resolve())
            with tempfile.TemporaryFile(mode="w+", encoding="utf-8") as errors:
                process = subprocess.Popen([str(args.executable.resolve()), "--profile-idle", str(entry)],
                                           env=environment, stdout=subprocess.PIPE, stderr=errors,
                                           text=True, encoding="utf-8")
                watchdog = threading.Timer(60, process.kill)
                watchdog.start()
                before = after = None
                lines = []
                try:
                    for line in process.stdout:
                        if line.strip() == "PROFILE_BEGIN":
                            before = sample(int(process._handle))
                        elif line.strip() == "PROFILE_END":
                            after = sample(int(process._handle))
                        lines.append(line)
                    process.wait(timeout=30)
                finally:
                    watchdog.cancel()
                    process.stdout.close()
                errors.seek(0)
                diagnostic = errors.read()
                if process.returncode or before is None or after is None:
                    raise RuntimeError(f"Profile {entry} failed: {diagnostic}\n{''.join(lines)}")
                elapsed = after[0] - before[0]
                report = {"entry": entry, "seconds": elapsed,
                          "cpu_seconds": after[1] - before[1],
                          "one_core_percent": 100 * (after[1] - before[1]) / elapsed,
                          "working_mib": after[2] / 1048576, "private_mib": after[3] / 1048576,
                          "metrics": [json.loads(line) for line in lines if line.startswith("{")],
                          "stderr": diagnostic}
                reports.append(report)
                print(json.dumps({key: value for key, value in report.items() if key not in ("metrics", "stderr")}), flush=True)
                args.output.write_text(json.dumps(reports, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
