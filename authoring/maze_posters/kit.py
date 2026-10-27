"""Maze 95 posters: the toolkit. A numpy SDF raymarcher for the 3D props, PIL drawing for the
flat pieces, the period's lettering (chrome, WordArt, neon tubes, gel), its furniture (Windows
dialogs, starbursts, stickers, slime) and the finishing passes (fringe, scanlines, grain, haze)."""
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

S = 512  # working size; posters ship at 256

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'conv'))
import conv as _conv  # noqa: E402  Plan Paint's CONV (authoring/conv)

FONTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'assets', 'fonts') + os.sep  # the repository's own OFL fonts


def font(name, size):
    return ImageFont.truetype(FONTS + name if not name.startswith('/') else name, size)


def hexc(v):
    return np.array([(v >> 16) & 255, (v >> 8) & 255, v & 255], float) / 255


# ---------------------------------------------------------------- vectors
def norm(v):
    return v / np.maximum(np.linalg.norm(v, axis=-1, keepdims=True), 1e-9)


def dot(a, b):
    return np.sum(a * b, axis=-1)


# ---------------------------------------------------------------- SDF primitives (p: N x 3)
def sd_sphere(p, c, r):
    return np.linalg.norm(p - np.asarray(c, float), axis=-1) - r


def sd_box(p, c, b, rad=0.0):
    q = np.abs(p - np.asarray(c, float)) - np.asarray(b, float) + rad
    return np.linalg.norm(np.maximum(q, 0), axis=-1) + np.minimum(np.max(q, axis=-1), 0) - rad


def sd_cyl(p, c, r, h):  # vertical, centre c, radius r, half height h
    q = p - np.asarray(c, float)
    d0 = np.hypot(q[:, 0], q[:, 2]) - r
    d1 = np.abs(q[:, 1]) - h
    return np.minimum(np.maximum(d0, d1), 0) + np.hypot(np.maximum(d0, 0), np.maximum(d1, 0))


def sd_fluted(p, c, r, h, flutes=16, depth=0.03):
    q = p - np.asarray(c, float)
    a = np.arctan2(q[:, 2], q[:, 0])
    rr = r - depth * (0.5 + 0.5 * np.cos(a * flutes)) ** 4
    d0 = np.hypot(q[:, 0], q[:, 2]) - rr
    d1 = np.abs(q[:, 1]) - h
    return np.minimum(np.maximum(d0, d1), 0) + np.hypot(np.maximum(d0, 0), np.maximum(d1, 0))


def sd_torus(p, c, R, r, tilt=0.0):
    q = p - np.asarray(c, float)
    ct, st = np.cos(tilt), np.sin(tilt)
    y = q[:, 1] * ct - q[:, 0] * st
    x = q[:, 1] * st + q[:, 0] * ct
    return np.hypot(np.hypot(x, q[:, 2]) - R, y) - r


def sd_ellipsoid(p, c, r):
    r = np.asarray(r, float)
    q = (p - np.asarray(c, float))
    k0 = np.linalg.norm(q / r, axis=-1)
    k1 = np.linalg.norm(q / (r * r), axis=-1)
    return k0 * (k0 - 1) / np.maximum(k1, 1e-9)


def smin(a, b, k):
    h = np.clip(0.5 + 0.5 * (b - a) / k, 0, 1)
    return b + (a - b) * h - k * h * (1 - h)


class Scene:
    """Collects (distance, material) pairs; union keeps the nearest."""

    def __init__(self, p):
        self.p = p
        self.d = np.full(len(p), 1e9)
        self.m = np.zeros(len(p), int)

    def add(self, d, mat):
        closer = d < self.d
        self.d = np.where(closer, d, self.d)
        self.m = np.where(closer, mat, self.m)


# ---------------------------------------------------------------- the raymarcher
def camera(eye, look, fov, W=S, H=S):
    eye = np.asarray(eye, float)
    f = norm(np.asarray(look, float) - eye)
    r = norm(np.cross(f, [0, 1, 0]))
    u = np.cross(r, f)
    ys, xs = np.mgrid[0:H, 0:W]
    k = np.tan(fov / 2)
    sx = (2 * (xs + .5) / W - 1) * k
    sy = (1 - 2 * (ys + .5) / H) * k * H / W
    d = norm(f[None, None] + sx[..., None] * r + sy[..., None] * u)
    return eye, d.reshape(-1, 3)


def march(scene_fn, o, d, steps=110, tmax=60.0):
    n = len(d)
    t = np.full(n, 0.002)
    hit = np.zeros(n, bool)
    live = np.ones(n, bool)
    mat = np.zeros(n, int)
    for _ in range(steps):
        idx = np.nonzero(live)[0]
        if len(idx) == 0:
            break
        oo = o if o.ndim == 1 else o[idx]
        p = oo + d[idx] * t[idx, None]
        sc = scene_fn(p)
        near = sc.d < 0.0008 * (1 + t[idx])
        hit[idx[near]] = True
        mat[idx[near]] = sc.m[near]
        t[idx] += np.where(near, 0, sc.d * 0.85)
        live[idx[near | (t[idx] > tmax)]] = False
    return t, hit, mat


def normal(scene_fn, p):
    e = 0.0015
    n = np.zeros_like(p)
    for i in range(3):
        dv = np.zeros(3); dv[i] = e
        n[:, i] = scene_fn(p + dv).d - scene_fn(p - dv).d
    return norm(n)


def shadow(scene_fn, p, l, steps=40):
    t = np.full(len(p), 0.02)
    res = np.ones(len(p))
    for _ in range(steps):
        h = scene_fn(p + l * t[:, None]).d
        res = np.minimum(res, np.clip(10 * h / t, 0, 1))
        t += np.clip(h, 0.01, 0.4)
    return np.clip(res, 0, 1)


