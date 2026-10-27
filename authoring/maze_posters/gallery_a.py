"""Maze 95 art, part one: renders from 1996 3D software, fractals, Frutiger Aero, airbrush.
Each returns an S x S picture; frames.py sets it in its frame."""
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

from kit import *
import ads


def fogged(rgb, t, col, near, far):
    k = np.clip((t - near) / (far - near), 0, 1)[..., None]
    return rgb * (1 - k) + hexc(col)[None, None] * k


def disc_sky(stops, discs=(), sun=None):
    base = sky_gradient(stops)

    def fn(d):
        out = base(d)
        for (dx, dy, dz), r, col in discs:
            v = norm(np.array([dx, dy, dz], float))
            c = np.clip(d @ v, -1, 1)
            out = np.where((c > np.cos(r))[:, None], hexc(col)[None], out)
        if sun is not None:
            v = norm(np.array(sun, float))
            out = out + np.clip(d @ v, 0, 1)[:, None] ** 60 * hexc(0xFFE8B0)[None] * 1.5
        return out
    return fn


# ---------------------------------------------------------------- Bryce and its friends
def terrain(x, z, scale=.3, height=3.0, seed=4, near=-2.0):
    pts = np.stack([x * scale, z * scale, np.full(len(x), float(seed))], -1)
    return (fbm3(pts, 5, seed) - .38) * height * np.clip((-z + near) / 4, 0, 1)


def bryce_sunset():
    def scene(p):
        sc = Scene(p)
        sc.add((p[:, 1] - terrain(p[:, 0], p[:, 2])) * .45, 1)
        sc.add(p[:, 1] + .02, 2)
        sc.add(sd_sphere(p, (.3, 1.3, -2.2), .55), 3)
        return sc

    def shade(mat, p, n, v):
        rock = hexc(0x7A4A3A)[None] * (.65 + .35 * fbm3(p * 3, 3)[:, None])
        alb = np.where((mat == 1)[:, None], rock, hexc(0x2A0A30)[None])
        alb = np.where((mat == 3)[:, None], hexc(0x101010)[None], alb)
        return alb, np.select([mat == 3, mat == 2], [.95, .75], 0), np.full(len(p), .6), np.zeros((len(p), 3))
    sky = disc_sky([(-1, 0xFF8040), (0, 0xFF6A50), (.12, 0xFFA060), (.35, 0xB04A90), (1, 0x200A40)], sun=(0, .08, -1))
    rgb, cov, t = render(scene, shade, sky, (0, .9, 3.5), (0, .8, -3), 1.0, light=(0, .25, -1))
    return to_img(fogged(rgb, t, 0xFF8A70, 6, 30))


def bryce_cubes():
    cubes = [((-1.4, 1.2, -2.5), .35), ((.2, 1.9, -4), .5), ((1.5, .9, -2), .3), ((-.3, .7, -1.2), .22), ((2.4, 2.4, -6), .7)]

    def scene(p):
        sc = Scene(p)
        sc.add(p[:, 1], 1)
        for c, r in cubes:
            sc.add(sd_box(p, c, (r, r, r), .03), 2)
        return sc

    def shade(mat, p, n, v):
        alb = np.where((mat == 1)[:, None], checker(p, 1.0, hexc(0xE8E0D0), hexc(0x30283A)), hexc(0x90E8F0)[None])
        return alb, np.select([mat == 2, mat == 1], [.6, .25], 0), np.full(len(p), 1.0), np.where((mat == 2)[:, None], hexc(0x205060)[None] * .5, 0)
    sky = disc_sky([(-1, 0x40C0B0), (.1, 0x60D0C0), (1, 0x902070)], discs=(((-.5, .5, -1), .08, 0xF0F0FF), ((.3, .6, -1), .04, 0xFFC0E0)))
    rgb, cov, t = render(scene, shade, sky, (0, 1.4, 3.2), (0, 1.2, -3), 1.0, light=(-.4, .7, .4))
    return to_img(fogged(rgb, t, 0x60D0C0, 8, 40))


def bryce_arch():
    def scene(p):
        sc = Scene(p)
        q = p.copy()
        q[:, 1] -= .0
        arch = sd_torus(np.stack([p[:, 0], p[:, 2] + 3, p[:, 1]], -1), (0, 0, 0), 1.3, .38)
        bump = .08 * (fbm3(p * 2.5, 3) - .5)
        sc.add(arch + bump, 1)
        sc.add(p[:, 1] + .1 + .02 * np.sin(p[:, 0] * 3 + p[:, 2] * 2), 2)
        return sc

    def shade(mat, p, n, v):
        rock = hexc(0xC89070)[None] * (.6 + .4 * fbm3(p * 4, 3)[:, None])
        alb = np.where((mat == 1)[:, None], rock, hexc(0x1A3A5A)[None])
        return alb, np.where(mat == 2, .7, 0), np.full(len(p), .4), np.zeros((len(p), 3))
    sky = disc_sky([(-1, 0xF0A0C0), (0, 0xF8C0D0), (.4, 0xC080C0), (1, 0x502870)], sun=(.3, .15, -1))
    rgb, cov, t = render(scene, shade, sky, (.4, .6, 2.4), (0, .9, -3), 1.05, light=(.3, .4, -1))
    return to_img(fogged(rgb, t, 0xF0B0D0, 4, 25))


