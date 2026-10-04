#!/usr/bin/env python3
"""The gate a kit game passes before it is delivered.

    python3 new-games/tools/check_game.py <id>                  everything that can be checked here
    python3 new-games/tools/check_game.py <id> --fetch-toolkit  also fetch the pinned GUI.Forms headers
                                                                (about 8 MB, once) and syntax-check the view
    python3 new-games/tools/check_game.py <id> --no-build       skip compiling and running the tests

It reports FAIL (must be fixed), WARN (decide, and say so in HANDOFF.md) and ok.
It exits non-zero on any FAIL. Passing does not mean the game is good; it means
the game keeps the collection's rules. Whether it is good is checked by playing it.

What this cannot do is build the PlaySuite window. The repository's CI does that
on four platforms when the branch is pushed; see new-games/guide/09-deliver.md.
"""
from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
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
    return files


def check_structure(directory: Path, manifest: dict, report: Report) -> None:
    print("Structure")
    for name in ("GAME.json", "README.md", "HANDOFF.md", "CMakeLists.txt"):
        report.require((directory / name).is_file(), name, f"{name} is missing")
    for key in ("core_sources", "ui_sources", "core_tests"):
        for item in manifest.get(key, []):
            report.require((directory / item).is_file(), f"{key}: {item}", f"{key} lists {item}, which does not exist")
    for key in ("audio_adapter", "contract_test"):
        report.require((directory / manifest.get(key, "?")).is_file(), f"{key}: {manifest.get(key)}", f"{key} file is missing")
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
    core = {directory / item for item in manifest["core_sources"]}
    core |= {path.with_suffix(".hpp") for path in core}
    others = {name for name in kitlib.used_namespaces() if name not in (manifest["namespace"], "games", "kit", "gui_forms", "paint")}
    problems = 0
    for path in files:
        raw = path.read_text(encoding="utf-8")
        text = strip_comments_and_strings(raw)
        relative = path.relative_to(REPO)
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
    print("Assets")
    prefix = manifest.get("audio_prefix", "")
    sources = kitlib.audio_sources()
    mine = [path for path in sources if prefix and path.name.startswith(prefix)]
    report.ok(f"{len(mine)} sound files named {prefix}* in assets/audio")
    big = [f"{path.name} ({path.stat().st_size // 1024} KiB)" for path in mine if path.stat().st_size > 3 * 1024 * 1024]
    if big:
        report.warn("large sound sources (music belongs in .m4a, effects in short .wav): " + ", ".join(big))
    view_text = "\n".join(path.read_text(encoding="utf-8") for path in authored_sources(directory, manifest)
                          if "src" in path.relative_to(directory).parts)
    view_text = re.sub(r"//[^\n]*", "", view_text)
    named = set(re.findall(r'"(%s[a-z0-9_]+)"' % re.escape(prefix), view_text)) if prefix else set()
    stems = {path.stem for path in mine}
    missing = sorted(named - stems)
    if missing and manifest["id"] != "templategame":
        report.warn("the view plays sounds that have no file yet (they stay silent): " + ", ".join(missing))
    unused = sorted(stems - named)
    if unused:
        report.warn("sound files the view never names: " + ", ".join(unused[:8]))
    name = manifest.get("audio_manifest") or ""
    if name:
        path = REPO / "assets" / "audio" / name
        if report.require(path.is_file(), f"assets/audio/{name}", f"assets/audio/{name} is missing"):
            data = json.loads(path.read_text(encoding="utf-8"))
            for entry in data.get("music", []):
                good = entry.get("id") in stems and isinstance(entry.get("loop_end_sample_exclusive"), int)
                report.require(good, f"music {entry.get('id')} has a file and a loop end",
                               f"music entry {entry.get('id')}: needs a matching file and loop_end_sample_exclusive")
    counts = set()
    for relative, expression in (("tools/prepare_portable_assets.py", r"if len\(records\) != (\d+):"),
                                 ("tools/verify_portable_assets.py", r"if len\(records\) != (\d+) or"),
                                 ("tests/audio_tests.cpp", r"require\(decoded == (\d+),")):
        match = re.search(expression, (REPO / relative).read_text(encoding="utf-8"))
        counts.add(int(match.group(1)) if match else -1)
    report.require(counts == {len(sources)}, f"audio inventory is {len(sources)} files in all three checks",
                   f"assets/audio has {len(sources)} sounds but the checks expect {sorted(counts)}; run wire_shelf.py again")


