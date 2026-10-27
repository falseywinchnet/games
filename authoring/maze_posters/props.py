"""3D props the posters share: a floating golden maze, the snail as a mascot."""
import numpy as np
from kit import *

# ---------------------------------------------------------------- the floating golden maze
GRID = None


def make_grid(w=9, h=9, seed=3):
    rng = np.random.default_rng(seed)
    g = np.ones((2 * h + 1, 2 * w + 1), bool)
    stack = [(0, 0)]
    seen = {(0, 0)}
    g[1, 1] = False
    while stack:
        x, y = stack[-1]
        nb = [(x + dx, y + dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)) if 0 <= x + dx < w and 0 <= y + dy < h and (x + dx, y + dy) not in seen]
        if not nb:
            stack.pop()
            continue
        nx, ny = nb[rng.integers(len(nb))]
        g[2 * y + 1 + (ny - y), 2 * x + 1 + (nx - x)] = False
        g[2 * ny + 1, 2 * nx + 1] = False
        seen.add((nx, ny))
        stack.append((nx, ny))
    g[1, 0] = False
    g[2 * h - 1, 2 * w] = False
    return g


def maze_scene(p):
    sc = Scene(p)
    g = GRID
    H, W = g.shape
    cell = 0.22
    # slab under the maze
    sc.add(sd_box(p, (0, -0.08, 0), (W * cell / 2, 0.08, H * cell / 2), 0.02), 1)
    # walls: a box per wall cell would be slow; use a repeated-cell distance with a lookup
    q = p / cell + np.array([W / 2, 0, H / 2])
    ix = np.clip(np.floor(q[:, 0]).astype(int), 0, W - 1)
    iz = np.clip(np.floor(q[:, 2]).astype(int), 0, H - 1)
    best = np.full(len(p), 1e9)
    for dz in (-1, 0, 1):
        for dx in (-1, 0, 1):
            jx = np.clip(ix + dx, 0, W - 1)
            jz = np.clip(iz + dz, 0, H - 1)
            wall = g[jz, jx]
            c = np.stack([(jx + .5 - W / 2) * cell, np.full(len(p), .1), (jz + .5 - H / 2) * cell], -1)
            d = np.linalg.norm(np.maximum(np.abs(p - c) - np.array([cell / 2, .1, cell / 2]) + .01, 0), axis=-1) - .01
            best = np.minimum(best, np.where(wall, d, 1e9))
    sc.add(best, 2)
    return sc


def maze_shade(mat, p, n, v):
    gold = hexc(0xF0B428)[None] * (0.8 + 0.2 * n[:, 1:2])
    slab = hexc(0xB07818)[None] * np.ones((len(p), 1))
    alb = np.where((mat == 2)[:, None], gold, slab)
    top = (n[:, 1] > .9) & (mat == 2)
    alb = np.where(top[:, None], hexc(0xFFE070)[None], alb)
    return alb, np.zeros(len(p)), np.full(len(p), 1.0), np.zeros((len(p), 3))


def space(d):
    out = np.zeros((len(d), 3)) + hexc(0x060418)
    return out + hexc(0x301060) * np.clip(d[:, 1] + .3, 0, 1)[:, None] * .5


# ---------------------------------------------------------------- the snail, far too close
def snail_scene(p):
    sc = Scene(p)
    body = sd_capsule(p, (-0.9, -0.55, 0), (0.55, -0.35, 0.1), 0.32)
    head = sd_sphere(p, (0.62, -0.18, 0.15), 0.36)
    sc.add(smin(body, head, 0.25), 1)
    sc.add(sd_ellipsoid(p, (-0.55, 0.35, -0.2), (0.75, 0.8, 0.55)), 2)  # the shell
    for sx in (-1, 1):
        base = np.array([0.6, 0.05, 0.15 + sx * 0.16])
        tip = np.array([0.82, 0.62, 0.35 + sx * 0.32])
        sc.add(sd_capsule(p, base, tip, 0.05), 1)
        sc.add(sd_sphere(p, tip + np.array([0.05, 0.1, 0.05 * sx]), 0.2), 3)  # googly eyes
        sc.add(sd_sphere(p, tip + np.array([0.18, 0.12, 0.08 + 0.05 * sx]), 0.09), 4)
    return sc


def snail_shade(mat, p, n, v):
    N = len(p)
    skin = hexc(0xB8E070)[None] * (0.9 + 0.1 * fbm3(p * 6, 3)[:, None])
    q = p - np.array([-0.55, 0.35, -0.2])
    r = np.hypot(q[:, 0], q[:, 1])
    a = np.arctan2(q[:, 1], q[:, 0])
    turn = r / 0.16 - a / (2 * np.pi)
    groove = np.abs(turn - np.floor(turn + .5)) < .12
    shell = np.where(groove[:, None], hexc(0x7A1A6A)[None], hexc(0xF060B8)[None])
    alb = np.select([(mat == 1)[:, None], (mat == 2)[:, None], (mat == 3)[:, None], (mat == 4)[:, None]], [skin * np.ones((N, 1)), shell, np.ones((N, 3)), np.ones((N, 3)) * .03], 0)
    spec = np.select([mat == 3, mat == 2, mat == 1], [1.0, .8, .5], .5)
    rim = np.clip(1 - np.abs(dot(n, -v)), 0, 1) ** 2.5
    emit = rim[:, None] * hexc(0x60F0FF)[None] * .6  # rim light from the glow behind
    return alb, np.zeros(N), spec, emit




def golden_maze(eye=(2.6, 2.4, 3.0), look=(0.1, 0, 0), fov=0.9, w=9, h=9, seed=3):
    """the maze as a floating slab of gold, as a layer"""
    global GRID
    GRID = make_grid(w, h, seed)
    rgb, cov, _ = render(maze_scene, maze_shade, lambda d: np.zeros((len(d), 3)), eye, look, fov, light=(-0.4, 1.0, 0.5), reflect=False)
    return to_img(rgb, cov)


def mascot(eye=(2.2, 0.4, 3.4), look=(0.1, 0.0, 0), fov=0.85):
    """the paint snail as a mascot: glossy, googly-eyed, far too pleased with itself"""
    rgb, cov, _ = render(snail_scene, snail_shade, lambda d: np.zeros((len(d), 3)), eye, look, fov, light=(0.4, 0.8, 0.7), reflect=False)
    return to_img(rgb, cov)
