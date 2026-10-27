"""Original deterministic hanafuda artwork for PlaySuite's Koi-Koi.

48 faces (hana_00.png .. hana_47.png, ids as in src/koikoi.hpp: month * 4, special cards first)
and a back (hana_back.png), 360 x 504 like the collection's other cards: a cream card with the
house border, and on it a hanafuda painting in the traditional flat colours (vermilion skies,
black hills, deep greens) drawn here from simple shapes, plus a small label (the month's
number and flower, and the card's kind) to help newcomers learn the deck. No borrowed or
generated images. Drawn at double size and reduced for clean edges.

  python3 tools/make_hanafuda.py <output-directory>
"""
import math
import random
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

W, H = 360, 504
S = 2  # supersampling
FONT = '/System/Library/Fonts/Supplemental/Georgia.ttf'
BOLD = '/System/Library/Fonts/Supplemental/Georgia Bold.ttf'
INK = '#1f1a17'
CREAM = '#fffcf3'
PAPER = '#f6ecd4'
VERMILION = '#c8382a'
DEEP_RED = '#9c2420'
GOLD = '#c9a24e'
BLACK = '#211c1a'
GREEN = '#2f6e3a'
DARK_GREEN = '#1f4a2a'
LEAF = '#4a8a3a'
BROWN = '#5b3a24'
WHITE = '#fbf8ee'
PINK = '#f2a7b8'
DEEP_PINK = '#d4587a'
PURPLE = '#6a3f8e'
LILAC = '#a07ac8'
BLUE = '#2d4f9a'
SKY = '#5a86c8'
YELLOW = '#f0c33a'

PX, PY, PW, PH = 26, 24, 308, 420  # the painting's panel on the card


class Pen:
    """Drawing in panel coordinates (0..PW, 0..PH), at S times resolution."""

    def __init__(self, img):
        self.img = img
        self.d = ImageDraw.Draw(img)

    def p(self, x, y):
        return ((PX + x) * S, (PY + y) * S)

    def poly(self, pts, fill, outline=None, width=0):
        self.d.polygon([self.p(x, y) for x, y in pts], fill=fill, outline=outline, width=int(width * S) if width else 0)

    def ellipse(self, x, y, rx, ry, fill, outline=None, width=0):
        a, b = self.p(x - rx, y - ry), self.p(x + rx, y + ry)
        self.d.ellipse((a[0], a[1], b[0], b[1]), fill=fill, outline=outline, width=int(width * S) if width else 0)

    def circle(self, x, y, r, fill, outline=None, width=0):
        self.ellipse(x, y, r, r, fill, outline, width)

    def line(self, pts, fill, width):
        self.d.line([self.p(x, y) for x, y in pts], fill=fill, width=max(1, int(width * S)), joint='curve')
        for x, y in (pts[0], pts[-1]):
            self.circle(x, y, width / 2, fill)

    def rect(self, x0, y0, x1, y1, fill):
        a, b = self.p(x0, y0), self.p(x1, y1)
        self.d.rectangle((a[0], a[1], b[0], b[1]), fill=fill)

    def text(self, x, y, s, size, fill, bold=False, anchor='mm'):
        f = ImageFont.truetype(BOLD if bold else FONT, int(size * S))
        self.d.text(self.p(x, y), s, font=f, fill=fill, anchor=anchor)


