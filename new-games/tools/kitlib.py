"""Shared helpers for the new-games tools. Standard library only."""
from __future__ import annotations

import json
import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
KIT = REPO / "new-games"
TEMPLATE = KIT / "template"
VENDOR = REPO / "vendor"

# Names a game may not take as its namespace: C++ and shell names, and the kit's own.
RESERVED_NAMESPACES = {"std", "gf", "games", "kit", "gui", "tg", "detail", "test"}
NUMBER_WORDS = [
    "ZERO", "ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT", "NINE", "TEN",
    "ELEVEN", "TWELVE", "THIRTEEN", "FOURTEEN", "FIFTEEN", "SIXTEEN", "SEVENTEEN", "EIGHTEEN",
    "NINETEEN", "TWENTY", "TWENTY-ONE", "TWENTY-TWO", "TWENTY-THREE", "TWENTY-FOUR",
    "TWENTY-FIVE", "TWENTY-SIX", "TWENTY-SEVEN", "TWENTY-EIGHT", "TWENTY-NINE", "THIRTY",
    "THIRTY-ONE",
]
# The shell records which games have been opened in a 32-bit mask.
MAXIMUM_ENTRIES = 31
SOURCE_SUFFIXES = (".cpp", ".hpp", ".h", ".cc", ".hh")


def game_dir(game_id: str) -> Path:
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
    for root in (REPO / "src", VENDOR):
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


def entry_names() -> list[str]:
    """The persisted Entry enumerators in src/suite.hpp, in order."""
    text = (REPO / "src" / "suite.hpp").read_text(encoding="utf-8")
    match = re.search(r"enum class Entry : int \{(.*?)\};", text, re.DOTALL)
    if not match:
        raise SystemExit("src/suite.hpp: the Entry enum was not found")
    body = re.sub(r"//[^\n]*", "", match.group(1))
    return [name.strip() for name in body.split(",") if name.strip()]


def entry_count() -> int:
    text = (REPO / "src" / "suite.hpp").read_text(encoding="utf-8")
    match = re.search(r"inline constexpr int entry_count = (\d+);", text)
    if not match:
        raise SystemExit("src/suite.hpp: entry_count was not found")
    return int(match.group(1))


def pascal(text: str) -> str:
    return "".join(part.capitalize() for part in re.split(r"[^A-Za-z0-9]+", text) if part)


def snake(text: str) -> str:
    return "_".join(part.lower() for part in re.split(r"[^A-Za-z0-9]+", text) if part)


def audio_sources() -> list[Path]:
    directory = REPO / "assets" / "audio"
    return sorted(p for p in directory.iterdir() if p.suffix.lower() in (".m4a", ".wav"))


def pinned_toolkit_commit() -> str:
    workflow = (REPO / ".github" / "workflows" / "applications.yml").read_text(encoding="utf-8")
    match = re.search(r"repository:\s*falseywinchnet/file_manager\s*\n\s*ref:\s*([0-9a-f]{40})", workflow)
    if not match:
        raise SystemExit("The pinned GUI.Forms commit was not found in .github/workflows/applications.yml")
    return match.group(1)
