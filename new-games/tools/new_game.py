#!/usr/bin/env python3
"""Start a new PlaySuite game from the kit's template.

    python3 new-games/tools/new_game.py --id tidepools --namespace tp --title "Tide Pools"

Creates vendor/<id>/ as a complete, building, tested game (the template's lamps
puzzle under your names), ready for you to replace its rules, scene and words.
It changes nothing else in the repository; wire_shelf.py does that later.
"""
from __future__ import annotations

import argparse
import json
import re
import shutil
import sys

import kitlib

TEXT_SUFFIXES = (".cpp", ".hpp", ".txt", ".md", ".json", ".py", ".cmake")


def substitute(text: str, names: dict[str, str]) -> str:
    # Longest and most specific names first, so one replacement cannot damage another.
    text = text.replace("TEMPLATEGAME1", names["magic"])
    text = text.replace("TemplateView", names["view_class"])
    text = text.replace("template_view", names["view_file"])
    text = text.replace("template_game", names["save_stem"])
    text = text.replace("TemplateGame", names["pascal"])
    text = text.replace("Template Game", names["title"])
    text = text.replace("templategame", names["id"])
    text = text.replace("TG_", names["namespace"].upper() + "_")
    text = re.sub(r"\btg_", names["namespace"] + "_", text)
    text = re.sub(r"\btg\b", names["namespace"], text)
    return text


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--id", required=True, help="folder and shelf id: lowercase letters and digits, e.g. tidepools")
    parser.add_argument("--namespace", required=True, help="the game's C++ namespace: 2 to 4 lowercase letters, e.g. tp")
    parser.add_argument("--title", required=True, help='the name on the box, e.g. "Tide Pools"')
    parser.add_argument("--kind", default="Puzzle", help='two or three words under the title, e.g. "Shore puzzle"')
    parser.add_argument("--blurb", default="", help="one sentence for the shelf ticket")
    options = parser.parse_args()

    if not re.fullmatch(r"[a-z][a-z0-9]{2,19}", options.id):
        return fail("--id must be 3 to 20 lowercase letters or digits, starting with a letter")
    if not re.fullmatch(r"[a-z]{2,4}", options.namespace):
        return fail("--namespace must be 2 to 4 lowercase letters")
    taken = kitlib.used_namespaces()
    if options.namespace in taken or options.namespace in kitlib.RESERVED_NAMESPACES:
        where = taken.get(options.namespace, "reserved")
        return fail(f"namespace '{options.namespace}' is already used ({where}); choose another")
    if options.id in kitlib.entry_names() or options.id in taken:
        return fail(f"'{options.id}' is already a shelf entry or a namespace; choose another id")
    destination = kitlib.VENDOR / options.id
    if destination.exists():
        return fail(f"{destination.relative_to(kitlib.REPO)} already exists")
    if not options.title.strip() or len(options.title) > 28:
        return fail("--title must be 1 to 28 characters (it has to fit a box)")

    names = {
        "id": options.id,
        "namespace": options.namespace,
        "title": options.title.strip(),
        "pascal": kitlib.pascal(options.title),
        "view_class": kitlib.pascal(options.title) + "View",
        "view_file": options.id + "_view",
        "save_stem": kitlib.snake(options.title),
        "magic": options.id.upper() + "1",
    }
    shutil.copytree(kitlib.TEMPLATE, destination, ignore=shutil.ignore_patterns("build", "*.png", "__pycache__"))
    for path in sorted(destination.rglob("*")):
        if not path.is_file():
            continue
        if path.suffix in TEXT_SUFFIXES:
            path.write_text(substitute(path.read_text(encoding="utf-8"), names), encoding="utf-8")
        if "template_view" in path.name:
            path.rename(path.with_name(path.name.replace("template_view", names["view_file"])))

    manifest_path = destination / "GAME.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["kind"] = options.kind
    manifest["blurb"] = options.blurb or "TODO: one sentence that makes someone pick up the box."
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    relative = destination.relative_to(kitlib.REPO)
    print(f"Created {relative}/ in namespace {options.namespace} (view {options.namespace}::{names['view_class']}).")
    print("Next:")
    print(f"  cmake -S {relative} -B .build/new-games/{options.id} && cmake --build .build/new-games/{options.id} -j 2")
    print(f"  ctest --test-dir .build/new-games/{options.id} --output-on-failure")
    print(f"  .build/new-games/{options.id}/{options.namespace}_preview .build/new-games/{options.id}/first.png 600 370")
    print("Then follow new-games/AGENTS.md from step 3.")
    return 0


def fail(message: str) -> int:
    print("new_game: " + message, file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
