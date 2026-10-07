#!/usr/bin/env python3
"""Build Stillwater's other tanks: a coral reef and a river pool.

Authoring only, like build_scene.py, and the same pipeline: geometry is prepared
offline for the processor and written as ambient archives (`assets/scene/*.ambient`).
These tanks are laid out here, procedurally and deterministically, and borrow from
the planted tank's archive (`assets/scene/riverscape.ambient`, written by
build_scene.py) only what both can share: the camera, the sand floor and its
material maps, the boulder and driftwood meshes (Desktop Habitats' Riverscape, MIT,
decimated by build_scene.py), the fish meshes and the crab. Everything else (coral,
anemones, sea grass, sea whips, roots, leaf litter, eelgrass and every layout) is
made here. Needs NumPy only.

    python3 build_tanks.py            # writes reef.ambient and pool.ambient
    python3 build_tanks.py --check    # rebuilds in memory and compares with the files

The output is byte-identical for a given NumPy (the shipped files: NumPy 1.26 on the
M4 Mac mini); other versions may differ in the last bits of a float.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path

import numpy as np

from ambient_archive import (ACTOR, ACTOR_PART, PLACEMENT, RISER, STATIC_VERTEX, SWAY_VERTEX, Archive,
                             archive_bytes, read_archive)

HERE = Path(__file__).resolve().parent
GAME = HERE.parent
SCENES = GAME / "assets/scene"

# Material numbers (riverscape_look.hpp).
SAND, ROCK, WOOD, PEBBLE, CORAL, LEAF = 7, 8, 9, 14, 17, 18
FISH_BODY, FISH_FIN, CRAB = 11, 12, 6
# The current that sways foliage runs along +x, a little toward the glass (motion.cpp).
CURRENT = np.array([1.0, 0.0, 0.22]) / math.hypot(1.0, 0.22)
TOWARD_CAMERA = np.array([0.0, 0.0, 1.0])


def ground_height(x, z):
    """The planted tank's sand height (build_scene.py), which the shared floor follows."""
    center = 0.3 - 0.25 * z
    half_width = max(0.45, 1.45 + 0.25 * z)
    channel = math.exp(-((x - center) / half_width) ** 2)
    return (0.12 + 0.055 * math.sin(x * 1.8 + z) + 0.045 * math.sin(z * 2.3 - x * 0.7)
            + 0.34 * max(0.0, -z / 5) + 0.14 * math.exp(-((x + 5) ** 2 / 5 + (z + 1) ** 2 / 4)) - 0.2 * channel)


def snorm8(values):
    return np.clip(np.round(np.asarray(values) * 127), -127, 127).astype(np.int8)


def unorm8(values):
    return np.clip(np.round(np.asarray(values) * 255), 0, 255).astype(np.uint8)


def encode_albedo(values):
    return unorm8(np.sqrt(np.clip(values, 0, 1)))


def unit(v):
    v = np.asarray(v, dtype=np.float64)
    n = np.linalg.norm(v, axis=-1, keepdims=True)
    return v / np.maximum(n, 1e-9)


def srgb(hex_color: str):
    """A display colour as linear albedo."""
    c = np.array([int(hex_color[i:i + 2], 16) / 255 for i in (1, 3, 5)])
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def affine(position=(0, 0, 0), scale=(1, 1, 1), yaw=0.0, pitch=0.0, roll=0.0):
    """Row-major 3x4: R = Ry(yaw) Rx(pitch) Rz(roll), then scale, then translate."""
    cy, sy, cp, sp, cr, sr = (math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch),
                              math.cos(roll), math.sin(roll))
    ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    rx = np.array([[1, 0, 0], [0, cp, -sp], [0, sp, cp]])
    rz = np.array([[cr, -sr, 0], [sr, cr, 0], [0, 0, 1]])
    m = ry @ rx @ rz @ np.diag(scale)
    return np.hstack([m, np.array(position, dtype=np.float64).reshape(3, 1)]).astype(np.float32)


class Statics:
    """Fixed meshes and their placements."""

    def __init__(self, base: Archive):
        self.meshes = []
        self.placements = []
        self.base = base
        self.borrowed = {}
        self.rock_points = []
        self.rock_normals = []

    def top(self, x, z, reach=0.25):
        """The height of the rock (or sand) surface at x, z: something to set coral on."""
        y = ground_height(x, z)
        if self.rock_points:
            points = np.concatenate(self.rock_points)
            near = (np.abs(points[:, 0] - x) < reach) & (np.abs(points[:, 2] - z) < reach)
            if np.any(near):
                y = max(y, float(points[near, 1].max()))
        return y

    def borrow(self, index: int) -> int:
        """A mesh from the planted tank's archive, added once."""
        if index not in self.borrowed:
            self.borrowed[index] = len(self.meshes)
            self.meshes.append(self.base.static_meshes[index])
        return self.borrowed[index]

    def add_mesh(self, vertices, triangles) -> int:
        self.meshes.append((vertices, np.asarray(triangles, dtype=np.int64).reshape(-1, 3)))
        return len(self.meshes) - 1

    def place(self, mesh: int, transform, tint=(1, 1, 1), uv_scale=(1, 1), material=ROCK):
        if material == ROCK:
            m = np.asarray(transform, dtype=np.float64).reshape(3, 4)
            points = self.meshes[mesh][0]["p"].astype(np.float64)
            self.rock_points.append(points @ m[:, :3].T + m[:, 3])
            normals = self.meshes[mesh][0]["n"].astype(np.float64) / 127.0
            self.rock_normals.append(unit(normals @ np.linalg.inv(m[:, :3])))
        record = np.zeros(1, PLACEMENT)
        record["mesh"] = mesh
        record["m"] = np.asarray(transform, dtype=np.float32).ravel()
        record["rgb"] = tint
        record["uv_scale"] = uv_scale
        record["material"] = material
        self.placements.append(record)


def static_vertices(points, normals, rgb, uv=None, moss=0.0):
    out = np.zeros(len(points), STATIC_VERTEX)
    out["p"] = points
    out["n"] = snorm8(unit(normals))
    out["moss"] = unorm8(np.broadcast_to(moss, (len(points),)))
    if uv is not None:
        out["uv"] = uv
    out["rgb"] = encode_albedo(np.broadcast_to(rgb, (len(points), 3)))
    return out


class Builder:
    """Accumulates one fixed mesh in world space."""

    def __init__(self):
        self.points, self.normals, self.rgb, self.uv, self.triangles = [], [], [], [], []
        self.count = 0

    def add(self, points, normals, rgb, triangles, uv=None):
        points = np.asarray(points, dtype=np.float64)
        self.points.append(points)
        self.normals.append(np.asarray(normals, dtype=np.float64))
        self.rgb.append(np.broadcast_to(np.asarray(rgb, dtype=np.float64), (len(points), 3)))
        self.uv.append(np.zeros((len(points), 2)) if uv is None else np.asarray(uv, dtype=np.float64))
        self.triangles.append(np.asarray(triangles, dtype=np.int64) + self.count)
        self.count += len(points)

    def tube(self, path, radii, rgb, sides=6, uv_scale=1.0, cap=True):
        """A tube along a polyline; rgb may be one colour or one per path point."""
        path = np.asarray(path, dtype=np.float64)
        n = len(path)
        tangents = unit(np.gradient(path, axis=0))
        reference = np.array([0.0, 0.0, 1.0]) if abs(tangents[0][2]) < 0.9 else np.array([1.0, 0.0, 0.0])
        normal = unit(np.cross(tangents[0], reference))
        points, normals, colors, uv = [], [], [], []
        rgb = np.broadcast_to(np.asarray(rgb, dtype=np.float64), (n, 3))
        along = 0.0
        for i in range(n):
            if i > 0:
                # Parallel transport of the frame.
                normal = unit(normal - tangents[i] * np.dot(normal, tangents[i]))
                along += float(np.linalg.norm(path[i] - path[i - 1]))
            binormal = np.cross(tangents[i], normal)
            for s in range(sides + 1):
                a = 2 * math.pi * s / sides
                d = normal * math.cos(a) + binormal * math.sin(a)
                points.append(path[i] + d * radii[i])
                normals.append(d)
                colors.append(rgb[i])
                uv.append((s / sides * uv_scale, along * uv_scale))
        triangles = []
        for i in range(n - 1):
            for s in range(sides):
                a = i * (sides + 1) + s
                b = a + sides + 1
                triangles += [(a, a + 1, b), (a + 1, b + 1, b)]
        if cap:
            tip = len(points)
            points.append(path[-1] + tangents[-1] * radii[-1] * 0.8)
            normals.append(tangents[-1])
            colors.append(rgb[-1])
            uv.append((0.5, along * uv_scale))
            base = (n - 1) * (sides + 1)
            for s in range(sides):
                triangles.append((base + s, base + s + 1, tip))
        self.add(points, normals, colors, triangles, uv)

    def mesh(self, moss=0.0):
        points = np.concatenate(self.points)
        uv = np.concatenate(self.uv)
        vertices = static_vertices(points, np.concatenate(self.normals), np.concatenate(self.rgb), uv, moss)
        return vertices, np.concatenate(self.triangles)