def check_wiring(manifest: dict, report: Report) -> None:
    game_id = manifest["id"]
    print("On the shelf")
    names = kitlib.entry_names()
    if game_id not in names:
        if game_id == "templategame":
            report.ok("the template is not a shelf entry")
        else:
            report.fail(f"Entry::{game_id} is not in src/suite.hpp; run new-games/tools/wire_shelf.py {game_id}")
        return
    for base in ("origin/main", "HEAD"):
        upstream = run(["git", "-C", str(REPO), "show", f"{base}:src/suite.hpp"])
        if upstream.returncode != 0:
            continue
        match = re.search(r"enum class Entry : int \{(.*?)\};", upstream.stdout, re.DOTALL)
        before = [name.strip() for name in re.sub(r"//[^\n]*", "", match.group(1)).split(",") if name.strip()] if match else []
        report.require(names[:len(before)] == before or game_id in before,
                       f"existing entries keep their values (compared with {base})",
                       "an existing Entry moved. Values are persisted in players' saves: append only.")
        break
    report.require(kitlib.entry_count() == len(names), f"entry_count is {len(names)}", "entry_count does not match the Entry enum")
    suite = (REPO / "src" / "suite.cpp").read_text(encoding="utf-8")
    table = suite[suite.find("entries{{"):suite.find("\n}};", suite.find("entries{{"))]
    rows = len(re.findall(r'^\s*\{"', table, re.MULTILINE))
    report.require(rows == len(names), f"{rows} boxes described", f"src/suite.cpp describes {rows} boxes for {len(names)} entries")
    report.require(f"case Entry::{game_id}:" in suite, "the box has an emblem", "src/suite.cpp has no emblem case for the game")
    report.require(f"TODO({game_id})" not in suite, "the emblem is drawn", "the emblem in src/suite.cpp is still the placeholder (search for TODO)")
    help_content = (REPO / "src" / "help_content.cpp").read_text(encoding="utf-8")
    report.require(f"case Entry::{game_id}:" in help_content,
                   "the shared help document has a game section",
                   "src/help_content.cpp needs this game's goal, moves, ending and controls")
    collection = (REPO / "src" / "collection.cpp").read_text(encoding="utf-8")
    member = f"{game_id}_"
    points = {
        "created on first visit": f"{member} = gf::make_control",
        "under the rail": f"entry == Entry::{game_id} ||",
        "returned by view()": f"        return {member};",
        "commands offered": f"return {member}.get();",
        "laid out below the rail": f"child == {member} ||",
        "set_cabinet called": f"(*{member}).set_cabinet(",
        "activated": f"(*{member}).activate();",
    }
    for what, needle in points.items():
        report.require(needle in collection, f"collection: {what}", f"src/collection.cpp: missing '{what}'")
    root = (REPO / "CMakeLists.txt").read_text(encoding="utf-8")
    application = (REPO / "cmake" / "Application.cmake").read_text(encoding="utf-8")
    marker = f"# >>> new-games: {game_id}"
    report.require(marker in root, "core and rules tests are built", "CMakeLists.txt has no block for the game")
    report.require(marker in application, "view and contract test are built", "cmake/Application.cmake has no block for the game")
    for source in manifest["core_sources"]:
        report.require(f"vendor/{game_id}/{source}" in root, f"core builds {source}", f"CMakeLists.txt does not build {source}; run wire_shelf.py again")
    for source in manifest["ui_sources"]:
        report.require(f"vendor/{game_id}/{source}" in application, f"shell builds {source}",
                       f"cmake/Application.cmake does not build {source}; run wire_shelf.py again")
    shell_test = (REPO / "tests" / "collection_ui_tests.cpp").read_text(encoding="utf-8")
    report.require(f"Entry::{game_id}" in shell_test, "the shell test opens the game", "tests/collection_ui_tests.cpp does not visit the game")
    shelf = (REPO / "src" / "shelf.cpp").read_text(encoding="utf-8")
    report.require(f'"{kitlib.NUMBER_WORDS[len(names)]} GAMES ' in shelf, "the shelf sign counts the game",
                   f"src/shelf.cpp: the sign should say {kitlib.NUMBER_WORDS[len(names)]} GAMES")
    for relative in ("README.md", "docs/GAME_CATALOG.md"):
        if manifest["title"] not in (REPO / relative).read_text(encoding="utf-8"):
            report.warn(f"{relative} does not mention {manifest['title']} yet")


def check_build(directory: Path, manifest: dict, report: Report) -> None:
    print("Build and tests (the game's own headless build)")
    if shutil.which("cmake") is None:
        report.fail("cmake was not found; install CMake 3.25 or later")
        return
    build = REPO / ".build" / "new-games" / manifest["id"]
    configure = run(["cmake", "-S", str(directory), "-B", str(build), "-DCMAKE_BUILD_TYPE=Release"])
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
    game_id = manifest["id"]
    includes = ["-I", str(include), "-I", str(REPO / "src"), "-I", str(directory / "src"), "-I", str(kitlib.KIT / "kit")]
    sources = [directory / item for item in manifest["ui_sources"]]
    sources += [directory / manifest["audio_adapter"], directory / manifest["contract_test"]]
    shell: list[Path] = []
    if game_id in kitlib.entry_names():
        # The wired shell must still compile: it includes every game's view.
        for other in sorted(kitlib.VENDOR.iterdir()):
            for folder in ("src", "phys"):
                if (other / folder).is_dir():
                    includes += ["-I", str(other / folder)]
        includes += ["-I", str(kitlib.VENDOR / "paint")]
        shell = [REPO / "src" / "collection.cpp", REPO / "src" / "suite.cpp", REPO / "src" / "shelf.cpp"]
    for source in sources + shell:
        result = run([compiler, "-std=c++20", "-fsyntax-only", "-Wall", "-Wextra", "-D_USE_MATH_DEFINES"] + includes + [str(source)])
        output = (result.stdout + result.stderr).strip()
        relative = source.relative_to(REPO)
        if result.returncode != 0:
            report.fail(f"{relative} does not compile:\n" + output[-2500:])
        elif "warning:" in output and source not in shell:
            report.fail(f"{relative} compiles with warnings:\n" + output[-1500:])
        else:
            report.ok(str(relative))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("id")
    parser.add_argument("--fetch-toolkit", action="store_true")
    parser.add_argument("--no-build", action="store_true")
    options = parser.parse_args()
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
    check_wiring(manifest, report)
    if not options.no_build:
        check_build(directory, manifest, report)
    check_syntax(directory, manifest, report, options.fetch_toolkit)
    print()
    if report.failures:
        print(f"{options.id}: {report.failures} FAIL, {report.warnings} WARN. Not ready.")
        return 1
    print(f"{options.id}: no failures, {report.warnings} WARN. The rules are kept; now play it and write HANDOFF.md honestly.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
