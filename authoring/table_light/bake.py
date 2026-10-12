#!/usr/bin/env python3
"""Bake the sunlit room's light onto the card table and test simplifications of it.

    bake.py <albedo.ppm> <cards.txt> <out dir> [card width in points, default 113]

<albedo.ppm> is the table drawn unlit (PLAYSUITE_LIGHT_PASS=albedo), and <cards.txt> every
card's place on it, "pile index x y width height" in points (PLAYSUITE_LIGHT_DUMP with
PLAYSUITE_LIGHT_PASS=height). A card is 63 mm wide, which sets the scale of everything, and
CARD_THICK_MM thick (0.6; a real card is 0.3).

The reference: every pixel lit by the path-traced room (trace.cpp), the sun's disc and the
rest of the sky and room taken separately, each cut by what the cards around it hide
(the horizon of the height field in 64 directions), cards under varnish, cloth matte,
and each pixel what the player's eyes see along their sight line: a card's top or side.
"""
import os
import subprocess
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SUN_UP, SUN_ROUND = np.radians(32), np.radians(80)  # as trace.cpp
SUN_LIGHT = np.array([1.0, .96, .90])
CARD_M = .063
SCATTER_M = .00025  # how far light spreads inside paper and felt before it comes out
CORNER_PT = 6   # the game draws cards with corners of this radius, in points
STACK_SHOWN = 4  # a true stack shows this many cards high: enough to read as a deck
DRAPE_M = .002  # how far a card takes to bend down over the edge of the one beneath
COAT_ALPHA = .10            # as trace.cpp
COAT_F0 = .04


def srgb_to_linear(c):
    c = c / 255.0
    return np.where(c <= .04045, c / 12.92, ((c + .055) / 1.055) ** 2.4)


def linear_to_srgb(c):
    c = np.clip(c, 0, 1)
    return np.where(c <= .0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - .055) * 255


def fresnel(c):
    return COAT_F0 + (1 - COAT_F0) * (1 - np.clip(c, 0, 1)) ** 5


def run_parts(tracer, mode, width, depth, nx, ny, samples, out, parts=6):
    if os.path.exists(out):
        return np.fromfile(out, np.float32)
    rows = (ny + parts - 1) // parts
    jobs = [subprocess.Popen([tracer, mode, str(width), str(depth), str(nx), str(ny),
                              str(samples), str(i * rows), str(rows), f"{out}.{i}"])
            for i in range(parts)]
    for job in jobs:
        if job.wait():
            sys.exit(f"{mode} part failed")
    data = np.concatenate([np.fromfile(f"{out}.{i}", np.float32) for i in range(parts)])
    for i in range(parts):
        os.remove(f"{out}.{i}")
    data.tofile(out)
    return data


def bilinear(grid, w, h):
    """A (ny, nx, ...) grid spanning the picture's corners, sampled at every pixel."""
    ny, nx = grid.shape[:2]
    gx = np.linspace(0, nx - 1, w)
    gy = np.linspace(0, ny - 1, h)
    x0 = np.clip(np.floor(gx).astype(int), 0, nx - 2)
    y0 = np.clip(np.floor(gy).astype(int), 0, ny - 2)
    fx = (gx - x0)[None, :]
    fy = (gy - y0)[:, None]
    extra = (None,) * (grid.ndim - 2)
    fx, fy = fx[(...,) + extra], fy[(...,) + extra]
    g00 = grid[y0][:, x0]
    g01 = grid[y0][:, x0 + 1]
    g10 = grid[y0 + 1][:, x0]
    g11 = grid[y0 + 1][:, x0 + 1]
    return (g00 * (1 - fx) + g01 * fx) * (1 - fy) + (g10 * (1 - fx) + g11 * fx) * fy


def shifted(a, dx, dy):
    """a moved so that out[y, x] = a[y + dy, x + dx], zero (bare cloth) beyond the edge."""
    h, w = a.shape
    out = np.zeros_like(a)
    ys, yd = (slice(dy, h), slice(0, h - dy)) if dy >= 0 else (slice(0, h + dy), slice(-dy, h))
    xs, xd = (slice(dx, w), slice(0, w - dx)) if dx >= 0 else (slice(0, w + dx), slice(-dx, w))
    out[yd, xd] = a[ys, xs]
    return out