def render(scene_fn, shade_fn, sky_fn, eye, look, fov, W=S, H=S, light=(-.5, .7, .5), reflect=True, shadows=True):
    """shade_fn(mat, p, n, v) -> (albedo N x 3, mirror N, spec N, emit N x 3).  Returns rgb, coverage, depth."""
    o, d = camera(eye, look, fov, W, H)
    t, hit, mat = march(scene_fn, o, d)
    rgb = sky_fn(d)
    l = norm(np.asarray(light, float))
    if hit.any():
        idx = np.nonzero(hit)[0]
        p = o + d[idx] * t[idx, None]
        n = normal(scene_fn, p)
        alb, mirror, spec, emit = shade_fn(mat[idx], p, n, d[idx])
        diff = np.clip(dot(n, l), 0, 1)
        if shadows:
            diff = diff * shadow(scene_fn, p + n * 0.004, np.broadcast_to(l, p.shape).copy())
        h = norm(l - d[idx])
        sp = spec * np.clip(dot(n, h), 0, 1) ** 40
        amb = 0.35 + 0.25 * n[:, 1]
        col = alb * (amb + 0.85 * diff)[:, None] + sp[:, None] + emit
        if reflect and (mirror > 0).any():
            ri = np.nonzero(mirror > 0)[0]
            rd = norm(d[idx[ri]] - 2 * dot(d[idx[ri]], n[ri])[:, None] * n[ri])
            ro = p[ri] + n[ri] * 0.004
            rt, rhit, rmat = march(scene_fn, ro, rd, steps=70)
            rc = sky_fn(rd)
            if rhit.any():
                j = np.nonzero(rhit)[0]
                rp = ro[j] + rd[j] * rt[j, None]
                rn = normal(scene_fn, rp)
                a2, _, s2, e2 = shade_fn(rmat[j], rp, rn, rd[j])
                rc[j] = a2 * (0.4 + 0.25 * rn[:, 1] + 0.8 * np.clip(dot(rn, l), 0, 1))[:, None] + e2
            fres = mirror[ri] * (0.6 + 0.4 * (1 - np.abs(dot(d[idx[ri]], n[ri]))) ** 3)
            col[ri] = col[ri] * (1 - fres[:, None]) + rc * fres[:, None]
        rgb[idx] = col
    return rgb.reshape(H, W, 3), hit.reshape(H, W).astype(float), t.reshape(H, W)


# ---------------------------------------------------------------- materials and skies
def checker(p, scale, a, b, axes=(0, 2)):
    c = (np.floor(p[:, axes[0]] / scale) + np.floor(p[:, axes[1]] / scale)) % 2
    return np.where(c[:, None] > 0, a, b)


def vnoise3(p, seed=0):
    """cheap 3D value noise (vectorised)."""
    i = np.floor(p).astype(int)
    f = p - i
    f = f * f * (3 - 2 * f)

    def h(x, y, z):
        v = (x * 73856093) ^ (y * 19349663) ^ (z * 83492791) ^ (seed * 2654435761)
        v = (v ^ (v >> 13)) * 1274126177
        return ((v ^ (v >> 16)) & 0xFFFF) / 65535.0

    x, y, z = i[:, 0], i[:, 1], i[:, 2]
    out = 0
    for dz in (0, 1):
        for dy in (0, 1):
            for dx in (0, 1):
                w = (f[:, 0] if dx else 1 - f[:, 0]) * (f[:, 1] if dy else 1 - f[:, 1]) * (f[:, 2] if dz else 1 - f[:, 2])
                out = out + w * h(x + dx, y + dy, z + dz)
    return out


def fbm3(p, octaves=4, seed=0):
    s, a, tot = 0, 0.5, 0
    for k in range(octaves):
        s = s + a * vnoise3(p * (2 ** k), seed + k)
        tot += a
        a *= 0.5
    return s / tot


def marble(p, base=0xF2EEE8, vein=0x9A9490, scale=3.0):
    t = fbm3(p * scale, 5)
    v = np.abs(np.sin((p[:, 0] * 2 + p[:, 1] * 3 + 6 * t) * np.pi))
    k = 1 - np.clip(v / 0.08, 0, 1)
    return hexc(base)[None] * (1 - 0.6 * k[:, None]) + hexc(vein)[None] * 0.6 * k[:, None]


def sky_gradient(stops):
    """stops: list of (height -1..1, 0xRRGGBB); by ray elevation."""
    hs = np.array([s[0] for s in stops])
    cs = np.array([hexc(s[1]) for s in stops])

    def fn(d):
        y = d[:, 1]
        out = np.zeros((len(d), 3))
        for c in range(3):
            out[:, c] = np.interp(y, hs, cs[:, c])
        return out
    return fn


# ---------------------------------------------------------------- 2D layers (PIL RGBA at S x S)
def layer():
    return Image.new('RGBA', (S, S), (0, 0, 0, 0))


def to_img(rgb, alpha=None):
    a = np.clip(rgb, 0, 1)
    im = Image.fromarray((a * 255 + .5).astype(np.uint8), 'RGB')
    if alpha is not None:
        im.putalpha(Image.fromarray((np.clip(alpha, 0, 1) * 255).astype(np.uint8), 'L'))
    return im


def arr(im):
    return np.asarray(im.convert('RGB'), float) / 255


def shadowed(im, off=(6, 6), blur=6, opacity=.55, color=(0, 0, 0)):
    """the layer with a soft drop shadow under it"""
    a = im.split()[3]
    sh = Image.new('RGBA', im.size, color + (0,))
    sh.putalpha(a.point(lambda v: int(v * opacity)))
    sh = sh.filter(ImageFilter.GaussianBlur(blur))
    out = layer()
    out.alpha_composite(sh, off)
    out.alpha_composite(im)
    return out


def glow(im, radius=10, strength=1.0):
    g = im.filter(ImageFilter.GaussianBlur(radius))
    a = np.asarray(g, float)
    a[..., 3] *= strength
    return Image.alpha_composite(Image.fromarray(np.clip(a, 0, 255).astype(np.uint8), 'RGBA'), im)


