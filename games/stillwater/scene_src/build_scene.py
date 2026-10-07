#!/usr/bin/env python3
"""Build Stillwater's compact CPU scene archive from the Stillwater Riverscape archive.

Authoring only. Reads the pinned Stillwater checkout (its translated Riverscape
geometry and Poly Haven material maps) and writes `assets/scene/riverscape.ambient`,
the ambient-engine archive this game ships. Needs NumPy, Pillow and
fast_simplification; none of them is used at run time.

Reductions, all deterministic:
  * swaying foliage: exact lattice decimation of each leaf ribbon (rows thinned
    along the blade, columns kept at the edges and midrib);
  * rocks, wood, sand and fish: quadric decimation with open borders preserved,
    so texture seams and fin edges stay where they were;
  * material maps: box-filtered to 256 x 256.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import struct
import zlib
from pathlib import Path

import numpy as np
from PIL import Image
import fast_simplification as fs

HERE = Path(__file__).resolve().parent
GAME = HERE.parent
DEFAULT_SOURCE = Path.home() / "Developer/Projects/stillwater"
SOURCE_ARCHIVE = "assets/riverscape.swscene.gz"
SOURCE_COMMIT = "4a80ee3"
TEXTURE_SIZE = 256

# Ambient archive section tags.
TAG_CAMERA, TAG_TEXTURE, TAG_STATIC, TAG_SWAY, TAG_ACTOR_MESH, TAG_ACTOR_PART, TAG_ACTOR, TAG_RISER = (
    b"CAMR", b"TEXT", b"STAT", b"SWAY", b"AMSH", b"APRT", b"ACTR", b"RISE")


def read_source(path: Path):
    data = gzip.open(path).read()
    header = np.frombuffer(data[:32], dtype=np.uint32)
    if data[:8] != b"STWSCN1\0":
        raise SystemExit("not a Stillwater habitat archive")
    nv, ni, ninst, nobj, nb, na = (int(v) for v in header[2:8])
    o = 32
    vertices = np.frombuffer(data, np.float32, nv * 32, o).reshape(nv, 8, 4); o += nv * 128
    indices = np.frombuffer(data, np.uint32, ni, o); o += ni * 4
    instances = np.frombuffer(data, np.float32, ninst * 28, o).reshape(ninst, 28); o += ninst * 112
    objects = np.frombuffer(data, np.uint8, nobj * 32, o).reshape(nobj, 32); o += nobj * 32
    batches = np.frombuffer(data, np.uint32, nb * 4, o).reshape(nb, 4); o += nb * 16
    actors = np.frombuffer(data, np.float32, na * 20, o).reshape(na, 20); o += na * 80
    if o != len(data):
        raise SystemExit("unexpected trailing data in the habitat archive")
    return vertices, indices, instances, objects, batches, actors, hashlib.sha256(path.read_bytes()).hexdigest()


def transform_rows(instance) -> np.ndarray:
    """Metal's column-major 4x4 to a row-major 3x4 affine transform."""
    m = instance[:16].reshape(4, 4).T
    return m[:3, :4].astype(np.float32)


def decimate(points, triangles, target_count, keep_border=True):
    """Quadric decimation (borders kept unless asked); returns the kept original vertex per output vertex."""
    triangles = triangles.astype(np.int32)
    if len(triangles) <= target_count:
        return np.arange(len(points)), triangles
    reduction = 1.0 - target_count / len(triangles)
    _, _, collapses = fs.simplify(points.astype(np.float64), triangles, target_reduction=reduction,
                                  return_collapses=True, preserve_border=keep_border)
    out_points, out_triangles, mapping = fs.replay_simplification(points.astype(np.float64), triangles,
                                                                 collapses)
    # Each output vertex takes all attributes (position included) from the original
    # vertex mapped to it that lies closest to the optimal output position.
    representative = np.full(len(out_points), -1, dtype=np.int64)
    best = np.full(len(out_points), np.inf)
    for original, target in enumerate(mapping):
        if target < 0:
            continue
        distance = float(np.sum((points[original] - out_points[target]) ** 2))
        if distance < best[target]:
            best[target] = distance
            representative[target] = original
    keep = out_triangles[(out_triangles[:, 0] != out_triangles[:, 1]) & (out_triangles[:, 1] != out_triangles[:, 2])
                         & (out_triangles[:, 0] != out_triangles[:, 2])]
    if np.any(representative[np.unique(keep)] < 0):
        raise SystemExit("decimation produced an unmapped vertex")
    return representative, keep


