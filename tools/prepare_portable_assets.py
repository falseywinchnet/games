#!/usr/bin/env python3
"""Prepare runtime pixels and portable audio; original authoring assets remain unchanged."""
import argparse
import hashlib
import json
import shutil
import struct
import subprocess
from pathlib import Path
from PIL import Image


def prepare_image(source: Path, destination: Path) -> None:
    with Image.open(source) as original:
        image = original.convert("RGBA").resize((1024, 512), Image.Resampling.LANCZOS)
    # Explicit top-left, tightly packed RGBA8. This artwork is opaque.
    alpha = image.getchannel("A").getextrema()
    if alpha != (255, 255):
        raise ValueError("The environment artwork must be opaque")
    destination.write_bytes(b"GPIX" + struct.pack("<III", 1, 1024, 512) + image.tobytes())


def prepare_cards(source: Path, destination: Path) -> None:
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


def loop_bounds(source: Path) -> dict[str, int]:
    bounds: dict[str, int] = {}
    for name in ("audio_manifest.json", "eggy_audio_manifest.json", "switchbox_audio_manifest.json", "fourpegs_audio_manifest.json", "atomprobe_audio_manifest.json", "parrots_audio_manifest.json", "dice_audio_manifest.json", "sheep_audio_manifest.json", "koikoi_audio_manifest.json", "zen_audio_manifest.json"):
        manifest = json.loads((source / "audio" / name).read_text(encoding="utf-8"))
        for entry in manifest["music"]:
            if entry.get("loop_start_sample", 0) != 0:
                raise ValueError("Unexpected nonzero producer loop start")
            stem = entry["id"]
            if name == "audio_manifest.json":
                stem = "music_" + stem + "_loop"
            end = entry["loop_end_sample_exclusive"]
            if stem in bounds and bounds[stem] != end:
                raise ValueError("Conflicting producer loop bounds: " + stem)
            bounds[stem] = end
    return bounds


def prepare_audio(ffmpeg: str, source: Path, destination: Path, frames: int, codec: str) -> dict:
    temporary = destination.with_suffix(".f32")
    subprocess.run([ffmpeg, "-v", "error", "-nostdin", "-y", "-i", str(source),
                    "-vn", "-ar", "48000", "-ac", "2", "-f", "f32le", str(temporary)], check=True)
    size = temporary.stat().st_size
    if size == 0 or size % 8 != 0 or size > 230400000:
        raise ValueError("Invalid or excessive decoded PCM length: " + source.name)
    available = size // 8
    if frames == 0:
        frames = available
    if frames > available:
        raise ValueError("Decoded PCM is shorter than producer loop: " + source.name)
    size = frames * 8
    # A minimal RIFF IEEE-float file avoids platform decoder/extended-header differences.
    pcm_path = destination if codec == "pcm" else destination.with_suffix(".trimmed.wav")
    with pcm_path.open("wb") as output, temporary.open("rb") as pcm:
        output.write(b"RIFF" + struct.pack("<I", size + 36) + b"WAVEfmt ")
        output.write(struct.pack("<IHHIIHH", 16, 3, 2, 48000, 384000, 8, 32))
        output.write(b"data" + struct.pack("<I", size))
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
    parser.add_argument("--audio-format", choices=("pcm", "vorbis"), default="vorbis")
    options = parser.parse_args()
    source = options.source.resolve()
    output = options.output.resolve()
    if output == source or source in output.parents or output in source.parents:
        raise ValueError("Runtime output must be separate from source assets")
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
    bounds = loop_bounds(source)
    records: list[dict] = []
    stems: set[str] = set()
    for path in sorted((source / "audio").iterdir()):
        if path.suffix.lower() not in (".m4a", ".wav"):
            continue
        if path.stem in stems:
            raise ValueError("Ambiguous audio source: " + path.stem)
        stems.add(path.stem)
        record = prepare_audio(options.ffmpeg, path, audio / (path.stem + extension),
                               bounds.get(path.stem, 0), options.audio_format)
        records.append(record)
    if len(records) != 424:
        raise ValueError("Expected all 424 source audio files")
    audio_format = "IEEE float32 LE" if options.audio_format == "pcm" else "Ogg Vorbis quality 6"
    manifest = {"sample_rate": 48000, "channels": 2, "format": audio_format, "files": records}
    (audio / "portable_manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print("Prepared environment pixels and", len(records), audio_format, "assets at", output)


if __name__ == "__main__":
    main()
