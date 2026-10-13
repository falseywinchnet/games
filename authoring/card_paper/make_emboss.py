"""The embossed finish of the cards' paper, baked once: assets/card_emboss.r16.

A playing card's air-cushion finish is a fine field of tiny dimples pressed into the stock.
They are drawn here on a grid four times as fine as the screen (a hexagonal lattice turned
45 degrees, each dimple jittered, about 0.8 device pixels apart), lit from the upper left,
and brought down to device pixels with Plan Paint's CONV area resize (authoring/conv), which
never rings. What is kept is the light's variation, normalized to unit standard deviation;
the table sets its depth (a few sRGB levels) when it lays the paper over a card.

File: the bytes "CEMB", then width and height as little-endian uint32, then width x height
little-endian uint16, row-major: 32768 + 4096 x (the light's deviation in standard
deviations). Run from the repository root:

    python3 authoring/card_paper/make_emboss.py
"""
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'conv'))
import conv  # noqa: E402

WIDTH, HEIGHT = 720, 1008     # device pixels: the largest card on a Retina screen fits
FINE = 4                      # drawn on a grid this many times finer
PITCH = 0.8                   # dimple spacing, device pixels
LIGHT = (-.45, -.6, .66)      # toward the lamp: up and to the left, and above
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'assets', 'card_emboss.r16')


def heights(x0, y0, pitch):
    """Dimple depth at grid points (x0, y0): a jittered hexagonal lattice turned 45 degrees."""
    x, y = (x0 + y0) * .70710678, (y0 - x0) * .70710678
    row_h = pitch * .866
    depth = np.zeros_like(x0)
    row = np.floor(y / row_h).astype(np.int64)
    for dr in (-1, 0, 1):
        r = row + dr
        shift = np.where(r & 1, pitch * .5, 0.0)
        col = np.floor((x - shift) / pitch).astype(np.int64)
        for dc in (-1, 0, 1):
            c = col + dc
            k = (r.astype(np.uint32) * np.uint32(73856093)) ^ (c.astype(np.uint32) * np.uint32(19349663))
            k = (k ^ (k >> np.uint32(13))) * np.uint32(0x5bd1e995)
            jit = ((k >> np.uint32(8)) & np.uint32(255)).astype(float) / 255.0
            jit2 = ((k >> np.uint32(16)) & np.uint32(255)).astype(float) / 255.0
            cx = c * pitch + shift + pitch * .5 + (jit - .5) * pitch * .45
            cy = r * row_h + row_h * .5 + (jit2 - .5) * row_h * .45
            d2 = ((x - cx) ** 2 + (y - cy) ** 2) / (pitch * pitch * .2)
            depth -= np.where(d2 < 1, (1 - d2) ** 2 * (.75 + .5 * jit), 0)
    return depth


def main():
    fw, fh = WIDTH * FINE, HEIGHT * FINE
    pitch = PITCH * FINE
    lx, ly, lz = LIGHT
    light = np.empty((fh, fw))
    strip = 256
    xs = np.arange(fw, dtype=float) + .5
    for top in range(0, fh, strip):
        # one row either side, so the slope at a strip's edge sees its neighbours
        rows = np.arange(max(0, top - 1), min(fh, top + strip + 1), dtype=float) + .5
        y0, x0 = np.meshgrid(rows, xs, indexing='ij')
        z = heights(x0, y0, pitch) * .9
        gy, gx = np.gradient(z)
        nx, ny = -gx * 5, -gy * 5
        lit = (nx * lx + ny * ly + lz) / np.sqrt(nx * nx + ny * ny + 1) / lz
        first = top - int(rows[0] - .5)
        light[top:top + strip] = lit[first:first + min(strip, fh - top)]
    small = conv.resize_area(light, WIDTH, HEIGHT)
    deviation = (small - small.mean()) / small.std()
    codes = np.clip(np.round(32768 + 4096 * deviation), 0, 65535).astype('<u2')
    with open(OUT, 'wb') as f:
        f.write(b'CEMB' + struct.pack('<II', WIDTH, HEIGHT))
        f.write(codes.tobytes())
    print('%s: %d x %d, deviation range %.2f to %.2f' % (os.path.normpath(OUT), WIDTH, HEIGHT, deviation.min(), deviation.max()))


if __name__ == '__main__':
    main()