def curve(p0, p1, p2, n=24):
    out = []
    for i in range(n + 1):
        t = i / n
        out.append(((1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * p1[0] + t * t * p2[0], (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * p1[1] + t * t * p2[1]))
    return out


def blossom(pen, x, y, r, petal, centre, petals=5, rot=0.0, notch=False):
    for k in range(petals):
        a = rot + k * 2 * math.pi / petals
        cx, cy = x + math.cos(a) * r * .55, y + math.sin(a) * r * .55
        pen.circle(cx, cy, r * .5, petal, outline=INK, width=.8)
        if notch:
            pen.circle(x + math.cos(a) * r * .98, y + math.sin(a) * r * .98, r * .12, PAPER)
    pen.circle(x, y, r * .28, centre)


def leaf(pen, x, y, ln, ang, wd, fill, vein=True):
    dx, dy = math.cos(ang), math.sin(ang)
    nx, ny = -dy, dx
    tip = (x + dx * ln, y + dy * ln)
    pts = [(x, y)]
    for i in range(1, 12):
        t = i / 12
        w = math.sin(math.pi * t) * wd
        pts.append((x + dx * ln * t + nx * w, y + dy * ln * t + ny * w))
    pts.append(tip)
    for i in range(11, 0, -1):
        t = i / 12
        w = math.sin(math.pi * t) * wd
        pts.append((x + dx * ln * t - nx * w, y + dy * ln * t - ny * w))
    pen.poly(pts, fill, outline=INK, width=.7)
    if vein:
        pen.line([(x, y), (x + dx * ln * .85, y + dy * ln * .85)], INK, .6)


def ribbon(pen, kind, x=150, y=60, ang=-.12):
    """A tanzaku (poem slip), tilted across the card: red with writing, plain red, or blue."""
    col = {'poem': VERMILION, 'red': VERMILION, 'blue': BLUE}[kind]
    L, Wd = 250, 46
    dx, dy = math.sin(-ang), math.cos(ang)
    nx, ny = math.cos(ang), math.sin(ang)
    top = (x, y)
    pts = [(top[0] - nx * Wd / 2, top[1] - ny * Wd / 2), (top[0] + nx * Wd / 2, top[1] + ny * Wd / 2),
           (top[0] + nx * Wd / 2 + dx * L, top[1] + ny * Wd / 2 + dy * L), (top[0] - nx * Wd / 2 + dx * L, top[1] - ny * Wd / 2 + dy * L)]
    pen.poly(pts, col, outline=INK, width=1.2)
    # a gold edge stripe
    pen.line([(top[0] - nx * (Wd / 2 - 6), top[1] - ny * (Wd / 2 - 6)), (top[0] - nx * (Wd / 2 - 6) + dx * L, top[1] - ny * (Wd / 2 - 6) + dy * L)], GOLD, 1.4)
    if kind == 'poem':
        # brush writing: a column of short, varied strokes in black
        rng = random.Random(7)
        for k in range(6):
            t = .14 + k * .13
            cx, cy = top[0] + dx * L * t, top[1] + dy * L * t
            for q in range(rng.randint(2, 3)):
                ox = rng.uniform(-10, 10)
                oy = rng.uniform(-8, 8)
                w = rng.uniform(5, 13)
                pen.line([(cx + ox * nx - w * .5, cy + oy + ox * ny), (cx + ox * nx + w * .5, cy + oy + ox * ny + rng.uniform(-4, 4))], BLACK, 2.6)


def band(pen, y0, y1, col, wave=0, seed=1):
    """A sky or ground band across the panel, with a soft wavy top edge."""
    rng = random.Random(seed)
    pts = [(0, y1)]
    for i in range(0, 21):
        x = i * PW / 20
        pts.append((x, y0 + (math.sin(i * .9 + seed) * wave if wave else 0) + rng.uniform(-wave * .3, wave * .3)))
    pts.append((PW, y1))
    pen.poly(pts, col)


def cloud(pen, x, y, w, col):
    for k in range(5):
        pen.ellipse(x + (k - 2) * w * .2, y - abs(k - 2) * 3, w * .16, 10, col)
    pen.rect(x - w * .45, y, x + w * .45, y + 8, col)


# ------------------------------------------------------------------ the twelve months
def pine(pen, sp):
    band(pen, 330, PH, VERMILION, 6, 3)
    pen.line(curve((60, PH), (90, 250), (150, 170)), BROWN, 16)
    pen.line(curve((150, 170), (200, 120), (260, 110)), BROWN, 10)
    pen.line(curve((110, 260), (160, 240), (230, 250)), BROWN, 8)
    for cx, cy, r in ((150, 160, 52), (250, 100, 44), (225, 245, 46), (80, 230, 40), (200, 175, 36)):
        pen.ellipse(cx, cy, r, r * .6, DARK_GREEN, outline=INK, width=1)
        for k in range(13):
            a = math.pi + k * math.pi / 12
            pen.line([(cx, cy + r * .2), (cx + math.cos(a) * r, cy + r * .2 + math.sin(a) * r * .65)], LEAF, 1.6)


def plum(pen, sp):
    band(pen, 0, 70, VERMILION, 0, 2)
    pen.line(curve((PW, 40), (180, 150), (40, 230)), BLACK, 12)
    pen.line(curve((150, 168), (120, 280), (160, 380)), BLACK, 8)
    pen.line(curve((90, 205), (60, 300), (30, 330)), BLACK, 6)
    for x, y, r in ((230, 95, 22), (120, 190, 20), (60, 225, 18), (150, 300, 20), (165, 375, 18), (40, 320, 16), (270, 70, 16), (200, 135, 15)):
        blossom(pen, x, y, r, '#e8434a', YELLOW, notch=True)


def cherry(pen, sp):
    band(pen, 340, PH, VERMILION, 5, 9)
    for x, y, r in ((70, 90, 48), (170, 70, 54), (260, 110, 42), (120, 170, 46), (220, 200, 40), (60, 230, 36)):
        for k in range(9):
            a = k * 2.4
            blossom(pen, x + math.cos(a) * r * .6, y + math.sin(a) * r * .45, 13, PINK, DEEP_PINK, rot=a)
        pen.ellipse(x, y + r * .5, r * .7, 6, '#e98aa2')


def wisteria(pen, sp):
    band(pen, 0, 34, BLACK, 0, 4)
    pen.line(curve((0, 30), (150, 60), (PW, 26)), BROWN, 9)
    for x0 in (40, 95, 150, 205, 260):
        L = 150 + (x0 * 7) % 70
        for k in range(14):
            t = k / 13
            yy = 44 + t * L
            pen.ellipse(x0 + math.sin(t * 5 + x0) * 4, yy, 12 * (1 - t * .55), 8, PURPLE if k % 2 else LILAC, outline=INK, width=.6)
        leaf(pen, x0 + 12, 50, 36, .9, 7, LEAF)


def iris(pen, sp):
    band(pen, 360, PH, '#3b6ea8', 3, 5)
    for x0 in (50, 120, 200, 265):
        for k in range(3):
            leaf(pen, x0 + k * 8, 390, 190 + k * 20, -math.pi / 2 - .15 + k * .15, 7, GREEN, vein=False)
    for x, y in ((70, 150), (150, 110), (230, 165), (110, 230), (250, 250)):
        for k, a in enumerate((-.9, 0, .9, math.pi)):
            leaf(pen, x, y, 34 if k < 3 else 26, -math.pi / 2 + a, 12, PURPLE if k != 1 else '#4a3ab0')
        pen.circle(x, y, 6, YELLOW)


def peony(pen, sp):
    for x, y, r in ((90, 250, 70), (220, 320, 60), (230, 150, 54)):
        for ring in range(3):
            rr = r * (1 - ring * .3)
            for k in range(8):
                a = k * .785 + ring * .4
                pen.circle(x + math.cos(a) * rr * .45, y + math.sin(a) * rr * .45, rr * .42, [DEEP_RED, '#d93a4a', '#f06070'][ring], outline=INK, width=.6)
        pen.circle(x, y, r * .15, YELLOW)
    for x, y, a in ((40, 380, -1.2), (160, 400, -1.6), (280, 400, -2.0), (150, 220, -.4), (280, 230, -2.4)):
        leaf(pen, x, y, 70, a, 18, DARK_GREEN)


def clover(pen, sp):
    band(pen, 360, PH, BLACK, 5, 6)
    for x0, arch in ((20, 1), (120, -1), (210, 1)):
        pts = curve((x0, PH), (x0 + 80 * arch + 60, 120), (x0 + 160, 50 + x0 * .3))
        pen.line(pts, '#7a2a3a', 3)
        for k, (x, y) in enumerate(pts[3::2]):
            leaf(pen, x, y, 18, .5 + k * .7, 6, '#5a8a3a', vein=False)
            pen.circle(x + 6, y + 6, 4, '#c8384a')


def pampas(pen, sp):
    pen.rect(0, 0, PW, 240, VERMILION if sp != 'geese' else '#d8b070')
    if sp == 'moon':
        pen.circle(150, 128, 84, '#fbf4de', outline=INK, width=1)  # the full moon, rising behind the hill
    # the black hill
    pts = [(0, PH)] + [(x, 250 - 60 * math.sin(math.pi * x / PW) + 8 * math.sin(x * .1)) for x in range(0, PW + 1, 8)] + [(PW, PH)]
    pen.poly(pts, BLACK)
    for k in range(18):
        x = 10 + k * 17
        pen.line(curve((x, PH), (x + 6, 300), (x + 20, 240 - (k % 3) * 20)), '#e8d8b0', 1.6)


def chrysanthemum(pen, sp):
    for x, y, r, c in ((90, 140, 60, YELLOW), (220, 230, 56, '#f08a2a'), (110, 330, 50, '#c8384a')):
        for ring in range(2):
            for k in range(16):
                a = k * math.pi / 8 + ring * .2
                rr = r * (1 - ring * .45)
                leaf(pen, x, y, rr, a, rr * .17, c if ring == 0 else '#fbe7a0', vein=False)
        pen.circle(x, y, r * .18, '#b8742a')
    for x, y, a in ((30, 230, -.8), (180, 120, -1.9), (280, 330, -2.6), (40, 400, -.3)):
        leaf(pen, x, y, 60, a, 20, GREEN)


def maple(pen, sp):
    def mleaf(x, y, r, rot, c):
        pts = []
        for k in range(10):
            a = rot + k * math.pi / 5
            rr = r if k % 2 == 0 else r * .45
            pts.append((x + math.cos(a) * rr, y + math.sin(a) * rr))
        pen.poly(pts, c, outline=INK, width=.6)
    pen.line(curve((0, 120), (160, 160), (PW, 90)), BROWN, 6)
    pen.line(curve((60, 140), (100, 260), (60, 360)), BROWN, 4)
    rng = random.Random(10)
    for k in range(16):
        x, y = rng.uniform(20, PW - 20), rng.uniform(60, 390)
        mleaf(x, y, rng.uniform(18, 30), rng.uniform(0, 6.28), rng.choice(['#c8282a', '#e0502a', '#b81a2a', '#e8742a']))


def willow(pen, sp):
    for x0 in (30, 110, 200, 280):
        for k in range(4):
            pen.line(curve((x0 + k * 8, 0), (x0 + k * 8 + 18, 160), (x0 + k * 8 - 6, 330 - k * 25)), '#3a7a3a', 2.4)
            for t in range(0, 300, 22):
                leaf(pen, x0 + k * 8 + 10 * math.sin(t * .01), t + 12, 16, 1.4, 4, '#5a9a40', vein=False)


def paulownia(pen, sp):
    for x, y, a in ((60, 330, -.6), (160, 360, -1.5), (260, 330, -2.4), (100, 250, -1.0), (230, 250, -2.1)):
        leaf(pen, x, y, 90, a, 40, '#2a6a3a')
    for x0 in (90, 155, 220):
        for k in range(9):
            t = k / 8
            pen.ellipse(x0 + math.sin(t * 6) * 6, 220 - t * 120, 13, 9, PURPLE if k % 2 else LILAC, outline=INK, width=.6)
        pen.line([(x0, 230), (x0, 100)], '#4a3a2a', 3)


PLANTS = [pine, plum, cherry, wisteria, iris, peony, clover, pampas, chrysanthemum, maple, willow, paulownia]


# ------------------------------------------------------------------ the special cards
def crane(pen):
    pen.circle(200, 120, 70, VERMILION)  # the sun
    # the crane, standing, wings folded, red crown
    pen.ellipse(130, 300, 60, 34, WHITE, outline=INK, width=1.2)
    pen.poly([(90, 290), (40, 260), (70, 300)], BLACK)  # tail
    pen.line(curve((170, 290), (205, 230), (190, 190)), WHITE, 9)
    pen.line(curve((170, 290), (205, 230), (190, 190)), INK, 1)
    pen.circle(190, 186, 11, WHITE, outline=INK, width=1)
    pen.circle(191, 178, 5, '#e02a2a')
    pen.poly([(198, 186), (232, 196), (198, 192)], '#c8a040')
    pen.line([(120, 330), (110, 400)], BLACK, 2.5)
    pen.line([(140, 330), (150, 400)], BLACK, 2.5)


def warbler(pen):
    x, y = 170, 120
    pen.ellipse(x, y, 34, 22, '#7a9a3a', outline=INK, width=1)
    pen.circle(x + 28, y - 10, 14, '#7a9a3a', outline=INK, width=1)
    pen.poly([(x + 40, y - 12), (x + 54, y - 8), (x + 40, y - 6)], '#3a3a2a')
    pen.circle(x + 32, y - 13, 2.5, BLACK)
    pen.poly([(x - 30, y), (x - 60, y + 8), (x - 30, y + 8)], '#5a7a2a', outline=INK, width=.6)


def curtain(pen):
    # a festival curtain hung on poles, with crests
    pen.rect(0, 250, PW, 340, '#d8d0c0')
    for k in range(7):
        pen.rect(k * 46, 250, k * 46 + 23, 340, '#c8382a')
    pen.line([(0, 250), (PW, 250)], BLACK, 4)
    for x in (70, 160, 250):
        pen.circle(x, 295, 18, WHITE, outline=INK, width=1)
        blossom(pen, x, 295, 12, PINK, DEEP_PINK)


def cuckoo(pen):
    pen.circle(250, 70, 30, YELLOW)
    pen.circle(262, 64, 26, VERMILION if False else PAPER)
    x, y = 140, 120
    pen.poly([(x - 60, y + 10), (x, y - 20), (x + 50, y), (x, y + 18)], '#3a3a50', outline=INK, width=1)
    pen.poly([(x - 10, y - 10), (x - 40, y - 60), (x + 20, y - 15)], '#4a4a60', outline=INK, width=.8)
    pen.circle(x + 40, y - 4, 10, '#3a3a50')
    pen.poly([(x + 48, y - 6), (x + 62, y - 2), (x + 48, y)], YELLOW)


def bridge(pen):
    # the eight-plank bridge: broad planks zigzagging across the iris marsh, on short posts
    pts = [(-10, 300), (80, 250), (150, 300), (230, 240), (320, 290)]
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        for q in (0, 1):
            off = q * 16
            pen.poly([(x0, y0 + off), (x1, y1 + off), (x1, y1 + off + 14), (x0, y0 + off + 14)], '#9a7450' if q else '#b08a5a', outline=INK, width=1.2)
        pen.line([(x1, y1 + 30), (x1, y1 + 70)], '#5a3a24', 5)


def butterflies(pen):
    for x, y, c in ((110, 90, '#3a5ab0'), (220, 140, '#e8a030')):
        for s in (-1, 1):
            pen.ellipse(x + s * 18, y - 8, 20, 14, c, outline=INK, width=1)
            pen.ellipse(x + s * 14, y + 12, 13, 10, c, outline=INK, width=1)
        pen.ellipse(x, y, 4, 18, BLACK)


def boar(pen):
    x, y = 160, 300
    pen.ellipse(x, y, 90, 45, '#5a3a2a', outline=INK, width=1.2)
    pen.ellipse(x + 80, y - 6, 30, 24, '#5a3a2a', outline=INK, width=1)
    pen.poly([(x + 100, y - 4), (x + 122, y + 4), (x + 102, y + 10)], '#3a2418')
    pen.circle(x + 90, y - 14, 3.5, WHITE)
    for dx in (-50, -20, 30, 60):
        pen.line([(x + dx, y + 35), (x + dx, y + 60)], '#3a2418', 6)
    for k in range(9):
        pen.line([(x - 70 + k * 16, y - 40), (x - 64 + k * 16, y - 52)], '#3a2418', 3)


def moon(pen):
    pampas(pen, 'moon')


def geese(pen):
    for k, (x, y) in enumerate(((90, 80), (170, 120), (240, 70))):
        pen.poly([(x - 30, y), (x, y - 26), (x + 8, y), (x - 6, y + 4)], BLACK)
        pen.poly([(x - 30, y), (x - 50, y + 22), (x - 6, y + 4)], BLACK)
        pen.ellipse(x - 10, y + 2, 18, 6, BLACK)


def sake(pen):
    pen.ellipse(170, 280, 80, 26, '#a81a1a', outline=INK, width=1.2)
    pen.poly([(90, 280), (110, 330), (230, 330), (250, 280)], '#c8282a', outline=INK, width=1.2)
    pen.ellipse(170, 335, 50, 12, '#8a1414', outline=INK, width=1)
    pen.ellipse(170, 280, 64, 18, GOLD)
    try:
        f = ImageFont.truetype('/System/Library/Fonts/ヒラギノ明朝 ProN.ttc', 30 * S)
        pen.d.text(pen.p(170, 281), '寿', font=f, fill='#8a1414', anchor='mm')
    except OSError:
        pen.text(170, 281, 'kotobuki', 9, '#8a1414', bold=True)


def deer(pen):
    x, y = 150, 280
    pen.ellipse(x, y, 70, 34, '#b8742a', outline=INK, width=1.2)
    pen.line(curve((x + 50, y - 20), (x + 80, y - 70), (x + 60, y - 100)), '#b8742a', 18)
    pen.ellipse(x + 58, y - 104, 18, 14, '#b8742a', outline=INK, width=1)
    for s in (-1, 1):
        pen.line(curve((x + 58 + s * 6, y - 116), (x + 58 + s * 30, y - 150), (x + 58 + s * 20, y - 175)), '#6a4a2a', 3)
    pen.circle(x + 64, y - 106, 3, BLACK)
    for dx in (-40, -10, 25, 50):
        pen.line([(x + dx, y + 25), (x + dx, y + 80)], '#8a5a2a', 5)
    for k in range(6):
        pen.circle(x - 40 + k * 15, y - 8 + (k % 2) * 10, 4, '#f4e0b0')


def rain_man(pen):
    pen.rect(0, 0, PW, PH, '#2a2a3a')
    willow(pen, None)
    # a stream, a figure with an umbrella, a frog
    pen.poly([(0, 330), (PW, 300), (PW, PH), (0, PH)], '#3a5a8a')
    pen.poly([(120, 230), (220, 210), (190, 250)], '#c8382a', outline=INK, width=1)  # umbrella
    pen.line([(180, 240), (175, 300)], BLACK, 3)
    pen.ellipse(170, 290, 24, 40, '#e8d8b8', outline=INK, width=1)
    pen.circle(172, 245, 11, '#f0d8b0', outline=INK, width=1)
    pen.ellipse(80, 380, 18, 12, '#5aa03a', outline=INK, width=1)
    for k in range(30):
        x = (k * 37) % PW
        y = (k * 53) % 300
        pen.line([(x, y), (x - 6, y + 18)], '#9ab0d0', 1)


def swallow(pen):
    x, y = 170, 140
    pen.poly([(x - 70, y - 30), (x, y), (x - 60, y + 20)], BLACK)
    pen.poly([(x + 70, y - 40), (x, y), (x + 60, y + 10)], BLACK)
    pen.ellipse(x, y + 4, 26, 12, BLACK)
    pen.ellipse(x + 2, y + 10, 14, 6, '#c8382a')
    pen.poly([(x - 20, y + 10), (x - 70, y + 60), (x - 40, y + 10)], BLACK)


def lightning(pen):
    pen.rect(0, 0, PW, PH, VERMILION)
    for k in range(6):
        cx, cy = 50 + k * 45, 120 + (k % 2) * 30
        pen.circle(cx, cy, 26, BLACK)
    pen.poly([(140, 150), (110, 250), (150, 240), (120, 340), (190, 220), (150, 230), (180, 150)], YELLOW, outline=INK, width=1)


def phoenix(pen):
    pen.rect(0, 0, PW, 160, '#e8b030')
    x, y = 150, 230
    for k in range(7):
        a = -2.6 + k * .35
        leaf(pen, x, y, 150, a, 18, [VERMILION, '#3a6ab0', '#2a8a4a', YELLOW, PURPLE, VERMILION, '#3a6ab0'][k])
    pen.ellipse(x, y + 10, 40, 30, VERMILION, outline=INK, width=1.2)
    pen.circle(x + 30, y - 20, 16, YELLOW, outline=INK, width=1)
    pen.poly([(x + 42, y - 22), (x + 60, y - 16), (x + 42, y - 12)], '#e88a2a')
    pen.circle(x + 34, y - 24, 3, BLACK)


SPECIAL = {0: crane, 4: warbler, 8: curtain, 12: cuckoo, 16: bridge, 20: butterflies, 24: boar, 28: moon, 29: geese, 32: sake, 36: deer,
           40: rain_man, 41: swallow, 43: lightning, 44: phoenix}
RIBBONS = {1: 'poem', 5: 'poem', 9: 'poem', 13: 'red', 17: 'red', 21: 'blue', 25: 'red', 33: 'blue', 37: 'blue', 42: 'red'}
BRIGHTS = {0, 8, 28, 40, 44}
ANIMALS = {4, 12, 16, 20, 24, 29, 32, 36, 41}
ROMAN = ['I', 'II', 'III', 'IV', 'V', 'VI', 'VII', 'VIII', 'IX', 'X', 'XI', 'XII']
PLANT_NAMES = ['Pine', 'Plum', 'Cherry', 'Wisteria', 'Iris', 'Peony', 'Bush Clover', 'Pampas', 'Chrysanthemum', 'Maple', 'Willow', 'Paulownia']


def face(cid):
    m = cid // 4
    img = Image.new('RGBA', (W * S, H * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # the house card: cream, a grey edge, a white inner line
    d.rounded_rectangle((2 * S, 2 * S, (W - 3) * S, (H - 3) * S), radius=20 * S, fill=CREAM, outline='#c5c0ab', width=3 * S)
    d.rounded_rectangle((8 * S, 8 * S, (W - 9) * S, (H - 9) * S), radius=15 * S, outline='#ffffff', width=2 * S)
    # the painting's panel
    panel = Image.new('RGBA', (W * S, H * S), (0, 0, 0, 0))
    pen = Pen(panel)
    pen.rect(0, 0, PW, PH, PAPER)
    if cid in (40, 43):
        SPECIAL[cid](pen)
    elif cid == 28:
        moon(pen)
    else:
        PLANTS[m](pen, 'geese' if cid == 29 else None)
        if cid in SPECIAL:
            SPECIAL[cid](pen)
        if cid in RIBBONS:
            ribbon(pen, RIBBONS[cid], x=110 + (cid % 7) * 6, y=70 + (m % 3) * 8)
    if cid % 4 == 3 and cid not in SPECIAL and cid not in RIBBONS:
        box = (PX * S, PY * S, (PX + PW) * S, (PY + PH) * S)
        panel.paste(panel.crop(box).transpose(Image.FLIP_LEFT_RIGHT), box[:2])
    # clip the panel to a rounded rectangle and lay it on the card
    mask = Image.new('L', (W * S, H * S), 0)
    ImageDraw.Draw(mask).rounded_rectangle((PX * S, PY * S, (PX + PW) * S, (PY + PH) * S), radius=10 * S, fill=255)
    img.paste(panel, (0, 0), Image.composite(panel, Image.new('RGBA', panel.size, (0, 0, 0, 0)), mask).split()[3])
    d.rounded_rectangle((PX * S, PY * S, (PX + PW) * S, (PY + PH) * S), radius=10 * S, outline=GOLD, width=3 * S)
    # the label strip: the month's numeral and flower, and the card's kind
    f = ImageFont.truetype(BOLD, 21 * S)
    f2 = ImageFont.truetype(FONT, 17 * S)
    kind = 'Bright' if cid in BRIGHTS else 'Animal' if cid in ANIMALS else 'Ribbon' if cid in RIBBONS else ''
    d.text((PX * S + 4 * S, (PY + PH + 30) * S), ROMAN[m], font=f, fill=VERMILION, anchor='lm')
    nx = PX * S + 4 * S + d.textlength(ROMAN[m], font=f) + 9 * S
    d.text((nx, (PY + PH + 30) * S), PLANT_NAMES[m], font=f2, fill=INK, anchor='lm')
    if kind:
        col = {'Bright': '#b0801a', 'Animal': GREEN, 'Ribbon': DEEP_RED}[kind]
        d.text(((PX + PW - 4) * S, (PY + PH + 30) * S), kind, font=f2, fill=col, anchor='rm')
        if kind == 'Bright':
            d.ellipse(((PX + PW - 22) * S, (PY + 8) * S, (PX + PW - 6) * S, (PY + 24) * S), fill=GOLD, outline=INK, width=S)
    return img.resize((W, H), Image.LANCZOS)


def back():
    img = Image.new('RGBA', (W * S, H * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((2 * S, 2 * S, (W - 3) * S, (H - 3) * S), radius=20 * S, fill=CREAM, outline='#c5c0ab', width=3 * S)
    d.rounded_rectangle((18 * S, 18 * S, (W - 19) * S, (H - 19) * S), radius=12 * S, fill='#7a1e1e', outline=GOLD, width=4 * S)
    # a lattice of small gold plum crests, and a koi in a gold ring at the centre
    pen = Pen(img)
    for yy in range(0, 440, 40):
        for xx in range(0, 300, 40):
            x, y = xx + (20 if (yy // 40) % 2 else 0) + 4, yy + 14
            if 0 < x < 300 and 0 < y < 420:
                pen.circle(x, y, 3, '#a8463a')
    pen.circle(154, 210, 74, '#7a1e1e', outline=GOLD, width=3)
    pen.circle(154, 210, 62, '#9a2a24', outline=GOLD, width=1.5)
    # the koi: a curved body, a fan tail, fins
    body = curve((110, 250), (130, 150), (200, 175), 30)
    for i, (x, y) in enumerate(body):
        r = 14 * math.sin(math.pi * min(1, i / 26)) + 3
        pen.circle(x, y, r, GOLD)
    pen.poly([(110, 250), (88, 270), (100, 236), (82, 222), (112, 242)], GOLD)
    pen.circle(196, 172, 2.5, '#7a1e1e')
    for x, y in ((150, 160), (165, 205)):
        pen.ellipse(x, y, 10, 5, '#e8c870')
    return img.resize((W, H), Image.LANCZOS)


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else 'assets/cards')
    out.mkdir(parents=True, exist_ok=True)
    for c in range(48):
        face(c).save(out / ('hana_%02d.png' % c))
    back().save(out / 'hana_back.png')
    print('wrote 49 cards to', out)


if __name__ == '__main__':
    main()
