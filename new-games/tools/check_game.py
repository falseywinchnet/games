#!/usr/bin/env python3
"""The gate a kit game passes before it is delivered.

    python3 new-games/tools/check_game.py <id>                  everything that can be checked here
    python3 new-games/tools/check_game.py <id> --fetch-toolkit  also fetch the pinned GUI.Forms headers
                                                                (about 8 MB, once) and syntax-check the view
    python3 new-games/tools/check_game.py <id> --no-build       skip compiling and running the tests

It reports FAIL (must be fixed), WARN (decide, and say so in HANDOFF.md) and ok.
It exits non-zero on any FAIL. Passing does not mean the game is good; it means
the game keeps the collection's rules. Whether it is good is checked by playing it.

The gate also configures the collection with this module. Pass --application (optionally --script)
to exercise an already-built native standalone window with isolated saves.
Release CI compiles and tests all modules on four platforms.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import kitlib

REPO = kitlib.REPO
TOOLKIT = REPO / ".build" / "new-games" / "toolkit"


class Report:
    def __init__(self) -> None:
        self.failures = 0
        self.warnings = 0

    def ok(self, message: str) -> None:
        print("  ok    " + message)

    def warn(self, message: str) -> None:
        self.warnings += 1
        print("  WARN  " + message)

    def fail(self, message: str) -> None:
        self.failures += 1
        print("  FAIL  " + message)

    def require(self, condition: bool, good: str, bad: str) -> bool:
        if condition:
            self.ok(good)
        else:
            self.fail(bad)
        return condition


def strip_comments_and_strings(source: str) -> str:
    pattern = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
    return pattern.sub(lambda match: re.sub(r"[^\n]", " ", match.group(0)), source)


def run(command: list[str], cwd: Path | None = None) -> subprocess.CompletedProcess:
    return subprocess.run(command, cwd=cwd, capture_output=True, text=True)


def authored_sources(directory: Path, manifest: dict) -> list[Path]:
    borrowed = {directory / item for item in manifest.get("borrowed", [])}
    files: list[Path] = []
    for folder in ("src", "tests", "tools", "dev"):
        for path in sorted((directory / folder).rglob("*")):
            if path.suffix in kitlib.SOURCE_SUFFIXES and path not in borrowed:
                files.append(path)
    files.extend(path for path in sorted(directory.glob("*.cpp")) if path not in borrowed)
    return files


def check_structure(directory: Path, manifest: dict, report: Report) -> None:
    print("Structure")
    for name in ("GAME.json", "README.md", "HANDOFF.md", "CMakeLists.txt", manifest.get("module", "module.cpp"), manifest.get("cover", "cover.cpp"), manifest.get("help", "help.md"), manifest.get("build", "build.cmake")):
        report.require((directory / name).is_file(), name, f"{name} is missing")
    for key in ("core_sources", "ui_sources", "core_tests"):
        for item in manifest.get(key, []):
            report.require((directory / item).is_file(), f"{key}: {item}", f"{key} lists {item}, which does not exist")
    # A game on a shared engine may have no audio adapter of its own: the engine plays its sounds.
    required = ("contract_test",) if manifest.get("engines") and "audio_adapter" not in manifest else ("audio_adapter", "contract_test")
    for key in required:
        report.require((directory / manifest.get(key, "?")).is_file(), f"{key}: {manifest.get(key)}", f"{key} file is missing")
    for engine in manifest.get("engines", []):
        report.require(engine in kitlib.engines(), f"built on the shared engine {engine}",
                       f"GAME.json names engine {engine!r}, which is not in engines/")
    stray = [path.name for path in directory.rglob("*") if path.suffix in (".mm", ".m", ".swift", ".cs", ".js", ".dll", ".dylib", ".so", ".exe")]
    report.require(not stray, "no platform-specific or binary sources", "platform-specific or binary files: " + ", ".join(stray[:6]))
    report.require(re.fullmatch(r"[a-z0-9_]+-v\d+\.txt", manifest.get("save_file", "")) is not None,
                   f"save file name {manifest.get('save_file')}", "save_file must look like my_game-v1.txt")
    if manifest["id"] != "templategame":
        report.require(manifest["namespace"] not in kitlib.RESERVED_NAMESPACES, f"namespace {manifest['namespace']}",
                       f"namespace {manifest['namespace']} is reserved")
        blurb = manifest.get("blurb", "")
        report.require(bool(blurb) and "TODO" not in blurb and len(blurb) <= 90, "the shelf blurb is written and fits the ticket",
                       "GAME.json blurb: write one sentence of at most 90 characters")
        report.require(len(manifest.get("title", "")) <= 28, "the title fits a box", "GAME.json title is longer than 28 characters")
        screens = sorted((directory / "screens").glob("*.png"))
        report.require(len(screens) >= 2 and any("600x370" in path.name for path in screens),
                       f"{len(screens)} screenshots, one at 600x370",
                       "screens/ needs at least two PNGs you have looked at, one with 600x370 in its name")
        heavy = [path.name for path in screens if path.stat().st_size > 700 * 1024]
        report.require(not heavy, "screenshots are compressed",
                       f"{len(heavy)} screenshots are over 700 KiB; run python3 new-games/tools/shrink_png.py vendor/{manifest['id']}/screens")


def check_style(directory: Path, manifest: dict, report: Report) -> None:
    print("House style (scripts/check-style.py on the files you wrote)")
    files = authored_sources(directory, manifest)
    result = run([sys.executable, str(REPO / "scripts" / "check-style.py")] + [str(path) for path in files])
    findings = [line for line in result.stdout.splitlines() if re.search(r":\d+: ", line)]
    if findings:
        for line in findings[:25]:
            report.fail(line.replace(str(REPO) + "/", ""))
        if len(findings) > 25:
            report.fail(f"... and {len(findings) - 25} more")
    else:
        report.ok(f"{len(files)} files, no spelling findings (semantic review is still yours)")


FORBIDDEN_EVERYWHERE = (
    (r"#\s*import\b", "Objective-C import"),
    (r"#\s*include\s*<(unistd|windows|Windows|sys/[a-z]+|dirent|pthread|dlfcn|CoreFoundation|AppKit|Cocoa)[./>]", "a platform header"),
    (r"\b(socket|connect|curl_easy|getaddrinfo|WSAStartup|URLSession)\s*\(", "network access"),
    (r"\b(system|popen|fork|execv?p?e?)\s*\(", "starting another process"),
    (r"\bs?rand\s*\(|std::random_device|std::(mt19937|minstd_rand|default_random_engine)\b|std::\w+_distribution\b",
     "library randomness (results differ between platforms; use the seeded integer generator in rules)"),
    (r"-ffast-math|/fp:fast|__FAST_MATH__", "fast-math"),
)
FORBIDDEN_IN_CORE = (
    (r'#\s*include\s*[<"]gui_forms/', "a GUI.Forms header in the pure core"),
    (r'#\s*include\s*"(suite|collection|shelf|capsule)\.hpp"', "a shell header in the pure core"),
    (r'#\s*include\s*"platform/(text|audio)\.hpp"', "text or audio in the pure core"),
    (r"std::chrono::(system|steady|high_resolution)_clock", "a clock in the pure core (pass time in as a parameter)"),
    (r"\bstd::(thread|async|jthread)\b", "threads in the pure core (see guide/05-performance.md before adding any)"),
    (r"\bstd::getenv\b|\bgetenv\s*\(", "environment access in the pure core"),
)


def check_portability(directory: Path, manifest: dict, report: Report) -> None:
    print("Standalone and portable")
    files = authored_sources(directory, manifest)
    core = {directory / item for item in manifest.get("core_sources", [])}
    core |= {path.with_suffix(".hpp") for path in core}
    # A game may use the shared engines it declares in GAME.json, and no other game's code.
    engine_namespaces = {kitlib.engines()[e]["namespace"] for e in manifest.get("engines", []) if e in kitlib.engines()}
    others = {name for name in kitlib.used_namespaces()
              if name not in (manifest["namespace"], "games", "kit", "gui_forms", "paint") and name not in engine_namespaces}
    problems = 0
    for path in files:
        raw = path.read_text(encoding="utf-8")
        text = strip_comments_and_strings(raw)
        relative = kitlib.display_path(path)
        rules = FORBIDDEN_EVERYWHERE + (FORBIDDEN_IN_CORE if path in core else ())
        for expression, what in rules:
            # Include lines are matched on the raw text; the stripped text has no strings.
            subject = raw if "include" in expression or "import" in expression else text
            match = re.search(expression, subject)
            if match:
                line = subject.count("\n", 0, match.start()) + 1
                report.fail(f"{relative}:{line}: {what}")
                problems += 1
        for include in re.findall(r'#\s*include\s*"([^"]+)"', raw):
            if include.startswith("../") or include.startswith("vendor/"):
                report.fail(f"{relative}: includes {include}; a game reaches into no other game's folder")
                problems += 1
        for name in sorted(others):
            if re.search(r"(?<![\w:])%s::" % re.escape(name), text):
                report.fail(f"{relative}: uses {name}::, another game's namespace. Copy what you borrow into your own.")
                problems += 1
                break
    if problems == 0:
        report.ok(f"{len(files)} files: no platform code, network, processes, library randomness or cross-game reach")
    namespace_pattern = re.compile(r"^namespace\s+(\w+)\s*\{", re.MULTILINE)
    wrong = []
    for path in sorted((directory / "src").rglob("*")):
        if path.suffix in kitlib.SOURCE_SUFFIXES:
            for name in namespace_pattern.findall(path.read_text(encoding="utf-8")):
                if name not in (manifest["namespace"], "gf"):
                    wrong.append(f"{path.name}: namespace {name}")
    report.require(not wrong, f"everything is inside namespace {manifest['namespace']}", "; ".join(wrong[:5]))


def check_contract(directory: Path, manifest: dict, report: Report) -> None:
    print("Shell contract (read from the view's source)")
    view_sources = [directory / item for item in manifest["ui_sources"]]
    # A game on a shared engine inherits the engine's view: read that too.
    for engine_id in manifest.get("engines", []):
        engine = kitlib.engines().get(engine_id)
        if engine is not None:
            for item in engine.get("ui_sources", []):
                view_sources += [engine["directory"] / item, (engine["directory"] / item).with_suffix(".hpp")]
    text = "\n".join(path.read_text(encoding="utf-8") for path in view_sources if path.is_file())
    header = directory / "src" / manifest["view_header"]
    text += header.read_text(encoding="utf-8") if header.is_file() else ""
    report.require("games::CommandSource" in text, "the view is a games::CommandSource", "the view must derive from games::CommandSource")
    report.require(re.search(r"void\s+set_cabinet\s*\(\s*bool\s+\w+,\s*bool\s+\w+,\s*bool\s+\w+,\s*bool\s+\w+", text) is not None,
                   "set_cabinet(foreground, music, sound, reduced)", "the view needs set_cabinet(bool foreground, bool music, bool sound, bool reduced)")
    report.require("void activate()" in text, "activate()", "the view needs activate()")
    report.require('"help"' in text, 'a "help" command', 'the view needs a command with the id "help"')
    primary = re.search(r'\{\s*"[a-z_]+"\s*,[^{};]*,\s*true\s*\}', text) or re.search(r"\.primary\s*=\s*true", text)
    report.require(primary is not None, "a primary command",
                   "mark one command primary: the fifth field of GameCommand, e.g. {\"new\", \"New game\", true, false, true}")
    report.require("games::state_directory()" in text, "saves under games::state_directory()",
                   "resolve the save path with games::state_directory(); do not build a path from HOME")
    report.require(manifest["save_file"] in text, f"saves to {manifest['save_file']}", f"{manifest['save_file']} does not appear in the view")
    report.require(re.search(r"\(\*timer_\)\.stop\(\)", text) is not None, "the timer can stop (idle and hidden)",
                   "the view never stops its timer; a settled or hidden game must do no work")
    report.require("point_from_window" in text, "pointer positions converted with point_from_window",
                   "convert pointer positions with point_from_window(event.position)")
    report.require(".scale()" in text, "device scale taken from the attached window", "take the device scale from attached_window()")
    report.require("reduced" in text, "the Motion switch is consumed", "the view ignores the reduced-motion flag")
    if manifest["id"] != "templategame":
        everything = "\n".join(path.read_text(encoding="utf-8") for path in authored_sources(directory, manifest))
        leftovers = [phrase for phrase in ("TEMPLATEGAME", "Template Game", "TEMPLATE GAME", "Light every lamp",
                                           "lamps still dark", "Every board here can be lit")
                     if phrase in everything]
        report.require(not leftovers, "no template wording left in the game",
                       "the template's own wording remains: " + ", ".join(leftovers))
        save_text = "".join(path.read_text(encoding="utf-8") for path in (directory / "src").glob("save.*"))
        report.require("TEMPLATEGAME" not in save_text, "the save magic is the game's own", "the save magic is still the template's")


def check_documents(directory: Path, manifest: dict, report: Report) -> None:
    print("Brief and handoff")
    brief = (directory / "README.md").read_text(encoding="utf-8") if (directory / "README.md").is_file() else ""
    handoff = (directory / "HANDOFF.md").read_text(encoding="utf-8") if (directory / "HANDOFF.md").is_file() else ""
    if manifest["id"] == "templategame":
        report.ok("template documents are placeholders")
        return
    report.require("TODO" not in brief and len(brief) > 600, "README.md holds the brief", "README.md: write the brief (no TODO left, see guide/02-the-brief.md)")
    for heading in ("VERIFIED", "NOT VERIFIED", "DECISIONS"):
        report.require(re.search(r"^#+\s*%s\b" % heading, handoff, re.MULTILINE | re.IGNORECASE) is not None,
                       f"HANDOFF.md has a {heading} section", f"HANDOFF.md needs a {heading} section")
    report.require("TODO" not in handoff, "HANDOFF.md is filled in", "HANDOFF.md still has TODO entries")


def check_assets(directory: Path, manifest: dict, report: Report) -> None:
    print("Folder assets and producer loop metadata")
    sys.path.insert(0, str(REPO / "tools"))
    from prepare_portable_assets import asset_inventory
    try:
        inventory = asset_inventory(REPO / "assets", [directory])
    except (ValueError, OSError) as error:
        report.fail(str(error))
        return
    prefix = manifest.get("audio_prefix", manifest["id"] + "_")
    mine = [entry for entry in inventory["audio"] if entry["source"].startswith(manifest["id"] + "/")]
    report.ok(f"{len(mine)} module sounds; dynamic full inventory has {len(inventory['audio'])} audio files")
    view_text = "\n".join(path.read_text(encoding="utf-8") for path in authored_sources(directory, manifest))
    named = set(re.findall(r'"(%s[a-z0-9_]+)"' % re.escape(prefix), view_text))
    stems = {entry["stem"] for entry in inventory["audio"]}
    missing = sorted(named - stems)
    if missing:
        report.warn("sound cues without source files (silent): " + ", ".join(missing))
    report.ok("all discovered music has sample-accurate loop metadata; manifests and filename collisions checked")


def check_wiring(directory: Path, manifest: dict, report: Report) -> None:
    print("Automatic module discovery")
    try:
        games = kitlib.catalog([directory], include_disabled=True)
        module = next(game for game in games if game["directory"] == directory.resolve())
    except (ValueError, OSError, StopIteration) as error:
        report.fail("catalog validation: " + str(error))
        return
    report.require(module.get("enabled", True),
                   f"{module['id']} discovered as permanent entry {module['entry_id']}",
                   "module is disabled; its saved id remains reserved")
    if directory.is_relative_to(REPO):
        relative = (directory / "GAME.json").relative_to(REPO).as_posix()
        previous = run(["git", "-C", str(REPO), "show", "HEAD:" + relative])
        if previous.returncode == 0:
            before = json.loads(previous.stdout)
            if "entry_id" in before:
                report.require(before["entry_id"] == module["entry_id"],
                               "permanent saved-game id unchanged", "An existing module's permanent entry_id changed")
    report.ok("cover, help, build and source files live in the game folder; no central edits required")


def check_build(directory: Path, manifest: dict, report: Report) -> None:
    print("Build and tests (the game's own headless build)")
    if shutil.which("cmake") is None:
        report.fail("cmake was not found; install CMake 3.25 or later")
        return
    build = REPO / ".build" / "new-games" / manifest["id"]
    configure = run(["cmake", "-S", str(directory), "-B", str(build), "-DCMAKE_BUILD_TYPE=Release",
                     "-D" + manifest["namespace"].upper() + "_KIT_DIR=" + str(kitlib.KIT / "kit")])
    if not report.require(configure.returncode == 0, "configured", "cmake configure failed:\n" + configure.stdout[-1500:] + configure.stderr[-1500:]):
        return
    compiled = run(["cmake", "--build", str(build), "--parallel", "2"])
    borrowed = [str(Path(item).name) for item in manifest.get("borrowed", [])]
    warnings = [line for line in (compiled.stdout + compiled.stderr).splitlines()
                if "warning:" in line and "third_party" not in line and "/kit/" not in line
                and not any(name in line for name in borrowed)]
    if not report.require(compiled.returncode == 0, "compiled", "the build failed:\n" + (compiled.stdout + compiled.stderr)[-3000:]):
        return
    if warnings:
        report.warn(f"{len(warnings)} compiler warnings in your files, first: {warnings[0].strip()}")
    else:
        report.ok("no compiler warnings in your files")
    tests = run(["ctest", "--test-dir", str(build), "--output-on-failure", "--timeout", "120"])
    summary = [line for line in tests.stdout.splitlines() if "tests passed" in line or "Total Test time" in line]
    report.require(tests.returncode == 0, "tests pass: " + " ".join(summary).strip(), "tests failed:\n" + tests.stdout[-3000:])
    seconds = re.search(r"Total Test time \(real\) =\s*([\d.]+)", tests.stdout)
    if seconds and float(seconds.group(1)) > 30:
        report.warn(f"the tests take {seconds.group(1)} s; the suite runs on four platforms, keep a game's tests under 30 s")


def check_collection_build(directory: Path, manifest: dict, report: Report) -> None:
    print("Collection configure with automatic module discovery")
    build = REPO / ".build" / "new-games" / (manifest["id"] + "-collection")
    configure = run(["cmake", "-S", str(REPO), "-B", str(build), "-DGAMES_BUILD_APPLICATION=OFF",
                     "-DCMAKE_BUILD_TYPE=Release", "-DGAMES_EXTRA_GAME_DIRS=" + str(directory)])
    if not report.require(configure.returncode == 0, "collection discovers and configures the module",
                          "collection configure failed:\n" + (configure.stdout + configure.stderr)[-2500:]):
        return
    target = manifest.get("rules_target", manifest["id"] + "_rules_tests")
    compiled = run(["cmake", "--build", str(build), "--target", target, "--parallel", "2"])
    if not report.require(compiled.returncode == 0, "module builds inside the collection",
                          "collection module build failed:\n" + (compiled.stdout + compiled.stderr)[-2500:]):
        return
    tests = run(["ctest", "--test-dir", str(build), "--output-on-failure", "--timeout", "120",
                 "--no-tests=error", "-R", "^" + manifest["id"] + "_rules"])
    report.require(tests.returncode == 0, "module rules pass in the collection build", "collection rules failed:\n" + tests.stdout[-2500:])


def check_native(directory: Path, manifest: dict, report: Report, application: Path, script: Path | None) -> None:
    print("Native standalone window and scripted input")
    if not application.is_file() or (script is not None and not script.is_file()):
        report.fail("--application and --script must name existing files")
        return
    with tempfile.TemporaryDirectory(prefix="playsuite-game-gate-") as state:
        environment = dict(os.environ, GAMES_STATE_DIR=state)
        command = [str(application.resolve()), "--game", manifest["id"], "--standalone", "--dev"]
        command += ["--script", str(script.resolve())] if script else ["--smoke-test"]
        try:
            result = subprocess.run(command, cwd=directory, env=environment, capture_output=True, text=True, timeout=330)
        except subprocess.TimeoutExpired:
            report.fail("native script did not close the window within 330 seconds")
            return
        report.require(result.returncode == 0, "native standalone script completed with isolated development saves",
                       "native standalone script failed:\n" + (result.stdout + result.stderr)[-2500:])


def fetch_toolkit(report: Report) -> bool:
    commit = kitlib.pinned_toolkit_commit()
    stamp = TOOLKIT / ".commit"
    if stamp.is_file() and stamp.read_text().strip() == commit:
        return True
    if shutil.which("git") is None:
        report.fail("git was not found")
        return False
    shutil.rmtree(TOOLKIT, ignore_errors=True)
    TOOLKIT.parent.mkdir(parents=True, exist_ok=True)
    steps = (
        ["git", "clone", "--quiet", "--filter=blob:none", "--no-checkout", "--sparse",
         "https://github.com/falseywinchnet/file_manager.git", str(TOOLKIT)],
        ["git", "-C", str(TOOLKIT), "sparse-checkout", "set", "gui_forms/include"],
        ["git", "-C", str(TOOLKIT), "checkout", "--quiet", commit],
    )
    for step in steps:
        result = run(step)
        if result.returncode != 0:
            report.fail("fetching the GUI.Forms headers failed: " + result.stderr.strip()[-400:])
            return False
    stamp.write_text(commit + "\n")
    return True


def check_syntax(directory: Path, manifest: dict, report: Report, fetch: bool) -> None:
    print("Hosted view against the pinned GUI.Forms headers (syntax only, nothing is linked)")
    if fetch and not fetch_toolkit(report):
        return
    include = TOOLKIT / "gui_forms" / "include"
    if not include.is_dir():
        report.warn("not checked: run again with --fetch-toolkit (about 8 MB, once). Say so in HANDOFF.md if you skip it.")
        return
    compiler = shutil.which("clang++") or shutil.which("g++") or shutil.which("c++")
    if compiler is None:
        report.fail("no C++ compiler was found")
        return
    sys.path.insert(0, str(REPO / "tools"))
    from game_catalog import generate
    generated = REPO / ".build" / "new-games" / (manifest["id"] + "-generated")
    try:
        generate(REPO, generated, [directory])
    except ValueError as error:
        report.fail(str(error))
        return
    includes = ["-I", str(include), "-I", str(REPO / "src"), "-I", str(directory / "src"), "-I", str(kitlib.KIT / "kit")]
    includes += ["-I", str(generated)]
    for engine_directory in kitlib.engine_directories(manifest):
        includes += ["-I", str(engine_directory)]
    sources = [directory / item for item in manifest["ui_sources"]]
    if manifest.get("audio_adapter"):
        sources.append(directory / manifest["audio_adapter"])
    sources.append(directory / manifest["contract_test"])
    sources += [directory / manifest["module"], directory / manifest["cover"]]
    for source in sources:
        result = run([compiler, "-std=c++20", "-fsyntax-only", "-Wall", "-Wextra", "-D_USE_MATH_DEFINES"] + includes + [str(source)])
        output = (result.stdout + result.stderr).strip()
        relative = kitlib.display_path(source)
        if result.returncode != 0:
            report.fail(f"{relative} does not compile:\n" + output[-2500:])
        elif "warning:" in output:
            report.fail(f"{relative} compiles with warnings:\n" + output[-1500:])
        else:
            report.ok(str(relative))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("id")
    parser.add_argument("--fetch-toolkit", action="store_true")
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument("--application", type=Path, help="already-built native games executable")
    parser.add_argument("--script", type=Path, help="finite native runner input script")
    options = parser.parse_args()
    if options.script and not options.application:
        parser.error("--script requires --application")
    directory = kitlib.game_dir(options.id)
    if not directory.is_dir():
        print(f"check_game: {directory} does not exist", file=sys.stderr)
        return 2
    manifest = kitlib.load_manifest(directory)
    report = Report()
    check_structure(directory, manifest, report)
    check_style(directory, manifest, report)
    check_portability(directory, manifest, report)
    check_contract(directory, manifest, report)
    check_documents(directory, manifest, report)
    check_assets(directory, manifest, report)
    check_wiring(directory, manifest, report)
    if not options.no_build:
        check_build(directory, manifest, report)
        check_collection_build(directory, manifest, report)
    check_syntax(directory, manifest, report, options.fetch_toolkit)
    if options.application:
        check_native(directory, manifest, report, options.application, options.script)
    else:
        report.warn("native window not exercised here; pass --application (and optionally --script) after the full application build")
    print()
    if report.failures:
        print(f"{options.id}: {report.failures} FAIL, {report.warnings} WARN. Not ready.")
        return 1
    print(f"{options.id}: no failures, {report.warnings} WARN. The rules are kept; now play it and write HANDOFF.md honestly.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
