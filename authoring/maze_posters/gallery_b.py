"""Maze 95 art, part two: neon and lasers, Kid Pix and MS Paint, clip art and WordArt, the
screensavers, Memphis, slime and lava, the millennium's plasma and gel, and a Magic Eye."""
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

from kit import *
import ads
import props
from gallery_a import gradient_map

NEON = [(40, 240, 255), (255, 240, 40), (255, 60, 200), (120, 255, 80), (255, 140, 30)]


def black():
    return Image.new('RGBA', (S, S), (0, 0, 0, 255))


def white(c=(255, 255, 255)):
    return Image.new('RGBA', (S, S), c + (255,))


def neon_shape(d, kind, x, y, r, c, w=3):
    if kind == 0:
        d.polygon([(x, y - r), (x + r, y + r * .8), (x - r, y + r * .8)], outline=c, width=w)
    elif kind == 1:
        d.rectangle([x - r, y - r, x + r * .4, y + r * .4], outline=c, width=w)
        d.rectangle([x - r * .4, y - r * .4, x + r, y + r], outline=c, width=w)
        for dx, dy in ((-r, -r), (r * .4, -r), (-r, r * .4), (r * .4, r * .4)):
            d.line([(x + dx, y + dy), (x + dx + r * .6, y + dy + r * .6)], fill=c, width=w)
    elif kind == 2:
        d.line([(x - r + i * r / 3, y + (r / 3 if i % 2 else -r / 3)) for i in range(7)], fill=c, width=w)
    elif kind == 3:
        d.ellipse([x - r, y - r, x + r, y + r], outline=c, width=w)
    else:
        d.line([(x - r, y + r), (x - r * .2, y - r * .1), (x + r * .2, y + r * .2), (x + r, y - r)], fill=c, width=w)


# ---------------------------------------------------------------- neon and lasers
def neon_shapes():
    out = black()
    grid = layer()
    d = ImageDraw.Draw(grid)
    for k in range(-12, 13):
        d.line([(256 + k * 18, 280), (256 + k * 90, S)], fill=(255, 40, 200, 200), width=2)
    for k in range(10):
        y = 280 + (k / 9) ** 2 * 232
        d.line([(0, y), (S, y)], fill=(255, 40, 200, 200), width=2)
    out.alpha_composite(glow(grid, 4, 1))
    sh = layer()
    d = ImageDraw.Draw(sh)
    rng = np.random.default_rng(4)
    for k in range(22):
        neon_shape(d, k % 5, rng.uniform(30, 480), rng.uniform(30, 300), rng.uniform(14, 38), NEON[k % 5] + (255,))
    out.alpha_composite(glow(sh, 6, 1.1))
    return out


def lasers():
    out = Image.fromarray(np.asarray(ads.vgrad(0x10104A, 0x02020A)))
    lay = layer()
    d = ImageDraw.Draw(lay)
    rng = np.random.default_rng(7)
    for k in range(26):
        a = rng.uniform(-.5, .5) + (np.pi if k % 2 else 0)
        x0, y0 = rng.uniform(0, S), rng.uniform(0, S)
        c = [(255, 60, 220), (60, 200, 255), (120, 120, 255), (255, 255, 255)][k % 4]
        d.line([(x0 - np.cos(a) * 700, y0 - np.sin(a) * 700), (x0 + np.cos(a) * 700, y0 + np.sin(a) * 700)], fill=c + (220,), width=2)
    out.alpha_composite(glow(glow(lay, 8, 1.4), 3, 1))
    for x, y in ((150, 160), (380, 300)):
        out.alpha_composite(lens_flare(x, y, .5, (255, 220, 255)))
    return out


def neon_memphis():
    out = black()
    lay = layer()
    d = ImageDraw.Draw(lay)
    rng = np.random.default_rng(12)
    for k in range(30):
        x, y = rng.uniform(20, 492), rng.uniform(20, 492)
        c = NEON[k % 5] + (255,)
        kind = k % 4
        if kind == 0:
            d.line([(x + i * 10, y + (12 if i % 2 else -12)) for i in range(7)], fill=c, width=5)
        elif kind == 1:
            d.polygon([(x, y), (x + 34, y + 10), (x + 8, y + 36)], fill=c)
        elif kind == 2:
            for i in range(3):
                for j in range(3):
                    d.ellipse([x + i * 12 - 3, y + j * 12 - 3, x + i * 12 + 3, y + j * 12 + 3], fill=c)
        else:
            d.arc([x - 30, y - 20, x + 30, y + 20], 0, 180, fill=c, width=5)
            d.arc([x + 30, y - 20, x + 90, y + 20], 180, 360, fill=c, width=5)
    out.alpha_composite(glow(lay, 6, 1.2))
    return out