def palm(d, x, y, s, col, lean=0.25, fronds=9, seed=1):
    rng = np.random.default_rng(seed)
    pts = []
    for i in range(21):
        t = i / 20
        pts.append((x + lean * 160 * s * t * t, y - 300 * s * t))
    for i in range(20):
        w = 9 * s * (1 - .45 * i / 20)
        x0, y0 = pts[i]
        x1, y1 = pts[i + 1]
        d.polygon([(x0 - w, y0), (x0 + w, y0), (x1 + w * .95, y1), (x1 - w * .95, y1)], fill=col)
        d.line([(x0 - w, y0 - 2), (x0 + w, y0 - 4)], fill=tuple(int(c * .7) for c in col[:3]) + (255,), width=max(1, int(2 * s)))
    tx, ty = pts[-1]
    for k in range(fronds):
        a = -np.pi + (k + .5) * np.pi * 2 / fronds + rng.uniform(-.2, .2)
        length = (120 + rng.uniform(-20, 30)) * s
        droop = 0.9 + rng.uniform(0, .5)
        spine = []
        for i in range(25):
            t = i / 24
            spine.append((tx + np.cos(a) * length * t, ty + np.sin(a) * length * t * .55 - 40 * s * t + droop * 70 * s * t * t))
        for i in range(24):
            (x0, y0), (x1, y1) = spine[i], spine[i + 1]
            leaf = 26 * s * np.sin(np.pi * (i + 1) / 25) ** .7
            nx, ny = -(y1 - y0), (x1 - x0)
            ln = max(np.hypot(nx, ny), 1e-6)
            nx, ny = nx / ln, ny / ln
            for side in (-1, 1):
                d.polygon([(x0, y0), (x1, y1), (x1 + side * nx * leaf + (x1 - x0) * 1.2, y1 + side * ny * leaf + 14 * s)], fill=col)
        d.line(spine, fill=col, width=max(2, int(4 * s)))


def catmull(pts, n=6, closed=True):
    out = []
    m = len(pts)
    rng = range(m) if closed else range(m - 1)
    for i in rng:
        p0, p1, p2, p3 = (np.array(pts[(i + k) % m], float) for k in (-1, 0, 1, 2))
        for k in range(n):
            t = k / n
            out.append(tuple(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (-p0 + 3 * p1 - 3 * p2 + p3) * t ** 3)))
    return out


def dolphin(d, x, y, s, col, belly, flip=False):
    """a bottlenose in profile, beak to the right: melon, short beak, falcate fin, flukes"""
    def P(px, py):
        return (x + (-px if flip else px) * s, y + py * s)
    body = [(132, 3), (118, -1), (104, -9), (96, -17), (82, -24), (50, -29), (14, -29), (-30, -23), (-70, -12), (-104, -1),
            (-118, 4), (-104, 10), (-70, 13), (-30, 21), (20, 26), (60, 21), (92, 12), (112, 6), (128, 6)]
    d.polygon([P(*q) for q in catmull(body)], fill=col)
    d.polygon([P(*q) for q in catmull([(92, 10), (60, 18), (20, 23), (-30, 18), (-60, 12), (-20, 25), (20, 27), (60, 22)])], fill=belly)
    fin = [(16, -27), (8, -40), (-4, -52), (-20, -60), (-16, -50), (-16, -38), (-26, -24)]
    d.polygon([P(*q) for q in catmull(fin, 4, False)] + [P(-26, -22)], fill=col)
    d.polygon([P(*q) for q in [(52, 21), (40, 38), (30, 46), (26, 42), (30, 26)]], fill=col)
    fl = [(-106, 2), (-128, -14), (-148, -20), (-142, -8), (-128, 4), (-142, 18), (-150, 30), (-128, 24), (-106, 8)]
    d.polygon([P(*q) for q in catmull(fl, 4)], fill=col)
    d.line([P(128, 5), P(110, 5), P(100, 8)], fill=tuple(int(c * .55) for c in col[:3]) + (255,), width=max(1, int(1.5 * s)))
    ex, ey = P(86, -8)
    d.ellipse([ex - 2.6 * s, ey - 2.6 * s, ex + 2.6 * s, ey + 2.6 * s], fill=(20, 20, 30, 255))