def bryce_desert():
    def scene(p):
        sc = Scene(p)
        sc.add(p[:, 1] - .25 * np.sin(p[:, 0] * .8) * np.sin(p[:, 2] * .6) - .1 * fbm3(p * .8, 3), 1)
        for cx, cz, s in ((-2.5, -7, 1.6), (1.5, -10, 2.4), (5, -14, 2)):
            pyr = np.maximum(np.abs(p[:, 0] - cx) + p[:, 1] * .8 - s, np.abs(p[:, 2] - cz) + p[:, 1] * .8 - s)
            sc.add(pyr * .6, 2)
        sc.add(sd_torus(np.stack([p[:, 0] - .2, p[:, 2] + 2.5, p[:, 1] - 1.4], -1), (0, 0, 0), .7, .16, .5), 3)
        return sc

    def shade(mat, p, n, v):
        sand = hexc(0xE8B878)[None] * (.85 + .15 * np.sin(p[:, 0] * 12 + p[:, 2] * 3)[:, None] * .3 + .1)
        alb = np.select([(mat == 1)[:, None], (mat == 2)[:, None]], [sand, hexc(0xC89858)[None] * np.ones((len(p), 1))], hexc(0x101010)[None] * np.ones((len(p), 1)))
        return alb, np.where(mat == 3, .95, 0), np.full(len(p), .8), np.zeros((len(p), 3))
    sky = disc_sky([(-1, 0xF0D0A0), (0, 0xF8E0B0), (.3, 0x80B8F0), (1, 0x1A4AA0)], sun=(-.5, .5, -1))
    rgb, cov, t = render(scene, shade, sky, (0, 1.2, 3.0), (0, 1.0, -3), 1.0, light=(-.5, .6, -.6))
    return to_img(fogged(rgb, t, 0xF0E0C0, 10, 40))


def underwater():
    def scene(p):
        sc = Scene(p)
        sc.add(p[:, 1] - .15 * np.sin(p[:, 0] * 2.1) * np.sin(p[:, 2] * 1.7) - .05 * np.sin(p[:, 0] * 9 + p[:, 2] * 4), 1)
        for c, r in (((-.8, .5, -1.5), .25), ((1.2, .3, -2.5), .35), ((.2, .25, -.6), .12)):
            sc.add(sd_sphere(p, c, r), 2)
        return sc

    def shade(mat, p, n, v):
        c = np.sin(p[:, 0] * 7 + np.sin(p[:, 2] * 5) * 2) * np.sin(p[:, 2] * 6 + np.sin(p[:, 0] * 4) * 2)
        caustic = np.clip(c - .3, 0, 1) * 1.6
        alb = np.where((mat == 1)[:, None], hexc(0xD8C8A0)[None] * np.ones((len(p), 1)), hexc(0xE0A0C0)[None] * np.ones((len(p), 1)))
        emit = np.where((mat == 1)[:, None], hexc(0x80F0FF)[None] * caustic[:, None] * .5, 0)
        return alb, np.where(mat == 2, .3, 0), np.full(len(p), .6), emit
    rgb, cov, t = render(scene, shade, sky_gradient([(-1, 0x003040), (1, 0x2090B0)]), (0, 1.0, 2.5), (0, .3, -2), 1.05, light=(.1, 1, .2))
    img = fogged(rgb, t, 0x0A5A7A, 1.5, 9)
    ys, xs = np.mgrid[0:S, 0:S].astype(float)
    rays = sum(np.exp(-((xs - (100 + k * 90) - (ys * .4)) / 14) ** 2) for k in range(5)) * np.clip(1 - ys / 400, 0, 1)
    img = img + rays[..., None] * hexc(0x80E0FF) * .25
    out = to_img(img).convert('RGBA')
    b = layer()
    d = ImageDraw.Draw(b)
    rng = np.random.default_rng(3)
    for _ in range(30):
        x, y, r = rng.uniform(100, 420), rng.uniform(20, 380), rng.uniform(2, 8)
        d.ellipse([x - r, y - r, x + r, y + r], outline=(200, 250, 255, 200), width=1)
        d.ellipse([x - r * .5, y - r * .6, x - r * .1, y - r * .2], fill=(255, 255, 255, 200))
    out.alpha_composite(b)
    return out