def lathe_sphere(rings, segments, radius_of, color_of, cut=-1.0):
    """A sphere whose radius and colour depend on direction; rows with y below `cut` are dropped."""
    points, normals, colors = [], [], []
    rows = []
    for r in range(rings + 1):
        lat = math.pi * r / rings
        y = math.cos(lat)
        if y < cut and r > 0:
            break
        rows.append(r)
        for s in range(segments + 1):
            lon = 2 * math.pi * s / segments
            d = np.array([math.sin(lat) * math.cos(lon), y, math.sin(lat) * math.sin(lon)])
            points.append(d * radius_of(d))
            normals.append(d)
            colors.append(color_of(d))
    triangles = []
    for i in range(len(rows) - 1):
        for s in range(segments):
            a = i * (segments + 1) + s
            b = a + segments + 1
            triangles += [(a, a + 1, b), (a + 1, b + 1, b)]
    points = np.array(points)
    # Normals from the displaced surface, so grooves shade.
    tri = np.array(triangles)
    face = np.cross(points[tri[:, 1]] - points[tri[:, 0]], points[tri[:, 2]] - points[tri[:, 0]])
    accumulated = np.zeros_like(points)
    for k in range(3):
        np.add.at(accumulated, tri[:, k], face)
    shaded = unit(np.where(np.linalg.norm(accumulated, axis=1, keepdims=True) > 1e-12, accumulated, normals))
    # Keep outward: the face winding above runs inward on this lattice.
    flip = np.sum(shaded * np.array(normals), axis=1) < 0
    shaded[flip] *= -1
    return points, shaded, np.array(colors), tri


class Foliage:
    """Swaying ribbons: one world-space mesh, one root per plant."""

    def __init__(self):
        self.roots = []
        self.records = []
        self.triangles = []
        self.count = 0

    def root(self, position) -> int:
        self.roots.append(position)
        return len(self.roots) - 1

    def ribbon(self, root, path, widths, rgb, translucency=0.4, compliance=0.8, start=0.0, columns=3, face=None):
        """A ribbon along `path`, `widths` per point; `start` is its distance from the root at path[0]."""
        path = np.asarray(path, dtype=np.float64)
        n = len(path)
        tangents = unit(np.gradient(path, axis=0))
        rgb = np.broadcast_to(np.asarray(rgb, dtype=np.float64), (n, 3))
        records = np.zeros(n * columns, SWAY_VERTEX)
        distance = start
        k = 0
        for i in range(n):
            if i > 0:
                distance += float(np.linalg.norm(path[i] - path[i - 1]))
            facing = TOWARD_CAMERA if face is None else np.asarray(face, dtype=np.float64)
            across = unit(np.cross(tangents[i], facing))
            if np.linalg.norm(np.cross(tangents[i], facing)) < 1e-6:
                across = np.array([1.0, 0.0, 0.0])
            normal = unit(np.cross(across, tangents[i]))
            # The current pushes along itself, kept across the ribbon's axis.
            bend = CURRENT - tangents[i] * np.dot(CURRENT, tangents[i])
            bend = unit(bend) if np.linalg.norm(bend) > 1e-3 else CURRENT
            for c in range(columns):
                u = c / (columns - 1)
                r = records[k]
                r["p"] = path[i] + across * (u - 0.5) * widths[i]
                r["n"] = snorm8(normal)
                r["translucency"] = unorm8(translucency)
                r["uv"] = (int(round(u * 65535)), int(round(i / max(1, n - 1) * 65535)))
                r["rgb"] = encode_albedo(rgb[i])
                r["compliance"] = unorm8(compliance / 1.2)
                r["bend"] = snorm8(bend)
                r["along"] = snorm8(tangents[i])
                r["distance"] = distance
                r["root"] = root
                k += 1
        triangles = []
        for i in range(n - 1):
            for c in range(columns - 1):
                a = i * columns + c
                b = a + columns
                triangles += [(a, b, a + 1), (a + 1, b, b + 1)]
        self.records.append(records)
        self.triangles.append(np.array(triangles, dtype=np.int64) + self.count)
        self.count += len(records)

    def section(self):
        return (np.concatenate(self.records), np.concatenate(self.triangles))


def curve(start, direction, length, rows, droop=0.0, wobble=0.0, rng=None, lean=None):
    """Points along a gently bending stem."""
    start = np.asarray(start, dtype=np.float64)
    d = unit(direction)
    lean = np.zeros(3) if lean is None else np.asarray(lean, dtype=np.float64)
    points = [start]
    step = length / rows
    phase = 0.0 if rng is None else rng.uniform(0, 6.28)
    for i in range(1, rows + 1):
        t = i / rows
        d = unit(d + lean * step + np.array([0, -droop * step, 0])
                 + wobble * step * np.array([math.sin(phase + t * 5), 0, math.cos(phase + t * 4)]))
        points.append(points[-1] + d * step)
    return np.array(points)


def fish(base: Archive, actor: int, kind: int, depth=1.0, length=1.0):
    """A fish's two parts (body and fins) from the planted tank, as `kind` (1..3), deeper or longer."""
    parts = base.parts[base.parts["actor"] == 0].copy()
    for p in parts:
        m = p["m"].reshape(3, 4).copy()
        m[:, 0] *= length
        m[:, 1] *= depth
        p["m"] = m.ravel()
        p["rgb"] = (kind, 1, 1)
        p["actor"] = actor
    return parts


def crab(base: Archive, actor: int, shell):
    parts = base.parts[base.parts["actor"] == 16].copy()
    for p in parts:
        if tuple(np.round(p["rgb"], 3)) == (0.48, 0.16, 0.065):
            p["rgb"] = shell
        p["actor"] = actor
    return parts


def swimmer(x, y, z, scale, radius=1.3, vertical=0.16, speed=0.1, phase=0.0, kind=0):
    record = np.zeros(1, ACTOR)
    record["center"] = (x, y, z, scale)
    record["cruise"] = (radius, vertical, speed, phase)
    record["kind"] = kind
    return record


def finish(base: Archive, statics: Statics, foliage: Foliage, parts, actors, risers) -> Archive:
    archive = Archive(camera=base.camera.copy(), textures=list(base.textures))
    archive.static_meshes = statics.meshes
    archive.placements = np.concatenate(statics.placements)
    archive.sway_roots = np.asarray(foliage.roots, dtype=np.float32).reshape(-1, 3)
    archive.sway = foliage.section()
    archive.actor_meshes = list(base.actor_meshes)
    archive.parts = np.concatenate(parts)
    archive.actors = np.concatenate(actors)
    archive.risers = risers
    return archive


def boulder_pile(statics, rng, centre, count, spread, size, height, tint, flatten=0.75, moss=0.0):
    """Boulders heaped around a point: larger at the bottom, smaller on top."""
    for k in range(count):
        level = k / max(1, count - 1)
        x = centre[0] + rng.uniform(-spread, spread) * (1 - 0.6 * level)
        z = centre[1] + rng.uniform(-spread, spread) * 0.6 * (1 - 0.6 * level)
        s = size * rng.uniform(0.7, 1.15) * (1 - 0.45 * level)
        y = ground_height(x, z) + level * height + s * flatten * 0.35
        mesh = statics.borrow(int(rng.integers(1, 11)))
        statics.place(mesh, affine((x, y, z), (s, s * flatten, s), rng.uniform(0, 6.28), rng.uniform(-0.2, 0.2)),
                      tint, (1.8, 1.4), ROCK)


# ---------------------------------------------------------------------- the reef

class RockMap:
    """The highest rock (or sand) under each 0.1-unit cell: where coral can sit."""

    cell = 0.1
    x0, x1, z0, z1 = -14.0, 14.0, -11.0, 7.0

    def __init__(self, statics: Statics):
        nx = int(round((self.x1 - self.x0) / self.cell))
        nz = int(round((self.z1 - self.z0) / self.cell))
        xs = self.x0 + (np.arange(nx) + 0.5) * self.cell
        zs = self.z0 + (np.arange(nz) + 0.5) * self.cell
        self.height = np.array([[ground_height(x, z) for x in xs] for z in zs])
        self.sand = self.height.copy()
        points = np.concatenate(statics.rock_points)
        ix = np.clip(((points[:, 0] - self.x0) / self.cell).astype(int), 0, nx - 1)
        iz = np.clip(((points[:, 2] - self.z0) / self.cell).astype(int), 0, nz - 1)
        np.maximum.at(self.height, (iz, ix), points[:, 1])

    def _index(self, x, z):
        ix = int(np.clip((x - self.x0) / self.cell, 0, self.height.shape[1] - 1))
        iz = int(np.clip((z - self.z0) / self.cell, 0, self.height.shape[0] - 1))
        return iz, ix

    def top(self, x, z, reach=0.15):
        iz, ix = self._index(x, z)
        k = max(1, int(round(reach / self.cell)))
        return float(self.height[max(0, iz - k):iz + k + 1, max(0, ix - k):ix + k + 1].max())

    def rock_above_sand(self, x, z):
        iz, ix = self._index(x, z)
        return float(self.height[iz, ix] - self.sand[iz, ix])

    def normal(self, x, z, step=0.3):
        dx = self.top(x + step, z) - self.top(x - step, z)
        dz = self.top(x, z + step) - self.top(x, z - step)
        return unit(np.array([-dx, 2 * step, -dz]))


def grid_triangles(rows, cols, wrap=False):
    """Two triangles per quad of a rows x cols lattice of points (row-major)."""
    triangles = []
    span = cols if wrap else cols - 1
    for i in range(rows - 1):
        for j in range(span):
            a = i * cols + j
            b = i * cols + (j + 1) % cols
            c = a + cols
            d = b + cols
            triangles += [(a, c, b), (b, c, d)]
    return triangles


