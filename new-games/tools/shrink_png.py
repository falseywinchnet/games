#!/usr/bin/env python3
"""Compress the PNGs the kit's preview tools write.

    python3 new-games/tools/shrink_png.py games/<id>/screens

The harness writes PNGs with their pixels stored uncompressed, which keeps the
C++ side tiny but makes files of several megabytes. This rewrites each file in
place with the same pixels, properly deflated (typically twenty times smaller).
Standard library only. Files that are already compressed are left alone.
"""
from __future__ import annotations

import struct
import sys
import zlib
from pathlib import Path

SIGNATURE = b"\x89PNG\r\n\x1a\n"


def chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)


def shrink(path: Path) -> tuple[int, int]:
    original = path.read_bytes()
    if not original.startswith(SIGNATURE):
        raise ValueError(f"{path} is not a PNG")
    offset = len(SIGNATURE)
    header = b""
    packed = b""
    while offset < len(original):
        length, kind = struct.unpack(">I4s", original[offset:offset + 8])
        data = original[offset + 8:offset + 8 + length]
        if kind == b"IHDR":
            header = data
        elif kind == b"IDAT":
            packed += data
        offset += 12 + length
    if not header or not packed:
        raise ValueError(f"{path} has no image data")
    pixels = zlib.decompress(packed)
    smaller = SIGNATURE + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(pixels, 9)) + chunk(b"IEND", b"")
    if len(smaller) < len(original):
        path.write_bytes(smaller)
        return len(original), len(smaller)
    return len(original), len(original)


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    files: list[Path] = []
    for argument in sys.argv[1:]:
        target = Path(argument)
        files += sorted(target.glob("*.png")) if target.is_dir() else [target]
    before = after = 0
    for path in files:
        was, now = shrink(path)
        before += was
        after += now
    print(f"{len(files)} files: {before // 1024} KiB -> {after // 1024} KiB")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