def whitted():
    def scene(p):
        sc = Scene(p)
        sc.add(p[:, 1], 1)
        sc.add(sd_sphere(p, (-.7, .75, -1.6), .75), 2)
        sc.add(sd_sphere(p, (.55, .5, -.4), .5), 3)
        return sc

    def shade(mat, p, n, v):
        alb = np.where((mat == 1)[:, None], checker(p, .5, hexc(0xF0E020), hexc(0xD02020)), hexc(0x202020)[None])
        alb = np.where((mat == 3)[:, None], hexc(0xC0E8FF)[None], alb)
        return alb, np.select([mat == 2, mat == 3, mat == 1], [.95, .6, .1], 0), np.full(len(p), 1.0), np.zeros((len(p), 3))
    rgb, cov, t = render(scene, shade, sky_gradient([(-1, 0x6080C0), (1, 0x2040A0)]), (0, 1.2, 3.0), (0, .4, -1), .9, light=(.6, 1, .6))
    return to_img(rgb)


def chrome_blob():
    def scene(p):
        sc = Scene(p)
        d = sd_sphere(p, (0, 0, 0), .55)
        for c, r in (((.6, .3, .2), .38), ((-.5, -.35, .1), .42), ((.15, -.6, -.3), .3), ((-.2, .55, -.2), .3)):
            d = smin(d, sd_sphere(p, c, r), .35)
        sc.add(d, 1)
        return sc

    def env(d):
        y = d[:, 1]
        return np.where((y > 0)[:, None], hexc(0x2060E0)[None] * (1 - y[:, None]) + hexc(0xF0F8FF)[None] * y[:, None] * 1.2,
                        hexc(0x0A1030)[None] * np.ones((len(d), 1)) + hexc(0x60C0FF)[None] * np.clip(-y * 3, 0, 1)[:, None] * .3)

    def shade(mat, p, n, v):
        return hexc(0x404850)[None] * np.ones((len(p), 1)), np.full(len(p), .95), np.full(len(p), 1.0), np.zeros((len(p), 3))
    rgb, cov, _ = render(scene, shade, env, (0, .2, 3.2), (0, 0, 0), .9, light=(-.4, .8, .5), shadows=False)
    bg = np.zeros((S, S, 3)) + np.linspace(0, 1, S)[:, None, None] * hexc(0x0A2A90) + (1 - np.linspace(0, 1, S)[:, None, None]) * hexc(0x60B0FF)
    return to_img(bg * (1 - cov[..., None]) + rgb * cov[..., None])


def lowpoly_island():
    def scene(p):
        sc = Scene(p)
        q = p - np.array([0, 0, -2])
        r = np.hypot(q[:, 0], q[:, 2])
        sc.add((p[:, 1] - (.9 - r * .55) - .15 * np.sin(np.arctan2(q[:, 2], q[:, 0]) * 3)) * .6, 1)
        sc.add(p[:, 1] - .05, 2)
        return sc

    def shade(mat, p, n, v):
        nq = np.round(n * 2.2) / 2.2  # faceted, as if made of big triangles
        k = np.clip(nq[:, 1], .2, 1)[:, None]
        grass = np.where((p[:, 1] > .25)[:, None], hexc(0x48C048)[None], hexc(0xF0D890)[None])
        alb = np.where((mat == 1)[:, None], grass * k, hexc(0x2070E0)[None])
        return alb, np.where(mat == 2, .35, 0), np.full(len(p), .2), np.zeros((len(p), 3))
    rgb, cov, _ = render(scene, shade, sky_gradient([(-1, 0x80C8FF), (1, 0x2060E0)]), (0, 1.6, 2.4), (0, .2, -2), 1.0, light=(.5, 1, .4), shadows=False)
    out = to_img(rgb).convert('RGBA')
    pl = layer()
    palm(ImageDraw.Draw(pl), 260, 250, .5, (30, 120, 40, 255), lean=.2, fronds=7, seed=2)
    out.alpha_composite(pl)
    return out


# ---------------------------------------------------------------- fractals, as the filters made them
def gradient_map(t, stops):
    pos = np.array([p for p, _ in stops])
    cols = np.array([hexc(c) for _, c in stops])
    return np.stack([np.interp(t, pos, cols[:, k]) for k in range(3)], -1)


def escape(c, z, iters=160):
    n = np.zeros(c.shape)
    alive = np.ones(c.shape, bool)
    for i in range(iters):
        z = np.where(alive, z * z + c, z)
        out = alive & (np.abs(z) > 4)
        n[out] = i + 1 - np.log2(np.log(np.abs(z[out])))
        alive &= ~out
    n[alive] = 0
    return n, alive


