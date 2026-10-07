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

def staghorn(builder, rng, base, colour, tip, height=1.0, depth=3, spread=0.5, radius=0.05):
    def grow(start, direction, length, r, level):
        rows = 4
        path = curve(start, direction, length, rows, wobble=0.6, rng=rng)
        t = np.linspace(0, 1, rows + 1)[:, None]
        last = level == depth
        colours = colour * (1 - t * (0.4 if last else 0.15)) + tip * t * (0.4 if last else 0.15)
        builder.tube(path, np.linspace(r, r * 0.72, rows + 1), colours, sides=5, cap=last)
        if last:
            return
        end = path[-1]
        heading = unit(path[-1] - path[-2])
        for _ in range(int(rng.integers(2, 4))):
            twist = rng.normal(0, 1, 3)
            twist[1] = abs(twist[1]) * 0.3
            child = unit(heading + unit(twist) * spread)
            grow(end, child, length * rng.uniform(0.62, 0.8), r * 0.72, level + 1)

    for _ in range(int(rng.integers(3, 5))):
        lean = rng.normal(0, 0.35, 3)
        lean[1] = 1.0
        grow(np.asarray(base, dtype=np.float64), lean, height * 0.42 * rng.uniform(0.8, 1.2), radius, 1)


def brain_coral(builder, centre, radius, ridge, valley, squash=0.7):
    def r_of(d):
        lon = math.atan2(d[2], d[0])
        lat = math.acos(max(-1.0, min(1.0, d[1])))
        meander = math.sin(14 * lat + 2.2 * math.sin(5 * lon) + 1.4 * math.sin(3 * lat * 2 + lon * 7))
        return radius * (1 + 0.035 * meander)

    def c_of(d):
        lon = math.atan2(d[2], d[0])
        lat = math.acos(max(-1.0, min(1.0, d[1])))
        meander = math.sin(14 * lat + 2.2 * math.sin(5 * lon) + 1.4 * math.sin(3 * lat * 2 + lon * 7))
        return valley + (ridge - valley) * (0.5 + 0.5 * meander)

    points, normals, colours, tri = lathe_sphere(14, 28, r_of, c_of, cut=-0.25)
    points[:, 1] *= squash
    normals = unit(normals * np.array([1, 1 / squash, 1]))
    builder.add(points + np.asarray(centre), normals, colours, tri)


def sea_fan(builder, rng, base, height, colour, facing_yaw):
    """A gorgonian: a flat, fine branching fan."""
    right = np.array([math.cos(facing_yaw), 0, -math.sin(facing_yaw)])

    def grow(start, angle, length, r, level):
        direction = unit(right * math.sin(angle) + np.array([0, math.cos(angle), 0]))
        path = curve(start, direction, length, 3, wobble=0.3, rng=rng)
        builder.tube(path, np.linspace(r, r * 0.8, 4), colour, sides=4, cap=level == 5)
        if level == 5:
            return
        for side in (-1, 1):
            grow(path[-1], angle + side * rng.uniform(0.18, 0.42), length * rng.uniform(0.7, 0.86), r * 0.8, level + 1)

    grow(np.asarray(base, dtype=np.float64), 0.0, height * 0.3, 0.035, 1)


def anemone(statics_builder, foliage, rng, base, radius, column, tentacle, tip, count=46, length=0.5):
    base = np.asarray(base, dtype=np.float64)
    top = base + np.array([0, radius * 0.9, 0])
    statics_builder.tube([base, base + np.array([0, radius * 0.5, 0]), top], [radius * 0.9, radius * 0.95, radius * 0.85],
                         column, sides=10, cap=True)
    root = foliage.root(tuple(top))
    for k in range(count):
        a = 2 * math.pi * (k * 0.618034 % 1.0)
        ring = radius * math.sqrt(rng.uniform(0.15, 1.0)) * 0.8
        start = top + np.array([math.cos(a) * ring, 0.02, math.sin(a) * ring])
        outward = np.array([math.cos(a), 0, math.sin(a)])
        direction = unit(outward * (0.35 + ring / radius) + np.array([0, 1.0, 0]))
        rows = 5
        path = curve(start, direction, length * rng.uniform(0.75, 1.15), rows, droop=-0.2, wobble=0.5, rng=rng,
                     lean=outward * 0.6)
        t = np.linspace(0, 1, rows + 1)[:, None]
        colours = tentacle * (1 - t ** 2) + tip * t ** 2
        widths = np.linspace(0.055, 0.03, rows + 1)
        foliage.ribbon(root, path, widths, colours, translucency=0.6, compliance=1.1, start=0.25)


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


def polyp_colony(builder, rng, centre, radius, colour, count=40):
    """Soft coral or zoanthids: a mound of small round polyps."""
    for _ in range(count):
        a = rng.uniform(0, 2 * math.pi)
        r = radius * math.sqrt(rng.uniform(0, 1))
        p = np.asarray(centre) + np.array([math.cos(a) * r, (radius - r) * 0.5, math.sin(a) * r])
        size = rng.uniform(0.05, 0.1)
        points, normals, colours, tri = lathe_sphere(4, 6, lambda d: size, lambda d: colour * (0.7 + 0.3 * d[1]))
        builder.add(points + p, normals, colours, tri)