def blacklight():
    ys, xs = np.mgrid[0:S, 0:S].astype(float)
    r = np.hypot(xs - 256, ys - 256)
    a = np.arctan2(ys - 256, xs - 256)
    rings = np.sin(r * .09 + np.sin(a * 6) * 1.6)
    t = (rings + 1) / 2
    col = gradient_map(t, [(0, 0x050008), (.45, 0x050008), (.55, 0x40FF40), (.7, 0xFF40C0), (.85, 0xFFA020), (1, 0xFFFF40)])
    velvet = .85 + .15 * np.random.default_rng(1).random((S, S))
    img = col * velvet[..., None]
    eye = (((xs - 256) / 120) ** 2 + ((ys - 256) / 60) ** 2) < 1
    img = np.where(eye[..., None], hexc(0xF8F8FF), img)
    img = np.where((r < 50)[..., None], hexc(0x30FF60), img)
    img = np.where((r < 34)[..., None], hexc(0xFF30A0), img)
    img = np.where((r < 18)[..., None], hexc(0x000000), img)
    return to_img(img)


# ---------------------------------------------------------------- Kid Pix, Paint, clip art, WordArt
def kidpix():
    out = white()
    d = ImageDraw.Draw(out)
    rng = np.random.default_rng(3)
    rainbow = [(255, 0, 0), (255, 140, 0), (255, 230, 0), (0, 200, 0), (0, 120, 255), (140, 0, 220)]
    for s in range(3):  # rainbow brush trails
        y0 = 120 + s * 130
        for i in range(160):
            x = 20 + i * 3
            y = y0 + 40 * np.sin(i * .06 + s)
            for k, c in enumerate(rainbow):
                d.ellipse([x - 3, y + k * 6 - 3, x + 3, y + k * 6 + 3], fill=c)
    stamps = ['star', 'heart', 'smile', 'fish', 'house', 'star', 'heart', 'smile', 'tree']
    for i, kind in enumerate(stamps):
        x, y = 60 + (i % 3) * 180 + rng.uniform(-20, 20), 70 + (i // 3) * 160 + rng.uniform(-20, 20)
        if kind == 'star':
            d.regular_polygon((x, y, 30), 5, rotation=rng.uniform(0, 70), fill=(255, 220, 0), outline=(0, 0, 0))
        elif kind == 'heart':
            d.ellipse([x - 26, y - 22, x, y + 4], fill=(240, 30, 80), outline=(0, 0, 0))
            d.ellipse([x, y - 22, x + 26, y + 4], fill=(240, 30, 80), outline=(0, 0, 0))
            d.polygon([(x - 25, y - 4), (x + 25, y - 4), (x, y + 28)], fill=(240, 30, 80))
        elif kind == 'smile':
            d.ellipse([x - 28, y - 28, x + 28, y + 28], fill=(255, 230, 0), outline=(0, 0, 0), width=2)
            d.ellipse([x - 12, y - 12, x - 5, y - 2], fill=(0, 0, 0))
            d.ellipse([x + 5, y - 12, x + 12, y - 2], fill=(0, 0, 0))
            d.arc([x - 16, y - 10, x + 16, y + 16], 20, 160, fill=(0, 0, 0), width=3)
        elif kind == 'fish':
            d.ellipse([x - 26, y - 14, x + 18, y + 14], fill=(255, 120, 0), outline=(0, 0, 0))
            d.polygon([(x - 22, y), (x - 40, y - 14), (x - 40, y + 14)], fill=(255, 120, 0), outline=(0, 0, 0))
            d.ellipse([x + 6, y - 6, x + 10, y - 2], fill=(0, 0, 0))
        elif kind == 'house':
            d.rectangle([x - 24, y - 6, x + 24, y + 30], fill=(220, 60, 60), outline=(0, 0, 0))
            d.polygon([(x - 30, y - 6), (x, y - 32), (x + 30, y - 6)], fill=(120, 70, 30), outline=(0, 0, 0))
            d.rectangle([x - 6, y + 10, x + 6, y + 30], fill=(80, 40, 10))
        else:
            d.rectangle([x - 5, y, x + 5, y + 30], fill=(120, 70, 30))
            d.ellipse([x - 24, y - 34, x + 24, y + 8], fill=(40, 180, 40), outline=(0, 0, 0))
    for _ in range(140):  # the splatter brush
        x, y, r = rng.uniform(0, S), rng.uniform(0, S), rng.uniform(1, 4)
        d.ellipse([x - r, y - r, x + r, y + r], fill=rainbow[rng.integers(0, 6)])
    return out


def mspaint():
    small = Image.new('RGB', (128, 128), (0, 128, 255))
    d = ImageDraw.Draw(small)
    d.rectangle([0, 86, 127, 127], fill=(0, 192, 0))
    rng = np.random.default_rng(2)
    for _ in range(260):  # the spray can sun
        a, r = rng.uniform(0, 2 * np.pi), rng.uniform(0, 16) ** .9
        d.point((104 + np.cos(a) * r, 22 + np.sin(a) * r), fill=(255, 255, 0))
    d.rectangle([20, 58, 60, 92], fill=(255, 0, 0), outline=(0, 0, 0))
    d.polygon([(16, 58), (40, 38), (64, 58)], fill=(128, 64, 0), outline=(0, 0, 0))
    d.rectangle([34, 74, 44, 92], fill=(128, 64, 0))
    d.rectangle([24, 64, 32, 72], fill=(255, 255, 255), outline=(0, 0, 0))
    d.rectangle([84, 66, 90, 96], fill=(128, 64, 0))
    d.ellipse([74, 46, 100, 72], fill=(0, 128, 0), outline=(0, 0, 0))
    d.ellipse([10, 12, 46, 28], fill=(255, 255, 255), outline=(0, 0, 0))
    d.ellipse([30, 8, 60, 26], fill=(255, 255, 255), outline=(0, 0, 0))
    d.line([(64, 100), (66, 92), (68, 100)], fill=(0, 0, 0))  # a person
    d.line([(66, 92), (66, 84)], fill=(0, 0, 0))
    d.line([(62, 88), (70, 88)], fill=(0, 0, 0))
    d.ellipse([63, 79, 69, 85], outline=(0, 0, 0))
    d.line([(0, 110), (20, 106), (40, 114), (60, 104), (80, 112), (100, 106), (127, 112)], fill=(0, 100, 0))
    return small.resize((S, S), Image.NEAREST).convert('RGBA')


def clipart():
    out = white((250, 250, 248))
    d = ImageDraw.Draw(out)
    o = (0, 0, 0)
    # a globe
    d.ellipse([40, 40, 200, 200], fill=(60, 140, 230), outline=o, width=3)
    for poly in ([(80, 80), (130, 70), (140, 110), (100, 140), (70, 120)], [(150, 130), (180, 140), (170, 180), (140, 170)]):
        d.polygon(poly, fill=(80, 190, 80), outline=o)
    # a light bulb
    d.ellipse([290, 40, 390, 140], fill=(255, 240, 80), outline=o, width=3)
    d.rectangle([320, 136, 360, 170], fill=(170, 170, 170), outline=o, width=2)
    for k in range(6):
        a = -np.pi + k * np.pi / 5
        d.line([(340 + np.cos(a) * 64, 90 + np.sin(a) * 64), (340 + np.cos(a) * 86, 90 + np.sin(a) * 86)], fill=o, width=3)
    # a rising chart
    for i, h in enumerate((40, 70, 60, 110, 150)):
        d.rectangle([60 + i * 34, 440 - h, 86 + i * 34, 440], fill=[(230, 60, 60), (60, 160, 60), (60, 100, 220), (230, 180, 40), (150, 60, 200)][i], outline=o, width=2)
    d.line([(50, 440), (240, 440)], fill=o, width=3)
    d.line([(60, 380), (130, 360), (170, 330), (230, 270)], fill=(230, 40, 40), width=6)
    d.polygon([(230, 260), (244, 262), (238, 276)], fill=(230, 40, 40))
    # a computer
    d.rectangle([290, 260, 450, 380], fill=(220, 214, 196), outline=o, width=3)
    d.rectangle([306, 276, 434, 360], fill=(0, 128, 128), outline=o, width=2)
    d.rectangle([330, 380, 410, 400], fill=(220, 214, 196), outline=o, width=2)
    d.rectangle([290, 410, 450, 440], fill=(220, 214, 196), outline=o, width=2)
    # a star and a tick
    d.regular_polygon((230, 230, 28), 5, rotation=18, fill=(255, 210, 0), outline=o)
    d.line([(420, 160), (440, 190), (480, 120)], fill=(30, 170, 30), width=12)
    return out


def wordart_study():
    out = white((250, 250, 245))
    out.alpha_composite(wordart('WOW!', font('ComicNeue-Bold.ttf', 120), (256, 130), bend=40, outline=3))
    out.alpha_composite(chrome_text('COOL', font('BarlowCondensed-Bold.ttf', 110), (256, 290), angle=-6))
    out.alpha_composite(wordart('RAD', font('LibreBaskerville-Bold.ttf', 96), (256, 430), stops=[(0, 0x00A040), (1, 0x80FF60)], horizontal=False, outline=2,
                                shadow=(8, 8), shadow_col=(0, 60, 0, 255)))
    return out


def dither_landscape():
    """VGA's sixteen colours, ordered-dithered over a lake at sunset"""
    n = 128
    ys, xs = np.mgrid[0:n, 0:n].astype(float)
    sky = gradient_map(np.clip(ys / 70, 0, 1), [(0, 0x1818A0), (.5, 0xA040C0), (.85, 0xFF7060), (1, 0xFFE070)])
    mt = 70 - 24 * np.abs(np.sin(xs * .05)) - 10 * np.sin(xs * .13 + 1)
    scene = np.where((ys > mt)[..., None], hexc(0x6A1090) * (1 - .3 * ((ys - mt) / 30).clip(0, 1))[..., None], sky)
    mirror = scene[np.clip(2 * 84 - ys.astype(int), 0, n - 1), xs.astype(int)]
    ripple = .85 + .15 * np.sin(ys * 1.7 + np.sin(xs * .3))
    img = np.where((ys > 84)[..., None], mirror * ripple[..., None] * .8 + hexc(0x0020A0) * .2, scene)
    vga = np.array([hexc(c) for c in (0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
                                     0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF)])
    bayer = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]) / 16 - .5
    jitter = bayer[ys.astype(int) % 4, xs.astype(int) % 4][..., None] * .35
    q = img + jitter
    dist = ((q[..., None, :] - vga[None, None]) ** 2).sum(-1)
    out = vga[dist.argmin(-1)]
    return to_img(out).resize((S, S), Image.NEAREST).convert('RGBA')