def mandelbrot():
    ys, xs = np.mgrid[0:S, 0:S] / S
    c = (-.7436 + (xs - .5) * .012) + 1j * (.1318 + (ys - .5) * .012)
    n, inside = escape(c, np.zeros_like(c), 300)
    t = (n * .035) % 1
    rgb = gradient_map(t, [(0, 0x000040), (.2, 0x2040FF), (.4, 0xFFFFFF), (.55, 0xFFB000), (.75, 0xA01000), (1, 0x000040)])
    rgb[inside] = 0
    return to_img(rgb)


def julia():
    ys, xs = np.mgrid[0:S, 0:S] / S
    z = (xs - .5) * 3 + 1j * (ys - .5) * 3
    n, inside = escape(np.full(z.shape, -.8 + .156j), z, 200)
    t = np.sqrt(n / (n.max() + 1e-9))
    rgb = gradient_map(t, [(0, 0x100020), (.3, 0x6020A0), (.6, 0xF060C0), (.85, 0xFFE070), (1, 0xFFFFFF)])
    rgb[inside] = hexc(0x200030)
    return to_img(rgb)


def spheroids():
    img = Image.new('RGBA', (S, S), (8, 8, 12, 255))
    ys, xs = np.mgrid[0:S, 0:S].astype(float)
    acc = np.zeros((S, S, 3))
    cov = np.zeros((S, S))
    rng = np.random.default_rng(11)
    for gy in range(4):
        for gx in range(4):
            cx, cy, r = 70 + gx * 124, 70 + gy * 124, 52
            dx, dy = (xs - cx) / r, (ys - cy) / r
            inside = dx * dx + dy * dy < 1
            nz = np.sqrt(np.clip(1 - dx * dx - dy * dy, 0, 1))
            lam = np.clip(-dx * .5 - dy * .6 + nz * .6, 0, 1)
            spec = np.clip(-dx * .4 - dy * .5 + nz * .77, 0, 1) ** 40
            a, b = rng.integers(0, 0xFFFFFF, 2)
            band = .5 + .5 * np.sin((dx * 3 + dy * 2) * (1 + gx) + gy)
            col = hexc(int(a))[None, None] * band[..., None] + hexc(int(b))[None, None] * (1 - band[..., None])
            sh = col * (.25 + .85 * lam[..., None]) + spec[..., None]
            acc = np.where(inside[..., None], sh, acc)
            cov = np.maximum(cov, inside)
    img.alpha_composite(to_img(acc, cov))
    return img


def kpt_texture():
    ys, xs = np.mgrid[0:S, 0:S].astype(float) / S * 2 * np.pi
    h = np.sin(xs * 3 + np.sin(ys * 2) * 2) + np.sin(ys * 4 + np.cos(xs * 3)) * .7 + .5 * np.sin((xs + ys) * 6)
    gy, gx = np.gradient(h)
    n = np.dstack([-gx * 40, -gy * 40, np.ones_like(h)])
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    lam = np.clip(n @ norm(np.array([-.5, -.6, .6])), 0, 1)
    spec = lam ** 30
    t = (h + 2.2) / 4.4
    col = gradient_map(t % 1, [(0, 0x401060), (.3, 0xE04080), (.5, 0xFFC040), (.7, 0x20C0A0), (1, 0x401060)])
    return to_img(col * (.3 + .8 * lam[..., None]) + spec[..., None] * .8)


def flame():
    rng = np.random.default_rng(5)
    n = 120000
    p = rng.uniform(-1, 1, (n, 2))
    col = rng.uniform(0, 1, n)
    hist = np.zeros((S, S))
    hue = np.zeros((S, S))
    maps = [(np.array([[.5, -.4], [.4, .5]]), np.array([.3, .1]), 0.0), (np.array([[.6, .2], [-.2, .6]]), np.array([-.4, .2]), .5),
            (np.array([[-.4, .3], [.3, .4]]), np.array([.1, -.5]), 1.0)]
    for it in range(60):
        k = rng.integers(0, 3, n)
        q = np.zeros_like(p)
        for i, (m, b, c) in enumerate(maps):
            sel = k == i
            v = p[sel] @ m.T + b
            if i == 0:
                v = np.sin(v)  # sinusoidal
            elif i == 1:
                r2 = (v ** 2).sum(1, keepdims=True)
                v = np.hstack([v[:, :1] * np.sin(r2) - v[:, 1:] * np.cos(r2), v[:, :1] * np.cos(r2) + v[:, 1:] * np.sin(r2)])  # swirl
            q[sel] = v
            col[sel] = (col[sel] + c) / 2
        p = q
        if it == 12:  # frame the attractor once it has settled
            centre = np.median(p, axis=0)
            scale = .42 / np.percentile(np.abs(p - centre), 97)
        if it > 12:
            ix = (((p[:, 0] - centre[0]) * scale + .5) * S).astype(int)
            iy = (((p[:, 1] - centre[1]) * scale + .5) * S).astype(int)
            ok = (ix >= 0) & (ix < S) & (iy >= 0) & (iy < S)
            np.add.at(hist, (iy[ok], ix[ok]), 1)
            np.add.at(hue, (iy[ok], ix[ok]), col[ok])
    a = np.log1p(hist) / np.log1p(hist.max())
    t = hue / np.maximum(hist, 1)
    rgb = gradient_map(t, [(0, 0xFF3010), (.5, 0xFFC040), (1, 0x40A0FF)]) * a[..., None] ** .6
    return to_img(np.asarray(to_img(rgb).filter(ImageFilter.GaussianBlur(.6)), float) / 255 * 1.4)