def win_dialog(w, h, title, lines, icon='x', buttons=('OK',), scale=1.0):
    """a Windows 95 dialog box, drawn at 1:1 in pixels of the S canvas"""
    im = Image.new('RGBA', (w, h), (192, 192, 192, 255))
    d = ImageDraw.Draw(im)
    d.line([(0, 0), (w - 1, 0)], fill=(223, 223, 223)); d.line([(0, 0), (0, h - 1)], fill=(223, 223, 223))
    d.line([(1, 1), (w - 2, 1)], fill=(255, 255, 255)); d.line([(1, 1), (1, h - 2)], fill=(255, 255, 255))
    d.line([(w - 1, 0), (w - 1, h - 1)], fill=(0, 0, 0)); d.line([(0, h - 1), (w - 1, h - 1)], fill=(0, 0, 0))
    d.line([(w - 2, 1), (w - 2, h - 2)], fill=(128, 128, 128)); d.line([(1, h - 2), (w - 2, h - 2)], fill=(128, 128, 128))
    th = int(22 * scale)
    for x in range(3, w - 3):  # the Windows 98 title gradient
        t = (x - 3) / max(1, w - 6)
        d.line([(x, 3), (x, 3 + th)], fill=(int(0 + 16 * t), int(0 + 132 * t), int(128 + 80 * t)))
    f = font('Carlito-Bold.ttf', int(15 * scale))
    d.text((8, 5), title, font=f, fill=(255, 255, 255))
    bx = w - 6 - int(18 * scale)
    d.rectangle([bx, 6, bx + int(16 * scale), 6 + int(14 * scale)], fill=(192, 192, 192), outline=(0, 0, 0))
    d.line([(bx + 4, 9), (bx + int(12 * scale), 3 + int(14 * scale))], fill=(0, 0, 0), width=2)
    d.line([(bx + int(12 * scale), 9), (bx + 4, 3 + int(14 * scale))], fill=(0, 0, 0), width=2)
    iy = th + int(18 * scale)
    r = int(16 * scale)
    if icon == 'x':
        d.ellipse([16, iy, 16 + 2 * r, iy + 2 * r], fill=(255, 0, 0), outline=(128, 0, 0))
        d.line([(16 + r * .55, iy + r * .55), (16 + r * 1.45, iy + r * 1.45)], fill=(255, 255, 255), width=int(4 * scale))
        d.line([(16 + r * 1.45, iy + r * .55), (16 + r * .55, iy + r * 1.45)], fill=(255, 255, 255), width=int(4 * scale))
    elif icon == '!':
        d.polygon([(16 + r, iy), (16 + 2 * r, iy + 2 * r), (16, iy + 2 * r)], fill=(255, 255, 0), outline=(0, 0, 0))
        d.text((16 + r - 3 * scale, iy + 6 * scale), '!', font=font('Carlito-Bold.ttf', int(22 * scale)), fill=(0, 0, 0))
    else:
        d.ellipse([16, iy, 16 + 2 * r, iy + 2 * r], fill=(255, 255, 255), outline=(0, 0, 128))
        d.text((16 + r - 4 * scale, iy + 2 * scale), 'i', font=font('LibreBaskerville-Bold.ttf', int(24 * scale)), fill=(0, 0, 160))
    fb = font('Carlito-Regular.ttf', int(14 * scale))
    for i, ln in enumerate(lines):
        d.text((24 + 2 * r, iy + 2 + i * int(17 * scale)), ln, font=fb, fill=(0, 0, 0))
    bw, bh = int(74 * scale), int(24 * scale)
    total = len(buttons) * bw + (len(buttons) - 1) * 8
    x0 = (w - total) // 2
    by = h - bh - int(12 * scale)
    for i, b in enumerate(buttons):
        x = x0 + i * (bw + 8)
        d.rectangle([x, by, x + bw, by + bh], fill=(192, 192, 192), outline=(0, 0, 0))
        d.line([(x + 1, by + 1), (x + bw - 2, by + 1)], fill=(255, 255, 255)); d.line([(x + 1, by + 1), (x + 1, by + bh - 2)], fill=(255, 255, 255))
        d.line([(x + bw - 1, by + 1), (x + bw - 1, by + bh - 1)], fill=(128, 128, 128)); d.line([(x + 1, by + bh - 1), (x + bw - 1, by + bh - 1)], fill=(128, 128, 128))
        tw = d.textlength(b, font=fb)
        d.text((x + (bw - tw) / 2, by + 4 * scale), b, font=fb, fill=(0, 0, 0))
    return im


def text_layer(txt, fnt, fill, xy, anchor='mm', spacing=0, chroma=0, italic=0.0, stroke=0, stroke_fill=None):
    """text with optional red/cyan chromatic offset and a shear for italics"""
    im = layer()
    d = ImageDraw.Draw(im)
    if spacing:
        widths = [d.textlength(ch, font=fnt) for ch in txt]
        total = sum(widths) + spacing * (len(txt) - 1)
        x = xy[0] - total / 2 if anchor[0] == 'm' else xy[0]
        for ch, w in zip(txt, widths):
            if chroma:
                d.text((x - chroma, xy[1]), ch, font=fnt, fill=(255, 40, 120, 200), anchor='l' + anchor[1])
                d.text((x + chroma, xy[1]), ch, font=fnt, fill=(40, 230, 255, 200), anchor='l' + anchor[1])
            d.text((x, xy[1]), ch, font=fnt, fill=fill, anchor='l' + anchor[1], stroke_width=stroke, stroke_fill=stroke_fill)
            x += w + spacing
    else:
        if chroma:
            d.text((xy[0] - chroma, xy[1]), txt, font=fnt, fill=(255, 40, 120, 200), anchor=anchor)
            d.text((xy[0] + chroma, xy[1]), txt, font=fnt, fill=(40, 230, 255, 200), anchor=anchor)
        d.text(xy, txt, font=fnt, fill=fill, anchor=anchor, stroke_width=stroke, stroke_fill=stroke_fill)
    if italic:
        im = im.transform(im.size, Image.AFFINE, (1, italic, -italic * xy[1], 0, 1, 0), Image.BICUBIC)
    return im


# ---------------------------------------------------------------- finishing
def duotone(rgb, dark, light, amount=1.0):
    lum = rgb @ np.array([.3, .59, .11])
    tone = hexc(dark)[None, None] * (1 - lum[..., None]) + hexc(light)[None, None] * lum[..., None]
    return rgb * (1 - amount) + tone * amount


def chroma(rgb, px=2):
    out = rgb.copy()
    out[..., 0] = np.roll(rgb[..., 0], -px, axis=1)
    out[..., 2] = np.roll(rgb[..., 2], px, axis=1)
    return out


def scanlines(rgb, depth=.12, period=4):
    ys = np.arange(rgb.shape[0])
    k = 1 - depth * ((ys % period) < period / 2)
    return rgb * k[:, None, None]


def grain(rgb, amount=.04, seed=3):
    rng = np.random.default_rng(seed)
    return rgb + rng.normal(0, amount, rgb.shape[:2])[..., None]


def haze(rgb, radius=3, amount=.25):
    im = to_img(rgb).filter(ImageFilter.GaussianBlur(radius))
    return rgb * (1 - amount) + arr(im) * amount + amount * .25 * arr(im) ** 2


def vhs(rgb, seed=1, bands=3):
    """a few tracking bands that slide rows sideways"""
    rng = np.random.default_rng(seed)
    out = rgb.copy()
    for _ in range(bands):
        y = rng.integers(0, rgb.shape[0] - 12)
        h = rng.integers(2, 10)
        out[y:y + h] = np.roll(out[y:y + h], rng.integers(-14, 14), axis=1)
        out[y:y + h] = out[y:y + h] * .85 + .15
    return out