# ---------------------------------------------------------------- the screensavers
PIPES = []


def _plan_pipes():
    rng = np.random.default_rng(15)
    dirs = [np.array(v, float) for v in ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1))]
    for colour in range(5):
        p = rng.integers(-3, 4, 3).astype(float) * .5
        d = dirs[rng.integers(0, 6)]
        for _ in range(9):
            q = np.clip(p + d * rng.integers(2, 6) * .5, -2.2, 2.2)
            PIPES.append((p.copy(), q.copy(), colour))
            p = q
            choices = [v for v in dirs if abs(np.dot(v, d)) < .5]
            d = choices[rng.integers(0, len(choices))]


_plan_pipes()


def pipes():
    cols = np.array([hexc(c) for c in (0xE02020, 0x20B040, 0x2060E0, 0xE0C020, 0xC040E0)])

    def scene(p):
        sc = Scene(p)
        for a, b, c in PIPES:
            sc.add(sd_capsule(p, a, b, .1), 1 + c)
            sc.add(sd_sphere(p, b, .16), 1 + c)
        return sc

    def shade(mat, p, n, v):
        return cols[np.clip(mat - 1, 0, 4)], np.zeros(len(p)), np.full(len(p), 1.0), np.zeros((len(p), 3))
    rgb, cov, _ = render(scene, shade, lambda d: np.zeros((len(d), 3)), (3.4, 2.6, 5.6), (0, 0, 0), .95, light=(.5, .8, .6), reflect=False, shadows=False)
    return to_img(rgb)