def lyapunov():
    ys, xs = np.mgrid[0:S, 0:S] / S
    a, b = 3.4 + xs * .6, 2.5 + ys * 1.4
    seq = 'AABAB'
    x = np.full(a.shape, .5)
    lam = np.zeros(a.shape)
    for i in range(160):
        r = a if seq[i % len(seq)] == 'A' else b
        x = r * x * (1 - x)
        if i > 20:
            lam += np.log(np.abs(r * (1 - 2 * x)) + 1e-12)
    lam /= 140
    t = np.clip(lam, -2, 1)
    pos = np.clip(-t / 2, 0, 1)
    rgb = gradient_map(pos, [(0, 0x000000), (.15, 0x403000), (.5, 0xFFD040), (1, 0xFFFFF0)])
    rgb = np.where((t > 0)[..., None], hexc(0x0A1A60) * (1 - t[..., None]), rgb)
    return to_img(rgb)


# ---------------------------------------------------------------- Frutiger Aero
def aero_hills():
    ys, xs = np.mgrid[0:S, 0:S].astype(float)
    sky = hexc(0x1A80F0)[None, None] * (1 - ys[..., None] / 320) + hexc(0xD8F2FF)[None, None] * (ys[..., None] / 320)
    img = np.clip(sky, 0, 1)
    for k, (base, amp, freq, ph, col) in enumerate(((330, 40, 1.1, .3, 0x58C040), (380, 50, 1.6, 2.0, 0x38A828), (440, 30, .9, 4.0, 0x2A9020))):
        top = base - amp * np.sin(xs / S * np.pi * freq + ph)
        gl = np.clip(1 - (ys - top) / 50, 0, 1) * .3
        img = np.where((ys > top)[..., None], hexc(col)[None, None] * (1 + gl[..., None]), img)
    out = to_img(img).convert('RGBA')
    sun = layer()
    ImageDraw.Draw(sun).ellipse([350, 60, 430, 140], fill=(255, 255, 240, 255))
    out.alpha_composite(glow(sun, 40, 1.6))
    out.alpha_composite(lens_flare(390, 100, .6))
    return out


def droplets():
    ys, xs = np.mgrid[0:S, 0:S].astype(float)
    u, v = (xs - 256) / 300, (ys - 256) / 180
    rot = np.array([[.8, .6], [-.6, .8]])
    lu, lv = u * .8 + v * .6, -u * .6 + v * .8
    leaf = (lu * lu + (lv * 1.6) ** 2 * (1 + lu * .4)) < 1
    vein = (np.abs(lv) < .012) | (np.abs(np.abs(lv) - (lu + 1) * .18 % .25) < .01)
    base = hexc(0x3AA030)[None, None] * (.75 + .35 * (1 - np.abs(lv[..., None])))
    base = np.where(vein[..., None], hexc(0x8AE070), base)
    bg = hexc(0x0A3A10) + hexc(0x80D0FF) * np.clip(1 - np.hypot(xs - 400, ys - 80) / 300, 0, 1)[..., None] * .5
    img = np.where(leaf[..., None], base, bg)
    out = to_img(img).convert('RGBA')
    d = ImageDraw.Draw(out)
    rng = np.random.default_rng(4)
    for _ in range(26):
        x, y, r = rng.uniform(90, 420), rng.uniform(140, 380), rng.uniform(5, 22)
        d.ellipse([x - r, y - r, x + r, y + r], fill=(150, 230, 130, 140), outline=(30, 90, 30, 170), width=2)
        d.ellipse([x - r * .6, y - r * .1, x + r * .7, y + r * .8], fill=(210, 255, 190, 140))
        d.ellipse([x - r * .55, y - r * .65, x - r * .1, y - r * .25], fill=(255, 255, 255, 230))
    return out