def smooth_normals(points, triangles, outward=None):
    """Vertex normals from the faces; flipped toward `outward` (per vertex) where given."""
    points = np.asarray(points, dtype=np.float64)
    tri = np.asarray(triangles, dtype=np.int64)
    face = np.cross(points[tri[:, 1]] - points[tri[:, 0]], points[tri[:, 2]] - points[tri[:, 0]])
    accumulated = np.zeros_like(points)
    for k in range(3):
        np.add.at(accumulated, tri[:, k], face)
    normals = unit(accumulated)
    if outward is not None:
        flip = np.sum(normals * np.asarray(outward), axis=1) < 0
        normals[flip] *= -1
    return normals


def speckle(rng, colours, amount=0.12):
    """Per-vertex brightness variation: the grain of polyps over a colony."""
    colours = np.asarray(colours, dtype=np.float64)
    return colours * rng.uniform(1 - amount, 1 + amount, (len(colours), 1))


def brain_coral(builder, rng, centre, radius, ridge, valley, squash=0.72):
    """A dome of meandering ridges and valleys."""
    phase = rng.uniform(0, 6.3, 4)
    lump = rng.uniform(0.04, 0.09)

    def meander(d):
        lon = math.atan2(d[2], d[0])
        lat = math.acos(max(-1.0, min(1.0, d[1])))
        return math.sin(17 * lat + 2.6 * math.sin(5 * lon + 3 * lat + phase[0]) +
                        1.7 * math.sin(7 * lat - 4 * lon + phase[1]))

    def r_of(d):
        lon = math.atan2(d[2], d[0])
        lat = math.acos(max(-1.0, min(1.0, d[1])))
        shape = 1 + lump * math.sin(3 * lon + phase[2]) * math.sin(2 * lat + phase[3])
        return radius * shape * (1 + 0.05 * meander(d))

    def c_of(d):
        m = meander(d)
        t = min(1.0, max(0.0, (m + 0.2) / 0.9))
        return valley + (ridge - valley) * t * t * (3 - 2 * t)

    rings = 20 if radius > 0.42 else 12
    points, normals, colours, tri = lathe_sphere(rings, rings * 2, r_of, c_of, cut=-0.3)
    points[:, 1] *= squash
    normals = unit(normals * np.array([1, 1 / squash, 1]))
    builder.add(points + np.asarray(centre), normals, colours, tri)


def table_coral(builder, rng, base, radius, stalk, colour, rim, tilt=None):
    """A table (plate) coral: a broad, slightly dished plate on a short stalk."""
    base = np.asarray(base, dtype=np.float64)
    top = base + np.array([0, stalk, 0])
    builder.tube([base, base + np.array([0, stalk * 0.5, 0]), top], [radius * 0.13, radius * 0.1, radius * 0.08],
                 colour * 0.55, sides=7, cap=False)
    tilt = np.zeros(3) if tilt is None else np.asarray(tilt, dtype=np.float64)
    phase = rng.uniform(0, 6.3, 3)
    rings, segments = 7, 32
    upper, lower, colours_up, colours_down = [], [], [], []
    for i in range(rings + 1):
        t = i / rings
        for j in range(segments):
            a = 2 * math.pi * j / segments
            edge = radius * (1 + 0.12 * math.sin(5 * a + phase[0]) + 0.06 * math.sin(9 * a + phase[1]))
            r = edge * t
            x, z = math.cos(a) * r, math.sin(a) * r
            lift = 0.22 * radius * t * t + 0.03 * radius * math.sin(3 * a + phase[2]) * t + x * tilt[0] + z * tilt[2]
            thickness = 0.035 + 0.045 * (1 - t)
            upper.append(top + np.array([x, 0.04 + lift, z]))
            lower.append(top + np.array([x, 0.04 + lift - thickness, z]))
            growing = colour * (1 - t ** 4) + rim * t ** 4
            colours_up.append(growing)
            colours_down.append(colour * 0.7 * (1 - t ** 4) + rim * 0.8 * t ** 4)
    cols = segments
    tri_up = grid_triangles(rings + 1, cols, wrap=True)
    count = len(upper)
    tri_down = [(a + count, c + count, b + count) for (a, b, c) in tri_up]
    # The rim joins the last rows of the two surfaces.
    last = rings * cols
    rim_tri = []
    for j in range(cols):
        a, b = last + j, last + (j + 1) % cols
        rim_tri += [(a, b, a + count), (b, b + count, a + count)]
    points = np.array(upper + lower)
    triangles = tri_up + tri_down + rim_tri
    up = np.tile([0.0, 1.0, 0.0], (count, 1))
    outward = np.concatenate([up, -up])
    normals = smooth_normals(points, triangles, outward)
    colours = speckle(rng, np.array(colours_up + colours_down), 0.2)
    builder.add(points, normals, colours, triangles)


def thicket(builder, rng, rocks, centre, radius, height, colour, tip, branch_radius=0.07):
    """Staghorn or bushy Acropora: a dense clump of short, thick, forking branches."""
    count = int(10 + 26 * radius)
    for _ in range(count):
        a = rng.uniform(0, 2 * math.pi)
        d = radius * math.sqrt(rng.uniform(0, 1))
        x, z = centre[0] + math.cos(a) * d, centre[1] + math.sin(a) * d
        y = rocks.top(x, z, 0.1) - 0.05
        outward = np.array([math.cos(a), 0, math.sin(a)]) * (0.3 + 0.7 * d / max(radius, 1e-6))
        direction = unit(outward * 0.8 + np.array([0, 1.0, 0]) + rng.normal(0, 0.15, 3))
        length = height * rng.uniform(0.55, 1.0) * (1 - 0.35 * d / max(radius, 1e-6))
        r0 = branch_radius * rng.uniform(0.85, 1.15)
        _branch(builder, rng, np.array([x, y, z]), direction, length, r0, colour, tip, 2)


def _branch(builder, rng, start, direction, length, r0, colour, tip, levels):
    rows = 3
    path = curve(start, direction, length, rows, wobble=0.35, rng=rng)
    t = np.linspace(0, 1, rows + 1)[:, None]
    end_tip = levels == 1
    shade = colour * (1 - t * 0.35) + tip * t * 0.35 if not end_tip else colour * (1 - t ** 2) + tip * t ** 2
    builder.tube(path, np.linspace(r0, r0 * 0.78, rows + 1), shade, sides=5, cap=True)
    if levels <= 1:
        return
    for _ in range(int(rng.integers(1, 3))):
        k = int(rng.integers(1, rows))
        heading = unit(path[k + 1] - path[k])
        side = unit(np.cross(heading, rng.normal(0, 1, 3)))
        child = unit(heading + side * rng.uniform(0.5, 0.9) + np.array([0, 0.25, 0]))
        _branch(builder, rng, path[k], child, length * rng.uniform(0.45, 0.65), r0 * 0.8, colour, tip, levels - 1)


def finger_coral(builder, rng, rocks, centre, radius, height, colour, tip):
    """Finger or pillar coral: upright, blunt, thick fingers."""
    for _ in range(int(7 + 18 * radius)):
        a = rng.uniform(0, 2 * math.pi)
        d = radius * math.sqrt(rng.uniform(0, 1))
        x, z = centre[0] + math.cos(a) * d, centre[1] + math.sin(a) * d
        y = rocks.top(x, z, 0.1) - 0.05
        lean = np.array([math.cos(a), 0, math.sin(a)]) * 0.25 * d / max(radius, 1e-6)
        length = height * rng.uniform(0.5, 1.0)
        path = curve((x, y, z), lean + np.array([0, 1.0, 0]), length, 3, wobble=0.15, rng=rng)
        t = np.linspace(0, 1, 4)[:, None]
        r = rng.uniform(0.07, 0.11)
        builder.tube(path, np.full(4, r), colour * (1 - t * 0.25) + tip * t * 0.25, sides=6, cap=True)


def plate_whorl(builder, rng, centre, colour, rim, plates=4, size=0.6):
    """Montipora-like plates: thin, tilted, wavy-edged plates stacked in a loose spiral."""
    centre = np.asarray(centre, dtype=np.float64)
    start = rng.uniform(0, 2 * math.pi)
    for k in range(plates):
        heading = start + k * rng.uniform(1.6, 2.4)
        radius = size * rng.uniform(0.6, 1.0)
        origin = centre + np.array([0, k * size * 0.28, 0])
        span = rng.uniform(2.2, 3.6)
        out = np.array([math.cos(heading), 0, math.sin(heading)])
        rings, segments = 4, 12
        top, bottom, colours = [], [], []
        for i in range(rings + 1):
            t = i / rings
            for j in range(segments + 1):
                a = heading - span / 2 + span * j / segments
                edge = radius * (1 + 0.1 * math.sin(6 * a + k))
                r = 0.08 + edge * t
                p = origin + np.array([math.cos(a) * r, 0, math.sin(a) * r])
                # Tilted up toward the light along the plate's outward direction.
                p[1] += 0.35 * np.dot(p - origin, out) * t + 0.05 * math.sin(5 * a) * t
                top.append(p + np.array([0, 0.025, 0]))
                bottom.append(p - np.array([0, 0.025, 0]))
                colours.append(colour * (1 - t ** 2) + rim * t ** 2)
        cols = segments + 1
        tri = grid_triangles(rings + 1, cols)
        n = len(top)
        tri_b = [(a + n, c + n, b + n) for (a, b, c) in tri]
        points = np.array(top + bottom)
        up = np.tile([0.0, 1.0, 0.0], (n, 1))
        normals = smooth_normals(points, tri + tri_b, np.concatenate([up, -up]))
        shade = np.concatenate([np.array(colours), np.array(colours) * 0.5])
        builder.add(points, normals, speckle(rng, shade, 0.08), tri + tri_b)