def resample(img, width, height):
    """Every resize of the posters: Plan Paint's CONV, in linear light with premultiplied
    alpha, rounded to 8 bits only at the end (authoring/conv). It never rings."""
    if img.size == (width, height):
        return img.copy()
    return _conv.resize_srgb_image(img, width, height)


def fit_within(img, width, height):
    """Scaled down, keeping its shape, to fit within width x height (never enlarged)."""
    k = min(width / img.width, height / img.height, 1.0)
    return resample(img, max(1, round(img.width * k)), max(1, round(img.height * k)))


def finish(rgb, size=256):
    im = to_img(np.clip(rgb, 0, 1))
    return resample(im, size, size)


def sd_capsule(p, a, b, r):
    a = np.asarray(a, float); b = np.asarray(b, float)
    pa = p - a
    ba = b - a
    h = np.clip(dot(pa, ba[None]) / np.dot(ba, ba), 0, 1)
    return np.linalg.norm(pa - ba[None] * h[:, None], axis=-1) - r


# ---------------------------------------------------------------- the period's lettering
def mask_text(txt, fnt, xy, anchor='mm', spacing=0):
    m = Image.new('L', (S, S), 0)
    d = ImageDraw.Draw(m)
    if '\n' in txt:
        d.multiline_text(xy, txt, font=fnt, fill=255, anchor=anchor, align='center', spacing=spacing)
    else:
        d.text(xy, txt, font=fnt, fill=255, anchor=anchor)
    return m


def arch(img, amount, x0=0, x1=S):
    """bends a layer up into an arch (WordArt's favourite), column by column"""
    a = np.asarray(img).copy()
    out = np.zeros_like(a)
    for x in range(S):
        t = np.clip((x - x0) / max(1, x1 - x0), 0, 1)
        dy = int(round(-amount * np.sin(np.pi * t)))
        out[:, x] = np.roll(a[:, x], dy, axis=0)
    return Image.fromarray(out)


def fill_gradient(mask, stops, horizontal=False, box=None):
    """a layer: the mask filled with a gradient running down (or across) its own extent"""
    a = np.asarray(mask, float) / 255
    ys, xs = np.nonzero(a > .1)
    if box is None:
        box = (xs.min(), ys.min(), xs.max(), ys.max()) if len(xs) else (0, 0, S, S)
    x0, y0, x1, y1 = box
    t = (np.arange(S)[None, :] - x0) / max(1, x1 - x0) if horizontal else (np.arange(S)[:, None] - y0) / max(1, y1 - y0)
    t = np.clip(np.broadcast_to(t, (S, S)), 0, 1)
    pos = np.array([p for p, _ in stops])
    cols = np.array([hexc(c) for _, c in stops])
    rgb = np.stack([np.interp(t, pos, cols[:, k]) for k in range(3)], -1)
    return to_img(rgb, a)


RAINBOW = [(0, 0xFF2020), (.2, 0xFF9A10), (.4, 0xFFF020), (.6, 0x20D040), (.8, 0x2080FF), (1, 0xA030E0)]


def wordart(txt, fnt, xy, stops=RAINBOW, horizontal=True, bend=0, outline=2, outline_col=(30, 30, 30, 255), shadow=(6, 6), shadow_col=(120, 120, 120, 255)):
    m = mask_text(txt, fnt, xy)
    if bend:
        m = arch(m, bend)
    lay = layer()
    if shadow:
        sh = Image.new('RGBA', (S, S), shadow_col)
        sh.putalpha(m)
        lay.alpha_composite(sh, shadow)
    if outline:
        o = Image.new('RGBA', (S, S), outline_col)
        o.putalpha(m.filter(ImageFilter.MaxFilter(2 * outline + 1)))
        lay.alpha_composite(o)
    lay.alpha_composite(fill_gradient(m, stops, horizontal))
    return lay


def chrome_text(txt, fnt, xy, angle=0, outline=6, depth=5, sky=0x2040C0, ground=0x3A1A08, horizon=0xF8C890, extrude=(40, 10, 60)):
    """bevelled chrome lettering: the mask blurred into a height, lit by a sky over a ground"""
    m = mask_text(txt, fnt, xy)
    if angle:
        m = m.rotate(angle, center=xy, resample=Image.BICUBIC)
    a = np.asarray(m, float) / 255
    h = np.asarray(m.filter(ImageFilter.GaussianBlur(4)), float) / 255
    gy, gx = np.gradient(h)
    n = np.dstack([-gx * 14, -gy * 14, np.ones_like(h)])
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    ny = -n[..., 1]
    up = hexc(sky)[None, None] * (1 - ny[..., None]) * .6 + hexc(0xE8F4FF)[None, None] * np.clip(ny[..., None] + .2, 0, 1)
    down = hexc(ground)[None, None] * .8 + hexc(horizon)[None, None] * np.clip(-ny[..., None], 0, 1)
    env = np.where((ny > -.05)[..., None], up, down)
    spec = np.clip(n[..., 0] * -.6 + ny * .6, 0, 1) ** 12
    chrome = to_img(env + spec[..., None], a)
    ol = m.filter(ImageFilter.MaxFilter(2 * outline + 1))
    base = layer()
    for k in range(depth, 0, -1):
        sh = Image.new('RGBA', (S, S), extrude + (255,))
        sh.putalpha(ol)
        base.alpha_composite(sh, (k * 2, k * 2))
    edge = Image.new('RGBA', (S, S), (10, 10, 30, 255))
    edge.putalpha(ol)
    base.alpha_composite(edge)
    base.alpha_composite(chrome)
    return base