def aurora():
    ys, xs = np.mgrid[0:S, 0:S].astype(float)
    img = hexc(0x001850)[None, None] * (1 - ys[..., None] / S) + hexc(0x0060B0)[None, None] * (ys[..., None] / S)
    for k in range(5):
        cy = 220 + 50 * np.sin(xs / 120 + k * 1.3) + k * 22
        w = 10 + k * 4
        band = np.exp(-((ys - cy) / w) ** 2)
        col = hexc([0x60FFE0, 0x40C0FF, 0xFFFFFF, 0x80FF80, 0x60A0FF][k])
        img = img + band[..., None] * col * (.55 - k * .07)
    out = to_img(img).convert('RGBA')
    sp = layer()
    d = ImageDraw.Draw(sp)
    rng = np.random.default_rng(8)
    for _ in range(40):
        x, y = rng.uniform(0, S), rng.uniform(0, S)
        d.ellipse([x - 1.5, y - 1.5, x + 1.5, y + 1.5], fill=(255, 255, 255, 220))
    out.alpha_composite(glow(sp, 3, 1))
    return out


def aquarium():
    out = ads.vgrad(0x40D0F0, 0x0050A0)
    rays = layer()
    d = ImageDraw.Draw(rays)
    for k in range(6):
        x = 40 + k * 90
        d.polygon([(x, 0), (x + 40, 0), (x + 140, S), (x + 60, S)], fill=(255, 255, 255, 30))
    out.alpha_composite(rays.filter(ImageFilter.GaussianBlur(6)))
    fish = layer()
    d = ImageDraw.Draw(fish)
    rng = np.random.default_rng(9)
    cols = [(255, 140, 20), (255, 230, 40), (255, 80, 160), (60, 230, 120), (240, 240, 255)]
    for i in range(9):
        x, y, s = rng.uniform(60, 450), rng.uniform(80, 440), rng.uniform(.6, 1.4)
        c = cols[i % 5]
        flip = -1 if i % 2 else 1
        d.ellipse([x - 34 * s, y - 18 * s, x + 34 * s, y + 18 * s], fill=c + (255,))
        d.polygon([(x - 30 * s * flip, y), (x - 56 * s * flip, y - 18 * s), (x - 56 * s * flip, y + 18 * s)], fill=c + (255,))
        d.ellipse([x + 16 * s * flip - 5 * s, y - 8 * s, x + 16 * s * flip + 5 * s, y + 2 * s], fill=(10, 10, 10, 255))
        d.ellipse([x - 20 * s, y - 14 * s, x + 10 * s, y - 6 * s], fill=(255, 255, 255, 140))
    out.alpha_composite(shadowed(fish, (4, 6), 4, .25))
    b = layer()
    d = ImageDraw.Draw(b)
    for _ in range(26):
        x, y, r = rng.uniform(0, S), rng.uniform(0, S), rng.uniform(3, 14)
        d.ellipse([x - r, y - r, x + r, y + r], fill=(220, 250, 255, 50), outline=(255, 255, 255, 200), width=2)
        d.ellipse([x - r * .5, y - r * .6, x - r * .05, y - r * .2], fill=(255, 255, 255, 230))
    out.alpha_composite(b)
    return out


def bokeh():
    out = ads.vgrad(0x003A40, 0x001018)
    rng = np.random.default_rng(14)
    for layer_blur in (10, 4, 1.5):
        b = layer()
        d = ImageDraw.Draw(b)
        for _ in range(16):
            x, y, r = rng.uniform(0, S), rng.uniform(0, S), rng.uniform(14, 50) * (1.6 if layer_blur > 5 else 1)
            c = [(120, 255, 220), (80, 180, 255), (255, 255, 255), (140, 255, 120)][rng.integers(0, 4)]
            d.ellipse([x - r, y - r, x + r, y + r], fill=c + (int(rng.uniform(40, 110)),), outline=c + (160,), width=2)
        out.alpha_composite(b.filter(ImageFilter.GaussianBlur(layer_blur)))
    return out


# ---------------------------------------------------------------- airbrush
def airbrush(img, r=1.6):
    return img.filter(ImageFilter.GaussianBlur(r))


def airbrush_hero():
    rgb, cov, _ = render(ads.dark_scene, ads.dark_shade, sky_gradient([(-1, 0x000000), (1, 0x100828)]), (0, .5, 2.4), (0, 1.5, -4), 1.1, light=(0, 1, -1))
    ys, xs = np.mgrid[0:S, 0:S]
    rays = np.clip(1 - np.hypot(xs - 256, ys - 230) / 300, 0, 1) ** 2
    out = to_img(rgb + rays[..., None] * hexc(0x40A0FF) * .4).convert('RGBA')
    rim = layer()
    ImageDraw.Draw(rim).ellipse([244, 330, 268, 420], outline=(120, 240, 255, 200), width=2)
    out.alpha_composite(glow(rim.filter(ImageFilter.GaussianBlur(3)), 10, .8))
    return airbrush(out, 1.4)