def leather_coral(builder, rng, base, radius, colour, rim):
    """A toadstool leather coral: a thick stalk under a broad cap with a folded edge."""
    base = np.asarray(base, dtype=np.float64)
    stalk = radius * rng.uniform(0.7, 1.0)
    builder.tube([base, base + np.array([0, stalk * 0.6, 0]), base + np.array([0, stalk, 0])],
                 [radius * 0.38, radius * 0.32, radius * 0.4], colour * 0.85, sides=8, cap=False)
    centre = base + np.array([0, stalk, 0])
    rings, segments = 6, 28
    folds = int(rng.integers(5, 8))
    phase = rng.uniform(0, 6.3)
    top, bottom, colours = [], [], []
    for i in range(rings + 1):
        t = i / rings
        for j in range(segments):
            a = 2 * math.pi * j / segments
            r = radius * t * (1 + 0.08 * math.sin(folds * a + phase))
            y = radius * 0.18 * (1 - t * t) + radius * 0.12 * math.sin(folds * a + phase) * t ** 3
            top.append(centre + np.array([math.cos(a) * r, y, math.sin(a) * r]))
            bottom.append(centre + np.array([math.cos(a) * r * 0.92, y - radius * 0.12 * (1 - 0.5 * t) - 0.02,
                                             math.sin(a) * r * 0.92]))
            colours.append(colour * (1 - t ** 3) + rim * t ** 3)
    tri = grid_triangles(rings + 1, segments, wrap=True)
    n = len(top)
    tri_b = [(a + n, c + n, b + n) for (a, b, c) in tri]
    last = rings * segments
    edge = []
    for j in range(segments):
        a, b = last + j, last + (j + 1) % segments
        edge += [(a, b, a + n), (b, b + n, a + n)]
    points = np.array(top + bottom)
    up = np.tile([0.0, 1.0, 0.0], (n, 1))
    normals = smooth_normals(points, tri + tri_b + edge, np.concatenate([up, -up]))
    shade = np.concatenate([np.array(colours), np.array(colours) * 0.7])
    builder.add(points, normals, speckle(rng, shade, 0.15), tri + tri_b + edge)


def disc_corals(builder, rng, rocks, centre, spread, count, colour, mouth):
    """Mushroom corals: a scatter of small flat discs on the rock, each with its mouth."""
    for _ in range(count):
        x = centre[0] + rng.normal(0, spread)
        z = centre[1] + rng.normal(0, spread * 0.7)
        y = rocks.top(x, z, 0.08)
        size = rng.uniform(0.1, 0.19)
        tint = colour * rng.uniform(0.8, 1.15)

        def c_of(d, tint=tint):
            centre_weight = max(0.0, d[1]) ** 8
            return tint * (1 - centre_weight) + mouth * centre_weight

        points, normals, colours, tri = lathe_sphere(4, 10, lambda d, s=size: s, c_of, cut=0.15)
        points[:, 1] *= 0.3
        normal = rocks.normal(x, z)
        builder.add(points + np.array([x, y - 0.02, z]) + normal * 0.01, unit(normals + normal * 0.3), colours, tri)


def tube_sponge(builder, rng, rocks, centre, colour, inside, tubes=4, height=1.0):
    """Tube sponges: open-topped tubes, darker within."""
    for _ in range(tubes):
        x = centre[0] + rng.normal(0, 0.18)
        z = centre[1] + rng.normal(0, 0.14)
        y = rocks.top(x, z, 0.08) - 0.05
        r = rng.uniform(0.11, 0.17)
        length = height * rng.uniform(0.55, 1.0)
        path = curve((x, y, z), (rng.normal(0, 0.12), 1.0, rng.normal(0, 0.12)), length, 4, wobble=0.1, rng=rng)
        radii = np.linspace(r * 0.85, r * 1.1, 5)
        t = np.linspace(0, 1, 5)[:, None]
        builder.tube(path, radii, colour * (0.75 + 0.35 * t), sides=9, cap=False)
        inner = Builder()
        inner.tube(path[1:], radii[1:] * 0.78, inside, sides=9, cap=False)
        points, normals, colours, uv, triangles = (inner.points[0], -inner.normals[0], inner.rgb[0], inner.uv[0],
                                                   inner.triangles[0][:, ::-1])
        builder.add(points, normals, colours, triangles, uv)
        # The lip, from the outer wall to the inner one.
        sides = 9
        lip_points, lip_normals = [], []
        for radius_scale in (1.0, 0.78):
            for s in range(sides + 1):
                a = 2 * math.pi * s / sides
                lip_points.append(path[-1] + np.array([math.cos(a), 0, math.sin(a)]) * radii[-1] * radius_scale
                                  + np.array([0, 0.01, 0]))
                lip_normals.append((0.0, 1.0, 0.0))
        lip = []
        for s in range(sides):
            a, b = s, s + 1
            lip += [(a, a + sides + 1, b), (b, a + sides + 1, b + sides + 1)]
        builder.add(lip_points, lip_normals, colour * 1.05, lip)


def zoanthids(builder, rng, rocks, centre, spread, count, colour, eye):
    """A mat of small colonial polyps: each a short dome with a bright centre."""
    for _ in range(count):
        x = centre[0] + rng.normal(0, spread)
        z = centre[1] + rng.normal(0, spread * 0.7)
        y = rocks.top(x, z, 0.06)
        size = rng.uniform(0.045, 0.07)

        def c_of(d):
            ring = max(0.0, d[1]) ** 6
            return colour * (1 - ring) + eye * ring

        points, normals, colours, tri = lathe_sphere(3, 7, lambda d, s=size: s, c_of, cut=-0.1)
        points[:, 1] *= 0.7
        builder.add(points + np.array([x, y, z]), normals, colours, tri)


def sea_fan(builder, rng, base, height, colour, facing_yaw):
    """A gorgonian: a fan of fine lace, open between its branches, across the current."""
    base = np.asarray(base, dtype=np.float64)
    right = np.array([math.cos(facing_yaw), 0, -math.sin(facing_yaw)])
    up = np.array([0.0, 1.0, 0.0])
    facing = np.cross(right, up)
    rows, cols = 24, 48
    phase = rng.uniform(0, 6.3, 4)
    points, colours = [], []
    for i in range(rows + 1):
        t = i / rows
        for j in range(cols + 1):
            a = -1.05 + 2.1 * j / cols
            reach = height * (0.1 + 0.9 * t) * (1 + 0.1 * math.sin(3 * a + phase[0]))
            bow = 0.1 * height * math.sin(a * 1.4 + phase[1]) * t
            points.append(base + right * math.sin(a) * reach * 0.85 + up * math.cos(a) * reach + facing * bow)
            colours.append(colour * (0.8 + 0.35 * t) * rng.uniform(0.75, 1.15))
    cols1 = cols + 1
    # The lace: small openings scattered through it, more of them toward the rim.
    front = []
    for i in range(rows):
        for j in range(cols):
            if rng.uniform() < 0.1 + 0.4 * (i / rows):
                continue
            a = i * cols1 + j
            front += [(a, a + cols1, a + 1), (a + 1, a + cols1, a + cols1 + 1)]
    count = len(points)
    back = [(a + count, c + count, b + count) for (a, b, c) in front]
    all_points = np.array(points + [q - facing * 0.012 for q in points])
    normals = np.concatenate([np.tile(facing, (count, 1)), np.tile(-facing, (count, 1))])
    builder.add(all_points, normals, np.array(colours + colours), front + back)
    for a in (-0.7, -0.25, 0.2, 0.62):
        a += rng.uniform(-0.1, 0.1)
        path = [base + right * math.sin(a) * height * k * 0.85 * 0.8 + up * math.cos(a) * height * k * 0.8
                for k in np.linspace(0, 1, 5)]
        builder.tube(path, np.linspace(0.04, 0.018, 5), colour * 0.8, sides=4, cap=True)


def anemone(statics_builder, foliage, rng, base, radius, column, tentacle, tip, count=46, length=0.5, bulb=1.0):
    """An anemone: a column under a crown of swaying tentacles, swollen toward their tips."""
    base = np.asarray(base, dtype=np.float64)
    top = base + np.array([0, radius * 0.7, 0])
    statics_builder.tube([base, base + np.array([0, radius * 0.4, 0]), top],
                         [radius * 0.85, radius * 0.9, radius * 0.95], column, sides=10, cap=True)
    root = foliage.root(tuple(top))
    for k in range(count):
        a = 2 * math.pi * (k * 0.618034 % 1.0)
        ring = radius * math.sqrt(rng.uniform(0.1, 1.0)) * 0.95
        start = top + np.array([math.cos(a) * ring, 0.02, math.sin(a) * ring])
        outward = np.array([math.cos(a), 0, math.sin(a)])
        direction = unit(outward * (0.45 + ring / radius) + np.array([0, 1.0, 0]))
        rows = 5
        path = curve(start, direction, length * rng.uniform(0.7, 1.15), rows, droop=-0.2, wobble=0.5, rng=rng,
                     lean=outward * 0.7)
        t = np.linspace(0, 1, rows + 1)
        colours = tentacle * (1 - t[:, None] ** 2) + tip * t[:, None] ** 2
        widths = 0.06 + 0.05 * bulb * np.sin(np.pi * np.clip(t * 1.15 - 0.15, 0, 1)) ** 2
        widths[-1] *= 0.6
        foliage.ribbon(root, path, widths, colours, translucency=0.6, compliance=1.1, start=0.25)