def horizon(height, px_m, angle, reach_px):
    """The tangent of the highest thing seen from each pixel looking towards `angle`."""
    best = np.zeros_like(height)
    ux, uy = np.cos(angle), np.sin(angle)
    seen = set()
    for k in range(1, reach_px + 1):
        dx, dy = int(round(k * ux)), int(round(k * uy))
        if (dx, dy) in seen:
            continue
        seen.add((dx, dy))
        distance = np.hypot(dx, dy) * px_m
        np.maximum(best, (shifted(height, dx, dy) - height) / distance, out=best)
    return best


def spread(a, radius):
    """`a` blurred about as a Gaussian of this radius (pixels): three box blurs a way."""
    side = max(1, int(round(radius * 1.2)))
    for axis in (0, 1):
        for _ in range(3):
            padded = np.concatenate([np.repeat(a.take([0], axis), side, axis), a,
                                     np.repeat(a.take([-1], axis), side, axis)], axis)
            total = np.cumsum(padded, axis)
            total = np.concatenate([np.zeros_like(a.take([0], axis)), total], axis)
            n = a.shape[axis]
            a = (total.take(range(2 * side + 1, 2 * side + 1 + n), axis) -
                 total.take(range(0, n), axis)) / (2 * side + 1)
    return a


def main():
    albedo_path, cards_path, out_dir = sys.argv[1:4]
    card_px = float(sys.argv[4]) if len(sys.argv) > 4 else 113.0
    thick_m = float(os.environ.get("CARD_THICK_MM", ".6")) / 1000
    os.makedirs(out_dir, exist_ok=True)
    tracer = os.path.join(out_dir, "trace")
    subprocess.run(["clang++", "-std=c++20", "-O3", "-o", tracer,
                    os.path.join(HERE, "trace.cpp")], check=True)

    # The captures as drawn, pixel for pixel: the light multiplies the print, never resamples
    # it. A 2x capture (PLAYSUITE_CAPTURE_SCALE=2) is what a Retina screen shows.
    albedo = srgb_to_linear(np.asarray(Image.open(albedo_path).convert("RGB"), np.float64))
    h, w = albedo.shape[:2]
    scale = 2 if w > 1500 else 1
    px_m = CARD_M / (card_px * scale)
    width, depth = w * px_m, h * px_m
    # The cards, exactly as the game places them (PLAYSUITE_LIGHT_DUMP), built on a raster
    # twice as fine as the picture: rounded rectangles, each lying on those beneath it.
    fine = 2
    hf, wf = h * fine, w * fine
    fine_m = px_m / fine
    cards = []
    with open(cards_path) as listing:
        for line in listing:
            pile, index, x, y, cw, ch = line.split()
            cards.append((int(pile), int(index), *(float(v) * scale * fine
                                                    for v in (x, y, cw, ch))))
    radius = CORNER_PT * scale * fine
    drape = DRAPE_M / fine_m

    def outside_by(card_no, xs, ys, rect=None):
        """How far (fine pixels) a place lies outside a card's rounded outline."""
        x, y, cw, ch = rect if rect else cards[card_no][2:]
        qx = np.abs(xs - (x + cw / 2)) - (cw / 2 - radius)
        qy = np.abs(ys - (y + ch / 2)) - (ch / 2 - radius)
        return (np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) +
                np.minimum(np.maximum(qx, qy), 0) - radius)

    top = np.full((hf, wf), -1, np.int32)  # the topmost card at each place
    top_h = np.zeros((hf, wf))              # the height of its face there
    for n, (pile, index, x, y, cw, ch) in enumerate(cards):
        x0, x1 = max(0, int(x) - 1), min(wf, int(x + cw) + 2)
        y0, y1 = max(0, int(y) - 1), min(hf, int(y + ch) + 2)
        if x0 >= x1 or y0 >= y1:
            continue
        ys, xs = np.mgrid[y0:y1, x0:x1] + .5
        inside = outside_by(n, xs, ys) < 0
        if pile >= 10:
            # A true stack, squarely on itself, shown at most four cards high.
            level = min(index, STACK_SHOWN - 1) if index >= 0 else max(0, STACK_SHOWN - 1 + index)
            face = np.full(inside.shape, (level + 1) * thick_m)
        else:
            # A fanned card drapes over the cards beneath it, bending down within DRAPE_M of
            # those of their edges that end under it, and lies on the cloth beyond them.
            def beneath(j):
                bx, by, bw, bh = cards[j][2:]
                far = 4 * (cw + ch)
                left = bx - far if abs(bx - x) < 1 else bx
                top_edge = by - far if abs(by - y) < 1 else by
                right = bx + bw + far if abs(bx + bw - x - cw) < 1 else bx + bw
                bottom = by + bh + far if abs(by + bh - y - ch) < 1 else by + bh
                rect = (left, top_edge, right - left, bottom - top_edge)
                return np.clip(-outside_by(j, xs, ys, rect) / drape, 0, 1)
            under = sum((beneath(j) for j, other in enumerate(cards)
                         if other[0] == pile and 0 <= other[1] < index), np.zeros(xs.shape))
            face = thick_m * (1 + under)
        top[y0:y1, x0:x1][inside] = n
        top_h[y0:y1, x0:x1][inside] = face[inside]

    def coarse(a):
        return a.reshape(h, fine, w, fine).mean((1, 3))
    height = coarse(top_h)
    card_share = coarse((top >= 0).astype(np.float64))
    card = card_share >= .5

    # The room's light on the bare cloth.
    nx, ny = 23, 16
    cells = run_parts(tracer, "cells", width, depth, nx, ny, 48,
                      os.path.join(out_dir, "cells.f32")).reshape(ny, nx, -1)
    bins = cells[..., :-1].reshape(ny, nx, 64, 16, 3).astype(np.float64)
    sun_seen = cells[..., -1].astype(np.float64)
    band_cos = np.sqrt(1 - (np.arange(16) + .5) / 16)  # each band's cosine from the zenith
    measure = np.pi / (64 * 16)
    rest = bins * measure                               # irradiance from each bin
    rest_coat = rest * (1 - fresnel(band_cos))[None, None, None, :, None]
    coat_grid = run_parts(tracer, "coat", width, depth, 56, 37, 16384,
                          os.path.join(out_dir, "coat.f32")).reshape(37, 56, 3)

    # Each pixel's view of the room, cut by the cards around it.
    reach = int(np.ceil(.03 / px_m))
    e_rest = np.zeros((h, w, 3))
    e_rest_coat = np.zeros((h, w, 3))
    near_y = np.clip(np.round(np.linspace(0, ny - 1, h)).astype(int), 0, ny - 1)
    near_x = np.clip(np.round(np.linspace(0, nx - 1, w)).astype(int), 0, nx - 1)
    for a in range(64):
        tangent = horizon(height, px_m, 2 * np.pi * (a + .5) / 64, reach)
        cut = 16 / (1 + tangent ** 2)  # bands below sin² = cos²(horizon) are seen
        whole = np.floor(cut).astype(int)
        part = cut - whole
        for target, source in ((e_rest, rest), (e_rest_coat, rest_coat)):
            prefix = np.concatenate([np.zeros((ny, nx, 1, 3)),
                                     np.cumsum(source[:, :, a], axis=2)], axis=2)
            lo = prefix[near_y[:, None], near_x[None, :], whole.clip(0, 16)]
            hi = prefix[near_y[:, None], near_x[None, :], (whole + 1).clip(0, 16)]
            target += lo + (hi - lo) * part[..., None]
    # Scale each pixel's cut light by the smooth field, so the cells' steps never show.
    for target, source in ((e_rest, rest), (e_rest_coat, rest_coat)):
        whole_cell = source.sum(axis=(2, 3))
        nearest = whole_cell[near_y][:, near_x]
        target *= bilinear(whole_cell, w, h) / np.maximum(nearest, 1e-9)
    e_rest_plane = bilinear(rest.sum(axis=(2, 3)), w, h)
    e_rest_coat_plane = bilinear(rest_coat.sum(axis=(2, 3)), w, h)

    # The sun: its disc through the glass, then the cards in its way.
    sun_up_tan = np.tan(SUN_UP)
    sun_angle = np.arctan2(-np.cos(SUN_ROUND), -np.sin(SUN_ROUND))  # towards it: up, left
    # On the fine raster, so the shadows' edges are sharp and smooth.
    best = horizon(top_h, fine_m, sun_angle,
                   int(np.ceil(top_h.max() / sun_up_tan / fine_m)) + 2)
    # Light spreads a little inside paper and among the cloth's fibres, so a sliver of
    # shadow finer than that reads as a faint grey, never a black line.
    lit = coarse(spread((best < sun_up_tan).astype(np.float64), SCATTER_M / fine_m))
    sun = np.array([-np.cos(SUN_UP) * np.sin(SUN_ROUND), -np.cos(SUN_UP) * np.cos(SUN_ROUND),
                    np.sin(SUN_UP)])
    sun_seen_px = bilinear(sun_seen, w, h)
    e_sun_plane = sun_seen_px[..., None] * SUN_LIGHT * sun[2]
    e_sun = e_sun_plane * lit[..., None]

    # The view from the player's eyes, and the varnish's part in it.
    xs = (np.arange(w) + .5) * px_m
    ys = (np.arange(h) + .5) * px_m
    eye = np.array([width * .5, depth + .30, .42])
    view = np.stack(np.broadcast_arrays(eye[0] - xs[None, :], eye[1] - ys[:, None],
                                        np.full((h, 1), eye[2])), -1)
    view = view / np.linalg.norm(view, axis=-1, keepdims=True)
    nv = view[..., 2]
    through_out = (1 - fresnel(nv))[..., None]
    through_sun = 1 - fresnel(sun[2])
    coat = bilinear(coat_grid.astype(np.float64), w, h)

    # Each place's top surface, lit: the cloth matte, the cards under varnish.
    felt = albedo / np.pi * (e_sun + e_rest)
    paper = albedo / np.pi * (e_sun * through_sun + e_rest_coat) * through_out + coat
    surface = paper * card_share[..., None] + felt * (1 - card_share[..., None])
    plane = np.where(card[..., None],
                     albedo / np.pi * (e_sun_plane * through_sun + e_rest_coat_plane) *
                     through_out + coat,
                     albedo / np.pi * (e_sun_plane + e_rest_plane))

    # The cards' tops stay exactly where the game draws them; each card's side hangs below
    # its outline towards the player's eyes, as far as its height above what lies under it
    # throws it. So a place shows a side if, looking from it away from the eyes, another
    # card's face stands higher above this place than that distance. The side is the cut
    # edges of the cards there, with a fine seam between cards, lit by the sun only where it
    # faces it. Worked on the fine raster, so the rounded outlines stay smooth.
    horizontal = np.hypot(view[..., 0], view[..., 1])

    def up(a):
        return np.repeat(np.repeat(a, fine, 0), fine, 1)
    reach = up(horizontal / view[..., 2]) / fine_m  # fine pixels towards the eyes per metre
    ux = up(view[..., 0] / np.maximum(horizontal, 1e-9))
    uy = up(view[..., 1] / np.maximum(horizontal, 1e-9))
    fx = np.arange(wf)[None, :] + .5 + np.zeros((hf, 1))
    fy = np.arange(hf)[:, None] + .5 + np.zeros((1, wf))
    step = thick_m / 4
    drop = np.full((hf, wf), np.inf)  # how far below a card's face the sight line meets it
    owner = np.full((hf, wf), -1, np.int32)
    qx = np.zeros((hf, wf))
    qy = np.zeros((hf, wf))
    for t in np.arange(step, top_h.max() + step, step):
        sx, sy = fx - t * reach * ux, fy - t * reach * uy
        ix = np.clip(sx.astype(int), 0, wf - 1)
        iy = np.clip(sy.astype(int), 0, hf - 1)
        theirs = top[iy, ix]
        found = (np.isinf(drop) & (theirs >= 0) & (theirs != top) &
                 (top_h[iy, ix] - top_h >= t))
        drop[found] = t
        owner[found] = theirs[found]
        qx[found] = sx[found]
        qy[found] = sy[found]
    on_side = np.isfinite(drop)
    # Each side's outward direction, from its card's outline.
    nx_side = np.zeros((hf, wf))
    ny_side = np.zeros((hf, wf))
    for n in np.unique(owner[on_side]):
        sel = on_side & (owner == n)
        _, _, x, y, cw, ch = cards[n]
        dx, dy = qx[sel] - (x + cw / 2), qy[sel] - (y + ch / 2)
        ax = np.abs(dx) - (cw / 2 - radius)
        ay = np.abs(dy) - (ch / 2 - radius)
        corner = (ax > 0) & (ay > 0)
        vx = np.where(corner, np.maximum(ax, 0), (ax >= ay).astype(float)) * np.sign(dx)
        vy = np.where(corner, np.maximum(ay, 0), (ay > ax).astype(float)) * np.sign(dy)
        length = np.maximum(np.hypot(vx, vy), 1e-9)
        nx_side[sel], ny_side[sel] = vx / length, vy / length
    side_sun = np.clip(nx_side * sun[0] + ny_side * sun[1], 0, None)
    seam = np.where((np.where(on_side, drop, 0) / thick_m) % 1 > .8, .55, .86)
    coarse_index = (np.arange(hf) // fine)[:, None], (np.arange(wf) // fine)[None, :]
    side_colour = ((SUN_LIGHT * (up(sun_seen_px) * side_sun)[..., None] +
                    up(e_rest_plane) * .5) / np.pi) * seam[..., None]
    fine_picture = np.where(on_side[..., None], side_colour,
                            surface[coarse_index[0], coarse_index[1]])
    reference = fine_picture.reshape(h, fine, w, fine, 3).mean((1, 3))
    if os.environ.get("BAKE_DEBUG"):
        np.savez_compressed(os.path.join(out_dir, "debug.npz"), on_side=on_side, top=top,
                            top_h=top_h, lit_fine=(best < sun_up_tan), owner=owner)

    # Exposure: white paper where the light is strongest keeps today's white.
    white = ((e_sun_plane * through_sun + e_rest_coat_plane) * through_out / np.pi).mean(-1)
    exposure = 1 / white.max()
    np.savez_compressed(os.path.join(out_dir, "bake.npz"), exposure=exposure,
                        e_sun_plane=e_sun_plane, e_rest_plane=e_rest_plane,
                        e_rest_coat_plane=e_rest_coat_plane, coat=coat, lit=lit,
                        e_rest=e_rest, e_rest_coat=e_rest_coat, card=card,
                        px_m=px_m)
    for name, picture in (("reference", reference), ("plane", plane)):
        full = Image.fromarray(linear_to_srgb(picture * exposure).round().astype(np.uint8))
        full.save(os.path.join(out_dir, f"{name}@2x.png"))
        # 1x averaged in linear light, not from the encoded picture
        small = picture.reshape(h // scale, scale, w // scale, scale, 3).mean((1, 3))
        Image.fromarray(linear_to_srgb(small * exposure).round().astype(np.uint8)).save(
            os.path.join(out_dir, f"{name}.png"))

    def stats(label, value):
        print(f"{label:28s} min {value.min():.4f}  mean {value.mean():.4f}  max {value.max():.4f}")
    print(f"scale {px_m * 1000:.3f} mm a pixel, table seen {width:.3f} x {depth:.3f} m")
    stats("sun on the cloth", e_sun_plane.mean(-1))
    stats("sky and room on the cloth", e_rest_plane.mean(-1))
    stats("varnish reflection, exposed", coat.mean(-1)[card] * exposure)
    stats("white paper, exposed", white * exposure)
    stats("sunlit share of pixels", lit)
    print("exposure", exposure)


if __name__ == "__main__":
    main()