def mystify():
    out = black()
    lay = layer()
    d = ImageDraw.Draw(lay)
    rng = np.random.default_rng(21)
    for col in ((255, 60, 200), (60, 220, 255), (255, 240, 80)):
        pts = rng.uniform(60, 450, (4, 2))
        vel = rng.uniform(-9, 9, (4, 2))
        for k in range(22):
            d.line([tuple(q) for q in pts] + [tuple(pts[0])], fill=col + (int(40 + 215 * (k / 21) ** 2),), width=2)
            pts = pts + vel
            vel = np.where((pts < 20) | (pts > 492), -vel, vel)
    out.alpha_composite(glow(lay, 5, 1.2))
    return out


def starfield():
    out = Image.new('RGBA', (S, S), (0, 0, 8, 255))
    lay = layer()
    d = ImageDraw.Draw(lay)
    rng = np.random.default_rng(23)
    for _ in range(500):
        a = rng.uniform(0, 2 * np.pi)
        r0 = rng.uniform(4, 320)
        L = r0 * rng.uniform(.05, .25)
        b = int(min(255, 70 + r0 * .7))
        d.line([(256 + np.cos(a) * r0, 256 + np.sin(a) * r0), (256 + np.cos(a) * (r0 + L), 256 + np.sin(a) * (r0 + L))], fill=(b, b, 255, 255),
               width=max(1, int(r0 / 110)))
    out.alpha_composite(glow(lay, 3, 1))
    return out