def encrust(builder, rng, rocks, palette, count):
    """Fills the rock: small coral heads, polyp mats, discs and clumps on every upward face."""
    taken = []
    names = list(palette)
    tries = 0
    placed = 0
    while placed < count and tries < count * 12:
        tries += 1
        x, z = rng.uniform(-12.5, 12.5), rng.uniform(-8.5, 0.6)
        if rocks.rock_above_sand(x, z) < 0.3:
            continue
        normal = rocks.normal(x, z)
        if normal[1] < 0.5:
            continue
        size = rng.uniform(0.18, 0.4)
        if any((x - tx) ** 2 + (z - tz) ** 2 < (size + ts) ** 2 * 0.8 for tx, tz, ts in taken):
            continue
        taken.append((x, z, size))
        placed += 1
        y = rocks.top(x, z, 0.08)
        colour, tip = palette[names[int(rng.integers(0, len(names)))]]
        kind = rng.uniform()
        if kind < 0.22:
            brain_coral(builder, rng, (x, y - size * 0.3, z), size, tip * 0.8, colour * 0.5)
        elif kind < 0.42:
            zoanthids(builder, rng, rocks, (x, z), size * 0.5, 18, colour, tip)
        elif kind < 0.58:
            thicket(builder, rng, rocks, (x, z), size, size * 1.6, colour, tip, 0.055)
        elif kind < 0.7:
            finger_coral(builder, rng, rocks, (x, z), size * 0.6, size * 1.4, colour, tip)
        elif kind < 0.82:
            disc_corals(builder, rng, rocks, (x, z), size * 0.5, 5, colour * 1.1, tip)
        else:
            plate_whorl(builder, rng, (x, y - 0.05, z), colour, tip, 2, size)


def encrust_faces(builder, rng, statics, palette, count):
    """Coral on the rock's faces toward the glass: polyp mats, discs, small heads and shelves."""
    points = np.concatenate(statics.rock_points)
    normals = np.concatenate(statics.rock_normals)
    facing = np.flatnonzero((normals[:, 2] > 0.4) & (normals[:, 1] > -0.3) & (points[:, 1] > 0.4))
    names = list(palette)
    taken = []
    for index in rng.permutation(facing):
        if len(taken) >= count:
            break
        p, n = points[index], normals[index]
        size = rng.uniform(0.16, 0.32)
        if any(np.sum((p - q) ** 2) < (size + r) ** 2 for q, r in taken):
            continue
        taken.append((p, size))
        colour, tip = palette[names[int(rng.integers(0, len(names)))]]
        kind = rng.uniform()
        if kind < 0.3:
            # A mat of polyps spread over the face.
            for _ in range(14):
                offset = unit(np.cross(n, rng.normal(0, 1, 3))) * rng.uniform(0, size)
                q = p + offset + n * 0.02
                r = rng.uniform(0.04, 0.06)
                pts, nrm, col, tri = lathe_sphere(3, 7, lambda d, r=r: r, lambda d: polyp(colour, tip, d, 6))
                builder.add(orient(pts, n) + q, orient(nrm, n), col, tri)
        elif kind < 0.55:
            pts, nrm, col, tri = lathe_sphere(8, 14, lambda d: size, lambda d: colour * (0.7 + 0.3 * d[1]), cut=0.0)
            pts[:, 1] *= 0.55
            builder.add(orient(pts, n) + p - n * size * 0.1, orient(unit(nrm * np.array([1, 1 / 0.55, 1])), n),
                        speckle(rng, col, 0.15), tri)
        elif kind < 0.75:
            # A shelf of plate coral jutting from the face.
            plate_whorl(builder, rng, p + n * 0.05 - np.array([0, 0.05, 0]), colour, tip, 1, size * 1.2)
        else:
            for _ in range(5):
                offset = unit(np.cross(n, rng.normal(0, 1, 3))) * rng.uniform(0, size)
                r = rng.uniform(0.09, 0.15)
                pts, nrm, col, tri = lathe_sphere(4, 10, lambda d, r=r: r, lambda d: polyp(colour, tip, d, 8),
                                                  cut=0.15)
                pts[:, 1] *= 0.3
                builder.add(orient(pts, n) + p + offset + n * 0.01, orient(nrm, n), col, tri)


def polyp(colour, centre, d, sharpness):
    """A polyp's colour toward its top: its own centre colour in a small spot."""
    spot = max(0.0, d[1]) ** sharpness
    return colour * (1 - spot) + centre * spot


def orient(vectors, normal):
    """Turns vectors so that +y points along `normal`."""
    y = unit(normal)
    x = unit(np.cross(np.array([0.0, 0.0, 1.0]) if abs(y[2]) < 0.9 else np.array([1.0, 0.0, 0.0]), y))
    z = np.cross(x, y)
    return np.asarray(vectors) @ np.array([x, y, z])


def rubble_mesh(rng):
    """A broken piece of branching coral, bleached: the reef's rubble."""
    builder = Builder()
    path = curve((-0.5, 0, 0), (1, 0.1, 0), 1.0, 3, wobble=0.4, rng=rng)
    builder.tube(path, np.linspace(0.13, 0.1, 4), np.array([1.0, 1.0, 1.0]), sides=5, cap=True)
    fork = curve(path[1], (0.6, 0.15, 0.7), 0.55, 2, wobble=0.3, rng=rng)
    builder.tube(fork, np.linspace(0.1, 0.08, 3), np.array([1.0, 1.0, 1.0]), sides=5, cap=True)
    points = np.concatenate(builder.points)
    vertices = static_vertices(points, np.concatenate(builder.normals), np.concatenate(builder.rgb),
                               np.concatenate(builder.uv))
    return vertices, np.concatenate(builder.triangles)


REEF_LAYOUT_SEED = 31415


