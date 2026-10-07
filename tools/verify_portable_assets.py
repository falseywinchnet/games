#!/usr/bin/env python3
"""Verify release data against the source inventory before application packaging."""
import argparse
import hashlib
import json
import os
import struct
from pathlib import Path
from prepare_portable_assets import asset_inventory, audio_verification, card_verification, png_layout


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify(source: Path, runtime: Path, extra_dirs=()) -> dict:
    audio = runtime / "audio"
    manifest = json.loads((audio / "portable_manifest.json").read_text(encoding="utf-8"))
    if manifest["sample_rate"] != 48000 or manifest["channels"] != 2:
        raise ValueError("Expected stereo 48 kHz runtime audio")
    if manifest["format"] != "Ogg Vorbis quality 6":
        raise ValueError("Release data must use compact Vorbis audio")
    inventory = asset_inventory(source, extra_dirs)
    originals = {entry["source"]: entry for entry in inventory["audio"]}
    records = manifest["files"]
    if len(records) != len(originals):
        raise ValueError("Incomplete source or runtime audio inventory")
    for key in ("producer_manifests", "module_manifests"):
        if manifest.get(key) != inventory[key]:
            raise ValueError("Producer or module manifest bytes changed: " + key)
    expected_resources = [{key: entry[key] for key in ("file", "source_sha256")}
                          for entry in inventory["resources"]]
    if manifest.get("module_resources") != expected_resources:
        raise ValueError("Incomplete module resource inventory")
    for record in expected_resources:
        if digest(runtime / record["file"]) != record["source_sha256"]:
            raise ValueError("Missing or altered module resource: " + record["file"])
    seen_sources: set[str] = set()
    seen_outputs: set[str] = set()
    for record in records:
        name = record["source"]
        output = record["file"]
        if name not in originals or name in seen_sources:
            raise ValueError("Missing or duplicate source audio: " + name)
        original = originals[name]
        if output != original["stem"] + ".ogg" or output in seen_outputs:
            raise ValueError("Invalid or duplicate runtime audio: " + output)
        if original["source_sha256"] != record["source_sha256"]:
            raise ValueError("Source audio changed: " + name)
        if digest(audio / output) != record["sha256"]:
            raise ValueError("Runtime audio changed: " + output)
        if type(record["frames"]) is not int or not 0 < record["frames"] <= 28800000:
            raise ValueError("Invalid audio frame count: " + output)
        bounds = inventory["loops"].get(original["stem"])
        if bounds is not None:
            if record.get("producer_loop") != bounds or record["frames"] != bounds["end"] - bounds["start"]:
                raise ValueError("Runtime dropped or altered producer loop: " + name)
        elif "producer_loop" in record:
            raise ValueError("Unexpected producer loop: " + name)
        seen_sources.add(name)
        seen_outputs.add(output)
    if (audio / "verification.tsv").read_text(encoding="utf-8") != audio_verification(manifest):
        raise ValueError("Native audio verification contract differs from complete manifest")
    actual_outputs = {path.name for path in audio.iterdir() if path.is_file()}
    if actual_outputs != seen_outputs | {"portable_manifest.json", "verification.tsv"}:
        raise ValueError("Runtime audio contains stale or unexpected files")
    expected_cards = {path.name for path in (source / "cards").glob("*.png")}
    actual_cards = {path.name for path in (runtime / "cards").glob("*.png")}
    if actual_cards != expected_cards:
        raise ValueError("Incomplete prepared hanafuda deck")
    if any((runtime / "cards").glob("*.bgpix")):
        raise ValueError("Runtime cards contain stale raw pixels")
    card_records = json.loads((runtime / "cards/manifest.json").read_text(encoding="utf-8"))
    if len(card_records) != len(expected_cards) or {record["file"] for record in card_records} != expected_cards:
        raise ValueError("Incomplete card provenance inventory")
    for record in card_records:
        name = record["source"]
        if Path(name).name != name or record["file"] != name:
            raise ValueError("Invalid source card name")
        if digest(source / "cards" / name) != record["source_sha256"] or digest(runtime / "cards" / record["file"]) != record["sha256"]:
            raise ValueError("Card provenance differs: " + name)
        if png_layout((runtime / "cards" / name).read_bytes()) != (record["width"], record["height"]):
            raise ValueError("Prepared card is not an 8-bit RGBA PNG of the recorded size: " + name)
    if (runtime / "cards/verification.tsv").read_text(encoding="utf-8") != card_verification(card_records):
        raise ValueError("Native card verification contract differs from the card manifest")
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
            if relative.parts[0] == "cards" and path.suffix.lower() == ".png":
                continue  # Source and prepared fingerprints were verified above.
            if digest(path) != digest(runtime / relative):
                raise ValueError("Missing or altered runtime resource: " + str(relative))
    expected_paths = {path.relative_to(source).as_posix() for path in source.rglob("*")
                      if path.is_file() and path.relative_to(source).parts[0] != "audio"
                      and not (path.relative_to(source).parts[0] == "cards" and path.suffix.lower() == ".png")}
    expected_paths.update("cards/" + name for name in expected_cards)
    expected_paths.update({"cards/manifest.json", "cards/verification.tsv", "nature-lake.gpix", "audio/portable_manifest.json", "audio/verification.tsv"})
    expected_paths.update("audio/" + name for name in seen_outputs)
    expected_paths.update(entry["file"] for entry in expected_resources)
    actual_paths = {path.relative_to(runtime).as_posix() for path in runtime.rglob("*") if path.is_file()}
    if actual_paths != expected_paths:
        raise ValueError("Runtime resource inventory differs; missing=" + str(sorted(expected_paths - actual_paths))
                         + "; unexpected=" + str(sorted(actual_paths - expected_paths)))
    if not (runtime / "sudoku/engine.js").is_file():
        raise ValueError("Missing offline Sudoku engine")
    return {"audio_files": len(records), "font_and_license_files": len(font_manifest["files"]),
            "bytes": sum(path.stat().st_size for path in runtime.rglob("*") if path.is_file())}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path("assets"))
    parser.add_argument("--extra-game-dir", type=Path, action="append", default=[])
    parser.add_argument("--runtime", type=Path, required=True)
    args = parser.parse_args()
    extra_dirs = args.extra_game_dir + [Path(value) for value in os.environ.get("GAMES_EXTRA_GAME_DIRS", "").split(";") if value]
    print(json.dumps(verify(args.source, args.runtime, extra_dirs), indent=2))


if __name__ == "__main__":
    main()