def flying_flags():
    out = Image.new('RGBA', (S, S), (0, 0, 0, 255))
    rng = np.random.default_rng(24)
    lay = layer()
    d = ImageDraw.Draw(lay)
    for _ in range(140):
        x, y = rng.uniform(0, S), rng.uniform(0, S)
        d.point((x, y), fill=(200, 200, 220, 255))
    flags = sorted([(rng.uniform(.3, 1.6), rng.uniform(40, 470), rng.uniform(40, 470)) for _ in range(9)])
    cols = ((240, 72, 72), (72, 192, 96), (72, 144, 240), (240, 208, 64))
    for s, cx, cy in flags:  # each a waving four-colour flag, the near ones bigger
        w = 34 * s
        for q in range(4):
            qx, qy = q % 2, q // 2
            pts = []
            for u, v in ((0, 0), (1, 0), (1, 1), (0, 1)):
                fx = (qx + u) * w
                fy = (qy + v) * w * .8 + 6 * s * np.sin((qx + u) * 1.6)
                pts.append((cx - w + fx, cy - w * .8 + fy))
            d.polygon(pts, fill=cols[q] + (255,))
        for k in range(5):
            d.line([(cx - w - 8 * s - k * 4 * s, cy - w * .6 + k * 6 * s), (cx - w - 2 * s, cy - w * .6 + k * 6 * s)], fill=(255, 255, 255, 140), width=max(1, int(s)))
    out.alpha_composite(lay)
    return out


def text3d():
    base = black()
    t = chrome_text('Rainstar', font('LibreBaskerville-Bold.ttf', 70), (256, 256), outline=3, depth=6)
    # turned in space, as the screensaver turned it: squeezed across, tipped, leaning
    t = t.resize((380, 560), Image.BICUBIC).rotate(10, resample=Image.BICUBIC, expand=False)
    base.alpha_composite(t, (66, -24))
    return base


def maze_saver():
    return Image.open(ads.FOOT[0]).convert('RGBA').resize((S, S), Image.LANCZOS)


# ---------------------------------------------------------------- Memphis, jazz, carpet
def memphis():
    out = white((250, 248, 242))
    d = ImageDraw.Draw(out)
    rng = np.random.default_rng(31)
    pal = [(255, 80, 160), (40, 200, 220), (255, 210, 0), (20, 20, 20), (120, 80, 220)]
    for k in range(34):
        x, y = rng.uniform(10, 500), rng.uniform(10, 500)
        c = pal[k % 5]
        kind = k % 6
        if kind == 0:
            d.line([(x + i * 9, y + (10 if i % 2 else -10)) for i in range(8)], fill=c, width=5)
        elif kind == 1:
            d.polygon([(x, y), (x + 40, y + 6), (x + 14, y + 40)], fill=c)
        elif kind == 2:
            d.rectangle([x, y, x + 8, y + 26], fill=c)
        elif kind == 3:
            for i in range(4):
                for j in range(4):
                    if (i + j) % 2:
                        d.rectangle([x + i * 8, y + j * 8, x + i * 8 + 8, y + j * 8 + 8], fill=(20, 20, 20))
        elif kind == 4:
            d.ellipse([x, y, x + 30, y + 30], outline=c, width=5)
        else:
            d.arc([x, y, x + 60, y + 40], 200, 340, fill=c, width=6)
    return out