def build_reef(base: Archive) -> Archive:
    rng = np.random.default_rng(REEF_LAYOUT_SEED)
    statics = Statics(base)
    # The floor: white coral sand.
    statics.place(statics.borrow(0), affine(), (1.0, 0.97, 0.9), (10, 6), SAND)
    rock = (0.36, 0.27, 0.31)
    # Live rock: a wall along the back, rising toward the sides; a bommie to the left, a
    # taller one to the right and a low outcrop behind the middle. A channel of sand runs
    # from the glass to the wall between them.
    for x in np.linspace(-12, 12, 13):
        lift = 1.2 + 2.6 * (abs(x) / 12) ** 1.4
        boulder_pile(statics, rng, (x + rng.uniform(-0.4, 0.4), -7.6 + rng.uniform(-0.5, 0.5)), 3, 1.0, 2.0,
                     lift, rock, 0.7)
    for x in (-8.5, -4.2, 4.8, 8.8):
        boulder_pile(statics, rng, (x, -5.6 + rng.uniform(-0.4, 0.4)), 2, 0.8, 1.6, 0.6, rock, 0.65)
    boulder_pile(statics, rng, (-6.3, -1.4), 5, 1.0, 1.5, 1.3, rock, 0.75)
    boulder_pile(statics, rng, (6.0, -2.4), 6, 1.1, 1.6, 1.8, rock, 0.75)
    boulder_pile(statics, rng, (1.6, -4.4), 2, 0.5, 1.2, 0.5, rock, 0.65)
    rocks = RockMap(statics)

    coral = Builder()
    foliage = Foliage()
    c = srgb
    # Mostly the browns, tans, olives and creams of living coral, with the reef's
    # brighter accents in the tips, the soft corals and the anemones.
    palette = {
        "tan": (c("#b08a5a"), c("#f0dcb0")), "brown": (c("#7a5a3a"), c("#d8b890")),
        "olive": (c("#6f7a3e"), c("#c8d890")), "purple": (c("#6c4a8c"), c("#d8c0ff")),
        "blue": (c("#4f6f9a"), c("#b8e0ff")), "pink": (c("#b0607a"), c("#ffd0e0")),
        "cream": (c("#c8b890"), c("#fff4d8")), "green": (c("#4f8a5a"), c("#b8f0b0")),
        "rust": (c("#a05a3a"), c("#ffc8a0")),
    }

    # Table corals: broad plates standing out from the wall and the bommies, at several heights.
    for x, z, radius, stalk, name in ((-7.4, -6.2, 1.25, 0.35, "cream"), (3.2, -6.8, 1.45, 0.4, "tan"),
                                      (6.4, -2.6, 1.05, 0.3, "olive"), (-5.4, -1.8, 0.9, 0.25, "cream"),
                                      (1.6, -4.4, 1.15, 0.45, "tan"), (9.6, -6.6, 1.1, 0.3, "cream"),
                                      (-10.2, -7.0, 1.0, 0.3, "olive")):
        colour, rim = palette[name]
        # Tipped a little toward the glass, so their tops show from the low viewpoint.
        table_coral(coral, rng, (x, rocks.top(x, z, 0.2) - 0.05, z), radius, stalk, colour, rim,
                    tilt=rng.normal(0, 0.05, 3) + np.array([0, 0, -0.16]))
    # Staghorn and bushy thickets.
    for x, z, radius, height, name in ((-8.8, -6.0, 1.0, 1.0, "purple"), (-2.6, -7.0, 1.1, 1.1, "tan"),
                                       (0.2, -6.6, 0.8, 0.9, "blue"), (5.2, -6.6, 0.9, 1.0, "cream"),
                                       (-6.9, -0.8, 0.7, 0.8, "pink"), (7.1, -1.8, 0.7, 0.9, "purple"),
                                       (5.0, -1.7, 0.6, 0.7, "tan"), (10.4, -5.4, 0.9, 1.0, "blue"),
                                       (-11.0, -5.6, 0.9, 1.0, "tan"), (-4.0, -5.4, 0.6, 0.8, "rust")):
        colour, tip = palette[name]
        thicket(coral, rng, rocks, (x, z), radius, height, colour, tip)
    # Finger corals.
    for x, z, radius, height, name in ((-5.6, -0.6, 0.45, 0.6, "cream"), (7.4, -3.0, 0.5, 0.7, "brown"),
                                       (-1.0, -6.4, 0.5, 0.75, "purple"), (8.2, -6.2, 0.5, 0.7, "olive"),
                                       (-9.6, -6.8, 0.45, 0.6, "pink")):
        colour, tip = palette[name]
        finger_coral(coral, rng, rocks, (x, z), radius, height, colour, tip)
    # Brain corals: on the rock and in the sand.
    for x, z, r, ridge, valley in ((-2.8, 1.4, 0.55, "#b8b070", "#4a4a24"), (3.6, 0.8, 0.45, "#c89c6a", "#58401e"),
                                   (-7.6, 0.8, 0.6, "#8aa898", "#2e4038"), (8.0, 0.2, 0.55, "#c79c86", "#5a3a2e"),
                                   (-4.8, -3.4, 0.5, "#a8a8c8", "#3c3c58"), (4.2, -4.2, 0.45, "#b8b070", "#4a4a24"),
                                   (-1.2, -5.2, 0.4, "#c8a070", "#584020"), (6.8, -0.6, 0.35, "#a0b080", "#405020")):
        y = rocks.top(x, z, r * 0.6)
        brain_coral(coral, rng, (x, y - r * 0.2, z), r, c(ridge), c(valley))
    # Plates, leathers, discs, zoanthids and sponges in the gaps.
    for x, z, name, plates, size in ((-6.0, -2.2, "brown", 4, 0.55), (5.6, -3.4, "rust", 5, 0.6),
                                     (-3.2, -6.0, "olive", 4, 0.6), (7.6, -5.2, "brown", 4, 0.55)):
        colour, rim = palette[name]
        plate_whorl(coral, rng, (x, rocks.top(x, z, 0.3) - 0.05, z), colour, rim, plates, size)
    for x, z, radius, name in ((-4.6, -0.9, 0.55, "cream"), (4.4, -2.6, 0.6, "olive"), (-0.6, -5.8, 0.5, "cream"),
                               (9.2, -4.6, 0.6, "tan")):
        colour, rim = palette[name]
        leather_coral(coral, rng, (x, rocks.top(x, z, 0.2) - 0.05, z), radius, colour, rim)
    for x, z, name, mouth in ((-7.0, -2.0, "#a02838", "#ffd040"), (6.6, -3.6, "#2a8a7a", "#e0ff90"),
                              (-1.8, -6.2, "#6a2a8a", "#ff90d0"), (2.8, -6.2, "#a02838", "#ffd040")):
        disc_corals(coral, rng, rocks, (x, z), 0.35, 12, c(name), c(mouth))
    for x, z, colour, eye in ((-5.0, -1.6, "#3f9a4a", "#ffb030"), (5.4, -1.2, "#d0602a", "#60ff90"),
                              (0.8, -5.8, "#3f7aa0", "#a0ffe0"), (-8.4, -5.4, "#d0602a", "#ffe060")):
        zoanthids(coral, rng, rocks, (x, z), 0.25, 70, c(colour), c(eye))
    for x, z, colour, inside, height in ((8.4, -2.4, "#c8601a", "#4a1a08", 1.2),
                                         (-9.2, -4.8, "#7a3a9a", "#2a0e3a", 1.3),
                                         (2.4, -5.2, "#c8a01a", "#4a3808", 0.9)):
        tube_sponge(coral, rng, rocks, (x, z), c(colour), c(inside), 4, height)
    # Sea fans at the back, standing across the current.
    for x, z, h, colour, yaw in ((-5.6, -8.2, 1.9, "#8a2a5a", 0.2), (7.4, -7.8, 1.7, "#b8602a", -0.3),
                                 (0.6, -8.6, 1.5, "#6a3a8a", 0.05)):
        sea_fan(coral, rng, (x, rocks.top(x, z, 0.3) - 0.1, z), h, c(colour), yaw)
    # Everything else that grows on the rock: small heads, mats and clumps wherever the
    # rock faces up and nothing larger stands.
    encrust(coral, rng, rocks, palette, 260)
    encrust_faces(coral, rng, statics, palette, 260)
    # Anemones on each bommie and one in the sand, where the clownfish live.
    anemones = ((-6.1, -0.2, 0.42, "#a8556a", "#f0a0a8", "#ffd0f0"), (6.0, -1.2, 0.4, "#8a6a3a", "#e6c27a", "#ffa070"),
                (2.4, 2.0, 0.36, "#3d6b3f", "#9fe08a", "#f08ac0"))
    for x, z, r, column, tentacle, tip in anemones:
        anemone(coral, foliage, rng, (x, rocks.top(x, z, 0.3) - 0.08, z), r, c(column), c(tentacle), c(tip), 90,
                0.42, 1.0)
    statics.place(statics.add_mesh(*coral.mesh()), affine(), (1, 1, 1), (1, 1), CORAL)

    # Rubble along the channel's edges and at the foot of the rock; shell grit over the sand.
    rubble = statics.add_mesh(*rubble_mesh(rng))
    for _ in range(170):
        side = rng.choice([-1, 1])
        x = side * rng.uniform(1.8, 4.2) + 0.4
        z = rng.uniform(-5, 3)
        if abs(x - 0.4) < 1.3 and abs(z - 1.0) < 1.0:
            continue
        s = rng.uniform(0.12, 0.26)
        shade = rng.choice([(0.85, 0.82, 0.78), (0.75, 0.7, 0.66), (0.8, 0.62, 0.62), (0.62, 0.6, 0.55)])
        statics.place(rubble, affine((x, ground_height(x, z) + s * 0.05, z), (s, s, s), rng.uniform(0, 6.3),
                                     rng.uniform(-0.3, 0.3), rng.uniform(-0.3, 0.3)), shade, (1, 1), PEBBLE)
    pebble = statics.borrow(12)
    for _ in range(220):
        x, z = rng.uniform(-11, 11), rng.uniform(-4, 7)
        if abs(x - 0.4) < 1.3 and abs(z - 1.0) < 1.0:
            continue
        s = rng.uniform(0.03, 0.08)
        shade = rng.choice([(0.95, 0.9, 0.85), (0.9, 0.7, 0.7), (0.8, 0.78, 0.7), (0.6, 0.55, 0.5)])
        statics.place(pebble, affine((x, ground_height(x, z) + s * 0.3, z), (s, s * 0.6, s), rng.uniform(0, 6.3)),
                      shade, (1, 1), PEBBLE)
    # Sea grass in the sand at the sides, and sea whips rising from the wall.
    grass_tuft(foliage, rng, (-9.6, 2.8), 50, 1.4, c("#4e7a2a"), c("#a9c96a"), 0.07, 0.7)
    grass_tuft(foliage, rng, (9.4, 2.4), 44, 1.3, c("#4e7a2a"), c("#a9c96a"), 0.07, 0.7)
    for x, z, colour, length in ((-3.6, -7.8, c("#e0a030"), 2.6), (9.0, -7.0, c("#d0603a"), 2.4)):
        base_y = rocks.top(x, z, 0.3) - 0.1
        for k in range(4):
            root = foliage.root((x, base_y, z))
            lean = np.array([rng.normal(0, 0.25), 1.0, rng.normal(0, 0.15)])
            l = length * rng.uniform(0.6, 1.05)
            rows = int(l * 3)
            path = curve((x + rng.normal(0, 0.15), base_y, z + rng.normal(0, 0.1)), lean, l, rows, wobble=0.2, rng=rng)
            foliage.ribbon(root, path, np.full(rows + 1, 0.09), colour, translucency=0.3, compliance=0.5)

    parts, actors = [], []
    meshes = list(base.actor_meshes)
    chromis = reshaped_fish(base, meshes, outline(0.125, 0.31, 0.01, 0.7), tail_fork=1.1, tail_length=1.15)
    anthias = reshaped_fish(base, meshes, outline(0.11, 0.33, 0.0, 0.7), tail_fork=1.9, tail_length=1.35,
                            dorsal=(1.6, 1.3))
    yellow_tang = reshaped_fish(base, meshes, outline(0.22, 0.29, -0.01, 0.55), tail_fork=0.2, tail_length=0.95,
                                dorsal=(5.0, 0.55), anal=(2.6, 0.6), snout=1.12)
    blue_tang = reshaped_fish(base, meshes, outline(0.16, 0.31, -0.01, 0.6), tail_fork=0.6, tail_length=1.0,
                              dorsal=(5.0, 0.5), anal=(2.4, 0.55))
    clown = reshaped_fish(base, meshes, outline(0.105, 0.31, 0.02, 0.7), tail_fork=-0.4, tail_length=0.8,
                          dorsal=(2.4, 0.9))

    def school(count, centre, spread, scale, radius, vertical, speed, phase, shape, kind):
        for _ in range(count):
            offset = rng.normal(0, 1, 3) * np.array(spread)
            actors.append(swimmer(centre[0] + offset[0], centre[1] + offset[1], centre[2] + offset[2],
                                  scale * rng.uniform(0.9, 1.1), radius, vertical, speed, phase + rng.normal(0, 0.12)))
            parts.append(fish_parts(base, len(actors) - 1, shape, kind))

    # Green chromis hanging in a cloud over the left of the reef; a school of lyretail
    # anthias over the right; a few yellow and blue tangs grazing along it; clownfish at
    # their anemones. Kinds 1..5 (TankStyle::fish).
    school(13, (-2.6, 3.6, -1.2), (1.0, 0.55, 0.8), 0.62, 1.4, 0.22, 0.11, 0.4, chromis, 4)
    school(10, (4.4, 4.4, -2.6), (0.9, 0.5, 0.7), 0.66, 1.1, 0.2, 0.13, 2.1, anthias, 5)
    for k in range(3):
        actors.append(swimmer(rng.uniform(-5, 5), rng.uniform(2.4, 3.8), rng.uniform(-3.5, -1.0),
                              rng.uniform(0.95, 1.05), 2.4, 0.3, 0.07 + 0.006 * k, 1 + k * 2.1))
        parts.append(fish_parts(base, len(actors) - 1, yellow_tang, 3))
    for k in range(2):
        actors.append(swimmer(rng.uniform(-4, 4), rng.uniform(3.0, 4.6), rng.uniform(-2.5, 0.5),
                              rng.uniform(1.0, 1.1), 2.0, 0.3, 0.075 + 0.008 * k, 4 + k * 2.5))
        parts.append(fish_parts(base, len(actors) - 1, blue_tang, 1))
    for (x, z) in ((-6.1, 0.0), (-5.9, -0.3), (6.0, -1.0), (2.4, 2.2)):
        y = rocks.top(x, z, 0.3) + 0.45
        actors.append(swimmer(x, y, z, rng.uniform(0.62, 0.7), 0.4, 0.07, 0.22 + rng.uniform(0, 0.05),
                              rng.uniform(0, 6.3)))
        parts.append(fish_parts(base, len(actors) - 1, clown, 2))
    # A red hermit crab on the sand.
    x, z = 1.9, 2.9
    actors.append(swimmer(x, ground_height(x, z) + 0.23, z, 0.75, 0.9, 0.0, 0.08, 1.0, kind=1))
    parts.append(crab(base, len(actors) - 1, (0.55, 0.09, 0.05)))
    archive = finish(base, statics, foliage, parts, actors, base.risers.copy())
    archive.actor_meshes = meshes
    return archive


