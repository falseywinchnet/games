#!/usr/bin/env python3
"""Folder discovery and generated roster regressions; no toolkit is required."""
from __future__ import annotations
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("game_catalog", ROOT / "tools/game_catalog.py")
catalog = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(catalog)

# SHA-256 of the complete original C++ help string bodies, before migration.
# These include paragraphs after semicolons and the entire Pen the Sheep text. Puzzle
# Solve's help was rewritten deliberately for its difficulty levels and new frames.
# Nature Cube's (its new finale) and Rock Stack's (Shift hurries; the bowl jiggles) were
# rewritten deliberately on 2026-10-10, at the user's word; Nature Cube's again on
# 2026-10-11 (a portal pair can be taken over; the ends light when joined), likewise. The Parrot's
# Table's became The Paradoxiary's on 2026-10-15, the game reworked at the user's word.
ORIGINAL_HELP = {'atom': 'ca18af9d89d615ad304a8208d958e7aace41d77f15a4e9ce7ca101b05cfd5467',
 'cube': 'a40a53845a6f5f01840fcddfca60e899d9d04576b2fb767f9b2718e32151d83e',
 'eggy': 'd899bd545ea8ac4956a1bd9f554d2df1794f0a138a2e7f78e9e041089d172c3d',
 'freecell': 'd30fad3d5f2ced150b5ec51c708664019ef030b69aa9f2c1350777dba8af5cbc',
 'gems': 'cfa82cf3f67b37ae0750339eec92c21e7a5ded06fb51e0eb37e8aa307fa9cfce',
 'hearts': '2cb0d288172f0f823ba50a9400e6ac8ff555664b300b69492b7a77cc23845a9a',
 'koikoi': '32d15393b7d307472e00dc9a949b12b3b02e705729a2869ce6176fddb71945a3',
 'liarsdice': 'd44667a75950b88c2777a7256779a051fba0b3f079ed6ca419de4000c65e1be9',
 'parrots': '0f1042efc38705e52471c9f419679677e40e93c46ddf683c97abf07635aaa487',
 'pegs': '183961b64a8360d584cdaeb5142f641af73293e7cd097a2558ecf2437a34f006',
 'penthesheep': '692afff977c635a3f8499723f3c0718f5dc220c771f1f5ae56d6067cb19e3a36',
 'rockstack': '741be15ca6fc17d134fe9f6cb7d3dcebab3f16437a500ed1783978c92d92561e',
 # Sudoku's digits moved beside the board and gained an eraser (2026-10-08).
 # Solitaire, Spider, Puzzle Solve, Nature Cube and Untangle: one sentence each now
 # names the shared Settings screen, which replaced the Options panel and Next command.
 'solitaire': 'c3af6b1376005add6ce94880a01f3e155a3ae0730d0dc494f84b0fb55436f8c6',
 'solve': 'ad54c2a2be773917f955cb662547f9d9a50feb6b9c42cd47193951101036599b',
 'spider': 'f3ffd088cef47869eb23fc81fbdfd2dd3f080953b42828fca2898b2b4d54f410',
 'sudoku': '9b4c028f42e73415f6b26e502d45cea6d16f1b2b2b78cdcbb3329c6a7f861fab',
 'switchbox': 'a900bf901111325449ad65dea3871803b5e104ea4db4fdf90af72b7a32405c65',
 'untangle': '90254adc26499fe7a6745291a71878dac9b8f505c161803dae8c47f068eac522'}

class CatalogTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="playsuite-catalog-")
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name) / "collection"
        self.output = Path(self.scratch.name) / "generated"
        (self.root / "games").mkdir(parents=True)

    def module(self, name="sample", entry=41, **overrides):
        directory = self.root / "games" / name
        directory.mkdir(parents=True)
        data = dict(schema_version=1, id=name, entry_id=entry, entry_name=name,
                    namespace="ns_" + name, title="A Garden", kind="Puzzle",
                    blurb="Grow a garden.", colors=[[1, 2, 3], [4, 5, 6], [7, 8, 9]],
                    module="module.cpp", cover="cover.cpp", help="help.md",
                    build="build.cmake", ui_sources=[], audio_sources=[], libraries=[])
        for filename in ("module.cpp", "cover.cpp", "build.cmake"):
            (directory / filename).write_text("// fixture\n", encoding="utf-8")
        (directory / "help.md").write_text('Complete help; more rules follow.\n\nUnicode: sheep — garden.\n', encoding="utf-8")
        data.update(overrides)
        self.save(directory, data)
        return directory, data

    @staticmethod
    def save(directory, data):
        (directory / "GAME.json").write_text(json.dumps(data), encoding="utf-8")

    def test_sparse_ids_and_numeric_order(self):
        self.module("last", 2147483647)
        self.module("middle", 4097)
        self.module("first", 3)
        games = catalog.generate(self.root, self.output)
        self.assertEqual([g["entry_id"] for g in games], [3, 4097, 2147483647])
        header = (self.output / "game_entries.hpp").read_text(encoding="utf-8")
        self.assertIn("entry_count = 3", header)
        self.assertIn("first = 3", header)
        self.assertIn("middle = 4097", header)
        self.assertIn("last = 2147483647", header)
        self.assertIn("Entry::first, Entry::middle, Entry::last", header)
        self.assertIn("return -1", header)

    def test_more_than_32_modules(self):
        for index in range(40):
            self.module(f"game_{index:02}", index * 19 + 5)
        games = catalog.generate(self.root, self.output)
        self.assertEqual(len(games), 40)
        self.assertIn("entry_count = 40", (self.output / "game_entries.hpp").read_text(encoding="utf-8"))

    def test_add_remove_and_restore_folder_only(self):
        first, data = self.module("first", 0)
        catalog.generate(self.root, self.output)
        before = (self.output / "game_entries.hpp").read_text(encoding="utf-8")
        second, data = self.module("second", 137)
        catalog.generate(self.root, self.output)
        self.assertIn("second = 137", (self.output / "game_entries.hpp").read_text(encoding="utf-8"))
        removed = self.root / "second.saved"
        shutil.move(second, removed)
        catalog.generate(self.root, self.output)
        self.assertEqual((self.output / "game_entries.hpp").read_text(encoding="utf-8"), before)
        shutil.move(removed, second)
        catalog.generate(self.root, self.output)
        self.assertIn("second = 137", (self.output / "game_entries.hpp").read_text(encoding="utf-8"))
        self.assertEqual(json.loads((first / "GAME.json").read_text(encoding="utf-8"))["entry_id"], 0)

    def test_external_module_and_idempotent_generation(self):
        self.module("local", 0)
        external, data = self.module("external", 91)
        destination = Path(self.scratch.name) / "external pack"
        shutil.move(external, destination)
        games = catalog.generate(self.root, self.output, [destination, destination])
        self.assertEqual([g["id"] for g in games], ["local", "external"])
        cmake = (self.output / "game_modules.cmake").read_text(encoding="utf-8")
        self.assertIn((destination / "module.cpp").as_posix(), cmake)
        stamps = {p.name: p.stat().st_mtime_ns for p in self.output.iterdir()}
        catalog.generate(self.root, self.output, [destination])
        self.assertEqual(stamps, {p.name: p.stat().st_mtime_ns for p in self.output.iterdir()})

    def test_duplicates_and_disabled_reservations(self):
        for key, value in (("id", "first"), ("entry_name", "first"),
                           ("entry_id", 7), ("namespace", "ns_first")):
            for disabled in (False, True):
                with self.subTest(key=key, disabled=disabled):
                    shutil.rmtree(self.root / "games")
                    self.module("first", 7, enabled=not disabled)
                    self.module("second", 8, **{key: value})
                    with self.assertRaisesRegex(ValueError, "duplicate " + key):
                        catalog.discover(self.root)

    def test_disabled_manifest_keeps_identity_without_sources(self):
        directory, data = self.module(enabled=False)
        (directory / "module.cpp").unlink()
        self.assertEqual(catalog.discover(self.root), [])
        self.assertEqual(catalog.discover(self.root, include_disabled=True)[0]["entry_id"], 41)
        with self.assertRaisesRegex(ValueError, "at least one enabled"):
            catalog.generate(self.root, self.output)

    def test_bad_identifiers_and_cpp_keywords(self):
        directory, original = self.module()
        for key in ("id", "entry_name", "namespace"):
            for value in ("9game", "two-words", "Game", "", "class", "namespace", "requires", "and"):
                with self.subTest(key=key, value=value):
                    self.save(directory, dict(original, **{key: value}))
                    with self.assertRaises(ValueError):
                        catalog.discover(self.root)

    def test_bad_entry_ids(self):
        directory, original = self.module()
        for value in (-1, 2147483648, True, 1.25, "1", None):
            with self.subTest(value=value):
                self.save(directory, dict(original, entry_id=value))
                with self.assertRaisesRegex(ValueError, "entry_id"):
                    catalog.discover(self.root)

    def test_missing_required_files(self):
        directory, original = self.module()
        for key in ("module", "cover", "build", "help"):
            with self.subTest(key=key):
                self.save(directory, dict(original, **{key: "missing.txt"}))
                with self.assertRaisesRegex(ValueError, "missing"):
                    catalog.discover(self.root)
        for key in ("ui_sources", "audio_sources"):
            self.save(directory, dict(original, **{key: ["missing.cpp"]}))
            with self.assertRaisesRegex(ValueError, "missing"):
                catalog.discover(self.root)

    def test_escaped_paths_and_cmake_interpolation_rejected(self):
        directory, original = self.module()
        outside = directory.parent / "outside.cpp"
        outside.write_text("fixture", encoding="utf-8")
        names = ["../outside.cpp", str(outside), 'odd"name.cpp',
                 "dollar$name.cpp", "semi;name.cpp", "line\nname.cpp"]
        # Windows creates symlinks only with Developer Mode or elevation; elsewhere a
        # link out of the folder must be rejected like any other escape.
        try:
            (directory / "linked.cpp").symlink_to(outside)
            names.append("linked.cpp")
        except OSError:
            pass
        for filename in names:
            with self.subTest(filename=filename):
                self.save(directory, dict(original, module=filename))
                # Invalid spelling is rejected before filesystem access, including
                # names that the Windows filesystem cannot represent at all.
                expected = "escapes its folder" if filename in ("../outside.cpp", "linked.cpp") else "invalid file path"
                with self.assertRaisesRegex(ValueError, expected):
                    catalog.generate(self.root, self.output)

    def test_assets_and_sources_are_module_local(self):
        directory, data = self.module()
        (directory / "assets").mkdir()
        (directory / "view.cpp").write_text("fixture", encoding="utf-8")
        (directory / "audio.cpp").write_text("fixture", encoding="utf-8")
        data.update(ui_sources=["view.cpp"], audio_sources=["audio.cpp"])
        self.save(directory, data)
        games = catalog.generate(self.root, self.output)
        self.assertEqual(games[0]["assets"], "assets")
        cmake = (self.output / "game_modules.cmake").read_text(encoding="utf-8")
        self.assertIn((directory / "view.cpp").as_posix(), cmake)
        self.assertIn((directory / "audio.cpp").as_posix(), cmake)

    def test_extra_topics_and_complete_text(self):
        directory, data = self.module()
        text = 'A full paragraph; after the semicolon.\n\nA "quoted" second paragraph — finished.'
        (directory / "sets.md").write_text(text + "\n", encoding="utf-8")
        data["help_topics"] = [dict(id="sets", title="The sets", file="sets.md")]
        self.save(directory, data)
        catalog.generate(self.root, self.output)
        cpp = (self.output / "game_registry.cpp").read_text(encoding="utf-8")
        self.assertIn(json.dumps(text, ensure_ascii=False), cpp)
        self.assertIn(json.dumps((directory / "help.md").read_text(encoding="utf-8").strip(), ensure_ascii=False), cpp)
        self.assertIn((directory / "sets.md").as_posix(), (self.output / "game_modules.cmake").read_text(encoding="utf-8"))

    def test_invalid_topics(self):
        directory, original = self.module()
        valid = dict(id="sets", title="Sets", file="help.md")
        variants = [[valid, valid], [dict(valid, id="rules")], [dict(valid, id="about")], [dict(valid, id="help")],
                    [dict(valid, title=42)], [dict(valid, title=" ")],
                    [dict(valid, file="missing.md")], [dict(valid, id="bad-id")]]
        for topics in variants:
            with self.subTest(topics=topics):
                self.save(directory, dict(original, help_topics=topics))
                with self.assertRaises(ValueError):
                    catalog.discover(self.root)

    def engine(self, name="scenery", namespace="scenery", **overrides):
        directory = self.root / "shared" / name
        (directory / "ui").mkdir(parents=True)
        for filename in ("build.cmake", "ui/view.cpp"):
            (directory / filename).write_text("# fixture\n", encoding="utf-8")
        data = dict(schema_version=1, id=name, namespace=namespace, title="Scenery",
                    summary="Shared scenery machinery.", build="build.cmake",
                    libraries=["scenery_core"], ui_sources=["ui/view.cpp"], ui_libraries=["scenery_ui"],
                    ui_directories=["ui"])
        data.update(overrides)
        (directory / "ENGINE.json").write_text(json.dumps(data), encoding="utf-8")
        return directory, data

    def test_engines_build_first_and_their_interface_only_when_used(self):
        engine, _ = self.engine()
        self.module("plain", 1)
        catalog.generate(self.root, self.output)
        cmake = (self.output / "game_modules.cmake").read_text(encoding="utf-8")
        self.assertIn(f'include("{(engine.resolve() / "build.cmake").as_posix()}")', cmake)
        self.assertNotIn("ui/view.cpp", cmake, "an unused engine adds no interface code")
        self.module("scene", 2, engines=["scenery"])
        games = catalog.generate(self.root, self.output)
        cmake = (self.output / "game_modules.cmake").read_text(encoding="utf-8")
        self.assertLess(cmake.index("ENGINE_DIR"), cmake.index("GAME_MODULE_DIR"), "engines precede games")
        self.assertEqual(cmake.count("ui/view.cpp"), 1)
        self.assertIn("list(APPEND GAMES_MODULE_LIBRARIES scenery_ui)", cmake)
        self.assertEqual([g["engines"] for g in games], [[], ["scenery"]])
        metadata = (self.output / "game_catalog_metadata.cmake").read_text(encoding="utf-8")
        self.assertIn("set(GAMES_ENGINE_IDS scenery)", metadata)
        self.assertIn("set(GAMES_MODULE_ENGINES_scene scenery)", metadata)

    def test_engine_errors(self):
        self.engine()
        self.module("scene", 2, engines=["missing"])
        with self.assertRaisesRegex(ValueError, "unknown engine"):
            catalog.discover(self.root)
        shutil.rmtree(self.root / "games" / "scene")
        self.module("scene", 2, namespace="scenery")
        with self.assertRaisesRegex(ValueError, "belongs to the engine"):
            catalog.discover(self.root)
        shutil.rmtree(self.root / "games" / "scene")
        self.module("scene", 2, engines=["scenery", "scenery"])
        with self.assertRaisesRegex(ValueError, "distinct engine ids"):
            catalog.discover(self.root)
        shutil.rmtree(self.root / "games" / "scene")
        self.module("scene", 2)
        for change, message in (({"id": "other"}, "folder name"), ({"namespace": "games"}, "reserved"),
                                ({"ui_sources": ["ui/missing.cpp"]}, "missing"),
                                ({"build": "../escape.cmake"}, "escapes"), ({"summary": ""}, "summary")):
            with self.subTest(change=change):
                shutil.rmtree(self.root / "shared")
                self.engine(**change)
                with self.assertRaisesRegex(ValueError, message):
                    catalog.discover(self.root)

    def test_malformed_manifest_has_actionable_error(self):
        directory, data = self.module()
        for payload in ("{broken", "[]", "null", "42"):
            with self.subTest(payload=payload):
                (directory / "GAME.json").write_text(payload, encoding="utf-8")
                with self.assertRaises(ValueError):
                    catalog.discover(self.root)

    def test_original_help_preserved(self):
        actual = {g["entry_name"]: g for g in catalog.discover(ROOT)}
        for name, expected in ORIGINAL_HELP.items():
            with self.subTest(game=name):
                self.assertIn(name, actual)
                game = actual[name]
                text = (game["directory"] / game["help"]).read_text(encoding="utf-8").strip()
                self.assertEqual(hashlib.sha256(text.encode()).hexdigest(), expected)
        self.assertEqual(len(ORIGINAL_HELP), 18)

if __name__ == "__main__":
    unittest.main()
