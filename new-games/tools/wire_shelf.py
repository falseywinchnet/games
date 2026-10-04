#!/usr/bin/env python3
"""Put a kit game on the PlaySuite shelf.

    python3 new-games/tools/wire_shelf.py <id>            apply the edits
    python3 new-games/tools/wire_shelf.py <id> --dry-run  show what would change

Reads vendor/<id>/GAME.json and makes every edit the shell needs: the persisted
shelf entry, the box's title and colours, a placeholder emblem, lazy creation and
hosting in the collection, the build targets, the tests and the audio inventory.
Safe to run again after you add source files or sounds: finished edits are
skipped and the build lists are regenerated.

Each edit looks for a known anchor in the shell's source. If the shell has
changed and an anchor is gone, the tool stops before writing anything and names
the edit; make that one by hand from new-games/guide/08-on-the-shelf.md.

It leaves three things to you: the emblem drawing in src/suite.cpp, the roster
sentences in README.md and docs/GAME_CATALOG.md, and the version and release
notes (the maintainer's).
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

import kitlib

REPO = kitlib.REPO


class Edits:
    """Collects whole-file replacements and writes them only if every edit found its anchor."""

    def __init__(self) -> None:
        self.texts: dict[Path, str] = {}
        self.notes: list[str] = []
        self.problems: list[str] = []

    def text(self, relative: str) -> str:
        path = REPO / relative
        if path not in self.texts:
            self.texts[path] = path.read_text(encoding="utf-8")
        return self.texts[path]

    def put(self, relative: str, text: str, note: str) -> None:
        path = REPO / relative
        if self.texts.get(path) != text:
            self.texts[path] = text
            if f"{relative}: {note}" not in self.notes:
                self.notes.append(f"{relative}: {note}")

    def insert_before(self, relative: str, anchor: str, addition: str, done: str, note: str) -> None:
        text = self.text(relative)
        if done in text:
            return
        index = text.find(anchor)
        if index < 0:
            self.problems.append(f"{relative}: anchor not found for '{note}'")
            return
        self.put(relative, text[:index] + addition + text[index:], note)

    def insert_after(self, relative: str, anchor: str, addition: str, done: str, note: str) -> None:
        text = self.text(relative)
        if done in text:
            return
        index = text.find(anchor)
        if index < 0:
            self.problems.append(f"{relative}: anchor not found for '{note}'")
            return
        index += len(anchor)
        self.put(relative, text[:index] + addition + text[index:], note)

    def replace_block(self, relative: str, game_id: str, block: str, before_anchor: str | None, note: str) -> None:
        """Keeps one marked block per game, replacing it on later runs."""
        text = self.text(relative)
        begin = f"# >>> new-games: {game_id}\n"
        end = f"# <<< new-games: {game_id}\n"
        marked = begin + block + end
        start = text.find(begin)
        if start >= 0:
            stop = text.find(end, start)
            if stop < 0:
                self.problems.append(f"{relative}: the block for {game_id} has no end marker")
                return
            self.put(relative, text[:start] + marked + text[stop + len(end):], note)
            return
        if before_anchor is None:
            separator = "" if text.endswith("\n") else "\n"
            self.put(relative, text + separator + marked, note)
            return
        index = text.find(before_anchor)
        if index < 0:
            self.problems.append(f"{relative}: anchor not found for '{note}'")
            return
        self.put(relative, text[:index] + marked + text[index:], note)


def cpp_string(text: str) -> str:
    return text.replace("\\", "\\\\").replace('"', '\\"')


def wire(game_id: str, edits: Edits) -> None:
    directory = kitlib.VENDOR / game_id
    manifest = kitlib.load_manifest(directory)
    if manifest.get("id") != game_id:
        raise SystemExit(f"GAME.json says id '{manifest.get('id')}', not '{game_id}'")
    ns = manifest["namespace"]
    view = f"{ns}::{manifest['view_class']}"
    member = f"{game_id}_"
    vendor = f"vendor/{game_id}"

    # ---- src/suite.hpp: the persisted entry. Append only; never reorder.
    names = kitlib.entry_names()
    if game_id not in names:
        count = kitlib.entry_count()
        if count != len(names):
            edits.problems.append("src/suite.hpp: entry_count does not match the Entry enum; fix that first")
        if count + 1 > kitlib.MAXIMUM_ENTRIES:
            edits.problems.append("src/suite.hpp: the shell's opened-games mask holds 31 entries; it must be widened first")
        text = edits.text("src/suite.hpp")
        pattern = re.compile(r"(enum class Entry : int \{.*?)(\n\};\ninline constexpr int entry_count = )(\d+);", re.DOTALL)
        match = pattern.search(text)
        if not match:
            edits.problems.append("src/suite.hpp: anchor not found for 'append the Entry'")
        else:
            replaced = text[:match.start()] + match.group(1) + f"\n    {game_id}," + match.group(2) + str(count + 1) + ";" + text[match.end():]
            edits.put("src/suite.hpp", replaced, f"append Entry::{game_id}, entry_count {count} -> {count + 1}")
        names = names + [game_id]
    total = len(names)

    # ---- src/suite.cpp: the box (title, kind, blurb, colours) and its emblem.
    cover = manifest["cover"]
    row = (f'    {{"{cpp_string(manifest["title"])}", "{cpp_string(manifest["kind"])}", "{cpp_string(manifest["blurb"])}",\n'
           f'     rgb({cover["top"][0]}, {cover["top"][1]}, {cover["top"][2]}), rgb({cover["bottom"][0]}, {cover["bottom"][1]}, {cover["bottom"][2]}), '
           f'rgb({cover["accent"][0]}, {cover["accent"][1]}, {cover["accent"][2]})}},\n')
    text = edits.text("src/suite.cpp")
    table = text.find("const std::array<EntryInfo, entry_count> entries{{")
    table_end = text.find("\n}};\n", table)
    if f'{{"{cpp_string(manifest["title"])}", ' not in text:
        if table < 0 or table_end < 0:
            edits.problems.append("src/suite.cpp: anchor not found for 'add the box's EntryInfo row'")
        else:
            edits.put("src/suite.cpp", text[:table_end + 1] + row + text[table_end + 1:], "add the box's title, kind, blurb and colours")
    emblem = (f"    case Entry::{game_id}:\n"
              f"        // TODO({game_id}): draw this game's box emblem. `s` is the emblem's size and (cx, cy) its centre.\n"
              f"        disc(p, cx, cy, s * .30, rgb({cover['accent'][0]}, {cover['accent'][1]}, {cover['accent'][2]}));\n"
              f"        disc(p, cx, cy, s * .16, rgb({cover['bottom'][0]}, {cover['bottom'][1]}, {cover['bottom'][2]}));\n"
              f"        break;\n")
    text = edits.text("src/suite.cpp")
    if f"case Entry::{game_id}:" not in text:
        function = text.find("void paint_entry_emblem(")
        first_case = text.find("    case Entry::", function)
        if function < 0 or first_case < 0:
            edits.problems.append("src/suite.cpp: anchor not found for 'add a placeholder emblem'")
        else:
            edits.put("src/suite.cpp", text[:first_case] + emblem + text[first_case:], "add a placeholder emblem (draw the real one)")

    # ---- src/collection.hpp / .cpp: lazy creation, the rail, commands, preferences.
    header = manifest["view_header"]
    edits.insert_before("src/collection.hpp", '\n#include "text_sprites.hpp"', f'#include "{header}"\n',
                        f'#include "{header}"', "include the view")
    edits.insert_before("src/collection.hpp", "    std::vector<gf::SubscriptionToken> subscriptions_;",
                        f"    std::shared_ptr<{view}> {member};\n", f"> {member};", "hold the view")
    edits.insert_before("src/collection.cpp", "    else {\n        const int i = puzzle_index(entry);",
                        f"    else if (entry == Entry::{game_id})\n"
                        f"        {member} = gf::make_control<{view}>(gf::StableId(\"{game_id}.view\"),\n"
                        f"            {ns}::Options{{.hosted = true}});\n",
                        f"{member} = gf::make_control", "create the view on first visit")
    edits.insert_after("src/collection.cpp", "    return entry == Entry::atom ||", f" entry == Entry::{game_id} ||",
                       f"return entry == Entry::atom || entry == Entry::{game_id}", "place the game under the rail")
    text = edits.text("src/collection.cpp")
    view_function = text.find("std::shared_ptr<gf::Control> Collection::view(Entry entry) const {")
    switch = text.find("    switch (entry) {\n", view_function)
    if f"        return {member};" not in text:
        if view_function < 0 or switch < 0:
            edits.problems.append("src/collection.cpp: anchor not found for 'return the view'")
        else:
            at = switch + len("    switch (entry) {\n")
            edits.put("src/collection.cpp", text[:at] + f"    case Entry::{game_id}:\n        return {member};\n" + text[at:], "return the view")
    edits.insert_after("src/collection.cpp", "CommandSource* Collection::source(Entry entry) const {\n",
                       f"    if (entry == Entry::{game_id})\n        return {member}.get();\n",
                       f"return {member}.get();", "offer the game's commands to the capsule")
    edits.insert_after("src/collection.cpp", "const bool railed = child == atomprobe_ ||", f" child == {member} ||",
                       f"child == atomprobe_ || child == {member}", "lay the game out below the rail")
    edits.insert_before("src/collection.cpp", "    (*shelf_).set_preferences(",
                        f"    if ({member})\n"
                        f"        (*{member}).set_cabinet(!shelf_open_ && active_ == Entry::{game_id}, cabinet.music,\n"
                        f"                                cabinet.sound, cabinet.reduced);\n",
                        f"(*{member}).set_cabinet(", "pass foreground and master switches")
    edits.insert_before("src/collection.cpp", "    } else\n        (*puzzles_[static_cast<std::size_t>(puzzle_index(active_))]).activate();",
                        f"    }} else if (active_ == Entry::{game_id}) {{\n"
                        f"        music_play(\"\", false);\n"
                        f"        (*{member}).activate();\n",
                        f"(*{member}).activate();", "activate the game when opened")

    # ---- src/shelf.cpp: the sign's count.
    text = edits.text("src/shelf.cpp")
    sign = re.search(r'SpriteSpec tag\{"([A-Z-]+) GAMES ', text)
    if not sign:
        edits.problems.append("src/shelf.cpp: anchor not found for 'update the sign's game count'")
    elif sign.group(1) != kitlib.NUMBER_WORDS[total]:
        edits.put("src/shelf.cpp", text[:sign.start(1)] + kitlib.NUMBER_WORDS[total] + text[sign.end(1):],
                  f"the sign now says {kitlib.NUMBER_WORDS[total]} GAMES")

    # ---- CMakeLists.txt: the portable core and its tests (every platform's core CI).
    core = " ".join(f"{vendor}/{source}" for source in manifest["core_sources"])
    block = (f"add_library({ns}_core STATIC {core})\n"
             f"target_include_directories({ns}_core PUBLIC {vendor}/src)\n"
             f"target_link_libraries({ns}_core PUBLIC game_paths)\n"
             f"target_compile_definitions({ns}_core PRIVATE _USE_MATH_DEFINES)\n")
    for index, test in enumerate(manifest["core_tests"]):
        suffix = "" if index == 0 else f"_{index + 1}"
        block += (f"add_executable({game_id}_rules_tests{suffix} {vendor}/{test})\n"
                  f"target_link_libraries({game_id}_rules_tests{suffix} PRIVATE {ns}_core)\n"
                  f"add_test(NAME {game_id}_rules{suffix} COMMAND {game_id}_rules_tests{suffix})\n")
    edits.replace_block("CMakeLists.txt", game_id, block, "if(GAMES_BUILD_APPLICATION)\n  include(cmake/Application.cmake)",
                        "build the game's core and rules tests")

    # ---- cmake/Application.cmake: the view, its adapters and the hosted contract test.
    ui = " ".join(f"{vendor}/{source}" for source in manifest["ui_sources"])
    contract = (f"target_sources(game_audio_adapters PRIVATE {vendor}/{manifest['audio_adapter']})\n"
                f"target_sources(vendor_game_ui PRIVATE {ui})\n"
                f"target_link_libraries(vendor_game_ui PUBLIC {ns}_core)\n"
                f"add_executable({game_id}_contract_tests {vendor}/{manifest['contract_test']})\n"
                f"target_include_directories({game_id}_contract_tests PRIVATE new-games/kit)\n"
                f"target_link_libraries({game_id}_contract_tests PRIVATE vendor_game_ui)\n"
                f"add_test(NAME {game_id}_view_contract COMMAND {game_id}_contract_tests)\n"
                f"set_tests_properties({game_id}_view_contract PROPERTIES TIMEOUT 90\n"
                f"    ENVIRONMENT \"GAMES_ASSET_DIR=${{GAMES_RUNTIME_ASSET_DIR}}\")\n")
    edits.replace_block("cmake/Application.cmake", game_id, contract, None,
                        "compile the view and adapters; run the hosted-view contract test")

    # ---- tests/collection_ui_tests.cpp: the shell test opens the game and its Help.
    text = edits.text("tests/collection_ui_tests.cpp")
    loop = re.search(r"for \(Entry entry : \{(Entry::koikoi[^}]*)\}\) \{", text)
    if not loop:
        edits.problems.append("tests/collection_ui_tests.cpp: anchor not found for 'visit the game in the shell test'")
    elif f"Entry::{game_id}" not in loop.group(1):
        edits.put("tests/collection_ui_tests.cpp", text[:loop.end(1)] + f", Entry::{game_id}" + text[loop.end(1):],
                  "open the game and its Help from the capsule in the shell test")

    # ---- Audio inventory: three places state the exact number of source sounds.
    count = len(kitlib.audio_sources())
    audio_edits = (
        ("tools/prepare_portable_assets.py", r"if len\(records\) != (\d+):"),
        ("tools/prepare_portable_assets.py", r"Expected all (\d+) source audio files"),
        ("tools/verify_portable_assets.py", r"if len\(records\) != (\d+) or"),
        ("tools/verify_portable_assets.py", r"or len\(originals\) != (\d+):"),
        ("tests/audio_tests.cpp", r"require\(decoded == (\d+),"),
        ("tests/audio_tests.cpp", r'<< "/(\d+) files\.\\n"'),
    )
    for relative, expression in audio_edits:
        text = edits.text(relative)
        match = re.search(expression, text)
        if not match:
            edits.problems.append(f"{relative}: anchor not found for 'audio file count'")
        elif int(match.group(1)) != count:
            edits.put(relative, text[:match.start(1)] + str(count) + text[match.end(1):], f"audio inventory is now {count} files")
    manifest_name = manifest.get("audio_manifest") or ""
    if manifest_name:
        if not (REPO / "assets" / "audio" / manifest_name).is_file():
            edits.problems.append(f"assets/audio/{manifest_name} is named in GAME.json but does not exist")
        text = edits.text("tools/prepare_portable_assets.py")
        if f'"{manifest_name}"' not in text:
            tuple_end = re.search(r'for name in \("audio_manifest\.json"[^)]*', text)
            if not tuple_end:
                edits.problems.append("tools/prepare_portable_assets.py: anchor not found for 'read the game's music loop bounds'")
            else:
                edits.put("tools/prepare_portable_assets.py", text[:tuple_end.end()] + f', "{manifest_name}"' + text[tuple_end.end():],
                          "read the game's music loop bounds")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("id")
    parser.add_argument("--dry-run", action="store_true", help="report the edits without writing")
    options = parser.parse_args()
    if not (kitlib.VENDOR / options.id).is_dir():
        print(f"wire_shelf: vendor/{options.id} does not exist", file=sys.stderr)
        return 2
    edits = Edits()
    wire(options.id, edits)
    if edits.problems:
        print("wire_shelf: nothing was written. These edits could not be placed:", file=sys.stderr)
        for problem in edits.problems:
            print("  " + problem, file=sys.stderr)
        print("Make them by hand from new-games/guide/08-on-the-shelf.md, then run this again.", file=sys.stderr)
        return 1
    if not edits.notes:
        print(f"{options.id} is already wired; nothing to change.")
        return 0
    for note in edits.notes:
        print(("would edit  " if options.dry_run else "edited  ") + note)
    if not options.dry_run:
        for path, text in edits.texts.items():
            if path.read_text(encoding="utf-8") != text:
                path.write_text(text, encoding="utf-8")
        print("\nStill yours to do: draw the emblem in src/suite.cpp (search for TODO), add the game to the roster")
        print("sentences in README.md and docs/GAME_CATALOG.md, then run check_game.py.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