def jazz():
    out = white()
    ys, xs = np.mgrid[0:S, 0:S].astype(float)
    a = np.asarray(out, float)
    teal = (np.abs(ys - (250 + 60 * np.sin(xs / 90))) < 36 + 10 * np.sin(xs / 13)) & (xs > 30) & (xs < 480)
    rough = np.random.default_rng(1).random((S, S)) > .12
    a[teal & rough, :3] = hexc(0x18A8B0) * 255
    out = Image.fromarray(a.astype(np.uint8))
    d = ImageDraw.Draw(out)
    pts = [(40 + i * 28, 210 + (60 if i % 2 else -40) + 10 * np.sin(i)) for i in range(16)]
    d.line(pts, fill=(110, 40, 160, 255), width=12)
    return out


def arcade_carpet():
    out = black()
    lay = layer()
    d = ImageDraw.Draw(lay)
    rng = np.random.default_rng(41)
    for k in range(60):
        x, y = rng.uniform(0, S), rng.uniform(0, S)
        c = NEON[k % 5] + (255,)
        kind = k % 5
        if kind == 0:  # a ringed planet
            d.ellipse([x - 14, y - 14, x + 14, y + 14], fill=c)
            d.arc([x - 26, y - 8, x + 26, y + 8], 160, 380, fill=NEON[(k + 2) % 5] + (255,), width=3)
        elif kind == 1:  # a rocket
            d.polygon([(x, y - 18), (x + 7, y + 6), (x - 7, y + 6)], fill=c)
            d.polygon([(x - 7, y + 6), (x - 12, y + 14), (x - 4, y + 8)], fill=c)
            d.polygon([(x + 7, y + 6), (x + 12, y + 14), (x + 4, y + 8)], fill=c)
        elif kind == 2:
            d.regular_polygon((x, y, 9), 5, rotation=k * 7, fill=c)
        elif kind == 3:
            d.line([(x + i * 6, y + (6 if i % 2 else -6)) for i in range(6)], fill=c, width=3)
        else:
            d.polygon([(x, y), (x + 16, y + 4), (x + 4, y + 16)], fill=c)
    out.alpha_composite(glow(lay, 3, .8))
    return out


# ---------------------------------------------------------------- slime, figures, lava
def slime_drips():
    out = ads.vgrad(0x6020A0, 0xFF60B0)
    lay = layer()
    d = ImageDraw.Draw(lay)
    c = (110, 255, 60, 255)
    d.rectangle([0, 0, S, 60], fill=c)
    rng = np.random.default_rng(5)
    x = 0
    while x < S:
        w = rng.uniform(16, 40)
        L = rng.uniform(40, 360)
        d.rounded_rectangle([x, 30, x + w, 60 + L], w / 2, fill=c)
        d.ellipse([x - 4, 50 + L, x + w + 4, 70 + L + w * .6], fill=c)
        x += w + rng.uniform(-6, 10)
    hl = layer()
    dh = ImageDraw.Draw(hl)
    for k in range(18):
        xx = rng.uniform(0, S)
        dh.rounded_rectangle([xx, 40, xx + 4, 40 + rng.uniform(30, 200)], 2, fill=(230, 255, 210, 170))
    lay.alpha_composite(hl)
    out.alpha_composite(shadowed(lay, (4, 8), 6, .35))
    return out


def figures():
    spots = [(-1.5, -.4, 0), (-.8, .4, 1), (0, -.2, 2), (.8, .5, 3), (1.5, -.3, 4), (-.4, 1.2, 5), (.5, 1.4, 6), (1.3, 1.0, 7)]

    def scene(p):
        sc = Scene(p)
        sc.add(p[:, 1], 1)
        for x, z, k in spots:
            body = sd_capsule(p, (x, .1, z), (x, .32, z), .12 + .03 * (k % 3))
            head = sd_sphere(p, (x, .5 + .03 * (k % 2), z), .13 + .02 * (k % 2))
            d = smin(body, head, .05)
            if k % 3 == 0:  # horns
                d = np.minimum(d, sd_capsule(p, (x - .07, .6, z), (x - .12, .76, z), .025))
                d = np.minimum(d, sd_capsule(p, (x + .07, .6, z), (x + .12, .76, z), .025))
            elif k % 3 == 1:  # a crest
                d = smin(d, sd_box(p, (x, .66, z), (.02, .06, .1), .01), .03)
            sc.add(d, 2 + k)
        return sc

    cols = np.array([hexc(c) for c in (0xFF4040, 0x40C0FF, 0xFFE040, 0x60FF60, 0xFF60D0, 0xFF9020, 0xA060FF, 0x40FFC0)])

    def shade(mat, p, n, v):
        alb = np.where((mat == 1)[:, None], checker(p, .5, hexc(0x303040), hexc(0x101018)), cols[np.clip(mat - 2, 0, 7)])
        return alb, np.where(mat == 1, .3, 0), np.full(len(p), 1.0), np.where((mat > 1)[:, None], alb * .15, 0)
    rgb, cov, _ = render(scene, shade, sky_gradient([(-1, 0x101018), (1, 0x302848)]), (0, 1.8, 3.4), (0, .2, .4), .9, light=(-.4, 1, .6))
    return to_img(rgb)


