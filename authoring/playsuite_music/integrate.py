#!/usr/bin/env python3
"""Package rendered PlaySuite music into the game's existing audio names and runtime.

Steps (plain Python + ffmpeg; no numpy or Pillow required):
  1. For each rendered master whose name already exists in assets/audio, encode AAC-LC 256 kb/s M4A
     (loops get a 4096-frame circular wrap appended so encoder end padding never touches the loop),
     decode it back exactly as tools/prepare_portable_assets.py does, and check sample alignment,
     frame count and seam continuity with the renderer's `analyse` command.
  2. Replace assets/audio/<name>.m4a in place and update audio_manifest.json / PACKAGED_SHA256.json.
  3. Build a runtime directory from an existing prepared runtime, re-encoding only audio whose source
     changed (using prepare_portable_assets.prepare_audio itself), then run verify_portable_assets.

Example (from the repository root):
  python3 authoring/playsuite_music/integrate.py --ffmpeg <ffmpeg.exe> \
      --masters .build/music-v2/out --work .build/music-v2/encode \
      --runtime-base .build/runtime-vorbis-atomprobe --runtime-out .build/runtime-playsuite
"""
import argparse
import hashlib
import json
import shutil
import struct
import subprocess
import sys
import types
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WRAP_FRAMES = 4096
CONTEXT_FRAMES = 2048
PRODUCER = "PlaySuite C++ synthesizer (authoring/playsuite_music), 2026-10"


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_wav24(path: Path):
    data = path.read_bytes()
    if data[:4] != b"RIFF" or data[8:16] != b"WAVEfmt " or data[36:40] != b"data":
        raise ValueError("unexpected WAV layout: " + str(path))
    fmt = struct.unpack("<HHIIHH", data[20:36])
    if fmt != (1, 2, 48000, 288000, 6, 24):
        raise ValueError("expected 48 kHz stereo 24-bit PCM: " + str(path))
    size = struct.unpack("<I", data[40:44])[0]
    return data[44:44 + size]


def write_wav24(path: Path, pcm: bytes):
    header = b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 2, 48000, 288000, 6, 24)
    path.write_bytes(header + b"data" + struct.pack("<I", len(pcm)) + pcm)


def mp4_boxes(data: bytes, off: int, end: int, path: str = ""):
    while off < end:
        size, typ = struct.unpack(">I4s", data[off:off + 8])
        header = 8
        if size == 1:
            size = struct.unpack(">Q", data[off + 8:off + 16])[0]; header = 16
        name = path + "/" + typ.decode("latin1")
        yield name, off, size, header
        if typ in (b"moov", b"trak", b"mdia", b"minf", b"stbl", b"edts"):
            yield from mp4_boxes(data, off + header, off + size, name)
        off += size


def skip_leading_context(m4a: Path, frames: int):
    """Move the MP4 edit-list start past `frames` of leading circular context.

    AAC reconstructs the first frames of a stream poorly (the encoder sees silence before them), which
    would put a small click at the loop seam. Encoding a little of the loop's end before sample 0 and
    starting the edit list after it makes decoders that honour edit lists (ffmpeg, AVFoundation) begin
    exactly at loop sample 0 with fully-coded audio. A decoder that ignored the edit list would still
    produce a seamless loop, because the encoded audio is a contiguous span of the repeating loop.
    """
    data = bytearray(m4a.read_bytes())
    movie_scale = None
    for name, off, size, header in mp4_boxes(data, 0, len(data)):
        if name == "/moov/mvhd":
            version = data[off + header]
            movie_scale = struct.unpack(">I", data[off + header + (12 if version == 0 else 20):][:4])[0]
        if name.endswith("/edts/elst"):
            body = off + header
            version = data[body]
            if struct.unpack(">I", data[body + 4:body + 8])[0] != 1 or movie_scale is None:
                raise ValueError("unexpected edit list layout: " + str(m4a))
            if version == 0:
                duration, media_time = struct.unpack(">Ii", data[body + 8:body + 16])
                duration -= round(frames * movie_scale / 48000)
                data[body + 8:body + 16] = struct.pack(">Ii", duration, media_time + frames)
            else:
                duration, media_time = struct.unpack(">Qq", data[body + 8:body + 24])
                duration -= round(frames * movie_scale / 48000)
                data[body + 8:body + 24] = struct.pack(">Qq", duration, media_time + frames)
            m4a.write_bytes(bytes(data))
            return media_time
    raise ValueError("no edit list in " + str(m4a))