def airbrush_dolphin():
    ys, xs = np.mgrid[0:S, 0:S].astype(float)
    hor = 300
    sky = hexc(0x401060)[None, None] * (1 - ys[..., None] / hor) + hexc(0xFF8040)[None, None] * (ys[..., None] / hor)
    sea = hexc(0x200840)[None, None] + hexc(0xFF9040)[None, None] * np.exp(-((xs - 256) / 50) ** 2)[..., None] * np.clip(1 - (ys - hor) / 200, 0, 1)[..., None] * (
        .5 + .5 * (np.sin(ys * .8) > .3))[..., None]
    img = np.where((ys < hor)[..., None], np.clip(sky, 0, 1), sea)
    sun = np.hypot(xs - 256, ys - hor) < 70
    img = np.where((sun & (ys < hor))[..., None], hexc(0xFFE080), img)
    out = to_img(img).convert('RGBA')
    m = layer()
    dolphin(ImageDraw.Draw(m), 256, 250, 1.4, (255, 255, 255, 255), (255, 255, 255, 255))
    m = m.rotate(24, center=(256, 250), resample=Image.BICUBIC)
    a = np.asarray(m, float)[..., 3] / 255
    t = np.clip((ys - 150) / 200, 0, 1)
    body = hexc(0x2A1A50)[None, None] * (1 - t[..., None]) + hexc(0xC06A90)[None, None] * t[..., None]
    out.alpha_composite(to_img(body, a))
    return airbrush(out, 1.2)


def airbrush_wolf():
    ys, xs = np.mgrid[0:S, 0:S].astype(float)
    img = hexc(0x050A30)[None, None] * (1 - ys[..., None] / S) + hexc(0x3050A0)[None, None] * (ys[..., None] / S)
    moon = np.hypot(xs - 300, ys - 190) < 120
    mt = fbm3(np.stack([xs.ravel() / 30, ys.ravel() / 30, np.full(S * S, 2.0)], -1), 4).reshape(S, S)
    img = np.where(moon[..., None], hexc(0xF0F0E0) * (.8 + .25 * mt[..., None]), img)
    img = img + hexc(0x8090FF) * np.clip(1 - np.hypot(xs - 300, ys - 190) / 220, 0, 1)[..., None] ** 2 * .35
    out = to_img(img).convert('RGBA')
    sil = layer()
    d = ImageDraw.Draw(sil)
    d.polygon([(0, 512), (0, 400), (90, 372), (300, 374), (360, 430), (512, 470), (512, 512)], fill=(5, 5, 15, 255))
    # a wolf sitting on the rock, head thrown back to howl
    wolf = [(120, 372), (128, 350), (150, 332), (168, 300), (186, 276), (204, 258), (214, 236), (222, 214), (230, 196), (240, 178), (252, 160), (262, 142),
            (270, 136), (268, 152), (276, 150), (286, 128), (290, 140), (282, 160), (272, 176), (262, 196), (254, 214), (250, 236), (252, 262), (258, 300),
            (262, 340), (270, 376), (252, 378), (244, 340), (236, 330), (230, 376), (214, 378), (212, 344), (190, 344), (168, 360), (150, 372)]
    d.polygon(wolf, fill=(5, 5, 15, 255))
    d.polygon([(246, 172), (238, 150), (254, 162)], fill=(5, 5, 15, 255))  # an ear
    d.polygon([(120, 372), (96, 380), (70, 376), (100, 366)], fill=(5, 5, 15, 255))  # the tail
    out.alpha_composite(sil)
    ads.stars(out, 80, 5)
    return airbrush(out, 1.0)


def sunset_grid():
    """the striped sun over a magenta grid running to the horizon"""
    ys, xs = np.mgrid[0:S, 0:S].astype(float)
    hor = 300
    t = np.clip(ys / hor, 0, 1)[..., None]
    sky = hexc(0x0A0420) * (1 - t) + hexc(0x7A1A80) * t
    sky += (np.random.default_rng(1).random((S, S)) > .997)[..., None] * .8 * (ys < 240)[..., None]
    r = np.hypot(xs - 256, ys - 250)
    st = np.clip((ys - 140) / 160, 0, 1)
    sun = hexc(0xFFE860) * (1 - st[..., None]) + hexc(0xFF2A90) * st[..., None]
    gap = (ys > 215) & ((ys - 215) % 14 < 2 + (ys - 215) / 16)
    sky = np.where(((r < 112) & ~gap & (ys < hor))[..., None], sun, sky)
    sky += np.clip(1 - r / 190, 0, 1)[..., None] ** 2 * hexc(0xFF3090) * .35
    z = 60.0 / np.maximum(ys - hor, .5)
    gx = (xs - 256) * z / 60
    lines = (np.abs(((gx + .5) % 1) - .5) < .02 * z ** .3 + .01) | (np.abs(((z * 2) % 1) - .5) > .46)
    floor = hexc(0x14042A) + lines[..., None] * hexc(0xF040E0) * np.clip(1.4 - z / 20, .2, 1)[..., None]
    img = np.where((ys >= hor)[..., None], floor, sky) + np.exp(-((ys - hor) / 6.0) ** 2)[..., None] * hexc(0xFF60D0) * .6
    return to_img(img).convert('RGBA')


