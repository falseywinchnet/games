#!/usr/bin/env python3
"""Prepare runtime pixels and portable audio; original authoring assets remain unchanged."""
import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
from pathlib import Path


def prepare_image(source: Path, destination: Path) -> None:
    from PIL import Image
    with Image.open(source) as original:
        image = original.convert("RGBA").resize((1024, 512), Image.Resampling.LANCZOS)
    # Explicit top-left, tightly packed RGBA8. This artwork is opaque.
    alpha = image.getchannel("A").getextrema()
    if alpha != (255, 255):
        raise ValueError("The environment artwork must be opaque")
    destination.write_bytes(b"GPIX" + struct.pack("<III", 1, 1024, 512) + image.tobytes())


def prepare_cards(source: Path, destination: Path) -> None:
    from PIL import Image
    destination.mkdir(parents=True, exist_ok=True)
    records = []
    for path in sorted(source.glob("*.png")):
        with Image.open(path) as original:
            image = original.convert("RGBA")
        data = bytearray(image.tobytes())
        for i in range(0, len(data), 4):
            r, g, b, a = data[i:i + 4]
            data[i:i + 4] = bytes(((b * a + 127) // 255, (g * a + 127) // 255, (r * a + 127) // 255, a))
        name = path.with_suffix(".bgpix").name
        prepared = b"BGPX" + struct.pack("<III", 1, *image.size) + data
        (destination / name).write_bytes(prepared)
        records.append({"source": path.name, "source_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                        "file": name, "sha256": hashlib.sha256(prepared).hexdigest()})
        # Older runtime directories carried both representations. Only prepared
        # pixels are used by Koi-Koi; the original artwork stays in source control.
        (destination / path.name).unlink(missing_ok=True)
    (destination / "manifest.json").write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")
    for path in source.iterdir():
        if path.is_file() and path.suffix.lower() != ".png":
            shutil.copy2(path, destination / path.name)


def prepare_fonts(source: Path, destination: Path) -> None:
    manifest = json.loads((source / "manifest.json").read_text(encoding="utf-8"))
    for record in manifest["files"]:
        filename = record["file"]
        if Path(filename).name != filename:
            raise ValueError("Font manifest requires simple filenames")
        digest = hashlib.sha256((source / filename).read_bytes()).hexdigest()
        if digest != record["sha256"]:
            raise ValueError("Font or license fingerprint differs: " + filename)
    destination.mkdir(parents=True, exist_ok=True)
    for record in manifest["files"]:
        shutil.copy2(source / record["file"], destination / record["file"])
    shutil.copy2(source / "manifest.json", destination / "manifest.json")
    shutil.copy2(source / "ATTRIBUTION.md", destination / "ATTRIBUTION.md")


AUDIO_EXTENSIONS = {".m4a", ".wav", ".ogg", ".mp3", ".flac"}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def asset_inventory(source: Path, extra_dirs=()) -> dict:
    """Inventory source bytes, including enabled folder modules. No timestamps.

    Module audio filenames retain their producer prefix and merge into audio/;
    all other module assets keep their relative paths below the module id.
    """
    from game_catalog import discover
    source = source.resolve()
    modules = discover(source.parent, extra_dirs)
    roots = [("root", source, "")]
    manifests = {}
    for module in modules:
        manifest = module["manifest_path"]
        manifests[module["id"]] = sha256(manifest)
        relative = module.get("assets", "assets")
        if relative is False or relative is None:
            continue
        asset_root = (module["directory"] / relative).resolve()
        if not asset_root.is_relative_to(module["directory"].resolve()):
            raise ValueError("Module assets escape its directory: " + module["id"])
        if asset_root.is_dir():
            prefix = module.get("audio_prefix", module["id"] + "_")
            if not isinstance(prefix, str) or not re.fullmatch(r"[a-z][a-z0-9_]*_", prefix):
                raise ValueError("Module audio_prefix must be a lowercase identifier ending in _: " + module["id"])
            roots.append((module["id"], asset_root, prefix))
        elif "assets" in module:
            raise ValueError("Missing module assets: " + str(asset_root))
    audio, resources, producers, loops = {}, {}, {}, {}
    for owner, root, prefix in roots:
        local_audio = {}
        for path in sorted(root.rglob("*")):
            if not path.is_file():
                continue
            if not path.resolve().is_relative_to(root):
                raise ValueError("Asset symlink escapes source: " + str(path))
            relative = path.relative_to(root)
            key = owner + "/" + relative.as_posix()
            if relative.parts[0] == "audio" and path.suffix.lower() in AUDIO_EXTENSIONS:
                stem = path.stem
                if not re.fullmatch(r"[A-Za-z0-9_.-]+", stem):
                    raise ValueError("Audio stem must be a portable filename: " + stem)
                if prefix and not stem.startswith(prefix):
                    raise ValueError("Module audio requires prefix " + prefix + ": " + str(path))
                if stem.casefold() in audio:
                    raise ValueError("Audio filename collision: " + stem)
                record = {"path": path, "source": key, "stem": stem, "source_sha256": sha256(path)}
                audio[stem.casefold()] = record
                local_audio[stem] = record
            elif owner != "root":
                output = owner + "/" + relative.as_posix()
                if (source / output).exists() or output.casefold() in resources:
                    raise ValueError("Module asset collision: " + output)
                resources[output.casefold()] = {"path": path, "file": output, "source_sha256": sha256(path)}
        audio_root = root / "audio"
        for path in sorted(audio_root.rglob("*.json")) if audio_root.is_dir() else []:
            document = json.loads(path.read_text(encoding="utf-8"))
            producer_key = owner + "/" + path.relative_to(root).as_posix()
            producers[producer_key] = {"sha256": sha256(path), "metadata": document}
            if not isinstance(document, dict):
                continue
            for entry in document.get("music", []):
                if not isinstance(entry, dict):
                    raise ValueError("Invalid music entry: " + producer_key)
                start, end = entry.get("loop_start_sample", 0), entry.get("loop_end_sample_exclusive")
                if type(start) is not int or type(end) is not int or not 0 <= start < end <= 28800000:
                    raise ValueError("Missing or invalid music loop bounds: " + producer_key)
                if entry.get("sample_rate", document.get("sample_rate", 48000)) != 48000:
                    raise ValueError("Loop metadata must use 48000 Hz sample units: " + producer_key)
                identifier = entry.get("id", "")
                explicit = entry.get("runtime", [])
                if isinstance(explicit, str):
                    explicit = [explicit]
                candidates = [Path(name).stem for name in explicit if isinstance(name, str)]
                filename = entry.get("file", "")
                if isinstance(filename, str):
                    candidates.append(Path(filename).stem)
                candidates.extend([identifier, "music_" + identifier + "_loop"])
                matches = {name for name in candidates if name in local_audio}
                if len(matches) != 1:
                    raise ValueError("Missing or ambiguous music source: " + producer_key + ": " + identifier)
                stem = matches.pop()
                bounds = {"start": start, "end": end}
                if stem in loops and loops[stem] != bounds:
                    raise ValueError("Conflicting producer loop bounds: " + stem)
                loops[stem] = bounds
        for stem in local_audio:
            if ("music" in stem.lower() or "loop" in stem.lower()) and stem not in loops:
                raise ValueError("Missing producer loop metadata: " + stem)
    return {"audio": sorted(audio.values(), key=lambda value: value["source"]),
            "resources": sorted(resources.values(), key=lambda value: value["file"]),
            "producer_manifests": producers, "module_manifests": manifests, "loops": loops}


def loop_bounds(source: Path) -> dict[str, int]:
    return {stem: bounds["end"] - bounds["start"] for stem, bounds in asset_inventory(source)["loops"].items()}


def audio_verification(manifest: dict) -> str:
    """Small native-test contract, itself verified against full provenance JSON."""
    lines = [str(len(manifest["files"]))]
    for record in sorted(manifest["files"], key=lambda record: record["file"]):
        if "producer_loop" in record:
            lines.append(Path(record["file"]).stem + "\t" + str(record["frames"]))
    return "\n".join(lines) + "\n"


def prepare_audio(ffmpeg: str, source: Path, destination: Path, frames: int, codec: str, start: int = 0) -> dict:
    temporary = destination.with_suffix(".f32")
    subprocess.run([ffmpeg, "-v", "error", "-nostdin", "-y", "-i", str(source),
                    "-vn", "-ar", "48000", "-ac", "2", "-f", "f32le", str(temporary)], check=True)
    size = temporary.stat().st_size
    if size == 0 or size % 8 != 0 or size > 230400000:
        raise ValueError("Invalid or excessive decoded PCM length: " + source.name)
    available = size // 8
    if frames == 0:
        frames = available
    if start + frames > available:
        raise ValueError("Decoded PCM is shorter than producer loop: " + source.name)
    size = frames * 8
    # A minimal RIFF IEEE-float file avoids platform decoder/extended-header differences.
    pcm_path = destination if codec == "pcm" else destination.with_suffix(".trimmed.wav")
    with pcm_path.open("wb") as output, temporary.open("rb") as pcm:
        output.write(b"RIFF" + struct.pack("<I", size + 36) + b"WAVEfmt ")
        output.write(struct.pack("<IHHIIHH", 16, 3, 2, 48000, 384000, 8, 32))
        output.write(b"data" + struct.pack("<I", size))
        pcm.seek(start * 8)
        remaining = size
        while remaining > 0:
            data = pcm.read(min(remaining, 65536))
            if not data:
                raise ValueError("PCM truncated during packaging")
            output.write(data)
            remaining -= len(data)
    temporary.unlink()
    if codec == "vorbis":
        subprocess.run([ffmpeg, "-v", "error", "-nostdin", "-y", "-i", str(pcm_path),
                        "-c:a", "libvorbis", "-q:a", "6", str(destination)], check=True)
        pcm_path.unlink()
    record = {"source": source.name, "file": destination.name, "frames": frames,
              "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
              "sha256": hashlib.sha256(destination.read_bytes()).hexdigest()}
    return record


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=Path("assets"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--ffmpeg", required=True)
    parser.add_argument("--extra-game-dir", type=Path, action="append", default=[])
    parser.add_argument("--audio-format", choices=("pcm", "vorbis"), default="vorbis")
    options = parser.parse_args()
    source = options.source.resolve()
    output = options.output.resolve()
    if output == source or source in output.parents or output in source.parents:
        raise ValueError("Runtime output must be separate from source assets")
    extra_dirs = options.extra_game_dir + [Path(value) for value in os.environ.get("GAMES_EXTRA_GAME_DIRS", "").split(";") if value]
    inventory = asset_inventory(source, extra_dirs)
    output.mkdir(parents=True, exist_ok=True)
    prepare_fonts(source / "fonts", output / "fonts")
    for path in source.iterdir():
        if path.name in ("audio", "cards", "fonts"):
            continue
        target = output / path.name
        if path.is_dir():
            shutil.copytree(path, target, dirs_exist_ok=True)
        else:
            shutil.copy2(path, target)
    prepare_image(source / "nature-lake.png", output / "nature-lake.gpix")
    prepare_cards(source / "cards", output / "cards")
    audio = output / "audio"
    audio.mkdir(exist_ok=True)
    extension = ".wav" if options.audio_format == "pcm" else ".ogg"
    unexpected = ".ogg" if extension == ".wav" else ".wav"
    if any(audio.glob("*" + unexpected)):
        raise ValueError("Use a separate output directory for each runtime audio format")
    records: list[dict] = []
    for entry in inventory["audio"]:
        bounds = inventory["loops"].get(entry["stem"], {"start": 0, "end": 0})
        record = prepare_audio(options.ffmpeg, entry["path"], audio / (entry["stem"] + extension),
                               bounds["end"] - bounds["start"], options.audio_format, bounds["start"])
        record["source"] = entry["source"]
        if entry["stem"] in inventory["loops"]:
            record["producer_loop"] = bounds
        records.append(record)
    resources = []
    for entry in inventory["resources"]:
        target = output / entry["file"]
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(entry["path"], target)
        resources.append({key: entry[key] for key in ("file", "source_sha256")})
    audio_format = "IEEE float32 LE" if options.audio_format == "pcm" else "Ogg Vorbis quality 6"
    manifest = {"schema_version": 2, "sample_rate": 48000, "channels": 2, "format": audio_format,
                "files": records, "module_resources": resources,
                "producer_manifests": inventory["producer_manifests"],
                "module_manifests": inventory["module_manifests"]}
    (audio / "portable_manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (audio / "verification.tsv").write_text(audio_verification(manifest), encoding="utf-8")
    print("Prepared environment pixels and", len(records), audio_format, "assets at", output)


if __name__ == "__main__":
    main()