def run(cmd):
    subprocess.run([str(c) for c in cmd], check=True)


def encode_and_verify(ffmpeg, renderer, masters: Path, work: Path, stem: str, report: dict) -> dict:
    loop = report["loop"]
    frames = report["samples"]
    pcm = read_wav24(masters / (stem + ".wav"))
    if len(pcm) != frames * 6:
        raise ValueError("master length differs from report: " + stem)
    enc_in = work / (stem + ".enc.wav")
    if loop:  # circular context before and after the loop body
        write_wav24(enc_in, pcm[-CONTEXT_FRAMES * 6:] + pcm + pcm[:WRAP_FRAMES * 6])
    else:
        write_wav24(enc_in, pcm)
    m4a = work / (stem + ".m4a")
    run([ffmpeg, "-v", "error", "-nostdin", "-y", "-i", enc_in, "-map_metadata", "-1", "-fflags", "+bitexact",
         "-flags:a", "+bitexact", "-c:a", "aac", "-b:a", "256k", "-ar", "48000", "-ac", "2", m4a])
    if loop:
        skip_leading_context(m4a, CONTEXT_FRAMES)
    dec = work / (stem + ".dec.f32")
    run([ffmpeg, "-v", "error", "-nostdin", "-y", "-i", m4a, "-vn", "-ar", "48000", "-ac", "2", "-f", "f32le", dec])
    out = subprocess.run([str(renderer), "analyse", str(dec), str(masters / (stem + ".f32")), str(frames), "1" if loop else "0"],
                         check=True, capture_output=True, text=True).stdout
    result = json.loads(out)
    if result["best_offset"] != 0:
        raise ValueError(f"{stem}: decoded AAC is misaligned by {result['best_offset']} frames")
    if result["decoded_frames"] < frames:
        raise ValueError(f"{stem}: decoded AAC shorter than the loop")
    enc_in.unlink()
    dec.unlink()
    result["m4a"] = str(m4a)
    return result