def airbrush_car():
    out = sunset_grid()
    car = layer()
    d = ImageDraw.Draw(car)
    d.polygon([(70, 400), (80, 372), (170, 352), (230, 318), (330, 312), (392, 344), (446, 352), (450, 386), (440, 404), (80, 410)], fill=(20, 18, 30, 255))
    d.polygon([(186, 352), (236, 322), (326, 318), (370, 346)], fill=(80, 200, 240, 255))
    d.line([(278, 320), (282, 350)], fill=(20, 18, 30, 255), width=6)
    d.line([(80, 376), (446, 368)], fill=(255, 60, 200, 255), width=3)
    for wx in (140, 380):
        d.ellipse([wx - 34, 376, wx + 34, 444], fill=(10, 10, 14, 255))
        d.ellipse([wx - 18, 392, wx + 18, 428], fill=(180, 180, 200, 255))
    d.rectangle([66, 370, 90, 382], fill=(255, 40, 60, 255))
    out.alpha_composite(glow(car, 6, .6))
    return airbrush(out, 1.0)


def clouds_wallpaper():
    """the sky every desktop of 1995 opened onto"""
    return clouds_sky(7, 0x3070E0, 0xA8D0F8)


def airbrush_space():
    ys, xs = np.mgrid[0:S, 0:S].astype(float)
    neb = fbm3(np.stack([xs.ravel() / 90, ys.ravel() / 90, np.zeros(S * S)], -1), 5, 2).reshape(S, S)
    neb2 = fbm3(np.stack([xs.ravel() / 60, ys.ravel() / 60, np.ones(S * S)], -1), 5, 7).reshape(S, S)
    img = hexc(0x050010) + hexc(0xC02080) * np.clip(neb - .45, 0, 1)[..., None] * 2.5 + hexc(0x2060FF) * np.clip(neb2 - .5, 0, 1)[..., None] * 2.5
    out = to_img(img).convert('RGBA')
    ads.stars(out, 200, 6)

    def scene(p):
        sc = Scene(p)
        sc.add(sd_sphere(p, (0, 0, 0), 1.0), 1)
        sc.add(sd_torus(p, (0, 0, 0), 1.7, .03, .4), 2)
        sc.add(sd_sphere(p, (-2.4, 1.0, -1), .25), 3)
        return sc

    def shade(mat, p, n, v):
        band = .5 + .5 * np.sin(p[:, 1] * 14 + np.sin(p[:, 0] * 3))
        alb = np.select([(mat == 1)[:, None], (mat == 2)[:, None]], [hexc(0xE0A060)[None] * (.7 + .3 * band[:, None]), hexc(0xD8C8A0)[None] * np.ones((len(p), 1))],
                        hexc(0xA0A0B0)[None] * np.ones((len(p), 1)))
        return alb, np.zeros(len(p)), np.full(len(p), .3), np.zeros((len(p), 3))
    rgb, cov, _ = render(scene, shade, lambda d: np.zeros((len(d), 3)), (1.5, .9, 4.2), (0, 0, 0), .9, light=(-1, .4, .6), reflect=False, shadows=False)
    out.alpha_composite(to_img(rgb, cov))
    return airbrush(out, .9)


ALL = [(bryce_sunset, 'gallery'), (bryce_cubes, 'gallery'), (bryce_arch, 'wood'), (bryce_desert, 'thin'), (underwater, 'plastic'), (whitted, 'canvas'),
       (chrome_blob, 'chrome'), (lowpoly_island, 'monitor'), (mandelbrot, 'chrome'), (julia, 'thin'), (spheroids, 'thin'), (kpt_texture, 'canvas'),
       (flame, 'thin'), (lyapunov, 'gilt'), (aero_hills, 'canvas'), (droplets, 'canvas'), (aurora, 'plastic'), (aquarium, 'plastic'), (bokeh, 'canvas'),
       (airbrush_hero, 'gilt'), (airbrush_dolphin, 'gilt'), (airbrush_wolf, 'wood'), (airbrush_car, 'thin'), (airbrush_space, 'gilt'),
       (clouds_wallpaper, 'monitor')]