def lava_lamp():
    out = ads.vgrad(0x1A0A20, 0x080408)
    ys, xs = np.mgrid[0:S, 0:S].astype(float)
    half = 60 + (ys - 90) * .1  # the glass tapers in at the top
    glass = (np.abs(xs - 256) < half) & (ys > 90) & (ys < 400)
    field = np.zeros((S, S))
    for cx, cy, r in ((250, 140, 26), (270, 220, 38), (240, 300, 30), (262, 372, 46), (245, 176, 14)):
        field += r * r / ((xs - cx) ** 2 + ((ys - cy) * .8) ** 2 + 1)
    blob = glass & (field > 1)
    img = np.asarray(out, float)[..., :3] / 255
    img = np.where(glass[..., None], hexc(0x501060) * (.8 + .4 * np.clip(1 - np.abs(xs - 236) / 60, 0, 1))[..., None], img)
    img = np.where(blob[..., None], hexc(0xFF6020) * (.8 + .3 * np.clip((field - 1) / 3, 0, 1))[..., None], img)
    out = to_img(img).convert('RGBA')
    d = ImageDraw.Draw(out)
    d.polygon([(186, 400), (326, 400), (360, 500), (152, 500)], fill=(160, 160, 170, 255))
    d.polygon([(200, 400), (312, 400), (300, 410), (212, 410)], fill=(90, 90, 100, 255))
    d.polygon([(218, 56), (294, 56), (306, 90), (206, 90)], fill=(160, 160, 170, 255))
    gl = layer()
    ImageDraw.Draw(gl).ellipse([196, 160, 316, 360], fill=(255, 120, 60, 60))
    out.alpha_composite(gl.filter(ImageFilter.GaussianBlur(30)))
    return out


# ---------------------------------------------------------------- the millennium
def plasma_circuit():
    ys, xs = np.mgrid[0:S, 0:S].astype(float) / S * 2 * np.pi
    v = np.sin(xs * 3) + np.sin(ys * 2 + xs) + np.sin(np.hypot(xs - 3, ys - 3) * 3)
    img = gradient_map((v + 3) / 6, [(0, 0x000820), (.4, 0x0040C0), (.7, 0x40A0FF), (1, 0xE0F8FF)])
    out = to_img(img).convert('RGBA')
    circ = layer()
    d = ImageDraw.Draw(circ)
    rng = np.random.default_rng(2)
    for _ in range(40):
        x, y = rng.uniform(0, S), rng.uniform(0, S)
        pts = [(x, y)]
        for _ in range(5):
            if rng.random() < .5:
                x += rng.uniform(-70, 70)
            else:
                y += rng.uniform(-50, 50)
            pts.append((x, y))
        d.line(pts, fill=(200, 240, 255, 200), width=2)
        d.ellipse([x - 4, y - 4, x + 4, y + 4], fill=(255, 255, 255, 255))
    out.alpha_composite(glow(circ, 4, 1))
    return out


def globe_orbit():
    out = ads.vgrad(0x001040, 0x000008)
    lay = layer()
    d = ImageDraw.Draw(lay)
    cx, cy, r = 256, 256, 140
    for k in range(-3, 4):
        w = r * np.cos(k * np.pi / 8)
        d.ellipse([cx - w * .35 * abs(k) / 3 - (r if k == 0 else w * (1 - abs(k) / 3.5)), cy - r, cx + (r if k == 0 else w * (1 - abs(k) / 3.5)), cy + r],
                  outline=(80, 200, 255, 200), width=2)
        y = cy + r * np.sin(k * np.pi / 8)
        hw = r * np.cos(k * np.pi / 8)
        d.ellipse([cx - hw, y - hw * .12, cx + hw, y + hw * .12], outline=(80, 200, 255, 160), width=2)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(140, 230, 255, 255), width=3)
    out.alpha_composite(glow(lay, 6, 1.1))
    orb = layer()
    d = ImageDraw.Draw(orb)
    for a, (rx, ry), c in ((.3, (230, 60), (255, 160, 40)), (-.5, (210, 80), (200, 120, 255))):
        ring = Image.new('RGBA', (S, S), (0, 0, 0, 0))
        ImageDraw.Draw(ring).ellipse([cx - rx, cy - ry, cx + rx, cy + ry], outline=c + (230,), width=3)
        ring = ring.rotate(np.degrees(a), center=(cx, cy), resample=Image.BICUBIC)
        orb.alpha_composite(ring)
    out.alpha_composite(glow(orb, 5, 1))
    out.alpha_composite(lens_flare(cx + 110, cy - 110, .5))
    return out