def update_manifests(assets_audio: Path, replaced: dict, reports: dict, sections_desc: dict):
    path = assets_audio / "audio_manifest.json"
    text = path.read_text(encoding="utf-8")
    manifest = json.loads(text)
    if json.dumps(manifest, indent=2) + "\n" != text:
        print("note: audio_manifest.json formatting differs from json.dumps(indent=2); rewriting canonically")
    for entry in manifest["music"]:
        stem = "music_" + entry["id"] + "_loop"
        if stem not in replaced:
            continue
        r, dec = reports[stem], replaced[stem]
        for key in ("wav_master_on_neo",):
            entry.pop(key, None)
        entry.update({
            "title": r["title"],
            "batch": "playsuite-v2",
            "producer": PRODUCER,
            "file": "authoring/playsuite_music render: " + stem + ".wav",
            "format": "AAC-LC 256 kb/s in M4A from a 24-bit 48 kHz master",
            "sample_rate": 48000, "channels": 2,
            "samples": r["samples"], "seconds": round(r["seconds"], 3), "bpm": round(r["bpm"], 4),
            "meter": "4/4", "bars": r["bars"], "samples_per_beat": round(r["samples"] / (r["bars"] * 4)),
            "loop_start_sample": 0, "loop_end_sample_exclusive": r["samples"],
            "wav_smpl_chunk": False,
            "sections": [[name, bars] for name, bars in sections_desc[stem]],
            "lufs_integrated": round(r["lufs_integrated"], 1), "true_peak_dbtp": round(r["true_peak_dbtp"], 1),
            "validation": {
                "peak_dbfs": r["peak_dbfs"], "clipped_samples": r["clipped_samples"], "dc_offset_max": r["dc_offset_max"],
                "loop_seam_jump": r["seam"]["jump"], "loop_seam_jump_percentile": r["seam"]["jump_percentile"],
                "loop_seam_d2_percentile": r["seam"]["d2_percentile"], "loop_seam_rms_step_db": r["seam"]["rms_step_db"],
                "circular_tail_residual": r["tail_residual"],
                "decoded_aac": {"frames": dec["decoded_frames"], "alignment_offset": dec["best_offset"], "snr_db": dec["snr_db"],
                                "true_peak_dbtp": dec["true_peak_dbtp"], "seam": dec.get("seam")},
            },
            "runtime": [stem + ".m4a"],
            "measurement_basis": "Rendered master; packaged AAC decoded and verified sample-aligned (offset 0)",
            "loop_metadata_basis": "Decoded PCM trimmed to loop_end_sample_exclusive; the AAC carries a short circular wrap after the loop",
        })
    for entry in manifest["stingers"]:
        stem = Path(entry["file"]).stem
        if stem not in replaced:
            continue
        r, dec = reports[stem], replaced[stem]
        entry.pop("wav_master_on_neo", None)
        entry.update({
            "file": "authoring/playsuite_music render: " + stem + ".wav",
            "batch": "playsuite-v2", "producer": PRODUCER, "title": r["title"],
            "channels": 2, "seconds": round(r["seconds"], 3), "samples": r["samples"],
            "lufs_integrated": round(r["lufs_integrated"], 1), "true_peak_dbtp": round(r["true_peak_dbtp"], 1),
            "validation": {"peak_dbfs": r["peak_dbfs"], "clipped_samples": r["clipped_samples"], "dc_offset_max": r["dc_offset_max"],
                           "start_abs": r["start_abs"], "end_abs": r["end_abs"],
                           "decoded_aac": {"frames": dec["decoded_frames"], "alignment_offset": dec["best_offset"], "snr_db": dec["snr_db"],
                                           "true_peak_dbtp": dec["true_peak_dbtp"]}},
            "runtime": [stem + ".m4a"],
            "format": "AAC-LC 256 kb/s in M4A from a 24-bit 48 kHz master",
            "measurement_basis": "Rendered master; packaged AAC decoded and verified sample-aligned (offset 0)",
        })
    manifest["generated_by"] = ("Original Neo synthesis; two delivered batches. Eleven loops and fourteen stingers replaced "
                                "by the PlaySuite C++ synthesizer (authoring/playsuite_music)")
    manifest.setdefault("validation_summary", {})["playsuite_v2"] = {
        "files_with_clipping": [s for s in replaced if reports[s]["clipped_samples"]],
        "replaced": sorted(s + ".m4a" for s in replaced)}
    path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    path = assets_audio / "PACKAGED_SHA256.json"
    records = json.loads(path.read_text(encoding="utf-8"))
    names = {s + ".m4a" for s in replaced}
    for rec in records:
        if rec["file"] in names:
            rec["batch"] = "playsuite-v2"
            rec["sha256"] = sha(assets_audio / rec["file"])
    missing = names - {rec["file"] for rec in records}
    if missing:
        raise ValueError("replaced files missing from PACKAGED_SHA256.json: " + ", ".join(sorted(missing)))
    path.write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")


