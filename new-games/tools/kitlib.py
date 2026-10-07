"""Shared helpers for the new-games tools. Standard library only."""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
KIT = REPO / "new-games"
TEMPLATE = KIT / "template"
VENDOR = REPO / "games"
ENGINES = REPO / "shared"

# Names a game may not take as its namespace: C++ and shell names, and the kit's own.
RESERVED_NAMESPACES = {"std", "gf", "games", "kit", "gui", "tg", "detail", "test"}
SOURCE_SUFFIXES = (".cpp", ".hpp", ".h", ".cc", ".hh")


def game_dir(game_id: str) -> Path:
    candidate = Path(game_id).expanduser()
    if candidate.is_dir():
        return candidate.resolve()
    return TEMPLATE if game_id == "templategame" else VENDOR / game_id


def load_manifest(directory: Path) -> dict:
    path = directory / "GAME.json"
    if not path.is_file():
        raise SystemExit(f"{path} is missing. Every kit game has a GAME.json.")
    return json.loads(path.read_text(encoding="utf-8"))


def used_namespaces() -> dict[str, str]:
    """Top-level namespaces already taken, mapped to where they were found."""
    found: dict[str, str] = {}
    pattern = re.compile(r"^namespace\s+([a-z_][a-z0-9_]*)\s*\{", re.MULTILINE)
    for root in (REPO / "src", VENDOR, ENGINES):
        for path in root.rglob("*"):
            if path.suffix not in SOURCE_SUFFIXES or "third_party" in path.parts:
                continue
            try:
                text = path.read_text(encoding="utf-8", errors="replace")
            except OSError:
                continue
            for match in pattern.finditer(text):
                found.setdefault(match.group(1), str(path.relative_to(REPO)))
    return found


def engines() -> dict[str, dict]:
    """Shared engines (shared/<id>/ENGINE.json), by id."""
    sys.path.insert(0, str(REPO / "tools"))
    from game_catalog import discover_engines
    return discover_engines(REPO)


def engine_directories(manifest: dict) -> list[Path]:
    """Include directories of the engines a game declares: sources, then interface code."""
    found = engines()
    result = []
    for engine_id in manifest.get("engines", []):
        engine = found.get(engine_id)
        if engine is None:
            raise SystemExit(f"GAME.json names engine {engine_id!r}, which is not in shared/")
        for key in ("source_directories", "ui_directories"):
            result += [engine["directory"] / value for value in engine.get(key, [])]
    return result


def catalog(extra_dirs=(), include_disabled=False) -> list[dict]:
    sys.path.insert(0, str(REPO / "tools"))
    from game_catalog import discover
    return discover(REPO, extra_dirs, include_disabled)


def entry_names() -> list[str]:
    return [game["id"] for game in catalog(include_disabled=True)]


def entry_count() -> int:
    return len(catalog())


def next_entry_id() -> int:
    """Permanent ids are append-only; disabled reservations still count."""
    value = max((game["entry_id"] for game in catalog(include_disabled=True)), default=-1) + 1
    if value > 2147483647:
        raise ValueError("The permanent entry-id range is exhausted")
    return value


def display_path(path: Path) -> str:
    try:
        return str(path.relative_to(REPO))
    except ValueError:
        return str(path)


def pascal(text: str) -> str:
    return "".join(part.capitalize() for part in re.split(r"[^A-Za-z0-9]+", text) if part)


def snake(text: str) -> str:
    return "_".join(part.lower() for part in re.split(r"[^A-Za-z0-9]+", text) if part)


def audio_sources(extra_dirs=()) -> list[Path]:
    sys.path.insert(0, str(REPO / "tools"))
    from prepare_portable_assets import asset_inventory
    return [entry["path"] for entry in asset_inventory(REPO / "assets", extra_dirs)["audio"]]


def pinned_toolkit_commit() -> str:
    workflow = (REPO / ".github" / "workflows" / "applications.yml").read_text(encoding="utf-8")
    match = re.search(r"repository:\s*falseywinchnet/file_manager\s*\n\s*ref:\s*([0-9a-f]{40})", workflow)
    if not match:
        raise SystemExit("The pinned GUI.Forms commit was not found in .github/workflows/applications.yml")
    return match.group(1)
