"""Shared app resources and notices; this does not install a GUI.Forms SDK."""
from pathlib import Path
import hashlib
import json
import os
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
TOOLKIT_REVISION = "4e126d6eb3ab837f1a44c0e9c9eb7901d50f80bb"


def missing_prepared_files(assets):
    """Every card, sound and game resource the prepared manifests list, absent from `assets`."""
    listed = []
    cards = assets / "cards/manifest.json"
    if cards.is_file():
        listed += ["cards/" + record["file"] for record in json.loads(cards.read_text(encoding="utf-8"))]
    audio = assets / "audio/portable_manifest.json"
    if audio.is_file():
        manifest = json.loads(audio.read_text(encoding="utf-8"))
        listed += ["audio/" + record["file"] for record in manifest.get("files", [])]
        listed += [record["file"] for record in manifest.get("module_resources", [])]
    return [name for name in listed if not (assets / name).is_file()]


def project_version():
    from publication_version import baseline, validate
    version = os.environ.get("GAMES_RELEASE_VERSION") or baseline(ROOT)
    validate(version)
    return version


def copy_resources(build, toolkit, destination):
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    compiled_version = re.search(r"^CMAKE_PROJECT_VERSION:STATIC=(.+)$", cache, re.MULTILINE)
    if not compiled_version or compiled_version.group(1) != project_version():
        raise RuntimeError("Packaging version differs from the configured native application")
    destination.mkdir(parents=True, exist_ok=True)
    assets = build / "assets"
    if not assets.is_dir():
        assets = build / "games.app/Contents/Resources/assets"
    shutil.copytree(assets, destination / "assets", dirs_exist_ok=True)
    # A package that lost a prepared file would only show it when the game opens.
    missing = missing_prepared_files(destination / "assets")
    if missing:
        raise RuntimeError("Packaged assets are missing prepared files: " + ", ".join(missing[:10]))
    shutil.copytree(toolkit / "assets/fonts", destination / "fonts", dirs_exist_ok=True)
    notices = destination / "licenses"
    notices.mkdir(exist_ok=True)
    entries = {
        "GUIForms.txt": toolkit / "LICENSE",
        "Unicode.txt": toolkit / "third_party/unicode/LICENSE.txt",
        "SheenBidi.txt": toolkit / "third_party/sheenbidi/LICENSE",
        "HarfBuzz.txt": toolkit / "third_party/harfbuzz/COPYING",
        "FreeType.txt": toolkit / "third_party/freetype/docs/FTL.TXT",
        "libunibreak.txt": toolkit / "third_party/libunibreak/LICENCE",
        "QuickJS.txt": build / "_deps/quickjs-src/LICENSE",
        "miniaudio.txt": build / "_deps/gui_forms_miniaudio-src/LICENSE",
        "Plan-Paint.txt": ROOT / "shared/felt/LICENSE",
    }
    for name, source in entries.items():
        shutil.copy2(source, notices / name)
    skia = toolkit / "third_party/skia"
    if skia.is_dir():
        for name, source in {
            "Skia.txt": skia / "LICENSE",
            "libpng.txt": skia / "third_party/externals/libpng/LICENSE",
            "zlib.txt": skia / "third_party/externals/zlib/LICENSE",
            "Skia-FreeType.txt": skia / "third_party/externals/freetype/docs/FTL.TXT",
        }.items():
            shutil.copy2(source, notices / name)
    # Ogg Vorbis is decoded by GUI.Forms' stx_vorbis with its embedded BFFT, and
    # threads run on GUI.Forms' copy of threadpool_atomic_fast.
    for name, source in {
        "stx_vorbis.txt": toolkit / "stx_vorbis/LICENSE",
        "BFFT.txt": toolkit / "stx_vorbis/third_party/bfft/LICENSE",
        "BFFT-provenance.json": toolkit / "stx_vorbis/third_party/bfft/provenance.json",
        "threadpool_atomic_fast.txt": toolkit / "src/core/threading/atomic_pool/LICENSE",
    }.items():
        shutil.copy2(source, notices / name)
    (destination / "README.txt").write_text(
        "PlaySuite\n\nA native collection of card games, puzzles and arcade scenes.\n"
        "Click a box to play or continue. Each game has Help in its command capsule.\n"
        "Music, Sound and Motion in the collection are master controls.\n"
        "Saves are per-user and remain separate from this application folder.\n"
        "Atom Probe, Four Pegs and Switchbox keep separate v2 saves; v1 saves remain intact.\n\n"
        "Source and release notes: https://github.com/falseywinchnet/games\n"
        "Font licenses accompany the fonts; other dependency notices are in licenses/.\n\n"
        "Author: Astra\nSponsor: Rainstar\n", encoding="utf-8")


def write_manifest(bundle, system, architecture):
    files = {}
    for path in sorted(bundle.rglob("*")):
        if path.is_file():
            files[path.relative_to(bundle).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    revision = subprocess.check_output(["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True).strip()
    manifest = {"version": project_version(), "source_revision": revision,
                "system": system, "architecture": architecture,
                "toolkit_source_revision": TOOLKIT_REVISION, "files": files}
    (bundle / "package-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
