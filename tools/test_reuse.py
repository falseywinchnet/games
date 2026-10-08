#!/usr/bin/env python3
"""Skips the tests whose inputs have not changed since they last passed.

Every test gets a key: the platform and build profile, its command line and
properties (with machine paths taken out), the compiler, the runner image, and the git tree hashes of
the source it depends on. A tree hash changes exactly when something under that
folder changes, so the key changes exactly when the test could behave differently.

What a test depends on, from where CTest says it was declared:
- a game's test (declared in games/<id>/, or named <id>_... by the shell's harness):
  that game's folder, the engines its GAME.json lists and any shared/<name> its
  build files name, and the common inputs;
- an engine's test (declared in shared/<name>/): that engine and the common inputs;
- anything else is the shell's: everything (all games, all of shared/).
The common inputs are the build (CMakeLists.txt, cmake/), the shell (src/), the test
sources (tests/), the new-game kit, the assets and the tools that pin and prepare them.

  plan    reads the last results, writes the CTest exclusion for the tests that
          passed with the same key, and the keys of this run.
  record  adds this run's passing tests to the results (failed tests are dropped).

Results live in a small JSON file the workflow keeps in the Actions cache: main's runs
are visible to every pull request, and a pull request's own runs to its re-runs.
Anything unexpected (no git, no CTest listing) means running every test.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
COMMON = ["CMakeLists.txt", "cmake", "src", "tests", "new-games/kit", "assets",
          "tools/game_catalog.py", "tools/package_common.py", "tools/prepare_portable_assets.py",
          "tools/test_reuse.py"]
SCHEMA = 1


TREES: dict[str, str] = {}


def tree(path: str) -> str:
    if path in TREES:
        return TREES[path]
    result = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "HEAD:" + path],
                            capture_output=True, text=True, check=False)
    if result.returncode == 0:
        TREES[path] = result.stdout.strip()
    elif "does not exist in" in result.stderr or "exists on disk, but not in" in result.stderr:
        TREES[path] = "absent"  # a path this commit doesn't have hashes as absent
    else:
        # Any other failure (no repository, an unsafe directory) must not look unchanged.
        raise OSError("git cannot read " + path + ": " + result.stderr.strip())
    return TREES[path]


def games() -> dict[str, list[str]]:
    """Each game's folder and the shared/ folders it depends on."""
    found: dict[str, list[str]] = {}
    for manifest in sorted((ROOT / "games").glob("*/GAME.json")):
        folder = manifest.parent
        data = json.loads(manifest.read_text(encoding="utf-8"))
        shared = {"shared/" + engine for engine in data.get("engines", [])}
        for build in ("build.cmake", "CMakeLists.txt"):
            if (folder / build).is_file():
                shared |= set(re.findall(r"shared/[A-Za-z0-9_]+", (folder / build).read_text(encoding="utf-8")))
        found[folder.name] = ["games/" + folder.name] + sorted(shared)
    return found


def compiler(build: Path) -> str:
    for record in sorted(build.glob("CMakeFiles/*/CMakeCXXCompiler.cmake")):
        text = record.read_text(encoding="utf-8", errors="replace")
        wanted = [line for line in text.splitlines()
                  if line.startswith(("set(CMAKE_CXX_COMPILER_ID ", "set(CMAKE_CXX_COMPILER_VERSION ",
                                      "set(CMAKE_CXX_COMPILER_TARGET "))]
        return "\n".join(wanted)
    return "unknown"


def listing(build: Path) -> dict:
    result = subprocess.run(["ctest", "--test-dir", str(build), "--show-only=json-v1"],
                            capture_output=True, text=True, check=True)
    return json.loads(result.stdout)


def portable(value: str, build: Path) -> str:
    # Machine paths differ between runners and runs; the key is about what is run.
    text = value.replace("\\", "/")
    for prefix, name in ((build.resolve().as_posix(), "<build>"), (ROOT.as_posix(), "<source>")):
        text = text.replace(prefix, name)
    return text


def owner(test: dict, graph: dict, known: dict[str, list[str]]) -> tuple[str, list[str]]:
    declared = ""
    if "backtrace" in test:
        node = graph["nodes"][test["backtrace"]]
        declared = Path(graph["files"][node["file"]]).resolve().as_posix()
    relative = declared[len(ROOT.as_posix()) + 1:] if declared.startswith(ROOT.as_posix() + "/") else ""
    parts = relative.split("/")
    if len(parts) > 1 and parts[0] == "games" and parts[1] in known:
        return "game:" + parts[1], known[parts[1]]
    if len(parts) > 1 and parts[0] == "shared":
        return "engine:" + parts[1], ["shared/" + parts[1]]
    # The shell's harness runs some tests per game (<id>_native_frames, <id>_view_contract).
    for game in sorted(known, key=len, reverse=True):
        if test["name"].startswith(game + "_"):
            return "game:" + game, known[game]
    return "shell", ["games", "shared"]