def mesh_slice(vertices, indices, first, count):
    tri = indices[first:first + count].reshape(-1, 3)
    used = np.unique(tri)
    remap = np.full(int(used.max()) + 1, -1, dtype=np.int64)
    remap[used] = np.arange(len(used))
    return vertices[used], remap[tri]


def snorm8(values):
    return np.clip(np.round(np.asarray(values) * 127), -127, 127).astype(np.int8)


def unorm8(values):
    return np.clip(np.round(np.asarray(values) * 255), 0, 255).astype(np.uint8)


def encode_albedo(values):
    """Linear albedo to 8 bits with a square-root curve, which keeps dark tones."""
    return unorm8(np.sqrt(np.clip(values, 0, 1)))


class Writer:
    def __init__(self):
        self.parts: list[bytes] = []

    def section(self, tag: bytes, count: int):
        self.parts.append(tag + struct.pack("<I", count))

    def raw(self, data):
        self.parts.append(data if isinstance(data, bytes) else np.ascontiguousarray(data).tobytes())

    def payload(self) -> bytes:
        return b"".join(self.parts)


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


def static_vertices(v):
    out = np.zeros(len(v), STATIC_VERTEX)
    out["p"] = v[:, 0, :3]
    out["n"] = snorm8(v[:, 1, :3])
    out["moss"] = unorm8(v[:, 1, 3])
    out["uv"] = v[:, 3, :2]
    out["rgb"] = encode_albedo(v[:, 2, :3])
    return out


def write_mesh(writer, vertex_records, triangles):
    writer.raw(struct.pack("<II", len(vertex_records), len(triangles) * 3))
    writer.raw(vertex_records)
    writer.raw(triangles.astype("<u4"))