# The tetra's body runs nose +x, its mid-line near this height; its half-height along
# it, measured from the mesh and smoothed.
FISH_MIDLINE = -0.009
TETRA_X = np.array([-0.44, -0.3, -0.24, -0.18, -0.12, -0.06, 0.0, 0.06, 0.12, 0.18, 0.24, 0.3, 0.36])
TETRA_HALF = np.array([0.03, 0.035, 0.045, 0.062, 0.078, 0.086, 0.089, 0.087, 0.084, 0.08, 0.068, 0.044, 0.02])


def tetra_half(x):
    return np.interp(x, TETRA_X, TETRA_HALF)


def outline(height, length, centre=0.0, power=0.6):
    """A body outline: half-height along x, a rounded disc or oval `length` either side of `centre`."""
    def half(x):
        t = np.clip(1 - ((np.asarray(x) - centre) / length) ** 2, 0, 1)
        return height * t ** power
    return half


def reshaped_fish(base, meshes, half, tail_fork=0.0, tail_length=1.0, dorsal=(1.0, 1.0), anal=(1.0, 1.0),
                  snout=1.0):
    """New body and fin meshes from the tetra's, appended to `meshes`; returns their indices.

    The body is made at least as deep as the outline `half(x)` (half-heights along it), and
    the fins attached to it follow. The dorsal and anal fins are stretched along the back
    and belly by `stretch` and made taller or lower by `lift`, (stretch, lift); the tail is
    lengthened, forked (positive) or rounded (negative)."""
    shaped = []
    for index in (0, 1):
        vertices, triangles = base.actor_meshes[index]
        v = vertices.copy()
        p = v["p"].astype(np.float64)
        n = v["n"].astype(np.float64) / 127.0
        part = v["part"]
        if index == 1:
            for fin, (stretch, lift), root, seat in ((2, dorsal, 0.069, 0.85), (3, anal, -0.044, -0.6)):
                mask = part == fin
                centre = 0.5 * (p[mask, 0].min() + p[mask, 0].max())
                p[mask, 0] = centre + (p[mask, 0] - centre) * stretch
                # Re-seated on the body's outline where the stretch has taken it.
                p[mask, 1] = FISH_MIDLINE + seat * tetra_half(p[mask, 0]) + (p[mask, 1] - root) * lift
            tail = part == 1
            start = -0.27
            behind = np.minimum(p[:, 0] - start, 0)
            spread = np.abs(p[:, 1] - FISH_MIDLINE) / 0.09
            p[tail, 0] = start + behind[tail] * tail_length * (1 + tail_fork * (spread[tail] ** 2 - 0.35))
        if snout != 1.0:
            ahead = np.maximum(p[:, 0] - 0.2, 0)
            p[:, 0] += ahead * (snout - 1)
        d = np.clip(half(p[:, 0]) / tetra_half(p[:, 0]), 1.0, 4.0)
        p[:, 1] = FISH_MIDLINE + (p[:, 1] - FISH_MIDLINE) * d
        n[:, 1] = n[:, 1] / d
        v["p"] = p.astype(np.float32)
        v["n"] = snorm8(unit(n))
        meshes.append((v, triangles))
        shaped.append(len(meshes) - 1)
    return tuple(shaped)


def fish_parts(base: Archive, actor: int, shape, kind: int):
    """A fish's body and fins on the reshaped meshes `shape`, coloured as `kind` (1..6)."""
    parts = base.parts[base.parts["actor"] == 0].copy()
    for p, mesh in zip(parts, shape):
        p["mesh"] = mesh
        p["rgb"] = (kind, 1, 1)
        p["actor"] = actor
    return parts


def grass_tuft(foliage, rng, centre, blades, length, colour, tip, width=0.07, spread=0.5, lean=0.25,
               translucency=0.5, compliance=0.7, rows_per_unit=2.8):
    for _ in range(blades):
        x = centre[0] + rng.normal(0, spread)
        z = centre[1] + rng.normal(0, spread * 0.6)
        y = ground_height(x, z) - 0.03
        root = foliage.root((x, y, z))
        l = length * rng.uniform(0.55, 1.15)
        rows = max(3, int(l * rows_per_unit))
        tilt = rng.normal(0, lean, 3)
        tilt[1] = 1.0
        path = curve((x, y, z), tilt, l, rows, droop=0.08, wobble=0.25, rng=rng)
        t = np.linspace(0, 1, rows + 1)[:, None]
        colours = colour * (1 - t) + tip * t
        widths = width * np.minimum(1.0, (1 - np.linspace(0, 1, rows + 1)) * 4 + 0.15)
        yaw = rng.uniform(0, math.pi)
        face = np.array([math.sin(yaw), 0.0, math.cos(yaw)])
        foliage.ribbon(root, path, widths, colours, translucency=translucency, compliance=compliance, face=face)


# ---------------------------------------------------------------------- the pool

def leaf_shape(builder, rng, centre, size, colour):
    """A fallen leaf, curled a little, lying on the bed."""
    yaw = rng.uniform(0, 2 * math.pi)
    forward = np.array([math.cos(yaw), 0, math.sin(yaw)])
    side = np.array([-math.sin(yaw), 0, math.cos(yaw)])
    curl = rng.uniform(0.0, 0.25)
    points, normals = [], []
    rows = 4
    for i in range(rows + 1):
        t = i / rows
        half = math.sin(math.pi * t) * 0.42 * size
        for c in (-1, 0, 1):
            lift = curl * size * abs(c) + 0.02
            points.append(centre + forward * (t - 0.5) * size + side * c * half + np.array([0, lift, 0]))
            normals.append(unit(np.array([0, 1.0, 0]) - side * c * curl))
    triangles = []
    for i in range(rows):
        for c in range(2):
            a = i * 3 + c
            b = a + 3
            triangles += [(a, b, a + 1), (a + 1, b, b + 1)]
    tint = colour * rng.uniform(0.7, 1.15)
    builder.add(points, normals, tint, triangles)