def build_reef(base: Archive) -> Archive:
    rng = np.random.default_rng(31415)
    statics = Statics(base)
    # The floor: white coral sand.
    statics.place(statics.borrow(0), affine(), (1.0, 0.97, 0.9), (10, 6), SAND)
    rock = (0.34, 0.27, 0.29)
    # The reef behind: a low ridge of live rock, higher at the ends, and two outcrops
    # in front of it to either side; the middle stays open water over the sand.
    for x in np.linspace(-11, 11, 14):
        lift = 0.5 + 1.6 * (abs(x) / 11) ** 1.5
        boulder_pile(statics, rng, (x + rng.uniform(-0.5, 0.5), -6.4 + rng.uniform(-0.6, 0.6)), 3, 1.1, 1.9,
                     lift, rock, 0.6)
    boulder_pile(statics, rng, (-5.6, -0.6), 5, 1.0, 1.35, 0.9, rock, 0.7)
    boulder_pile(statics, rng, (5.8, -1.2), 5, 1.1, 1.45, 1.1, rock, 0.7)
    boulder_pile(statics, rng, (-2.4, -3.6), 2, 0.6, 1.0, 0.4, rock, 0.6)
    boulder_pile(statics, rng, (2.6, -4.2), 2, 0.6, 1.1, 0.5, rock, 0.6)
    # Shell grit on the sand, kept clear of the chest.
    pebble = statics.borrow(12)
    for _ in range(900):
        x, z = rng.uniform(-11, 11), rng.uniform(-3, 7)
        if abs(x - 0.4) < 1.3 and abs(z - 1.0) < 1.0:
            continue
        s = rng.uniform(0.03, 0.09)
        shade = rng.choice([(0.95, 0.9, 0.85), (0.9, 0.7, 0.7), (0.8, 0.78, 0.7), (0.6, 0.55, 0.5)])
        statics.place(pebble, affine((x, ground_height(x, z) + s * 0.3, z), (s, s * 0.6, s), rng.uniform(0, 6.3)),
                      shade, (1, 1), PEBBLE)

    coral = Builder()
    foliage = Foliage()
    purple, gold, pink, blue, rust = (srgb("#8a66d8"), srgb("#d8ac4a"), srgb("#d877b0"), srgb("#6fa6e0"),
                                      srgb("#cf7a4e"))
    tips = {id(purple): srgb("#e4d8ff"), id(gold): srgb("#fbecb0"), id(pink): srgb("#ffd8f0"),
            id(blue): srgb("#e0f2ff"), id(rust): srgb("#ffd0a8")}
    # Staghorn and branching corals on the rock: on the outcrops, along the ridge.
    stag = [(-6.2, -0.4, purple, 1.9), (-5.0, -0.9, gold, 1.5), (-4.6, 0.0, rust, 1.2),
            (5.2, -1.0, pink, 1.8), (6.4, -1.5, blue, 1.7), (6.0, -0.2, gold, 1.2),
            (-9.2, -6.0, gold, 2.2), (-7.0, -6.4, purple, 2.4), (-4.2, -6.2, blue, 1.9), (-1.6, -6.6, rust, 1.8),
            (1.2, -6.2, purple, 1.9), (3.8, -6.6, gold, 2.1), (6.6, -6.0, pink, 2.3), (9.0, -6.4, blue, 2.2),
            (-2.4, -3.6, pink, 1.4), (2.6, -4.2, blue, 1.5)]
    for x, z, colour, height in stag:
        staghorn(coral, rng, (x, statics.top(x, z) - 0.05, z), colour, tips[id(colour)], height, radius=0.075)
    # Brain corals: domes in the sand and on the rock.
    for x, z, r, ridge, valley in ((-2.6, 1.6, 0.55, "#b5c27a", "#4c5a2a"), (3.4, 0.6, 0.45, "#d0ac66", "#5c4320"),
                                   (-7.4, 0.6, 0.6, "#94b2ae", "#3a4f4a"), (7.6, 0.4, 0.55, "#c79c86", "#5a3a2e"),
                                   (-5.6, -2.0, 0.5, "#a8b0d0", "#3c4060")):
        brain_coral(coral, (x, min(statics.top(x, z, 0.4), ground_height(x, z) + 1.6) - r * 0.25, z), r,
                    srgb(ridge), srgb(valley))
    # Sea fans standing in the current behind.
    for x, z, h, colour, yaw in ((-3.2, -7.2, 3.4, "#b8386a", 0.2), (8.0, -5.4, 2.8, "#e07a32", -0.4),
                                 (2.0, -7.6, 3.0, "#8a48b8", 0.0), (-8.4, -5.6, 2.6, "#e07a32", 0.3)):
        sea_fan(coral, rng, (x, statics.top(x, z) - 0.1, z), h, srgb(colour), yaw)
    # Soft corals and zoanthid mats on the rock.
    for x, z, colour in ((-6.0, -1.4, "#58c0a0"), (5.4, -2.0, "#e8a040"), (-4.4, -1.2, "#e868a0"),
                         (6.8, -0.8, "#70d070"), (-8.2, -6.0, "#e868a0"), (7.6, -6.6, "#58c0a0")):
        polyp_colony(coral, rng, (x, statics.top(x, z) - 0.05, z), 0.45, srgb(colour))
    # Anemones on each outcrop and one in the sand, where the clownfish live.
    for x, z, r, column, tentacle, tip, count, length in (
            (-5.3, 0.3, 0.42, "#a8556a", "#f08aa0", "#c8c0ff", 64, 0.6),
            (2.9, 2.4, 0.34, "#3d6b3f", "#7fd08a", "#f07ab8", 52, 0.5),
            (5.4, -0.2, 0.4, "#8a6a3a", "#e6c27a", "#ff9f6a", 58, 0.58)):
        top = statics.top(x, z, 0.3)
        anemone(coral, foliage, rng, (x, top - 0.08, z), r, srgb(column), srgb(tentacle), srgb(tip), count, length)
    statics.place(statics.add_mesh(*coral.mesh()), affine(), (1, 1, 1), (1, 1), CORAL)
    # Sea grass in the sand to either side, and sea whips rising from the reef.
    grass_tuft(foliage, rng, (-9.0, 2.6), 70, 1.6, srgb("#4e7a2a"), srgb("#a9c96a"), 0.07, 0.8)
    grass_tuft(foliage, rng, (8.8, 2.2), 60, 1.5, srgb("#4e7a2a"), srgb("#a9c96a"), 0.07, 0.8)
    grass_tuft(foliage, rng, (-1.2, 4.6), 26, 0.9, srgb("#5a842e"), srgb("#b7d47a"), 0.06, 0.6)
    for x, z, colour, length in ((-7.8, -6.2, srgb("#c0482a"), 3.0), (-2.8, -6.6, srgb("#e0a030"), 3.4),
                                 (3.0, -6.8, srgb("#8a3ab0"), 3.8), (7.2, -5.8, srgb("#c0482a"), 2.8),
                                 (-0.4, -6.9, srgb("#e0a030"), 2.6)):
        base_y = statics.top(x, z) - 0.1
        for k in range(5):
            root = foliage.root((x, base_y, z))
            lean = np.array([rng.normal(0, 0.25), 1.0, rng.normal(0, 0.15)])
            l = length * rng.uniform(0.6, 1.05)
            rows = int(l * 3)
            path = curve((x + rng.normal(0, 0.15), base_y, z + rng.normal(0, 0.1)), lean, l, rows, wobble=0.2, rng=rng)
            foliage.ribbon(root, path, np.full(rows + 1, 0.07), colour, translucency=0.3, compliance=0.5)

    parts, actors = [], []
    # Blue tangs, clownfish by their anemones, yellow tangs: kinds 1, 2, 3 (TankStyle::fish).
    for k in range(8):
        actors.append(swimmer(rng.uniform(-5, 4.5), rng.uniform(2.6, 5.8), rng.uniform(-1.5, 1.8),
                              rng.uniform(1.45, 1.7), 1.6, 0.2, 0.085 + 0.004 * k, k * 2.6))
        parts.append(fish(base, len(actors) - 1, 1, depth=1.65, length=1.05))
    for (x, z) in ((-5.3, 0.5), (-5.1, 0.2), (2.9, 2.6), (3.1, 2.3), (5.4, 0.0)):
        y = statics.top(x, z, 0.3) + 0.55
        actors.append(swimmer(x, y, z, rng.uniform(1.0, 1.15), 0.5, 0.08, 0.2 + rng.uniform(0, 0.05),
                              rng.uniform(0, 6.3)))
        parts.append(fish(base, len(actors) - 1, 2, depth=1.35, length=0.9))
    for k in range(6):
        actors.append(swimmer(rng.uniform(-4, 5), rng.uniform(3.2, 6.8), rng.uniform(-2.5, 0.8),
                              rng.uniform(1.3, 1.5), 1.4, 0.25, 0.1 + 0.005 * k, 1 + k * 3.1))
        parts.append(fish(base, len(actors) - 1, 3, depth=1.9, length=0.92))
    # A red hermit crab on the sand.
    x, z = 1.9, 2.9
    actors.append(swimmer(x, ground_height(x, z) + 0.23, z, 0.75, 0.9, 0.0, 0.08, 1.0, kind=1))
    parts.append(crab(base, len(actors) - 1, (0.55, 0.09, 0.05)))
    return finish(base, statics, foliage, parts, actors, base.risers.copy())


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
    for k in range(11):
        actors.append(swimmer(rng.uniform(-4.5, 4.5), rng.uniform(2.6, 6.0), rng.uniform(-1.0, 2.0),
                              rng.uniform(1.15, 1.35), 1.5, 0.22, 0.11 + 0.004 * k, k * 2.2))
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
