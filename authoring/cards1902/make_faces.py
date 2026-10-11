#!/usr/bin/env python3
"""The Classic card faces: a clean reprint of Goodall & Son's 1902 pack.

The design is the English pattern as Goodall printed it for the coronation of
Edward VII in 1902 (Worshipful Company of Makers of Playing Cards collection,
acquisition 206), long out of copyright and the pattern later standard packs
follow. It was chromolithographed in four flat inks on cream board, so it can be
reprinted rather than redrawn: each scan is straightened and cropped, the stains
and yellowing of the old board are divided out, every pixel is given to one of
the five colours (paper, red, blue, black, yellow), specks are voted away, and
the inks are laid again, antialiased, on fresh paper at the game's card size.

The scans are not part of the repository. Fetch them (W0206_Card1.jpg ...
W0206_Card52.jpg) from https://card-collection.makersofplayingcards.org/ into a
folder, then:

    python3 authoring/cards1902/make_faces.py <scan-folder> assets

which writes assets/classic_00.png ... classic_51.png (suit * 13 + rank - 1,
suits clubs, diamonds, hearts, spades), 360 x 504 like the painted faces.
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image

W, H = 360, 504            # the card art, as the painted faces
INSET, RADIUS = 2.4, 18.0  # the card within it, as the painted faces
SS = 4                     # the inks are sorted and cleaned at four times the size

# The scans run aces, kings, queens, jacks, then ten down to two; each in hearts,
# spades, diamonds, clubs. The game counts suits clubs, diamonds, hearts, spades.
SCAN_SUITS = [2, 3, 1, 0]
SCAN_RANKS = [1, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2]

# The five colours relative to clean paper (1 = paper): the starting guesses for each
# scan's own, found by k-means.
PAPER, RED, BLUE, BLACK, YELLOW = range(5)
GUESSES = np.array([
    [1.00, 1.00, 1.00],
    [0.92, 0.22, 0.20],
    [0.32, 0.42, 0.86],
    [0.12, 0.11, 0.12],
    [0.98, 0.82, 0.22],
])
# The inks as they are laid again: the pack's own hues, a touch cleaner than the worn
# scans, on a soft off-white board (the table tints it faintly as it draws the card).
INKS = np.array([
    [0, 0, 0],          # paper: drawn separately
    [200, 34, 40],
    [52, 78, 172],
    [26, 24, 28],
    [236, 190, 52],
], dtype=np.float64)


def box(a: np.ndarray, r: int) -> np.ndarray:
    """Mean over a (2r+1)^2 square, edges clamped."""
    p = np.pad(a, ((r + 1, r), (r + 1, r)) + ((0, 0),) * (a.ndim - 2), mode="edge")
    c = p.cumsum(0).cumsum(1)
    n = 2 * r + 1
    s = c[n:, n:] - c[:-n, n:] - c[n:, :-n] + c[:-n, :-n]
    return s / (n * n)


def straighten(rgb: np.ndarray) -> np.ndarray:
    """The card alone, square to the frame: the scans lie on black."""
    card = rgb.max(axis=2) > 90
    h, w = card.shape
    rows = np.arange(int(h * .2), int(h * .8))
    left = np.array([np.argmax(card[y]) for y in rows], dtype=np.float64)
    right = np.array([w - 1 - np.argmax(card[y, ::-1]) for y in rows], dtype=np.float64)
    slope = (np.polyfit(rows, left, 1)[0] + np.polyfit(rows, right, 1)[0]) / 2
    image = Image.fromarray(rgb)
    if abs(slope) > 1e-4:
        image = image.rotate(-np.degrees(np.arctan(slope)), resample=Image.Resampling.BICUBIC, fillcolor=(0, 0, 0))
    rgb = np.asarray(image)
    card = rgb.max(axis=2) > 90
    cols = np.nonzero(card.mean(axis=0) > .6)[0]
    rows = np.nonzero(card.mean(axis=1) > .6)[0]
    # A few pixels in from the cut, clear of its shadow; the corners are cut again later.
    x0, x1, y0, y1 = cols[0] + 6, cols[-1] - 6, rows[0] + 6, rows[-1] - 6
    return rgb[y0:y1 + 1, x0:x1 + 1]


def paper_field(rgb: np.ndarray) -> np.ndarray:
    """The bare board's colour everywhere, stains and all, from the pixels no ink touches."""
    block = 32
    h, w, _ = rgb.shape
    bh, bw = (h + block - 1) // block, (w + block - 1) // block
    sums = np.zeros((bh, bw, 3))
    counts = np.zeros((bh, bw))
    bare = (rgb.min(axis=2) > 150) & (rgb.mean(axis=2) > 180)
    ys, xs = np.nonzero(bare)
    np.add.at(sums, (ys // block, xs // block), rgb[ys, xs].astype(np.float64))
    np.add.at(counts, (ys // block, xs // block), 1)
    known = counts > block * block * .08
    field = np.where(known[..., None], sums / np.maximum(counts, 1)[..., None], 0)
    if not known.any():
        return np.full_like(rgb, 235, dtype=np.float64)
    # Fill the blocks under large inked areas from their neighbours.
    for _ in range(200):
        if known.all():
            break
        p = np.pad(field, ((1, 1), (1, 1), (0, 0)))
        k = np.pad(known.astype(np.float64), 1)
        total = sum(np.roll(np.roll(p, dy, 0), dx, 1)[1:-1, 1:-1] for dy in (-1, 0, 1) for dx in (-1, 0, 1))
        weight = sum(np.roll(np.roll(k, dy, 0), dx, 1)[1:-1, 1:-1] for dy in (-1, 0, 1) for dx in (-1, 0, 1))
        grow = ~known & (weight > 0)
        field[grow] = total[grow] / weight[grow][:, None]
        known = known | grow
    field = box(field, 2)
    small = Image.fromarray(np.clip(field, 0, 255).astype(np.uint8))
    return np.asarray(small.resize((w, h), Image.Resampling.BILINEAR)).astype(np.float64)


def sort_inks(flat: np.ndarray, allowed: list) -> np.ndarray:
    """Each pixel's colour index, from k-means seeded with the five guesses, among the
    colours the card was printed in."""
    pixels = flat.reshape(-1, 3)
    rng = np.random.default_rng(1902)
    sample = pixels[rng.choice(len(pixels), size=min(len(pixels), 250000), replace=False)]
    centres = GUESSES.copy()
    for _ in range(10):
        d = ((sample[:, None, :] - centres[None, :, :]) ** 2).sum(axis=2)
        d[:, [k for k in range(len(centres)) if k not in allowed]] = np.inf
        nearest = d.argmin(axis=1)
        for k in range(len(centres)):
            chosen = sample[nearest == k]
            # A colour a card does not use stays where it was guessed.
            if len(chosen) > 200:
                centres[k] = chosen.mean(axis=0)
    centres[PAPER] = 1.0
    barred = np.array([0.0 if k in allowed else np.inf for k in range(len(centres))])
    labels = np.empty(len(pixels), dtype=np.uint8)
    for start in range(0, len(pixels), 1 << 20):
        chunk = pixels[start:start + (1 << 20)]
        d = ((chunk[:, None, :] - centres[None, :, :]) ** 2).sum(axis=2) + barred[None, :]
        labels[start:start + len(chunk)] = d.argmin(axis=1)
    return labels.reshape(flat.shape[:2])


def vote(labels: np.ndarray, r: int = 1) -> np.ndarray:
    """Each pixel takes the colour most of its neighbourhood has: print grain and specks go."""
    counts = np.stack([box((labels == k).astype(np.float64), r) for k in range(5)], axis=2)
    return counts.argmax(axis=2).astype(np.uint8)


def paper(h: int, w: int) -> np.ndarray:
    """Soft off-white board. Its tint, formation, fibres and the grain of the ink are the
    table's own finish, laid over every style as the game draws the card."""
    return np.broadcast_to(np.array([245.0, 244.0, 238.0]), (h, w, 3)).copy()


def rounded_alpha(h: int, w: int) -> np.ndarray:
    """The card's cut, antialiased: a rounded rectangle INSET from the art's edge."""
    y, x = np.mgrid[0:h, 0:w] + .5
    x0, y0, x1, y1 = INSET, INSET, w - INSET, h - INSET
    cx = np.clip(x, x0 + RADIUS, x1 - RADIUS)
    cy = np.clip(y, y0 + RADIUS, y1 - RADIUS)
    d = np.hypot(x - cx, y - cy) - RADIUS
    return np.clip(.5 - d, 0, 1)


def board_of(rgb: np.ndarray) -> np.ndarray:
    """Where the old card is: between its outer edges along every row and every column (so
    black ink inside counts), drawn in three points from its worn, fringed edge."""
    # The old board is cream; the scanner's ground is black, or white at a corner.
    cream = rgb.astype(np.int32)
    bright = (rgb.min(axis=2) > 110) & (cream[..., 0] - cream[..., 2] > 12)
    h, w = bright.shape
    x = np.arange(w)[None, :]
    y = np.arange(h)[:, None]
    has_row, has_col = bright.any(axis=1), bright.any(axis=0)
    left = np.where(has_row, bright.argmax(axis=1), w)[:, None]
    right = np.where(has_row, w - 1 - bright[:, ::-1].argmax(axis=1), -1)[:, None]
    top = np.where(has_col, bright.argmax(axis=0), h)[None, :]
    bottom = np.where(has_col, h - 1 - bright[::-1, :].argmax(axis=0), -1)[None, :]
    inside = (x >= left) & (x <= right) & (y >= top) & (y <= bottom)
    return box(inside.astype(np.float64), 12) > .999


def reprint(scan: Path, allowed: list) -> Image.Image:
    rgb = straighten(np.asarray(Image.open(scan).convert("RGB")))
    # The card's own area in the art, worked at SS times.
    cw, ch = round((W - 2 * INSET) * SS), round((H - 2 * INSET) * SS)
    rgb = np.asarray(Image.fromarray(rgb).resize((cw, ch), Image.Resampling.LANCZOS))
    flat = np.clip(rgb.astype(np.float64) / np.maximum(paper_field(rgb), 1), 0, 1.2)
    labels = vote(vote(sort_inks(flat, allowed), 1), 1)
    # The old card's own rounded corners, and the dark beyond them, are paper here: the
    # new cut is made below.
    labels[~board_of(rgb)] = PAPER
    # Coverage of each colour at the art's size: the mean of the SS x SS cell.
    oh, ow = ch // SS, cw // SS
    ox, oy = int(round(INSET)), int(round(INSET))
    one = np.stack([(labels == k) for k in range(5)], axis=2).astype(np.float64)[:oh * SS, :ow * SS]
    cover = one.reshape(oh, SS, ow, SS, 5).mean(axis=(1, 3))
    out = paper(H, W)[oy:oy + oh, ox:ox + ow]
    for k in (RED, BLUE, BLACK, YELLOW):
        c = cover[..., k][..., None]
        out = out * (1 - c) + INKS[k][None, None, :] * c
    art = np.zeros((H, W, 4))
    art[oy:oy + oh, ox:ox + ow, :3] = out
    # The rim the inset leaves is paper too, under the cut.
    edge = paper(H, W)
    filled = np.zeros((H, W), dtype=bool)
    filled[oy:oy + oh, ox:ox + ow] = True
    art[~filled, :3] = edge[~filled]
    art[..., 3] = rounded_alpha(H, W) * 255
    return Image.fromarray(np.clip(np.round(art), 0, 255).astype(np.uint8), "RGBA")


def main() -> None:
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    scans, out = Path(sys.argv[1]), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    for n in range(1, 53):
        scan = scans / f"W0206_Card{n}.jpg"
        if not scan.exists():
            scan = scans / f"card{n}.jpg"
        suit = SCAN_SUITS[(n - 1) % 4]
        rank = SCAN_RANKS[(n - 1) // 4]
        index = suit * 13 + rank - 1
        # Courts are in all four inks; aces and pips only in their suit's.
        allowed = [PAPER, RED, BLUE, BLACK, YELLOW] if rank > 10 else [PAPER, RED if suit in (1, 2) else BLACK]
        reprint(scan, allowed).save(out / f"classic_{index:02d}.png", optimize=True)
        print(f"{scan.name} -> classic_{index:02d}.png", flush=True)


if __name__ == "__main__":
    main()
