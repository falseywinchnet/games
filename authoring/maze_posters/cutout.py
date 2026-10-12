"""Cuts a character out of a crayon drawing for use as a sprite: the background is whatever an
outside flood fill reaches without crossing the black outline (or, for scribbled fur, whatever
is mostly background-coloured round about), and the rest is the character."""
import numpy as np
from PIL import Image, ImageFilter
from collections import deque


def flood_outside(dark, seeds):
    h, w = dark.shape
    bg = np.zeros((h, w), bool)
    q = deque()
    for y, x in seeds:
        if not dark[y, x] and not bg[y, x]:
            bg[y, x] = True
            q.append((y, x))
    while q:
        y, x = q.popleft()
        for ny, nx in ((y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)):
            if 0 <= ny < h and 0 <= nx < w and not bg[ny, nx] and not dark[ny, nx]:
                bg[ny, nx] = True
                q.append((ny, nx))
    return bg


def border_seeds(h, w, skip_bottom=(), skip_right=()):
    seeds = [(0, x) for x in range(w)] + [(y, 0) for y in range(h)]
    seeds += [(y, w - 1) for y in range(h) if not any(a <= y <= b for a, b in skip_right)]
    seeds += [(h - 1, x) for x in range(w) if not any(a <= x <= b for a, b in skip_bottom)]
    return seeds


def by_outline(rgb, dark_level=110, close=2, skip_bottom=(), skip_right=()):
    lum = rgb @ np.array([.3, .59, .11])
    dark = Image.fromarray(((lum < dark_level) * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(2 * close + 1))
    dark = np.asarray(dark) > 0
    bg = flood_outside(dark, border_seeds(*lum.shape, skip_bottom, skip_right))
    fg = ~bg
    # the outline itself belongs to the character, not to the fill's gap
    return np.asarray(Image.fromarray((fg * 255).astype(np.uint8)).filter(ImageFilter.MinFilter(3)).filter(ImageFilter.MaxFilter(3))) > 0


def by_colour(rgb, is_bg, radius=9):
    """for scribbled fur: a pixel is background if most of what is round it is background-coloured"""
    m = Image.fromarray((is_bg(rgb) * 255).astype(np.uint8)).filter(ImageFilter.BoxBlur(radius))
    fg = np.asarray(m) < 110
    fg = np.asarray(Image.fromarray((fg * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.MinFilter(5))) > 0
    return largest(fg)


def largest(mask):
    """keeps the biggest connected piece (the character), drops specks"""
    h, w = mask.shape
    seen = np.zeros_like(mask)
    best, best_n = None, 0
    ys, xs = np.nonzero(mask[::8, ::8])
    for y0, x0 in zip(ys * 8, xs * 8):
        if seen[y0, x0] or not mask[y0, x0]:
            continue
        comp = []
        q = deque([(y0, x0)])
        seen[y0, x0] = True
        while q:
            y, x = q.popleft()
            comp.append((y, x))
            for ny, nx in ((y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)):
                if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not seen[ny, nx]:
                    seen[ny, nx] = True
                    q.append((ny, nx))
        if len(comp) > best_n:
            best_n, best = len(comp), comp
    out = np.zeros_like(mask)
    if best:
        yy, xx = zip(*best)
        out[list(yy), list(xx)] = True
    return out


def sprite(path, mask_fn):
    img = Image.open(path).convert('RGB')
    rgb = np.asarray(img, float)
    fg = mask_fn(rgb)
    alpha = Image.fromarray((fg * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(.7))
    out = img.convert('RGBA')
    out.putalpha(alpha)
    ys, xs = np.nonzero(fg)
    return out.crop((xs.min(), ys.min(), xs.max() + 1, ys.max() + 1))


def greenish(rgb):
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    return (g > 120) & (g > b + 40) & (r < g + 40) & ~((r > 180) & (g > 120) & (b > 70) & (r > g))  # green-yellow, not peach skin