def gold_text(txt, fnt, xy, angle=0, outline=5, depth=4):
    return chrome_text(txt, fnt, xy, angle, outline, depth, sky=0x8A5A10, ground=0x4A2A04, horizon=0xFFE890, extrude=(90, 40, 0))


def neon_text(txt, fnt, xy, col, width=3, glow_r=10):
    """a bent glass tube: the letters' outline, glowing, with a white-hot core"""
    m = mask_text(txt, fnt, xy)
    ring = np.asarray(m.filter(ImageFilter.MaxFilter(2 * width + 1)), float) - np.asarray(m.filter(ImageFilter.MinFilter(2 * width + 1)), float)
    ring = Image.fromarray(np.clip(ring, 0, 255).astype(np.uint8))
    tube = Image.new('RGBA', (S, S), col + (255,))
    tube.putalpha(ring)
    core = Image.new('RGBA', (S, S), (255, 255, 255, 255))
    core.putalpha(ring.filter(ImageFilter.MinFilter(3)))
    lay = glow(glow(tube, glow_r, 1.6), glow_r // 3, 1)
    lay.alpha_composite(core)
    return lay


def outlined(txt, fnt, xy, fill, stroke=3, stroke_fill=(0, 0, 0, 255), anchor='mm', angle=0, shadow=None):
    lay = layer()
    d = ImageDraw.Draw(lay)
    if shadow:
        d.text((xy[0] + shadow[0], xy[1] + shadow[1]), txt, font=fnt, anchor=anchor, fill=(0, 0, 0, 160), stroke_width=stroke, stroke_fill=(0, 0, 0, 160))
    d.text(xy, txt, font=fnt, anchor=anchor, fill=fill, stroke_width=stroke, stroke_fill=stroke_fill)
    if angle:
        lay = lay.rotate(angle, center=xy, resample=Image.BICUBIC)
    return lay


# ---------------------------------------------------------------- furniture
def starburst(cx, cy, r, points, fill, line, text, fnt, angle=-12, text_fill=(200, 20, 30, 255)):
    lay = layer()
    d = ImageDraw.Draw(lay)
    pts = []
    for k in range(points * 2):
        a = k * np.pi / points + np.radians(angle)
        rr = r if k % 2 == 0 else r * .74
        pts.append((cx + np.cos(a) * rr, cy + np.sin(a) * rr))
    d.polygon(pts, fill=line)
    d.polygon([(cx + (x - cx) * .9, cy + (y - cy) * .9) for x, y in pts], fill=fill)
    tl = layer()
    td = ImageDraw.Draw(tl)
    lines = text.split('\n')
    for i, ln in enumerate(lines):
        td.text((cx, cy + (i - (len(lines) - 1) / 2) * fnt.size * .95), ln, font=fnt, anchor='mm', fill=text_fill, stroke_width=2, stroke_fill=(255, 255, 255, 255))
    lay.alpha_composite(tl.rotate(-angle * .8, center=(cx, cy), resample=Image.BICUBIC))
    return shadowed(lay, (5, 5), 3, .5)


def gel(box, col, radius=None, text=None, fnt=None, text_fill=(255, 255, 255, 255)):
    """an aqua gel button: a rounded lozenge, darker below, a glassy highlight across its top"""
    x0, y0, x1, y1 = box
    radius = radius or (y1 - y0) // 2
    m = Image.new('L', (S, S), 0)
    ImageDraw.Draw(m).rounded_rectangle(box, radius, fill=255)
    base = hexc(col)
    lay = fill_gradient(m, [(0, col), (.55, col), (1, int(''.join(f'{min(255, int(c * 255 * 1.35)):02x}' for c in base), 16))], box=box)
    hl = Image.new('L', (S, S), 0)
    ImageDraw.Draw(hl).rounded_rectangle([x0 + 6, y0 + 3, x1 - 6, y0 + (y1 - y0) * .48], radius, fill=170)
    hi = fill_gradient(hl, [(0, 0xFFFFFF), (1, 0xFFFFFF)], box=box)
    a = np.asarray(hi, float)
    ramp = np.clip(1 - (np.arange(S)[:, None] - y0) / max(1, (y1 - y0) * .5), 0, 1)
    a[..., 3] *= ramp * .85
    lay.alpha_composite(Image.fromarray(a.astype(np.uint8)))
    ol = Image.new('L', (S, S), 0)
    ImageDraw.Draw(ol).rounded_rectangle(box, radius, outline=255, width=2)
    edge = Image.new('RGBA', (S, S), tuple(int(c * 255 * .5) for c in base) + (255,))
    edge.putalpha(ol)
    lay.alpha_composite(edge)
    if text:
        ImageDraw.Draw(lay).text(((x0 + x1) / 2, (y0 + y1) / 2), text, font=fnt, anchor='mm', fill=text_fill, stroke_width=1, stroke_fill=(0, 0, 0, 90))
    return shadowed(lay, (3, 4), 4, .4)


def brushed_metal(box, tone=0xB8BCC4, circular=None, seed=1):
    """a brushed aluminium plate: fine streaks along it (or round a centre)"""
    x0, y0, x1, y1 = box
    rng = np.random.default_rng(seed)
    ys, xs = np.mgrid[0:S, 0:S].astype(float)
    if circular:
        cx, cy = circular
        r = np.hypot(xs - cx, ys - cy)
        streak = np.interp(r, np.arange(800), rng.standard_normal(800))
    else:
        rowsn = rng.standard_normal(S)
        streak = rowsn[ys.astype(int)] + .4 * rng.standard_normal((S, S))
    k = .82 + .05 * streak + .12 * (1 - (ys - y0) / max(1, y1 - y0))
    rgb = hexc(tone)[None, None] * k[..., None]
    m = Image.new('L', (S, S), 0)
    ImageDraw.Draw(m).rounded_rectangle(box, 8, fill=255)
    return to_img(rgb, np.asarray(m, float) / 255)


def bolts(lay, pts, r=5):
    d = ImageDraw.Draw(lay)
    for x, y in pts:
        d.ellipse([x - r, y - r, x + r, y + r], fill=(120, 124, 132, 255), outline=(40, 40, 46, 255))
        d.line([(x - r * .6, y - r * .6), (x + r * .6, y + r * .6)], fill=(40, 40, 46, 255), width=2)
    return lay


def lens_flare(cx, cy, size=1.0, col=(255, 240, 200)):
    """the flare the period put on everything: a hot point, a halo, a streak, ghosts toward the centre"""
    lay = layer()
    d = ImageDraw.Draw(lay)
    for r, a in ((60, 40), (30, 90), (12, 220)):
        rr = r * size
        d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], fill=col + (a,))
    d.line([(cx - 160 * size, cy), (cx + 160 * size, cy)], fill=col + (180,), width=max(1, int(3 * size)))
    d.ellipse([cx - 70 * size, cy - 70 * size, cx + 70 * size, cy + 70 * size], outline=(255, 200, 150, 70), width=3)
    vx, vy = 256 - cx, 256 - cy
    for t, r, c in ((.5, 14, (120, 255, 200)), (.8, 24, (200, 120, 255)), (1.2, 10, (255, 200, 120)), (1.5, 30, (120, 180, 255))):
        x, y, rr = cx + vx * t, cy + vy * t, r * size
        d.ellipse([x - rr, y - rr, x + rr, y + rr], fill=c + (50,))
    return glow(lay.filter(ImageFilter.GaussianBlur(1.5)), 8, .8)


