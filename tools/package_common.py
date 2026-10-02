"""Shared app resources and notices; this does not install a GUI.Forms SDK."""
from pathlib import Path
import hashlib
import json
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
TOOLKIT_REVISION = "7b260cfb9f3267392e1470b0fcf4cd2497437819"


def project_version():
    source = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r"project\(Games VERSION ([0-9.]+)", source)
    if not match:
        raise RuntimeError("Games version is missing")
    return match.group(1)


def copy_resources(build, toolkit, destination):
    destination.mkdir(parents=True, exist_ok=True)
    assets = build / "assets"
    if not assets.is_dir():
        assets = build / "games.app/Contents/Resources/assets"
    shutil.copytree(assets, destination / "assets", dirs_exist_ok=True)
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
        "Plan-Paint.txt": ROOT / "vendor/paint/LICENSE",
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
    # stb_vorbis' dual public-domain/MIT notice is retained verbatim.
    vorbis = (build / "_deps/gui_forms_miniaudio-src/extras/stb_vorbis.c").read_text(encoding="utf-8")
    start = vorbis.rfind("ALTERNATIVE A - MIT License")
    if start < 0:
        raise RuntimeError("Pinned Vorbis source is missing its license notice")
    (notices / "stb_vorbis.txt").write_text(vorbis[start:], encoding="utf-8")
    (destination / "README.txt").write_text(
        "PlaySuite\n\nA native collection of card games, puzzles and arcade scenes.\n"
        "Choose a game and use Play / Continue. Each game has Help.\n"
        "Music, Sound and Motion in the collection are master controls.\n"
        "Saves are per-user and remain separate from this application folder.\n"
        "The replacement games keep v2 saves and do not overwrite v1 saves.\n\n"
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