def build_pool(base: Archive) -> Archive:
    rng = np.random.default_rng(27182)
    statics = Statics(base)
    # The bed: dark silt over sand.
    statics.place(statics.borrow(0), affine(), (0.55, 0.45, 0.33), (10, 6), SAND)
    # Rounded river stones, washed smooth: flattened boulders, half buried.
    for _ in range(16):
        x, z = rng.uniform(-10, 10), rng.uniform(-6, 2.5)
        s_ = rng.uniform(0.45, 1.6) * (1.0 if z < -2 else 0.7)
        mesh = statics.borrow(int(rng.integers(1, 11)))
        statics.place(mesh, affine((x, ground_height(x, z) + s_ * 0.1, z), (s_, s_ * 0.5, s_ * 0.85),
                                   rng.uniform(0, 6.3)), (0.42, 0.38, 0.3), (1.8, 1.4), ROCK)
    # Gravel.
    pebble = statics.borrow(12)
    for _ in range(3200):
        x, z = rng.uniform(-11, 11), rng.uniform(-6, 7)
        s = rng.uniform(0.04, 0.12)
        shade = rng.choice([(0.27, 0.24, 0.2), (0.18, 0.17, 0.14), (0.32, 0.27, 0.2), (0.15, 0.13, 0.12)])
        statics.place(pebble, affine((x, ground_height(x, z) + s * 0.25, z), (s, s * 0.6, s * 0.8), rng.uniform(0, 6.3)),
                      shade, (1, 1), PEBBLE)
    # The far bank: a slope of stones and silt rising out of the murk behind the bed, so the
    # pool has a back rather than a horizon. A generator of its own keeps the rest as it was.
    bank = np.random.default_rng(16180)
    for x in np.linspace(-12, 12, 11):
        lift = 1.0 + 1.4 * bank.uniform() + 1.2 * (abs(x) / 12) ** 2
        boulder_pile(statics, bank, (x + bank.uniform(-0.6, 0.6), -9.0 + bank.uniform(-0.6, 0.6)), 3, 1.2, 2.2, lift,
                     (0.3, 0.27, 0.21), 0.55)
    wood = Builder()
    # Roots reaching down from the bank on the left and over the back.
    for k in range(9):
        x0 = -11.5 + rng.uniform(-0.5, 0.8) + k * 0.45
        top = np.array([x0, 10.5, -3.5 + rng.uniform(-1.5, 1.0)])
        path = [top]
        d = unit(np.array([rng.uniform(0.2, 0.6), -1.0, rng.uniform(-0.1, 0.3)]))
        while path[-1][1] > ground_height(path[-1][0], path[-1][2]) + 0.1 and len(path) < 40:
            d = unit(d + rng.normal(0, 0.18, 3) + np.array([0.03, -0.05, 0]))
            path.append(path[-1] + d * 0.4)
        n = len(path)
        radii = np.linspace(rng.uniform(0.16, 0.26), 0.04, n)
        wood.tube(np.array(path), radii, np.array([0.8, 0.75, 0.68]), sides=7, uv_scale=1.6)
        # Fine rootlets off each root.
        for j in range(2, n - 2, 4):
            start = path[j]
            twig = curve(start, rng.normal(0, 1, 3) + np.array([0, -0.6, 0]), rng.uniform(0.6, 1.4), 4, droop=0.3,
                         wobble=0.8, rng=rng)
            wood.tube(twig, np.linspace(radii[j] * 0.45, 0.012, 5), np.array([0.75, 0.7, 0.62]), sides=4, uv_scale=1.6)
    # Sunken branches lying across the bed, one propped on a stone.
    for start, direction, length, radius in (((-7.5, 0.25, 1.6), (1.0, 0.06, -0.35), 6.5, 0.24),
                                             ((2.0, 0.2, 2.8), (1.0, 0.18, -0.25), 5.0, 0.2),
                                             ((-2.5, 0.3, -3.5), (1.0, 0.0, 0.15), 5.5, 0.2)):
        path = curve(start, direction, length, 14, wobble=0.18, rng=rng)
        for p in path:
            p[1] = max(p[1], ground_height(p[0], p[2]) + radius * 0.6)
        radii = np.linspace(radius, radius * 0.4, len(path))
        wood.tube(path, radii, np.array([0.7, 0.62, 0.52]), sides=8, uv_scale=1.2)
        for j in (3, 6, 9, 12):
            twig = curve(path[j], (rng.uniform(-0.6, 0.6), 1.0, rng.uniform(-0.6, 0.3)), rng.uniform(0.9, 2.0), 5,
                         wobble=0.6, rng=rng)
            wood.tube(twig, np.linspace(radii[j] * 0.45, 0.015, 6), np.array([0.7, 0.62, 0.52]), sides=5,
                      uv_scale=1.2)
    statics.place(statics.add_mesh(*wood.mesh(moss=0.25)), affine(), (0.5, 0.42, 0.33), (1, 1), WOOD)
    # Leaf litter.
    leaves = Builder()
    palette = [srgb("#6b4a26"), srgb("#8a5a2a"), srgb("#4f4128"), srgb("#a0672e"), srgb("#5e5a2c")]
    for _ in range(520):
        x, z = rng.uniform(-11, 11), rng.uniform(-6, 6.5)
        leaf_shape(leaves, rng, np.array([x, ground_height(x, z), z]), rng.uniform(0.25, 0.5),
                   palette[int(rng.integers(0, len(palette)))])
    statics.place(statics.add_mesh(*leaves.mesh()), affine(), (1, 1, 1), (1, 1), LEAF)

    foliage = Foliage()
    # Eelgrass in long clumps at the back and to the right, a few in front.
    for centre, blades, length in (((-7.5, -4.5), 70, 7.0), ((-2.5, -5.5), 60, 8.0), ((4.0, -5.0), 70, 7.5),
                                   ((8.8, -2.0), 55, 6.0), ((7.4, 2.8), 30, 3.2), ((-8.6, 3.2), 26, 3.0)):
        grass_tuft(foliage, rng, centre, blades, length, srgb("#3d5a1e"), srgb("#8fa64a"), width=0.11, spread=0.7,
                   lean=0.12, translucency=0.75, compliance=0.9, rows_per_unit=2.2)

    parts, actors = [], []
    # A shoal of minnows, a few rudd, and darters along the bottom: kinds 1, 2, 3.
    # The minnows keep together: one loop, each a little off the shoal's centre.
    shoal = np.random.default_rng(14142)
    for k in range(11):
        offset = shoal.normal(0, 1, 3) * np.array([1.1, 0.5, 0.8])
        actors.append(swimmer(-0.5 + offset[0], 4.2 + offset[1], 0.4 + offset[2], shoal.uniform(1.1, 1.3), 2.6, 0.3,
                              0.1, 0.7 + shoal.normal(0, 0.1)))
        parts.append(fish(base, len(actors) - 1, 1, depth=1.05, length=1.1))
    for k in range(4):
        actors.append(swimmer(rng.uniform(-4, 4), rng.uniform(3.0, 5.0), rng.uniform(-2.0, 1.0),
                              rng.uniform(1.6, 1.85), 2.0, 0.3, 0.06 + 0.005 * k, 1.3 + k * 1.7))
        parts.append(fish(base, len(actors) - 1, 2, depth=1.3, length=1.0))
    for k in range(3):
        x, z = rng.uniform(-4, 4), rng.uniform(0.5, 2.5)
        actors.append(swimmer(x, ground_height(x, z) + 0.45, z, 1.0, 1.0, 0.04, 0.07, k * 2.0))
        parts.append(fish(base, len(actors) - 1, 3, depth=0.9, length=1.05))
    # A crayfish-coloured crab under the branch.
    x, z = -1.8, 1.8
    actors.append(swimmer(x, ground_height(x, z) + 0.23, z, 0.8, 0.8, 0.0, 0.07, 2.0, kind=1))
    parts.append(crab(base, len(actors) - 1, (0.24, 0.17, 0.08)))
    # Gas from the silt: a few slow bubbles, now and then.
    risers = np.zeros(8, RISER)
    for i in range(8):
        x, z = rng.uniform(-6, 6), rng.uniform(-3, 2)
        risers[i]["base"] = (x, ground_height(x, z), z)
        risers[i]["size"] = rng.uniform(0.02, 0.04)
        risers[i]["phase"] = rng.uniform(0, 40)
        risers[i]["speed"] = rng.uniform(0.5, 0.8)
        risers[i]["height"] = rng.uniform(25, 40)
    return finish(base, statics, foliage, parts, actors, risers)


TANKS = {"reef": build_reef, "pool": build_pool}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--base", type=Path, default=SCENES / "riverscape.ambient")
    parser.add_argument("--check", action="store_true", help="compare with the files instead of writing")
    parser.add_argument("tanks", nargs="*", default=list(TANKS))
    args = parser.parse_args()
    base = read_archive(args.base)
    report_path = HERE / "tanks_report.json"
    report = json.loads(report_path.read_text()) if report_path.exists() else {}
    failed = False
    for name in args.tanks:
        archive = TANKS[name](base)
        data = archive_bytes(archive)
        path = SCENES / f"{name}.ambient"
        if args.check:
            same = path.exists() and path.read_bytes() == data
            print(f"{name}: {'matches' if same else 'DIFFERS'}")
            failed = failed or not same
            continue
        path.write_bytes(data)
        static_triangles = sum(len(archive.static_meshes[p["mesh"]][1]) for p in archive.placements)
        report[name] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
                        "static_triangles": int(static_triangles), "placements": len(archive.placements),
                        "sway_triangles": len(archive.sway[1]), "sway_roots": len(archive.sway_roots),
                        "creatures": len(archive.actors), "risers": len(archive.risers),
                        "base_sha256": hashlib.sha256(args.base.read_bytes()).hexdigest()}
        print(name, json.dumps(report[name]))
    if not args.check:
        report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    raise SystemExit(1 if failed else 0)


if __name__ == "__main__":
    main()