def lightning(lay, p0, p1, col=(200, 230, 255), seed=1, width=3):
    rng = np.random.default_rng(seed)
    d = ImageDraw.Draw(lay)
    pts = [p0]
    n = 12
    for k in range(1, n):
        t = k / n
        x = p0[0] + (p1[0] - p0[0]) * t + rng.uniform(-14, 14)
        y = p0[1] + (p1[1] - p0[1]) * t + rng.uniform(-14, 14)
        pts.append((x, y))
        if rng.random() < .25:
            d.line([(x, y), (x + rng.uniform(-40, 40), y + rng.uniform(10, 40))], fill=col + (200,), width=1)
    pts.append(p1)
    d.line(pts, fill=col + (255,), width=width)
    d.line(pts, fill=(255, 255, 255, 255), width=1)


def slime(lay, cx, cy, r, col, seed=1, drips=5):
    """a splat of slime: a lumpy blob, drips running down, a wet highlight"""
    rng = np.random.default_rng(seed)
    d = ImageDraw.Draw(lay)
    pts = []
    for k in range(36):
        a = k * 2 * np.pi / 36
        rr = r * (1 + .22 * np.sin(3 * a + seed) + .12 * rng.standard_normal())
        pts.append((cx + np.cos(a) * rr, cy + np.sin(a) * rr * .85))
    d.polygon(pts, fill=col)
    for _ in range(drips):
        x = cx + rng.uniform(-r * .8, r * .8)
        L = rng.uniform(r * .4, r * 1.6)
        w = rng.uniform(r * .08, r * .18)
        d.rounded_rectangle([x - w, cy, x + w, cy + r * .5 + L], w, fill=col)
        d.ellipse([x - w * 1.4, cy + r * .5 + L - w, x + w * 1.4, cy + r * .5 + L + w * 1.8], fill=col)
    for _ in range(6):
        x, y, rr = cx + rng.uniform(-r * 1.6, r * 1.6), cy + rng.uniform(-r * 1.2, r * 1.2), rng.uniform(r * .06, r * .16)
        d.ellipse([x - rr, y - rr, x + rr, y + rr], fill=col)
    hl = tuple(min(255, int(c * 1.5 + 60)) for c in col[:3]) + (230,)
    d.ellipse([cx - r * .5, cy - r * .55, cx - r * .1, cy - r * .35], fill=hl)
    d.ellipse([cx + r * .15, cy - r * .5, cx + r * .3, cy - r * .4], fill=hl)
    return lay


def stripes(box, a=(250, 210, 0), b=(20, 20, 20), w=14):
    """warning stripes, as across a construction barrier"""
    x0, y0, x1, y1 = box
    lay = layer()
    d = ImageDraw.Draw(lay)
    d.rectangle(box, fill=a + (255,))
    for k in range(-int(y1 - y0), int(x1 - x0) + 40, 2 * w):
        d.polygon([(x0 + k, y1), (x0 + k + w, y1), (x0 + k + w + (y1 - y0), y0), (x0 + k + (y1 - y0), y0)], fill=b + (255,))
    m = Image.new('L', (S, S), 0)
    ImageDraw.Draw(m).rectangle(box, fill=255)
    out = layer()
    out.paste(lay, (0, 0), m)
    return out


def win95(w, h, title, active=True, gradient=False):
    """a bare Windows 95 window: grey, bevelled, a navy title bar (gradient for 98), its three buttons"""
    im = Image.new('RGBA', (w, h), (192, 192, 192, 255))
    d = ImageDraw.Draw(im)
    for (x0, y0, x1, y1), c in (((0, 0, w - 1, 0), (223, 223, 223)), ((0, 0, 0, h - 1), (223, 223, 223)), ((1, 1, w - 2, 1), (255, 255, 255)),
                                ((1, 1, 1, h - 2), (255, 255, 255)), ((w - 1, 0, w - 1, h - 1), (0, 0, 0)), ((0, h - 1, w - 1, h - 1), (0, 0, 0)),
                                ((w - 2, 1, w - 2, h - 2), (128, 128, 128)), ((1, h - 2, w - 2, h - 2), (128, 128, 128))):
        d.line([(x0, y0), (x1, y1)], fill=c)
    for x in range(3, w - 3):
        t = (x - 3) / max(1, w - 6)
        c = (int(16 * t), int(132 * t), int(128 + 80 * t)) if gradient else (0, 0, 128)
        if not active:
            c = (128, 128, 128)
        d.line([(x, 3), (x, 22)], fill=c)
    d.text((8, 5), title, font=font('Carlito-Bold.ttf', 15), fill=(255, 255, 255))
    for k, glyph in enumerate(('x', '□', '_')):
        bx = w - 22 - k * 18
        d.rectangle([bx, 6, bx + 15, 19], fill=(192, 192, 192), outline=(0, 0, 0))
        d.line([(bx + 1, 7), (bx + 14, 7)], fill=(255, 255, 255))
        if glyph == 'x':
            d.line([(bx + 4, 9), (bx + 11, 16)], fill=(0, 0, 0), width=2)
            d.line([(bx + 11, 9), (bx + 4, 16)], fill=(0, 0, 0), width=2)
        elif glyph == '_':
            d.line([(bx + 4, 15), (bx + 10, 15)], fill=(0, 0, 0), width=2)
        else:
            d.rectangle([bx + 4, 9, bx + 11, 15], outline=(0, 0, 0))
    return im


