#!/usr/bin/env python3
"""Verify release data against the source inventory before application packaging."""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify(source: Path, runtime: Path) -> dict:
    audio = runtime / "audio"
    manifest = json.loads((audio / "portable_manifest.json").read_text(encoding="utf-8"))
    if manifest["sample_rate"] != 48000 or manifest["channels"] != 2:
        raise ValueError("Expected stereo 48 kHz runtime audio")
    if manifest["format"] != "Ogg Vorbis quality 6":
        raise ValueError("Release data must use compact Vorbis audio")
    originals = {path.name: path for path in (source / "audio").iterdir()
                 if path.suffix.lower() in (".m4a", ".wav")}
    records = manifest["files"]
    if len(records) != 298 or len(originals) != 298:
        raise ValueError("Incomplete source or runtime audio inventory")
    seen_sources: set[str] = set()
    seen_outputs: set[str] = set()
    for record in records:
        name = record["source"]
        output = record["file"]
        if name not in originals or name in seen_sources:
            raise ValueError("Missing or duplicate source audio: " + name)
        if output != Path(name).with_suffix(".ogg").name or output in seen_outputs:
            raise ValueError("Invalid or duplicate runtime audio: " + output)
        if digest(originals[name]) != record["source_sha256"]:
            raise ValueError("Source audio changed: " + name)
        if digest(audio / output) != record["sha256"]:
            raise ValueError("Runtime audio changed: " + output)
        if not 0 < record["frames"] <= 28800000:
            raise ValueError("Invalid audio frame count: " + output)
        seen_sources.add(name)
        seen_outputs.add(output)
    actual_outputs = {path.name for path in audio.iterdir() if path.is_file()}
    if actual_outputs != seen_outputs | {"portable_manifest.json"}:
        raise ValueError("Runtime audio contains stale or unexpected files")
    source_fonts = source / "fonts"
    fonts = runtime / "fonts"
    for name in ("manifest.json", "ATTRIBUTION.md"):
        if digest(source_fonts / name) != digest(fonts / name):
            raise ValueError("Font provenance changed: " + name)
    font_manifest = json.loads((source_fonts / "manifest.json").read_text(encoding="utf-8"))
    for record in font_manifest["files"]:
        name = record["file"]
        if Path(name).name != name:
            raise ValueError("Font inventory requires simple filenames")
        if digest(source_fonts / name) != record["sha256"] or digest(fonts / name) != record["sha256"]:
            raise ValueError("Font or license changed: " + name)
    pixels = (runtime / "nature-lake.gpix").read_bytes()
    if len(pixels) != 16 + 1024 * 512 * 4 or pixels[:16] != b"GPIX" + struct.pack("<III", 1, 1024, 512):
        raise ValueError("Invalid prepared environment pixels")
    if any(alpha != 255 for alpha in pixels[19::4]):
        raise ValueError("Environment pixels must be opaque")
    for path in source.rglob("*"):
        if path.is_file() and path.relative_to(source).parts[0] != "audio":
            relative = path.relative_to(source)
            if digest(path) != digest(runtime / relative):
                raise ValueError("Missing or altered runtime resource: " + str(relative))
    if not (runtime / "sudoku/engine.js").is_file():
        raise ValueError("Missing offline Sudoku engine")
    return {"audio_files": len(records), "font_and_license_files": len(font_manifest["files"]),
            "bytes": sum(path.stat().st_size for path in runtime.rglob("*") if path.is_file())}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path("assets"))
    parser.add_argument("--runtime", type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(verify(args.source, args.runtime), indent=2))


if __name__ == "__main__":
    main()