def plan(args: argparse.Namespace) -> None:
    build = Path(args.build)
    keys_path = build / "test-reuse-keys.json"
    exclude_path = Path(args.exclude)
    exclude_path.write_text("", encoding="utf-8")
    keys_path.write_text("{}", encoding="utf-8")
    try:
        data = listing(build)
        known = games()
        common = {path: tree(path) for path in COMMON}
        toolchain = compiler(build)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print("Test reuse unavailable (" + str(error) + "); every test runs", flush=True)
        return
    results = {}
    if Path(args.results).is_file():
        try:
            stored = json.loads(Path(args.results).read_text(encoding="utf-8"))
            if stored.get("schema") == SCHEMA and stored.get("profile") == args.profile:
                results = stored.get("passed", {})
        except ValueError:
            pass
    keys = {}
    reused = []
    groups: dict[str, list[str]] = {}
    for test in data.get("tests", []):
        group, folders = owner(test, data["backtraceGraph"], known)
        material = {
            "schema": SCHEMA, "profile": args.profile, "name": test["name"],
            "command": [portable(part, build) for part in test.get("command", [])],
            "properties": portable(json.dumps(test.get("properties", []), sort_keys=True), build),
            "compiler": toolchain, "common": common,
            # A new runner image (system libraries, drivers) runs everything once.
            "image": os.environ.get("ImageOS", "") + " " + os.environ.get("ImageVersion", ""),
            "folders": {folder: tree(folder) for folder in folders},
        }
        key = hashlib.sha256(json.dumps(material, sort_keys=True).encode("utf-8")).hexdigest()
        keys[test["name"]] = key
        groups.setdefault(group, []).append(test["name"])
        if results.get(test["name"]) == key:
            reused.append(test["name"])
    keys_path.write_text(json.dumps(keys, indent=1, sort_keys=True), encoding="utf-8")
    if reused:
        exclude_path.write_text("^(" + "|".join(re.escape(name) for name in sorted(reused)) + ")$", encoding="utf-8")
    running = len(keys) - len(reused)
    print("Tests: " + str(running) + " to run, " + str(len(reused)) + " reused (unchanged since they passed)", flush=True)
    for group in sorted(groups):
        names = groups[group]
        fresh = [name for name in names if name not in reused]
        print("  " + group + ": " + (str(len(fresh)) + " run" if fresh else "reused"), flush=True)


def record(args: argparse.Namespace) -> None:
    build = Path(args.build)
    keys_path = build / "test-reuse-keys.json"
    keys = json.loads(keys_path.read_text(encoding="utf-8")) if keys_path.is_file() else {}
    results_path = Path(args.results)
    passed = {}
    if results_path.is_file():
        try:
            stored = json.loads(results_path.read_text(encoding="utf-8"))
            if stored.get("schema") == SCHEMA and stored.get("profile") == args.profile:
                passed = stored.get("passed", {})
        except ValueError:
            pass
    # Keep what is still current; a renamed or changed test's old entry is dropped.
    passed = {name: key for name, key in passed.items() if keys.get(name) == key}
    fresh = 0
    for junit in args.junit:
        path = Path(junit)
        if not path.is_file():
            continue
        for case in ElementTree.parse(path).getroot().iter("testcase"):
            name = case.get("name", "")
            ok = case.get("status", "run") in ("run", "passed") and case.find("failure") is None \
                and case.find("error") is None and case.find("skipped") is None
            if ok and name in keys:
                passed[name] = keys[name]
                fresh += 1
            elif name in passed:
                del passed[name]
    results_path.parent.mkdir(parents=True, exist_ok=True)
    results_path.write_text(json.dumps({"schema": SCHEMA, "profile": args.profile, "passed": passed},
                                       indent=1, sort_keys=True), encoding="utf-8")
    print("Recorded " + str(fresh) + " passing tests; " + str(len(passed)) + " known good", flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=("plan", "record"))
    parser.add_argument("--build", required=True, help="the CTest build directory")
    parser.add_argument("--profile", required=True, help="platform and build profile, e.g. app-macos-arm64")
    parser.add_argument("--results", required=True, help="the results file kept between runs")
    parser.add_argument("--exclude", default="test-reuse-exclude.txt", help="plan: where the CTest -E pattern goes")
    parser.add_argument("--junit", nargs="*", default=[], help="record: CTest JUnit reports of this run")
    args = parser.parse_args()
    if args.command == "plan":
        plan(args)
    else:
        record(args)


if __name__ == "__main__":
    sys.exit(main())