def lattice_decimate(component, vertices, spacing):
    """Row/column thinning of one ribbon whose vertices form an implicit row-major lattice."""
    tri = component
    first, last = int(tri.min()), int(tri.max())
    count = last - first + 1
    if len(np.unique(tri)) != count:
        return None
    distance = vertices[first:last + 1, 6, 3]
    # Rows share their v coordinate (wide leaves) or their distance from the root
    # (narrow strands); either way every row must have the same length.
    columns = 0
    for key in (vertices[first:last + 1, 3, 1], distance):
        row_starts = np.flatnonzero(np.r_[True, key[1:] != key[:-1]])
        lengths = np.diff(np.r_[row_starts, count])
        if len(lengths) >= 2 and np.all(lengths == lengths[0]) and lengths[0] >= 2:
            columns = int(lengths[0])
            break
    if columns == 0:
        return None
    rows = count // columns
    if rows < 2:
        return None
    expected = set()
    for r in range(rows - 1):
        for c in range(columns - 1):
            a = first + r * columns + c
            b = a + columns
            expected.add(tuple(sorted((a, b, a + 1))))
            expected.add(tuple(sorted((a + 1, b, b + 1))))
    if set(map(tuple, np.sort(tri, axis=1))) != expected or len(tri) != len(expected):
        return None
    row_distance = distance.reshape(rows, columns).mean(axis=1)
    keep_rows = [0]
    for r in range(1, rows - 1):
        if row_distance[r] - row_distance[keep_rows[-1]] >= spacing:
            keep_rows.append(r)
    if keep_rows[-1] != rows - 1:
        if len(keep_rows) > 1 and row_distance[rows - 1] - row_distance[keep_rows[-1]] < spacing * 0.5:
            keep_rows[-1] = rows - 1
        else:
            keep_rows.append(rows - 1)
    p0 = vertices[first, 0, :3]
    closed = columns > 3 and np.allclose(vertices[first + columns - 1, 0, :3], p0, atol=1e-5)
    if columns <= 3:
        keep_cols = list(range(columns))
    elif closed:
        keep_cols = list(range(0, columns - 1, 2)) + [columns - 1]
    else:
        keep_cols = [0, columns // 2, columns - 1]
    # Preserve the original winding: (a, b, a+1) and (a+1, b, b+1).
    out = []
    for ri in range(len(keep_rows) - 1):
        for ci in range(len(keep_cols) - 1):
            a = first + keep_rows[ri] * columns + keep_cols[ci]
            a1 = first + keep_rows[ri] * columns + keep_cols[ci + 1]
            b = first + keep_rows[ri + 1] * columns + keep_cols[ci]
            b1 = first + keep_rows[ri + 1] * columns + keep_cols[ci + 1]
            out.append((a, b, a1))
            out.append((a1, b, b1))
    return np.array(out, dtype=np.int64)


def sway_section(writer, vertices, indices, instances, first, count, spacing):
    tri = indices[first:first + count].reshape(-1, 3).astype(np.int64)
    # Components follow vertex order: a ribbon owns a contiguous vertex range.
    order = np.argsort(tri.min(axis=1), kind="stable")
    tri = tri[order]
    lows = tri.min(axis=1)
    parent = {}
    groups = []
    start = 0
    span_end = int(tri[0].max())
    for i in range(1, len(tri) + 1):
        if i == len(tri) or lows[i] > span_end:
            groups.append(tri[start:i])
            start = i
            if i < len(tri):
                span_end = int(tri[i].max())
        else:
            span_end = max(span_end, int(tri[i].max()))
    kept = []
    lattice = fallback = 0
    for group in groups:
        reduced = lattice_decimate(group, vertices, spacing)
        if reduced is None:
            kept.append(group)
            fallback += len(group)
        else:
            kept.append(reduced)
            lattice += 1
    tri = np.concatenate(kept)
    used = np.unique(tri)
    remap = np.full(int(used.max()) + 1, -1, dtype=np.int64)
    remap[used] = np.arange(len(used))
    v = vertices[used]
    anchors, root = np.unique(v[:, 4, :3].round(5), axis=0, return_inverse=True)
    out = np.zeros(len(v), SWAY_VERTEX)
    out["p"] = v[:, 0, :3]
    out["n"] = snorm8(v[:, 1, :3])
    out["translucency"] = unorm8(v[:, 4, 3])
    out["uv"] = np.clip(np.round(v[:, 3, :2] * 65535), 0, 65535).astype(np.uint16)
    bound = v[:, 7, 0].astype(np.int64)
    out["rgb"] = encode_albedo(v[:, 2, :3] * instances[bound, 16:19])
    out["compliance"] = unorm8(v[:, 5, 3] / 1.2)
    out["bend"] = snorm8(v[:, 5, :3])
    out["along"] = snorm8(v[:, 6, :3])
    out["distance"] = v[:, 6, 3]
    out["root"] = root.ravel()
    writer.section(TAG_SWAY, 1)
    writer.raw(struct.pack("<I", len(anchors)))
    writer.raw(anchors.astype("<f4"))
    write_mesh(writer, out, remap[tri])
    return {"triangles_in": count // 3, "triangles_out": len(tri), "ribbons_thinned": lattice,
            "triangles_unthinned": fallback, "roots": len(anchors)}


def actor_mesh(vertices, indices, first, count, target):
    v, tri = mesh_slice(vertices, indices, first, count)
    # Fish are a few dozen pixels long: their open edges (fins, eyes) may simplify too.
    representative, tri = decimate(v[:, 0, :3], tri, target, keep_border=False)
    used = np.unique(tri)
    remap = np.full(len(representative), -1, dtype=np.int64)
    remap[used] = np.arange(len(used))
    v = v[representative[used]]
    out = np.zeros(len(v), ACTOR_VERTEX)
    out["p"] = v[:, 0, :3]
    out["n"] = snorm8(v[:, 1, :3])
    out["part"] = np.clip(np.round(v[:, 3, 2]), 0, 255).astype(np.uint8)
    out["uv"] = np.clip(np.round(np.clip(v[:, 3, :2], 0, 1) * 65535), 0, 65535).astype(np.uint16)
    out["fin"] = unorm8(v[:, 3, 3])
    return out, remap[tri]


def sphere_mesh(rings=8, segments=12):
    points = []
    for row in range(rings + 1):
        lat = np.pi * row / rings
        for column in range(segments + 1):
            lon = 2 * np.pi * column / segments
            points.append((np.sin(lat) * np.cos(lon), np.cos(lat), np.sin(lat) * np.sin(lon)))
    tri = []
    for row in range(rings):
        for column in range(segments):
            a = row * (segments + 1) + column
            b = a + segments + 1
            # Outward faces counter-clockwise, like every archive mesh.
            tri += [(a, a + 1, b), (a + 1, b + 1, b)]
    out = np.zeros(len(points), ACTOR_VERTEX)
    out["p"] = points
    out["n"] = snorm8(points)
    return out, np.array(tri)


def affine(position, scale, yaw):
    c, s = np.cos(yaw), np.sin(yaw)
    return np.array([[c * scale[0], 0, s * scale[2], position[0]],
                     [0, scale[1], 0, position[1]],
                     [-s * scale[0], 0, c * scale[2], position[2]]], dtype=np.float32)


def ground_height(x, z):
    """Riverscape math.js ground height (Chase Lean, MIT), as Stillwater translated it."""
    center = 0.3 - 0.25 * z
    half_width = max(0.45, 1.45 + 0.25 * z)
    channel = np.exp(-((x - center) / half_width) ** 2)
    return (0.12 + 0.055 * np.sin(x * 1.8 + z) + 0.045 * np.sin(z * 2.3 - x * 0.7)
            + 0.34 * max(0.0, -z / 5) + 0.14 * np.exp(-((x + 5) ** 2 / 5 + (z + 1) ** 2 / 4)) - 0.2 * channel)


def crab_parts(actor_index, sphere):
    """Stillwater's two crabs (scene.cpp build_animals), material 6."""
    shell = (0.48, 0.16, 0.065)
    parts = [((0, 0, 0), (0.38, 0.19, 0.3), 0, shell, 0, 0)]
    for side in (-1, 1):
        for leg in range(4):
            offset = -0.25 + leg * 0.16
            parts.append(((side * 0.39, -0.02, offset), (0.3, 0.033, 0.04), side * (0.5 - leg * 0.32), shell,
                          3, side + leg * 1.7))
            parts.append(((side * 0.61, -0.11, offset - 0.02), (0.12, 0.028, 0.035), side * 0.5, shell,
                          3, side + leg * 1.7))
        parts.append(((side * 0.38, 0.02, 0.34), (0.12, 0.10, 0.18), side * 0.3, (0.65, 0.24, 0.08), 0, 0))
        parts.append(((side * 0.14, 0.2, 0.19), (0.035, 0.1, 0.035), 0, shell, 0, 0))
        parts.append(((side * 0.14, 0.29, 0.2), (0.044, 0.04, 0.044), 0, (0.008, 0.015, 0.01), 0, 0))
    out = np.zeros(len(parts), ACTOR_PART)
    for i, (position, scale, yaw, color, joint, phase) in enumerate(parts):
        out[i]["m"] = affine(position, scale, yaw).ravel()
        out[i]["rgb"] = color
        out[i]["mesh"] = sphere
        out[i]["material"] = 6
        out[i]["actor"] = actor_index
        out[i]["anatomy"] = (joint, phase)
    return out


def texture(path: Path, normal_map: bool):
    image = Image.open(path).convert("RGB").resize((TEXTURE_SIZE, TEXTURE_SIZE), Image.Resampling.BOX)
    data = np.asarray(image, dtype=np.uint8)
    return struct.pack("<HHI", TEXTURE_SIZE, TEXTURE_SIZE, 1 if normal_map else 0) + data.tobytes()


def build(source: Path, output: Path, spacing: float):
    vertices, indices, instances, objects, batches, actors, digest = read_source(source / SOURCE_ARCHIVE)
    material = instances[:, 20].astype(int)
    report = {"source": f"stillwater@{SOURCE_COMMIT} {SOURCE_ARCHIVE}", "source_sha256": digest}
    writer = Writer()

    writer.section(TAG_CAMERA, 1)
    # Eye, target, vertical field of view (degrees), near, far, light direction.
    light = np.array([0, 0.9138, 0.4061])
    light /= np.linalg.norm(light)
    writer.raw(np.array([0, 4.65, 20.5, 0, 4.15, 0, 25.8, 0.1, 100, *light], dtype="<f4"))

    names = ["sand_01_diff", "sand_01_nor_gl", "rock_boulder_dry_diff", "rock_boulder_dry_nor_gl",
             "rough_wood_diff", "rough_wood_nor_gl"]
    writer.section(TAG_TEXTURE, len(names))
    for name in names:
        writer.raw(texture(source / "assets/materials" / f"{name}.jpg", name.endswith("nor_gl")))

    # Fixed scenery: one mesh per batch, decimated per category, with its instances.
    targets = {7: 0.33, 8: 0.26, 9: 0.30}
    static_meshes, static_instances, static_tris = [], [], 0
    sway = None
    fish_batches = []
    for batch_index, (first, count, first_instance, instance_count) in enumerate(batches):
        mat = int(material[first_instance])
        if mat == 10:
            sway = (first, count)
            continue
        if mat in (11, 12):
            fish_batches.append((first, count, first_instance, instance_count, mat))
            continue
        v, tri = mesh_slice(vertices, indices, int(first), int(count))
        if mat in targets:
            representative, tri = decimate(v[:, 0, :3], tri, max(64, int(len(tri) * targets[mat])))
            used = np.unique(tri)
            remap = np.full(len(representative), -1, dtype=np.int64)
            remap[used] = np.arange(len(used))
            v = v[representative[used]]
            tri = remap[tri]
        mesh_index = len(static_meshes)
        static_meshes.append((static_vertices(v), tri))
        static_tris += len(tri) * int(instance_count)
        for i in range(int(first_instance), int(first_instance + instance_count)):
            record = np.zeros(1, STATIC_INSTANCE)
            record["m"] = transform_rows(instances[i]).ravel()
            record["rgb"] = instances[i, 16:19]
            record["uv_scale"] = instances[i, 21:23]
            record["material"] = mat
            static_instances.append((mesh_index, record))
    writer.section(TAG_STATIC, len(static_meshes))
    for records, tri in static_meshes:
        write_mesh(writer, records, tri)
    writer.raw(struct.pack("<I", len(static_instances)))
    for mesh_index, record in static_instances:
        writer.raw(struct.pack("<I", mesh_index))
        writer.raw(record)
    report["static"] = {"meshes": len(static_meshes), "instances": len(static_instances),
                        "triangles": static_tris}

    report["sway"] = sway_section(writer, vertices, indices, instances, int(sway[0]), int(sway[1]), spacing)

    # Creatures: two decimated fish meshes, one sphere for the crabs.
    meshes, parts = [], []
    for first, count, first_instance, instance_count, mat in fish_batches:
        mesh_records, tri = actor_mesh(vertices, indices, int(first), int(count), 800 if mat == 11 else 400)
        mesh_index = len(meshes)
        meshes.append((mesh_records, tri))
        for i in range(int(first_instance), int(first_instance + instance_count)):
            record = np.zeros(1, ACTOR_PART)
            record["m"] = transform_rows(instances[i]).ravel()
            record["rgb"] = instances[i, 16:19]
            record["mesh"] = mesh_index
            record["material"] = mat
            record["actor"] = int(instances[i, 23])
            parts.append(record)
    sphere_records, sphere_tri = sphere_mesh()
    sphere_index = len(meshes)
    meshes.append((sphere_records, sphere_tri))
    actor_records = np.zeros(len(actors) + 2, ACTOR)
    actor_records["center"] = actors[:, 0:4].tolist() + [[0, 0, 0, 0]] * 2
    actor_records["cruise"] = actors[:, 4:8].tolist() + [[0, 0, 0, 0]] * 2
    for crab, (x, z) in enumerate(((-1.8, 0.9), (2.2, 0.5))):
        index = len(actors) + crab
        actor_records[index]["center"] = (x, ground_height(x, z) + 0.23, z, 0.8)
        actor_records[index]["cruise"] = (0.9, 0, 0.09, 0)
        actor_records[index]["kind"] = 1
        parts.append(crab_parts(index, sphere_index))
    writer.section(TAG_ACTOR_MESH, len(meshes))
    for records, tri in meshes:
        write_mesh(writer, records, tri)
    parts = np.concatenate(parts)
    writer.section(TAG_ACTOR_PART, len(parts))
    writer.raw(parts)
    writer.section(TAG_ACTOR, len(actor_records))
    writer.raw(actor_records)
    report["actors"] = {"fish": len(actors), "crabs": 2, "mesh_triangles": [len(t) for _, t in meshes],
                        "parts": len(parts)}

    # The air stone: thirty bubbles rising from one point (Stillwater scene.cpp).
    random = np.random.default_rng(270923)
    risers = np.zeros(30, RISER)
    for i in range(30):
        x, z = 7.6 + random.uniform(-0.32, 0.32), -1.8 + random.uniform(-0.3, 0.3)
        risers[i]["base"] = (x, ground_height(x, z), z)
        risers[i]["size"] = random.uniform(0.015, 0.048)
        risers[i]["phase"] = random.uniform(0, 10)
        risers[i]["speed"] = random.uniform(0.7, 1.5)
        risers[i]["height"] = 10
    writer.section(TAG_RISER, len(risers))
    writer.raw(risers)

    payload = writer.payload()
    compressed = zlib.compress(payload, 9)
    header = b"AMBSCN\0\1" + struct.pack("<IIII", 1, len(payload), len(compressed), zlib.crc32(payload)) + bytes(8)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(header + compressed)
    report["bytes"] = {"payload": len(payload), "file": len(header) + len(compressed)}
    report["output_sha256"] = hashlib.sha256(output.read_bytes()).hexdigest()
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE, help="Stillwater checkout")
    parser.add_argument("--output", type=Path, default=GAME / "assets/scene/riverscape.ambient")
    parser.add_argument("--spacing", type=float, default=0.42,
                        help="minimum distance along a foliage ribbon between kept rows (world units)")
    args = parser.parse_args()
    report = build(args.source, args.output, args.spacing)
    (HERE / "build_report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
