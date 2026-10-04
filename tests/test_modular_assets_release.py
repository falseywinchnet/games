"""Inventory and publication contract regressions; no external authoring tools."""
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from prepare_portable_assets import asset_inventory, audio_verification
from verify_portable_assets import verify
from publication_version import select


def digest(data):
    return hashlib.sha256(data).hexdigest()


class ModuleAssets(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "assets"
        (self.source / "audio").mkdir(parents=True)
        self.modules = []
        fake_catalog = types.SimpleNamespace(discover=lambda root, extra_dirs: self.modules)
        self.patch = patch.dict(sys.modules, {"game_catalog": fake_catalog})
        self.patch.start()
        self.addCleanup(self.patch.stop)

    def module(self, name="sample"):
        directory = self.root / "vendor" / name
        (directory / "assets/audio").mkdir(parents=True)
        manifest = directory / "GAME.json"
        manifest.write_text(json.dumps({"id": name, "schema_version": 1, "entry_id": 100}))
        module = {"id": name, "directory": directory, "manifest_path": manifest, "audio_prefix": name + "_"}
        self.modules.append(module)
        return directory / "assets"

    def audio(self, root, stem, music=True):
        (root / "audio" / (stem + ".wav")).write_bytes(b"test source audio")
        if music:
            (root / "audio/producer.json").write_text(json.dumps({"music": [
                {"id": stem, "loop_start_sample": 1, "loop_end_sample_exclusive": 3, "custom": "retain me"}]}))

    def runtime(self):
        self.audio(self.source, "music_menu_loop")
        for name, data in {"fonts/manifest.json": b'{"files": []}', "fonts/ATTRIBUTION.md": b"notice",
                           "cards/card.png": b"source", "nature-lake.png": b"source", "sudoku/engine.js": b"engine"}.items():
            target = self.source / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        inventory = asset_inventory(self.source)
        runtime = self.root / "runtime"
        for path in self.source.rglob("*"):
            relative = path.relative_to(self.source)
            if path.is_file() and relative.parts[0] not in ("audio", "cards"):
                target = runtime / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(path.read_bytes())
        (runtime / "cards").mkdir()
        pixels = b"BGPX" + struct.pack("<III", 1, 1, 1) + b"\0\0\0\xff"
        (runtime / "cards/card.bgpix").write_bytes(pixels)
        (runtime / "cards/manifest.json").write_text(json.dumps([{"source": "card.png", "file": "card.bgpix",
            "source_sha256": digest(b"source"), "sha256": digest(pixels)}]))
        (runtime / "nature-lake.gpix").write_bytes(b"GPIX" + struct.pack("<III", 1, 1024, 512) + b"\0\0\0\xff" * (1024 * 512))
        (runtime / "audio").mkdir()
        records = []
        for entry in inventory["audio"]:
            name = entry["stem"] + ".ogg"
            (runtime / "audio" / name).write_bytes(b"encoded fixture")
            record = {"source": entry["source"], "file": name, "source_sha256": entry["source_sha256"],
                      "sha256": digest(b"encoded fixture"), "frames": 2}
            if entry["stem"] in inventory["loops"]:
                record["producer_loop"] = inventory["loops"][entry["stem"]]
            records.append(record)
        for entry in inventory["resources"]:
            target = runtime / entry["file"]
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(entry["path"].read_bytes())
        manifest = {"sample_rate": 48000, "channels": 2, "format": "Ogg Vorbis quality 6", "files": records,
                    "module_resources": [{key: entry[key] for key in ("file", "source_sha256")} for entry in inventory["resources"]],
                    "producer_manifests": inventory["producer_manifests"], "module_manifests": inventory["module_manifests"]}
        (runtime / "audio/portable_manifest.json").write_text(json.dumps(manifest))
        (runtime / "audio/verification.tsv").write_text(audio_verification(manifest))
        return runtime, manifest

    def test_new_module_audio_resources_and_metadata(self):
        assets = self.module()
        self.audio(assets, "sample_music")
        (assets / "scene.txt").write_text("scene")
        runtime, manifest = self.runtime()
        self.assertEqual(verify(self.source, runtime)["audio_files"], 2)
        self.assertEqual(manifest["producer_manifests"]["sample/audio/producer.json"]["metadata"]["music"][0]["custom"], "retain me")
        (runtime / "sample/scene.txt").unlink()
        with self.assertRaises((ValueError, FileNotFoundError)):
            verify(self.source, runtime)

    def test_loop_is_required(self):
        self.audio(self.source, "new_music", music=False)
        with self.assertRaisesRegex(ValueError, "Missing producer loop"):
            asset_inventory(self.source)

    def test_missing_declared_music_rejected(self):
        (self.source / "audio/new.json").write_text(json.dumps({"music": [{"id": "missing", "loop_end_sample_exclusive": 10}]}))
        with self.assertRaisesRegex(ValueError, "Missing or ambiguous"):
            asset_inventory(self.source)

    def test_filename_collision_case_insensitive(self):
        self.audio(self.source, "sample_hit", music=False)
        self.audio(self.module(), "sample_hit", music=False)
        with self.assertRaisesRegex(ValueError, "collision"):
            asset_inventory(self.source)

    def test_prefix_required(self):
        self.audio(self.module(), "other_hit", music=False)
        with self.assertRaisesRegex(ValueError, "prefix"):
            asset_inventory(self.source)

    def test_dropped_audio_rejected(self):
        runtime, manifest = self.runtime()
        manifest["files"] = []
        (runtime / "audio/portable_manifest.json").write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, "Incomplete"):
            verify(self.source, runtime)

    def test_altered_loop_rejected(self):
        runtime, manifest = self.runtime()
        manifest["files"][0]["frames"] = 3
        (runtime / "audio/portable_manifest.json").write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, "producer loop"):
            verify(self.source, runtime)

    def test_native_contract_cannot_drop_loops(self):
        runtime, _ = self.runtime()
        (runtime / "audio/verification.tsv").write_text("1\n")
        with self.assertRaisesRegex(ValueError, "verification contract"):
            verify(self.source, runtime)

    def test_manifest_change_even_with_preserved_mtime(self):
        runtime, _ = self.runtime()
        path = self.source / "audio/producer.json"
        timestamp = path.stat().st_mtime_ns
        path.write_text(path.read_text().replace("retain me", "changed!!"))
        os.utime(path, ns=(timestamp, timestamp))
        with self.assertRaisesRegex(ValueError, "manifest bytes changed"):
            verify(self.source, runtime)

    def test_stale_disabled_module_resource_rejected(self):
        runtime, _ = self.runtime()
        (runtime / "obsolete").mkdir()
        (runtime / "obsolete/scene.txt").write_text("old")
        with self.assertRaisesRegex(ValueError, "unexpected"):
            verify(self.source, runtime)


