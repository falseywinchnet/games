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
# These include paragraphs after semicolons and the entire Pen the Sheep text.
ORIGINAL_HELP = {'atom': 'ca18af9d89d615ad304a8208d958e7aace41d77f15a4e9ce7ca101b05cfd5467',
 'cube': '84c4463ce7dc59d84be565dbab6e547a8b05729596ef9a12ce52458e18e7b54b',
 'eggy': 'd899bd545ea8ac4956a1bd9f554d2df1794f0a138a2e7f78e9e041089d172c3d',
 'freecell': 'b41e475de83b95b1e0172736c9c2ddb272d094f8d5d5512d03a3040bc2f74407',
 'gems': 'cfa82cf3f67b37ae0750339eec92c21e7a5ded06fb51e0eb37e8aa307fa9cfce',
 'hearts': '2cb0d288172f0f823ba50a9400e6ac8ff555664b300b69492b7a77cc23845a9a',
 'koikoi': '32d15393b7d307472e00dc9a949b12b3b02e705729a2869ce6176fddb71945a3',
 'liarsdice': 'd44667a75950b88c2777a7256779a051fba0b3f079ed6ca419de4000c65e1be9',
 'parrots': 'b761336d7a3e1b6277b980cf8fea44a8aa99c9d92caa9afd7bdbb26367695cc9',
 'pegs': '183961b64a8360d584cdaeb5142f641af73293e7cd097a2558ecf2437a34f006',
 'penthesheep': '045390875c57892369bdcdcf784506f06459d57014930b42e2e14a1a3f91ef75',
 'rockstack': '44a440789430322b44d32b8df5ee3b3f13a9d28d04618544230bd5e96fea615a',
 'solitaire': '3feb19370ff30645a3e9e48dfab46f78efde2f3c95884247db1a2ed7dea2ac36',
 'solve': 'bc748b3ca39eb8311ba1fb8b605dbf509eac2c0d4c514b7736fc474a58bf4fb6',
 'spider': '920aaed5375f03bab6d22293fc48ac69a18cc51c930847cf5a6edd623f4faf3c',
 'sudoku': '5a2e157274e6ea553bd624bf6962e8d9ef839e3502cb0386e903a24ee1b60133',
 'switchbox': 'a900bf901111325449ad65dea3871803b5e104ea4db4fdf90af72b7a32405c65',
 'untangle': 'e5c765b5c2a5ed4f0cdf0f5788bd7253ba62dc13f5daa5764998553639324ebb'}

class CatalogTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="playsuite-catalog-")
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name) / "collection"
        self.output = Path(self.scratch.name) / "generated"
        (self.root / "vendor").mkdir(parents=True)

    def module(self, name="sample", entry=41, **overrides):
        directory = self.root / "vendor" / name
        directory.mkdir(parents=True)
        data = dict(schema_version=1, id=name, entry_id=entry, entry_name=name,
                    namespace="ns_" + name, title="A Garden", kind="Puzzle",
                    blurb="Grow a garden.", colors=[[1, 2, 3], [4, 5, 6], [7, 8, 9]],
                    module="module.cpp", cover="cover.cpp", help="help.md",
                    build="build.cmake", ui_sources=[], audio_sources=[], libraries=[])
        for filename in ("module.cpp", "cover.cpp", "build.cmake"):
            (directory / filename).write_text("// fixture\n")
        (directory / "help.md").write_text('Complete help; more rules follow.\n\nUnicode: sheep — garden.\n')
        data.update(overrides)
        self.save(directory, data)
        return directory, data

    @staticmethod
    def save(directory, data):
        (directory / "GAME.json").write_text(json.dumps(data))

    def test_sparse_ids_and_numeric_order(self):
        self.module("last", 2147483647)
        self.module("middle", 4097)
        self.module("first", 3)
        games = catalog.generate(self.root, self.output)
        self.assertEqual([g["entry_id"] for g in games], [3, 4097, 2147483647])
        header = (self.output / "game_entries.hpp").read_text()
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
        self.assertIn("entry_count = 40", (self.output / "game_entries.hpp").read_text())

    def test_add_remove_and_restore_folder_only(self):
        first, data = self.module("first", 0)
        catalog.generate(self.root, self.output)
        before = (self.output / "game_entries.hpp").read_text()
        second, data = self.module("second", 137)
        catalog.generate(self.root, self.output)
        self.assertIn("second = 137", (self.output / "game_entries.hpp").read_text())
        removed = self.root / "second.saved"
        shutil.move(second, removed)
        catalog.generate(self.root, self.output)
        self.assertEqual((self.output / "game_entries.hpp").read_text(), before)
        shutil.move(removed, second)
        catalog.generate(self.root, self.output)
        self.assertIn("second = 137", (self.output / "game_entries.hpp").read_text())
        self.assertEqual(json.loads((first / "GAME.json").read_text())["entry_id"], 0)

    def test_external_module_and_idempotent_generation(self):
        self.module("local", 0)
        external, data = self.module("external", 91)
        destination = Path(self.scratch.name) / "external pack"
        shutil.move(external, destination)
        games = catalog.generate(self.root, self.output, [destination, destination])
        self.assertEqual([g["id"] for g in games], ["local", "external"])
        cmake = (self.output / "game_modules.cmake").read_text()
        self.assertIn(str(destination / "module.cpp"), cmake)
        stamps = {p.name: p.stat().st_mtime_ns for p in self.output.iterdir()}
        catalog.generate(self.root, self.output, [destination])
        self.assertEqual(stamps, {p.name: p.stat().st_mtime_ns for p in self.output.iterdir()})

    def test_duplicates_and_disabled_reservations(self):
        for key, value in (("id", "first"), ("entry_name", "first"),
                           ("entry_id", 7), ("namespace", "ns_first")):
            for disabled in (False, True):
                with self.subTest(key=key, disabled=disabled):
                    shutil.rmtree(self.root / "vendor")
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
        outside.write_text("fixture")
        (directory / "linked.cpp").symlink_to(outside)
        for filename in ("../outside.cpp", str(outside), "linked.cpp", 'odd"name.cpp',
                         "dollar$name.cpp", "semi;name.cpp", "line\nname.cpp"):
            with self.subTest(filename=filename):
                if not filename.startswith("/") and "/" not in filename and filename != "linked.cpp":
                    (directory / filename).write_text("fixture")
                self.save(directory, dict(original, module=filename))
                with self.assertRaises(ValueError):
                    catalog.generate(self.root, self.output)

    def test_assets_and_sources_are_module_local(self):
        directory, data = self.module()
        (directory / "assets").mkdir()
        (directory / "view.cpp").write_text("fixture")
        (directory / "audio.cpp").write_text("fixture")
        data.update(ui_sources=["view.cpp"], audio_sources=["audio.cpp"])
        self.save(directory, data)
        games = catalog.generate(self.root, self.output)
        self.assertEqual(games[0]["assets"], "assets")
        cmake = (self.output / "game_modules.cmake").read_text()
        self.assertIn(str(directory / "view.cpp"), cmake)
        self.assertIn(str(directory / "audio.cpp"), cmake)

    def test_extra_topics_and_complete_text(self):
        directory, data = self.module()
        text = 'A full paragraph; after the semicolon.\n\nA "quoted" second paragraph — finished.'
        (directory / "sets.md").write_text(text + "\n")
        data["help_topics"] = [dict(id="sets", title="The sets", file="sets.md")]
        self.save(directory, data)
        catalog.generate(self.root, self.output)
        cpp = (self.output / "game_registry.cpp").read_text()
        self.assertIn(json.dumps(text, ensure_ascii=False), cpp)
        self.assertIn(json.dumps((directory / "help.md").read_text().strip(), ensure_ascii=False), cpp)
        self.assertIn(str(directory / "sets.md"), (self.output / "game_modules.cmake").read_text())

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

    def test_malformed_manifest_has_actionable_error(self):
        directory, data = self.module()
        for payload in ("{broken", "[]", "null", "42"):
            with self.subTest(payload=payload):
                (directory / "GAME.json").write_text(payload)
                with self.assertRaises(ValueError):
                    catalog.discover(self.root)

    def test_original_help_preserved(self):
        actual = {g["entry_name"]: g for g in catalog.discover(ROOT)}
        for name, expected in ORIGINAL_HELP.items():
            with self.subTest(game=name):
                self.assertIn(name, actual)
                game = actual[name]
                text = (game["directory"] / game["help"]).read_text().strip()
                self.assertEqual(hashlib.sha256(text.encode()).hexdigest(), expected)
        self.assertEqual(len(ORIGINAL_HELP), 18)

if __name__ == "__main__":
    unittest.main()