def build_runtime(ffmpeg, renderer, masters: Path, base: Path, out: Path, reports: dict):
    # tools/prepare_portable_assets.py imports Pillow only for the lake artwork; audio helpers do not need it.
    if "PIL" not in sys.modules:
        stub = types.ModuleType("PIL"); stub.Image = types.SimpleNamespace(); sys.modules["PIL"] = stub
    sys.path.insert(0, str(ROOT / "tools"))
    import prepare_portable_assets as prep
    import verify_portable_assets as verify
    source = ROOT / "assets"
    if out.exists():
        shutil.rmtree(out)
    shutil.copytree(base, out)
    # The lake pixels are derived from nature-lake.png; reuse them only if that artwork is unchanged.
    if sha(source / "nature-lake.png") != sha(base / "nature-lake.png"):
        raise ValueError("nature-lake.png changed since the base runtime; run prepare_portable_assets.py with Pillow")
    refreshed = []
    for path in source.rglob("*"):
        if path.is_file() and path.relative_to(source).parts[0] != "audio":
            target = out / path.relative_to(source)
            if not target.exists() or sha(target) != sha(path):
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, target)
                refreshed.append(str(path.relative_to(source)))
    audio = out / "audio"
    manifest = json.loads((audio / "portable_manifest.json").read_text(encoding="utf-8"))
    bounds = prep.loop_bounds(source)
    sources = {p.name: p for p in (source / "audio").iterdir() if p.suffix.lower() in (".m4a", ".wav")}
    if len(manifest["files"]) != len(sources):
        raise ValueError("runtime and source audio inventories differ in size")
    reencoded, checks = [], {}
    for i, rec in enumerate(manifest["files"]):
        src = sources[rec["source"]]
        if sha(src) == rec["source_sha256"]:
            continue
        new = prep.prepare_audio(ffmpeg, src, audio / rec["file"], bounds.get(src.stem, 0), "vorbis")
        manifest["files"][i] = new
        reencoded.append(rec["file"])
        if src.stem in reports:
            dec = audio / (src.stem + ".check.f32")
            run([ffmpeg, "-v", "error", "-nostdin", "-y", "-i", audio / rec["file"], "-f", "f32le", "-ac", "2", "-ar", "48000", dec])
            r = reports[src.stem]
            res = json.loads(subprocess.run([str(renderer), "analyse", str(dec), str(masters / (src.stem + ".f32")),
                                             str(r["samples"]), "1" if r["loop"] else "0"], check=True, capture_output=True, text=True).stdout)
            dec.unlink()
            if r["loop"] and res["decoded_frames"] != r["samples"]:
                raise ValueError(f"runtime Vorbis loop length {res['decoded_frames']} != {r['samples']}: {src.stem}")
            checks[src.stem] = res
    (audio / "portable_manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    summary = verify.verify(source, out)
    return refreshed, reencoded, checks, summary


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ffmpeg", required=True)
    ap.add_argument("--renderer", default=str(ROOT / ".build/music-v2/bin/playsuite_music.exe"))
    ap.add_argument("--masters", type=Path, default=ROOT / ".build/music-v2/out")
    ap.add_argument("--work", type=Path, default=ROOT / ".build/music-v2/encode")
    ap.add_argument("--runtime-base", type=Path, default=ROOT / ".build/runtime-vorbis-atomprobe")
    ap.add_argument("--runtime-out", type=Path, default=ROOT / ".build/runtime-playsuite")
    ap.add_argument("--skip-assets", action="store_true", help="only rebuild the runtime from current assets")
    ap.add_argument("--skip-runtime", action="store_true")
    args = ap.parse_args()
    assets_audio = ROOT / "assets/audio"
    args.work.mkdir(parents=True, exist_ok=True)
    reports = {}
    for rp in sorted(args.masters.glob("*.json")):
        r = json.loads(rp.read_text(encoding="utf-8"))
        if (assets_audio / (r["id"] + ".m4a")).exists():
            reports[r["id"]] = r
        elif not r["id"].startswith("instrument_"):
            print("skipping (no existing packaged file):", r["id"])
    sections = {stem: r["sections"] for stem, r in reports.items() if r["loop"]}
    replaced = {}
    if not args.skip_assets:
        for stem, r in reports.items():
            res = encode_and_verify(args.ffmpeg, args.renderer, args.masters, args.work, stem, r)
            shutil.copyfile(res["m4a"], assets_audio / (stem + ".m4a"))
            replaced[stem] = res
            seam = res.get("seam") or {}
            print(f"{stem}: frames {r['samples']} decoded {res['decoded_frames']} offset {res['best_offset']} "
                  f"snr {res['snr_db']} dB tp {res['true_peak_dbtp']} dBTP seam {seam}")
        update_manifests(assets_audio, replaced, reports, sections)
        (args.work / "aac_checks.json").write_text(json.dumps(replaced, indent=2) + "\n", encoding="utf-8")
    if not args.skip_runtime:
        refreshed, reencoded, checks, summary = build_runtime(args.ffmpeg, args.renderer, args.masters, args.runtime_base,
                                                              args.runtime_out, reports)
        print("refreshed non-audio files:", refreshed)
        print("re-encoded runtime audio:", len(reencoded))
        for stem, res in checks.items():
            print(f"  runtime {stem}: frames {res['decoded_frames']} offset {res['best_offset']} seam {res.get('seam')}")
        (args.work / "runtime_checks.json").write_text(json.dumps(checks, indent=2) + "\n", encoding="utf-8")
        print("verify_portable_assets:", json.dumps(summary))


if __name__ == "__main__":
    main()