def visualizer():
    ys, xs = np.mgrid[0:S, 0:S].astype(float)
    r = np.hypot(xs - 256, ys - 256) / S
    a = np.arctan2(ys - 256, xs - 256)
    a = np.abs(((a * 6 / np.pi) % 2) - 1)  # a twelve-fold kaleidoscope
    v = np.sin(r * 30 - a * 4) + np.sin(a * 9 + r * 12) * .7 + np.cos(r * 50 * a)
    hue = (v * .15 + r * 1.5) % 1
    rgb = np.stack([.5 + .5 * np.cos(2 * np.pi * (hue + k / 3)) for k in range(3)], -1)
    rgb *= np.clip(1.4 - r * 2.2, 0, 1)[..., None] * (.6 + .4 * np.abs(np.sin(v * 2)))[..., None]
    return to_img(rgb)


def gel_orbs():
    out = ads.vgrad(0xFFFFFF, 0xB8E0FF)
    rng = np.random.default_rng(6)
    for k in range(12):
        x, y, r = rng.uniform(70, 440), rng.uniform(70, 440), rng.uniform(26, 60)
        c = [0x2A9AFF, 0x30C040, 0xFF6020, 0xA040FF, 0x20D0C0][k % 5]
        out.alpha_composite(gel((x - r, y - r, x + r, y + r), c, radius=int(r)))
    return out


def magic_eye():
    """an autostereogram: let your eyes go past the wall and a snail comes out of it"""
    n = S
    ys, xs = np.mgrid[0:n, 0:n].astype(float)
    depth = np.zeros((n, n))
    depth[np.hypot(xs - 300, ys - 230) < 110] = 1.0                                          # the shell
    depth[(np.abs(ys - 330) < 34) & (xs > 110) & (xs < 420)] = .7                           # the body
    depth[np.hypot(xs - 140, ys - 300) < 44] = .7                                          # the head
    depth[(np.abs(xs - 122) < 8) & (ys > 200) & (ys < 290)] = .7                           # a stalk
    depth[(np.abs(xs - 158) < 8) & (ys > 190) & (ys < 290)] = .7
    period = 80
    rng = np.random.default_rng(9)
    coarse = Image.fromarray((rng.random((n // 6, period // 6 + 1)) * 255).astype(np.uint8)).resize((period + 6, n), Image.BILINEAR)
    tile = gradient_map(np.asarray(coarse, float)[:, :period] / 255, [(0, 0x20A040), (.3, 0xF0E040), (.6, 0xE04080), (1, 0x2060E0)])
    img = np.zeros((n, n, 3))
    for y in range(n):
        for x in range(n):
            sep = int(period - depth[y, x] * 16)
            img[y, x] = img[y, x - sep] if x >= sep else tile[y, x % period]
    return to_img(np.asarray(Image.fromarray((img * 255).astype(np.uint8)).filter(ImageFilter.SMOOTH), float) / 255)


ALL = [(neon_shapes, 'thin'), (lasers, 'wood'), (neon_memphis, 'plastic'), (blacklight, 'canvas'), (kidpix, 'taped'), (mspaint, 'taped'), (clipart, 'taped'),
       (wordart_study, 'taped'), (dither_landscape, 'monitor'), (pipes, 'thin'), (mystify, 'thin'), (starfield, 'thin'), (flying_flags, 'monitor'),
       (text3d, 'monitor'), (maze_saver, 'monitor'), (memphis, 'plastic'), (jazz, 'canvas'), (arcade_carpet, 'canvas'), (slime_drips, 'plastic'),
       (figures, 'canvas'), (lava_lamp, 'wood'), (plasma_circuit, 'chrome'), (globe_orbit, 'chrome'), (visualizer, 'monitor'), (gel_orbs, 'plastic'),
       (magic_eye, 'gallery')]
