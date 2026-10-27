"""Read and write ambient scene archives (shared/ambient/src/archive.hpp) from Python.

Authoring only. The record layouts mirror archive.hpp exactly; `read_archive` and
`write_archive` round-trip a file byte for byte.
"""
from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

STATIC_VERTEX = np.dtype([("p", "<f4", 3), ("n", "i1", 3), ("moss", "u1"), ("uv", "<f4", 2),
                          ("rgb", "u1", 3), ("pad", "u1")])
STATIC_INSTANCE = np.dtype([("m", "<f4", 12), ("rgb", "<f4", 3), ("uv_scale", "<f4", 2),
                            ("material", "<u4")])
SWAY_VERTEX = np.dtype([("p", "<f4", 3), ("n", "i1", 3), ("translucency", "u1"), ("uv", "<u2", 2),
                        ("rgb", "u1", 3), ("compliance", "u1"), ("bend", "i1", 3), ("pad0", "u1"),
                        ("along", "i1", 3), ("pad1", "u1"), ("distance", "<f4"), ("root", "<u4")])
ACTOR_VERTEX = np.dtype([("p", "<f4", 3), ("n", "i1", 3), ("part", "u1"), ("uv", "<u2", 2),
                         ("fin", "u1"), ("pad", "u1", 3)])
ACTOR_PART = np.dtype([("m", "<f4", 12), ("rgb", "<f4", 3), ("mesh", "<u4"), ("material", "<u4"),
                       ("actor", "<i4"), ("anatomy", "<f4", 2)])
ACTOR = np.dtype([("center", "<f4", 4), ("cruise", "<f4", 4), ("kind", "<u4"), ("pad", "<u4", 3)])
RISER = np.dtype([("base", "<f4", 3), ("size", "<f4"), ("phase", "<f4"), ("speed", "<f4"),
                  ("height", "<f4"), ("pad", "<f4")])
PLACEMENT = np.dtype([("mesh", "<u4"), ("m", "<f4", 12), ("rgb", "<f4", 3), ("uv_scale", "<f4", 2),
                      ("material", "<u4")])


@dataclass
class Texture:
    width: int
    height: int
    normal_map: bool
    rgb: bytes


@dataclass
class Archive:
    camera: np.ndarray                      # 12 floats
    textures: list = field(default_factory=list)
    static_meshes: list = field(default_factory=list)   # (STATIC_VERTEX array, (n, 3) indices)
    placements: np.ndarray = None            # PLACEMENT records
    sway_roots: np.ndarray = None            # (n, 3)
    sway: tuple = None                       # (SWAY_VERTEX array, (n, 3) indices)
    actor_meshes: list = field(default_factory=list)
    parts: np.ndarray = None
    actors: np.ndarray = None
    risers: np.ndarray = None


class _Cursor:
    def __init__(self, data: bytes):
        self.data = data
        self.at = 0

    def take(self, count: int) -> bytes:
        out = self.data[self.at:self.at + count]
        if len(out) != count:
            raise ValueError("archive ends early")
        self.at += count
        return out

    def u32(self) -> int:
        return struct.unpack("<I", self.take(4))[0]

    def tag(self, name: bytes) -> int:
        if self.take(4) != name:
            raise ValueError(f"expected section {name!r}")
        return self.u32()

    def array(self, dtype, count: int):
        dtype = np.dtype(dtype)
        return np.frombuffer(self.take(dtype.itemsize * count), dtype).copy()

    def mesh(self, dtype):
        vertices, indices = self.u32(), self.u32()
        return self.array(dtype, vertices), self.array("<u4", indices).reshape(-1, 3).astype(np.int64)


def read_archive(path: Path) -> Archive:
    raw = Path(path).read_bytes()
    if raw[:8] != b"AMBSCN\0\1":
        raise ValueError("not an ambient archive")
    version, payload_bytes, compressed_bytes, checksum = struct.unpack("<IIII", raw[8:24])
    payload = zlib.decompress(raw[32:])
    if version != 1 or len(payload) != payload_bytes or zlib.crc32(payload) != checksum:
        raise ValueError("damaged archive")
    c = _Cursor(payload)
    c.tag(b"CAMR")
    archive = Archive(camera=c.array("<f4", 12))
    for _ in range(c.tag(b"TEXT")):
        w, h, kind = struct.unpack("<HHI", c.take(8))
        archive.textures.append(Texture(w, h, kind == 1, c.take(w * h * 3)))
    for _ in range(c.tag(b"STAT")):
        archive.static_meshes.append(c.mesh(STATIC_VERTEX))
    archive.placements = c.array(PLACEMENT, c.u32())
    c.tag(b"SWAY")
    archive.sway_roots = c.array("<f4", c.u32() * 3).reshape(-1, 3)
    archive.sway = c.mesh(SWAY_VERTEX)
    for _ in range(c.tag(b"AMSH")):
        archive.actor_meshes.append(c.mesh(ACTOR_VERTEX))
    archive.parts = c.array(ACTOR_PART, c.tag(b"APRT"))
    archive.actors = c.array(ACTOR, c.tag(b"ACTR"))
    archive.risers = c.array(RISER, c.tag(b"RISE"))
    if c.at != len(payload):
        raise ValueError("trailing data")
    return archive


def _mesh(parts, vertices, triangles):
    parts.append(struct.pack("<II", len(vertices), len(triangles) * 3))
    parts.append(np.ascontiguousarray(vertices).tobytes())
    parts.append(np.ascontiguousarray(triangles, dtype="<u4").tobytes())


def archive_bytes(a: Archive) -> bytes:
    parts = [b"CAMR" + struct.pack("<I", 1), np.asarray(a.camera, "<f4").tobytes()]
    parts.append(b"TEXT" + struct.pack("<I", len(a.textures)))
    for t in a.textures:
        parts.append(struct.pack("<HHI", t.width, t.height, 1 if t.normal_map else 0) + t.rgb)
    parts.append(b"STAT" + struct.pack("<I", len(a.static_meshes)))
    for vertices, triangles in a.static_meshes:
        _mesh(parts, vertices, triangles)
    parts.append(struct.pack("<I", len(a.placements)) + np.ascontiguousarray(a.placements).tobytes())
    parts.append(b"SWAY" + struct.pack("<I", 1) + struct.pack("<I", len(a.sway_roots)))
    parts.append(np.asarray(a.sway_roots, "<f4").tobytes())
    _mesh(parts, *a.sway)
    parts.append(b"AMSH" + struct.pack("<I", len(a.actor_meshes)))
    for vertices, triangles in a.actor_meshes:
        _mesh(parts, vertices, triangles)
    parts.append(b"APRT" + struct.pack("<I", len(a.parts)) + np.ascontiguousarray(a.parts).tobytes())
    parts.append(b"ACTR" + struct.pack("<I", len(a.actors)) + np.ascontiguousarray(a.actors).tobytes())
    parts.append(b"RISE" + struct.pack("<I", len(a.risers)) + np.ascontiguousarray(a.risers).tobytes())
    payload = b"".join(parts)
    compressed = zlib.compress(payload, 9)
    header = b"AMBSCN\0\1" + struct.pack("<IIII", 1, len(payload), len(compressed), zlib.crc32(payload)) + bytes(8)
    return header + compressed


def write_archive(a: Archive, path: Path) -> bytes:
    data = archive_bytes(a)
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_bytes(data)
    return data