def button95(d, box, label, fnt, pressed=False):
    x0, y0, x1, y1 = box
    d.rectangle(box, fill=(192, 192, 192), outline=(0, 0, 0))
    hi, lo = ((128, 128, 128), (255, 255, 255)) if pressed else ((255, 255, 255), (128, 128, 128))
    d.line([(x0 + 1, y0 + 1), (x1 - 2, y0 + 1)], fill=hi)
    d.line([(x0 + 1, y0 + 1), (x0 + 1, y1 - 2)], fill=hi)
    d.line([(x1 - 1, y0 + 1), (x1 - 1, y1 - 1)], fill=lo)
    d.line([(x0 + 1, y1 - 1), (x1 - 1, y1 - 1)], fill=lo)
    d.text(((x0 + x1) / 2, (y0 + y1) / 2), label, font=fnt, anchor='mm', fill=(0, 0, 0))


def teal_desktop():
    return Image.new('RGBA', (S, S), (0, 128, 128, 255))


def clouds_sky(seed=2, top=0x2A6AD8, bottom=0x9AC8F8):
    ys, xs = np.mgrid[0:S, 0:S] / S
    pts = np.stack([xs.ravel() * 6, ys.ravel() * 9, np.full(S * S, float(seed))], -1)
    cl = fbm3(pts, 5, seed).reshape(S, S)
    skyc = hexc(top)[None, None] * (1 - ys[..., None]) + hexc(bottom)[None, None] * ys[..., None]
    cloud = np.clip((cl - .48) * 4, 0, 1)[..., None]
    return to_img(skyc * (1 - cloud) + .98 * cloud).convert('RGBA')


def sticker(cx, cy, r, ring_text, centre_text, fnt, col=(220, 30, 40), text_col=(255, 255, 255, 255), holo=False):
    """a round sticker; holographic if asked (a rainbow sheen)"""
    lay = layer()
    d = ImageDraw.Draw(lay)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=col + (255,), outline=(255, 255, 255, 255), width=3)
    if holo:
        ys, xs = np.mgrid[0:S, 0:S]
        hue = ((xs + ys) / 60.0) % 1
        rgb = np.stack([.6 + .4 * np.cos(2 * np.pi * (hue + k / 3)) for k in range(3)], -1)
        m = Image.new('L', (S, S), 0)
        ImageDraw.Draw(m).ellipse([cx - r + 3, cy - r + 3, cx + r - 3, cy + r - 3], fill=200)
        lay.alpha_composite(to_img(rgb, np.asarray(m, float) / 255 * .85))
    d.multiline_text((cx, cy), centre_text, font=fnt, anchor='mm', align='center', fill=text_col, spacing=0)
    if ring_text:
        small = font('BarlowCondensed-Bold.ttf', max(9, int(r * .2)))
        n = len(ring_text)
        for i, ch in enumerate(ring_text):
            a = -np.pi / 2 + (i - n / 2) * .26
            g = layer()
            ImageDraw.Draw(g).text((cx + np.cos(a) * r * .78, cy + np.sin(a) * r * .78), ch, font=small, anchor='mm', fill=text_col)
            lay.alpha_composite(g.rotate(-np.degrees(a) - 90, center=(cx + np.cos(a) * r * .78, cy + np.sin(a) * r * .78)))
    return shadowed(lay, (3, 3), 2, .4)


def cd_badge(cx, cy, r=30, label='CD-ROM'):
    lay = layer()
    d = ImageDraw.Draw(lay)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(220, 220, 230, 255), outline=(40, 40, 60, 255), width=2)
    for k, c in enumerate(((255, 120, 200), (120, 220, 255), (255, 240, 140))):
        d.arc([cx - r + 6 + k * 3, cy - r + 6 + k * 3, cx + r - 6 - k * 3, cy + r - 6 - k * 3], 200, 320, fill=c + (255,), width=3)
    d.ellipse([cx - r * .23, cy - r * .23, cx + r * .23, cy + r * .23], fill=(40, 40, 60, 255))
    d.text((cx, cy + r + 8), label, font=font('BarlowCondensed-Bold.ttf', 13), fill=(255, 255, 255, 255), anchor='mm', stroke_width=2, stroke_fill=(0, 0, 0, 255))
    return lay


def frame_shot(lay, path, box, border=(240, 240, 240, 255)):
    """a screenshot with a chunky border, as on the back of a box"""
    x0, y0, x1, y1 = box
    im = resample(Image.open(path).convert('RGB'), x1 - x0, y1 - y0)
    d = ImageDraw.Draw(lay)
    d.rectangle([x0 - 4, y0 - 4, x1 + 3, y1 + 3], fill=border, outline=(0, 0, 0, 255), width=2)
    lay.paste(im, (x0, y0))
    return lay


def paste(canvas, lay, xy=(0, 0), shadow=None):
    tmp = layer()
    tmp.alpha_composite(lay, xy)
    canvas.alpha_composite(shadowed(tmp, *shadow) if shadow else tmp)