class PublicationVersions(unittest.TestCase):
    def test_main_version_is_stable_for_reruns(self):
        self.assertEqual(select("0.5.0", "push", "refs/heads/main", 41),
                         {"version": "0.5.41", "tag": "v0.5.41", "publish": "true"})

    def test_pr_and_manual_build_do_not_publish(self):
        self.assertEqual(select("0.5.0", "pull_request", "refs/pull/3/merge", 41)["publish"], "false")
        self.assertEqual(select("0.5.0", "workflow_dispatch", "refs/heads/main", 41)["version"], "0.5.0")

    def test_tag_and_manual_opt_in(self):
        self.assertEqual(select("0.5.0", "push", "refs/tags/v0.5.44", 45)["version"], "0.5.44")
        self.assertEqual(select("0.5.0", "workflow_dispatch", "refs/heads/main", 41, True)["publish"], "true")

    def test_bad_tag_and_nonmain_publish_rejected(self):
        for arguments in [("0.5.0", "push", "refs/tags/v0.4.9", 1),
                          ("0.5.0", "workflow_dispatch", "refs/heads/feature", 1, True),
                          ("0.5.0", "push", "refs/heads/main", 65536)]:
            with self.assertRaises(ValueError):
                select(*arguments)


class PublicationGate(unittest.TestCase):
    def test_incomplete_native_packages_cannot_publish(self):
        import publish_tested_release as publication
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory) / "playsuite-0.5.9-windows-x64-setup.exe").write_bytes(b"fixture")
            env = {"GITHUB_REPOSITORY": "owner/repo", "GITHUB_SHA": "abc", "GITHUB_REF": "refs/heads/main",
                   "RELEASE_VERSION": "0.5.9", "RELEASE_TAG": "v0.5.9"}
            with patch.dict(os.environ, env), patch.object(sys, "argv", ["publish", "--artifacts", directory]), \
                 patch.object(publication, "gh", return_value="abc") as call:
                with self.assertRaisesRegex(ValueError, "package set"):
                    publication.main()
                self.assertFalse(any(args.args[0] == "release" for args in call.call_args_list))

    def test_conflicting_tag_cannot_publish(self):
        import publish_tested_release as publication
        suffixes = ("windows-x64.zip", "windows-x64-setup.exe", "macos-arm64.zip", "macos-arm64.pkg",
                    "linux-amd64.tar.gz", "linux-amd64.deb", "linux-arm64.tar.gz", "linux-arm64.deb")
        with tempfile.TemporaryDirectory() as directory:
            for suffix in suffixes:
                (Path(directory) / ("playsuite-0.5.9-" + suffix)).write_bytes(b"fixture")
            env = {"GITHUB_REPOSITORY": "owner/repo", "GITHUB_SHA": "abc", "GITHUB_REF": "refs/heads/main",
                   "RELEASE_VERSION": "0.5.9", "RELEASE_TAG": "v0.5.9"}
            with patch.dict(os.environ, env), patch.object(sys, "argv", ["publish", "--artifacts", directory]), \
                 patch.object(publication, "gh", return_value="abc") as call, \
                 patch.object(publication.subprocess, "run", return_value=types.SimpleNamespace(returncode=0, stdout='{"sha":"other"}', stderr="")):
                with self.assertRaisesRegex(ValueError, "different commit"):
                    publication.main()
                self.assertFalse(any(args.args[0] == "release" for args in call.call_args_list))


if __name__ == "__main__":
    unittest.main()
