"""The -jaks, redrawn to their reference proportions: one wobbly black MS Paint line on white
(or on the skin), the head tall and tilted in three-quarter view, the shoulders running out flat."""
import numpy as np
from PIL import Image, ImageDraw

from kit import *


def M(pts, dx=0, dy=0):
    """from the reference's frame (a 726 x 600 view of it) into the 512 square"""
    return [((x - 120 + dx) * 1.18, (y - 75 + dy) * 1.18) for x, y in pts]


def stroke(d, pts, w=4, col=(0, 0, 0, 255), seed=0, closed=False):
    pts = catmull(M(pts), 5, closed)
    rng = np.random.default_rng(seed)
    pts = [(x + rng.normal(0, .35), y + rng.normal(0, .35)) for x, y in pts]
    d.line(pts + ([pts[0]] if closed else []), fill=col, width=w, joint='curve')


def catmull(pts, n=6, closed=False):
    out = []
    m = len(pts)
    rng = range(m) if closed else range(m - 1)
    for i in rng:
        p0 = np.array(pts[(i - 1) % m if closed else max(i - 1, 0)], float)
        p1, p2 = np.array(pts[i], float), np.array(pts[(i + 1) % m], float)
        p3 = np.array(pts[(i + 2) % m if closed else min(i + 2, m - 1)], float)
        for k in range(n):
            t = k / n
            out.append(tuple(.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (-p0 + 3 * p1 - 3 * p2 + p3) * t ** 3)))
    if not closed:
        out.append(tuple(pts[-1]))
    return out


HEAD = [(265, 125), (300, 108), (360, 104), (420, 112), (465, 135), (492, 170), (505, 215), (512, 260), (515, 300), (505, 340), (488, 375),
        (465, 405), (440, 425), (410, 438), (380, 440), (350, 432), (320, 415), (298, 390), (288, 362), (275, 340), (258, 305), (242, 270),
        (234, 230), (237, 190), (248, 155)]
NECK_L = [(258, 305), (266, 345), (268, 382), (258, 420), (232, 452), (200, 466), (166, 476)]
NECK_IN = [(294, 312), (298, 360), (300, 408)]
NECK_R = [(430, 440), (428, 456), (452, 464), (518, 468)]
COLLAR = [(336, 483), (352, 492), (372, 492), (390, 483)]


def skin_fill(d, col=(255, 255, 255, 255)):
    d.polygon(catmull(M(HEAD), 5, True), fill=col)
    body = M(NECK_L) + M([(300, 470), (430, 470)]) + M(list(reversed(NECK_R)))
    d.polygon(M([(258, 305), (300, 430), (430, 440), (452, 464), (518, 468), (518, 520), (166, 520), (166, 476), (232, 452), (258, 420), (268, 382)]), fill=col)


def base_lines(d, gaunt=False):
    stroke(d, HEAD, 4, seed=1, closed=True)
    stroke(d, NECK_L, 4, seed=2)
    stroke(d, NECK_IN, 3, seed=3)
    stroke(d, NECK_R, 4, seed=4)
    stroke(d, COLLAR, 3, seed=5)


def sad_face(d):
    stroke(d, [(335, 192), (368, 188), (400, 188), (432, 190), (465, 196)], 3, seed=6)          # the forehead
    stroke(d, [(318, 214), (355, 208), (390, 206), (430, 208), (470, 214)], 3, seed=7)
    stroke(d, [(394, 245), (398, 253)], 3, seed=8)
    stroke(d, [(438, 240), (442, 250)], 3, seed=9)
    eyes(d)
    stroke(d, [(436, 292), (446, 308), (456, 322), (458, 334), (452, 346)], 3, seed=12)          # the nose
    stroke(d, [(406, 336), (394, 340), (392, 348), (402, 352), (414, 350)], 3, seed=13)
    stroke(d, [(420, 354), (438, 355), (452, 348)], 3, seed=14)
    stroke(d, [(382, 386), (400, 388), (420, 388), (440, 386), (456, 380)], 4, seed=15)          # the mouth


def eyes(d, lids=True):
    for upper, lower, (px, py) in (([(340, 268), (352, 262), (368, 262), (384, 266), (392, 274)], [(346, 278), (360, 282), (376, 282), (388, 277)], (376, 272)),
                                   ([(448, 270), (462, 265), (478, 267), (488, 276)], [(452, 282), (468, 286), (484, 282)], (472, 279))):
        x, y = M([(px, py)])[0]
        d.ellipse([x - 10, y - 9, x + 10, y + 9], fill=(0, 0, 0, 255))
        d.ellipse([x + 3, y - 5, x + 7, y - 1], fill=(255, 255, 255, 255))
        stroke(d, upper, 4, seed=int(px))
        stroke(d, lower, 3, seed=int(py))


def wojak_face(skin=(255, 255, 255, 255)):
    img = layer()
    d = ImageDraw.Draw(img)
    skin_fill(d, skin)
    base_lines(d)
    sad_face(d)
    return img


def chudjak_face(skin=(255, 255, 255, 255)):
    img = layer()
    d = ImageDraw.Draw(img)
    skin_fill(d, skin)
    base_lines(d)
    d.polygon(catmull(M([(238, 250), (236, 200), (246, 158), (266, 126), (300, 108), (360, 102), (420, 110), (465, 133), (492, 168), (500, 196),
                          (482, 188), (452, 168), (420, 160), (386, 162), (352, 172), (322, 192), (300, 222), (284, 256), (262, 264)]), 5, True),
              fill=(10, 10, 10, 255))   # the hair, cut short across the crown, parted on the right
    stroke(d, [(398, 196), (404, 222), (402, 244)], 3, seed=20)                                    # strain in the forehead
    stroke(d, [(430, 186), (426, 210)], 3, seed=21)
    for pts in ([(336, 232), (370, 238), (402, 254)], [(498, 238), (472, 240), (448, 254)]):       # brows, furious
        stroke(d, pts, 8, seed=len(pts))
    for (x0, y0, x1, y1) in ((334, 252, 408, 302), (448, 258, 506, 306)):                          # the glasses
        a, b = M([(x0, y0), (x1, y1)])
        d.rectangle([a[0], a[1], b[0], b[1]], outline=(0, 0, 0, 255), width=5)
    stroke(d, [(408, 270), (428, 266), (448, 270)], 4, seed=22)
    stroke(d, [(334, 262), (300, 258), (278, 262)], 4, seed=23)
    for (x, y) in ((378, 282), (478, 286)):                                                         # eyes, narrowed
        c = M([(x, y)])[0]
        d.ellipse([c[0] - 14, c[1] - 6, c[0] + 14, c[1] + 6], fill=(255, 255, 255, 255), outline=(0, 0, 0, 255), width=3)
        d.ellipse([c[0] - 4, c[1] - 6, c[0] + 8, c[1] + 6], fill=(0, 0, 0, 255))
    stroke(d, [(430, 300), (436, 330), (430, 352), (410, 354)], 3, seed=24)                       # nose
    stroke(d, [(400, 346), (394, 352), (404, 356)], 3, seed=25)
    lips = M([(384, 382), (400, 374), (416, 379), (434, 375), (452, 384), (436, 398), (416, 403), (398, 397)])
    d.polygon(catmull(lips, 5, True), fill=(255, 255, 255, 255))
    stroke(d, [(384, 382), (400, 374), (416, 379), (434, 375), (452, 384), (436, 398), (416, 403), (398, 397)], 4, seed=26, closed=True)
    stroke(d, [(386, 384), (418, 387), (450, 384)], 3, seed=27)
    stroke(d, [(410, 418), (426, 430)], 3, seed=28)                                                 # the clenched chin
    return img


def doomer_face():
    img = layer()
    d = ImageDraw.Draw(img)
    # the black hoodie, the rust-red ribbed collar under it
    d.polygon(M([(60, 600), (90, 420), (200, 360), (262, 330), (300, 430), (430, 445), (470, 400), (560, 430), (620, 600)]), fill=(14, 14, 16, 255))
    d.polygon(M([(150, 600), (170, 440), (230, 400), (262, 380), (240, 470), (210, 600)]), fill=(48, 48, 52, 255))   # the hood's inner fold
    collar = M([(268, 440), (300, 452), (360, 462), (430, 456), (452, 470), (440, 486), (360, 492), (290, 480), (262, 466)])
    d.polygon(collar, fill=(160, 70, 30, 255))
    for k in range(12):
        x = 280 + k * 15
        stroke(d, [(x, 452 + abs(k - 6)), (x + 2, 482)], 2, seed=40 + k)
    face = layer()
    fd = ImageDraw.Draw(face)
    fd.polygon(catmull(M(HEAD), 5, True), fill=(255, 255, 255, 255))
    fd.polygon(M([(258, 305), (300, 430), (430, 440), (452, 464), (300, 470), (262, 450)]), fill=(255, 255, 255, 255))
    img.alpha_composite(face)
    stroke(d, HEAD, 4, seed=1, closed=True)
    stroke(d, NECK_IN, 3, seed=3)
    # eyes, half shut and looking off; the nose; the mouth, a cigarette drooping from it
    for upper, (px, py) in (([(340, 272), (360, 264), (384, 266), (394, 276)], (370, 276)), ([(448, 274), (466, 268), (488, 278)], (466, 281))):
        x, y = M([(px, py)])[0]
        d.ellipse([x - 9, y - 6, x + 9, y + 8], fill=(0, 0, 0, 255))
        stroke(d, upper, 5, seed=int(px))
        stroke(d, [(upper[0][0] + 4, upper[0][1] + 12), (upper[-1][0] - 4, upper[-1][1] + 8)], 2, seed=int(py))
    stroke(d, [(436, 292), (446, 308), (456, 322), (458, 334), (452, 346)], 3, seed=12)
    stroke(d, [(406, 336), (394, 340), (392, 348), (402, 352), (414, 350)], 3, seed=13)
    stroke(d, [(390, 388), (420, 386), (446, 384)], 4, seed=15)
    a, b = M([(440, 384), (488, 420)])
    d.line([a, b], fill=(250, 250, 248, 255), width=10)
    d.line([a, b], fill=(0, 0, 0, 255), width=2)
    d.line([b, (b[0] + 6, b[1] + 5)], fill=(255, 110, 30, 255), width=10)
    for k in range(3):
        stroke(d, [(492 + k * 4, 414 - k * 30), (500 + k * 4, 396 - k * 30), (494 + k * 4, 380 - k * 30)], 2, (160, 160, 170, 255), seed=50 + k)
    # the beanie, ribbed, pulled down to the brow
    beanie = M([(232, 236), (234, 170), (262, 124), (320, 96), (400, 94), (460, 112), (500, 150), (514, 210), (506, 230), (400, 214), (300, 226)])
    d.polygon(catmull(beanie, 5, True), fill=(30, 30, 34, 255))
    band = M([(232, 236), (300, 226), (400, 214), (506, 230), (510, 196), (400, 182), (300, 192), (234, 200)])
    d.polygon(catmull(band, 4, True), fill=(56, 56, 62, 255))
    for k in range(16):
        x = 240 + k * 17
        top = 200 - 14 * np.sin(np.pi * k / 15)
        stroke(d, [(x, top), (x + 1, top + 30)], 2, (20, 20, 22, 255), seed=60 + k)
    return img


# ---------------------------------------------------------------- Apu, looking for his mommy
APU_GREEN, APU_DARK, APU_LIP = (96, 146, 62, 255), (66, 110, 44, 255), (166, 98, 76, 255)


def blob(d, pts, fill, w=4, closed=True):
    sm = catmull(pts, 6, closed)
    d.polygon(sm, fill=fill)
    d.line(sm + [sm[0]], fill=(0, 0, 0, 255), width=w, joint='curve')


def apu():
    img = layer()
    d = ImageDraw.Draw(img)
    # legs, then the blue shirt
    for x in (214, 280):
        blob(d, [(x - 22, 440), (x + 26, 440), (x + 30, 470), (x - 26, 472)], APU_GREEN)
    blob(d, [(170, 330), (330, 330), (346, 380), (340, 450), (160, 450), (154, 380)], (40, 96, 214, 255))
    # the bear, clutched under his left arm (our right)
    bear = (150, 104, 66, 255)
    for (cx, cy, rx, ry) in ((356, 396, 40, 44), (356, 336, 34, 31), (330, 310, 12, 12), (382, 310, 12, 12)):
        blob(d, [(cx + np.cos(a) * rx, cy + np.sin(a) * ry) for a in np.linspace(0, 2 * np.pi, 14, endpoint=False)], bear, 3)
    d.ellipse([344, 328, 352, 336], fill=(0, 0, 0, 255))
    d.ellipse([362, 328, 370, 336], fill=(0, 0, 0, 255))
    d.ellipse([350, 340, 362, 349], fill=(60, 30, 20, 255))
    blob(d, [(320, 356), (360, 372), (352, 392), (314, 380)], APU_GREEN, 3)          # his arm round the bear
    # the other arm, up, with the spatula
    blob(d, [(176, 352), (128, 318), (118, 334), (168, 378)], APU_GREEN, 3)
    d.line([(124, 326), (96, 238)], fill=(70, 56, 40, 255), width=10)
    blob(d, [(74, 238), (116, 238), (114, 178), (78, 178)], (196, 200, 208, 255), 3)
    for k in range(3):
        d.line([(86 + k * 10, 186), (86 + k * 10, 228)], fill=(130, 136, 144, 255), width=3)
    # the head: round and soft, lid-lobes up on top, the big glossy eyes under them
    blob(d, [(132, 250), (150, 196), (196, 160), (256, 150), (320, 160), (372, 196), (390, 250), (376, 300), (330, 336), (256, 346), (184, 336), (140, 300)], APU_GREEN)
    for (cx, cy, rx, ry) in ((210, 178, 64, 44), (310, 182, 58, 42)):                 # the lid lobes
        blob(d, [(cx + np.cos(a) * rx, cy + np.sin(a) * ry * (1.0 if np.sin(a) < 0 else .55)) for a in np.linspace(np.pi, 3 * np.pi, 16, endpoint=False)], APU_GREEN)
    for (cx, cy, rx, ry) in ((212, 224, 44, 34), (308, 226, 40, 32)):                  # the eyes, sad and shining
        blob(d, [(cx + np.cos(a) * rx, cy + np.sin(a) * ry) for a in np.linspace(0, 2 * np.pi, 16, endpoint=False)], (255, 255, 255, 255), 3)
        d.ellipse([cx - rx * .7, cy - ry * .55, cx + rx * .7, cy + ry * .95], fill=(12, 12, 14, 255))
        for gx, gy, gr in ((cx - rx * .15, cy - ry * .1, 5), (cx + rx * .2, cy + ry * .1, 3)):
            d.ellipse([gx - gr, gy - gr, gx + gr, gy + gr], fill=(255, 255, 255, 255))
        d.line(catmull([(cx - rx - 4, cy - ry * .2), (cx, cy - ry - 2), (cx + rx + 4, cy - ry * .4)], 6), fill=(0, 0, 0, 255), width=4)   # the lid, drooping
        d.chord([cx - rx, cy - ry - 4, cx + rx, cy + ry * .1], 180, 360, fill=APU_GREEN)
        d.arc([cx - rx, cy - ry - 4, cx + rx, cy + ry * .1], 180, 360, fill=(0, 0, 0, 255), width=4)
    # the long lips, turned a little down: worried
    lips = [(170, 290), (210, 280), (260, 282), (310, 278), (350, 288), (346, 300), (306, 294), (258, 298), (210, 296), (174, 304)]
    blob(d, lips, APU_LIP, 4)
    d.line(catmull([(178, 296), (230, 289), (290, 290), (342, 294)], 6), fill=(110, 60, 46, 255), width=3)
    return img


# ---------------------------------------------------------------- the crayon style (as the supplied drawings are done)
def sketch(d, pts, w=5, passes=2, seed=0, closed=False, col=(14, 14, 14, 255)):
    """a line gone over twice by an unsteady hand"""
    rng = np.random.default_rng(seed)
    base = catmull(M(pts), 5, closed)
    for k in range(passes):
        off = rng.normal(0, 1.4, 2)
        line = [(x + off[0] + rng.normal(0, .6), y + off[1] + rng.normal(0, .6)) for x, y in base]
        d.line(line + ([line[0]] if closed else []), fill=col, width=w - k, joint='curve')


def crayon_fill(img, pts, col, shade=None, seed=0, shift=(4, 3), angle=.9, coverage=.35, region=None):
    """flat paint a little off its outline, scribbled over with crayon strokes in a second tone"""
    poly = catmull(M(pts), 5, True)
    poly = [(x + shift[0], y + shift[1]) for x, y in poly]
    m = Image.new('L', (S, S), 0)
    ImageDraw.Draw(m).polygon(poly, fill=255)
    paint = Image.new('RGBA', (S, S), col)
    img.paste(paint, (0, 0), m)
    if shade:
        rng = np.random.default_rng(seed)
        strokes = layer()
        sd = ImageDraw.Draw(strokes)
        xs = [p[0] for p in poly]
        ys = [p[1] for p in poly]
        for _ in range(int(coverage * 900)):
            x, y = rng.uniform(min(xs), max(xs)), rng.uniform(min(ys), max(ys))
            if region is not None and not region(x, y):
                continue
            L = rng.uniform(10, 34)
            a = angle + rng.normal(0, .15)
            sd.line([(x, y), (x + np.cos(a) * L, y + np.sin(a) * L)], fill=shade, width=int(rng.integers(2, 5)))
        clip = Image.new('RGBA', (S, S), (0, 0, 0, 0))
        clip.paste(strokes, (0, 0), m)
        img.alpha_composite(clip)
    # a few white gaps, as crayon skips on paper
    rng = np.random.default_rng(seed + 9)
    gaps = layer()
    gd = ImageDraw.Draw(gaps)
    for _ in range(60):
        x, y = rng.uniform(0, S), rng.uniform(0, S)
        gd.line([(x, y), (x + rng.uniform(4, 12), y - rng.uniform(1, 6))], fill=(255, 255, 255, 150), width=2)
    clip = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    clip.paste(gaps, (0, 0), m)
    img.alpha_composite(clip)


FACE = HEAD
BUST = [(258, 305), (266, 345), (268, 382), (258, 420), (232, 452), (200, 466), (166, 476), (160, 600), (530, 600), (518, 468), (452, 464), (428, 456), (430, 440),
        (410, 438), (380, 440), (350, 432), (320, 415), (298, 390), (288, 362), (275, 340)]
SKIN_C, SKIN_SHADE = (246, 228, 212, 255), (226, 186, 160, 255)


def crayon_face(img, d, shade_right=True, seed=0):
    crayon_fill(img, BUST, SKIN_C, SKIN_SHADE, seed=seed + 1, coverage=.14, region=lambda x, y: x > 230)
    crayon_fill(img, FACE, SKIN_C, SKIN_SHADE, seed=seed + 2, coverage=.16, region=lambda x, y: x > 360 or y > 400)


def crayon_lines(d, seed=0, face=True):
    sketch(d, HEAD, 6, seed=seed + 1, closed=True)
    sketch(d, NECK_L, 6, seed=seed + 2)
    sketch(d, NECK_IN, 4, seed=seed + 3)
    sketch(d, NECK_R, 6, seed=seed + 4)
    sketch(d, COLLAR, 4, seed=seed + 5)


def crayon_eyes(d, seed=0, lids=1.0):
    for upper, lower, (px, py) in (([(340, 268), (352, 262), (368, 262), (384, 266), (392, 274)], [(346, 278), (360, 282), (376, 282), (388, 277)], (376, 272)),
                                   ([(448, 270), (462, 265), (478, 267), (488, 276)], [(452, 282), (468, 286), (484, 282)], (472, 279))):
        x, y = M([(px, py)])[0]
        d.ellipse([x - 11, y - 10, x + 11, y + 10], fill=(10, 10, 10, 255))
        d.ellipse([x + 2, y - 6, x + 7, y - 1], fill=(255, 255, 255, 255))
        sketch(d, upper, 6, seed=seed + int(px))
        sketch(d, lower, 4, seed=seed + int(py))


def wojak_crayon():
    img = layer()
    d = ImageDraw.Draw(img)
    crayon_face(img, d)
    d = ImageDraw.Draw(img)
    crayon_lines(d)
    sketch(d, [(335, 192), (368, 188), (400, 188), (432, 190), (465, 196)], 4, seed=6)
    sketch(d, [(318, 214), (355, 208), (390, 206), (430, 208), (470, 214)], 4, seed=7)
    sketch(d, [(394, 245), (398, 253)], 4, seed=8)
    sketch(d, [(438, 240), (442, 250)], 4, seed=9)
    crayon_eyes(d)
    sketch(d, [(436, 292), (446, 308), (456, 322), (458, 334), (452, 346)], 5, seed=12)
    sketch(d, [(406, 336), (394, 340), (392, 348), (402, 352), (414, 350)], 5, seed=13)
    sketch(d, [(420, 354), (438, 355), (452, 348)], 4, seed=14)
    sketch(d, [(382, 386), (400, 388), (420, 388), (440, 386), (456, 380)], 6, seed=15)
    return img


def chudjak_crayon():
    img = layer()
    d = ImageDraw.Draw(img)
    crayon_face(img, d, seed=20)
    hair = [(240, 222), (238, 190), (248, 156), (266, 126), (300, 108), (360, 102), (420, 110), (465, 133), (490, 162), (494, 180), (470, 172), (440, 156),
            (404, 150), (370, 156), (340, 150), (312, 162), (290, 184), (270, 212), (252, 230)]
    crayon_fill(img, hair, (24, 22, 22, 255), (60, 56, 56, 255), seed=21, shift=(2, 2), coverage=.4)
    d = ImageDraw.Draw(img)
    crayon_lines(d, seed=20)
    sketch(d, hair, 5, seed=22, closed=True)
    sketch(d, [(398, 196), (404, 222), (402, 244)], 4, seed=23)
    sketch(d, [(430, 186), (426, 210)], 4, seed=24)
    for pts in ([(336, 232), (370, 238), (402, 254)], [(498, 238), (472, 240), (448, 254)]):
        sketch(d, pts, 10, seed=len(pts) + 30)
    for (x0, y0, x1, y1) in ((334, 252, 408, 302), (448, 258, 506, 306)):
        sketch(d, [(x0, y0), (x1, y0), (x1, y1), (x0, y1)], 6, seed=x0, closed=True)
    sketch(d, [(408, 270), (428, 266), (448, 270)], 5, seed=40)
    sketch(d, [(334, 262), (300, 258), (278, 262)], 5, seed=41)
    for (x, y) in ((378, 282), (478, 286)):
        c = M([(x, y)])[0]
        d.ellipse([c[0] - 15, c[1] - 7, c[0] + 15, c[1] + 7], fill=(255, 255, 255, 255), outline=(10, 10, 10, 255), width=4)
        d.ellipse([c[0] - 4, c[1] - 7, c[0] + 9, c[1] + 7], fill=(10, 10, 10, 255))
    sketch(d, [(430, 300), (436, 330), (430, 352), (410, 354)], 5, seed=42)
    lips = [(384, 382), (400, 374), (416, 379), (434, 375), (452, 384), (436, 398), (416, 403), (398, 397)]
    crayon_fill(img, lips, (214, 150, 140, 255), None, seed=43, shift=(1, 1))
    d = ImageDraw.Draw(img)
    sketch(d, lips, 5, seed=44, closed=True)
    sketch(d, [(386, 384), (418, 387), (450, 384)], 4, seed=45)
    return img


def doomer_crayon():
    img = layer()
    d = ImageDraw.Draw(img)
    hood = [(60, 600), (90, 420), (200, 360), (262, 330), (300, 430), (430, 445), (470, 400), (560, 430), (620, 600)]
    crayon_fill(img, hood, (22, 22, 26, 255), (54, 54, 60, 255), seed=50, shift=(0, 0), angle=-.8, coverage=.6)
    crayon_fill(img, [(268, 440), (300, 452), (360, 462), (430, 456), (452, 470), (440, 486), (360, 492), (290, 480), (262, 466)], (176, 74, 34, 255),
                (120, 48, 20, 255), seed=51, shift=(0, 0), angle=1.5, coverage=.3)
    crayon_fill(img, FACE, SKIN_C, SKIN_SHADE, seed=52, coverage=.35, region=lambda x, y: x > 330 or y > 380)
    crayon_fill(img, [(258, 305), (300, 430), (430, 440), (452, 464), (300, 470), (262, 450)], SKIN_C, SKIN_SHADE, seed=53, coverage=.2)
    d = ImageDraw.Draw(img)
    sketch(d, HEAD, 6, seed=54, closed=True)
    sketch(d, hood, 5, seed=55)
    for upper, (px, py) in (([(340, 272), (360, 264), (384, 266), (394, 276)], (370, 276)), ([(448, 274), (466, 268), (488, 278)], (466, 281))):
        x, y = M([(px, py)])[0]
        d.ellipse([x - 10, y - 6, x + 10, y + 9], fill=(10, 10, 10, 255))
        sketch(d, upper, 7, seed=int(px))
        sketch(d, [(upper[0][0] + 4, upper[0][1] + 13), (upper[-1][0] - 4, upper[-1][1] + 9)], 3, seed=int(py))
    sketch(d, [(436, 292), (446, 308), (456, 322), (458, 334), (452, 346)], 5, seed=56)
    sketch(d, [(406, 336), (394, 340), (392, 348), (402, 352), (414, 350)], 5, seed=57)
    sketch(d, [(390, 388), (420, 386), (446, 384)], 6, seed=58)
    rng = np.random.default_rng(5)  # stubble
    for _ in range(320):
        a = rng.uniform(.2, np.pi - .2)
        r = rng.uniform(.55, .95)
        x, y = M([(392 + np.cos(a) * 105 * r, 360 + np.sin(a) * 70 * r)])[0]
        d.point((x, y), fill=(80, 70, 66, 255))
    a, b = M([(440, 384), (488, 420)])
    d.line([a, b], fill=(250, 250, 248, 255), width=11)
    d.line([a, b], fill=(10, 10, 10, 255), width=2)
    d.line([b, (b[0] + 7, b[1] + 6)], fill=(255, 110, 30, 255), width=11)
    for k in range(3):
        sketch(d, [(492 + k * 4, 414 - k * 30), (500 + k * 4, 396 - k * 30), (494 + k * 4, 380 - k * 30)], 3, seed=60 + k, col=(150, 150, 160, 255))
    beanie = [(232, 236), (234, 170), (262, 124), (320, 96), (400, 94), (460, 112), (500, 150), (514, 210), (506, 230), (400, 214), (300, 226)]
    crayon_fill(img, beanie, (30, 30, 34, 255), (66, 66, 72, 255), seed=61, shift=(1, 1), angle=1.4, coverage=.5)
    band = [(232, 236), (300, 226), (400, 214), (506, 230), (510, 196), (400, 182), (300, 192), (234, 200)]
    crayon_fill(img, band, (58, 58, 64, 255), (36, 36, 40, 255), seed=62, shift=(0, 0), angle=1.57, coverage=.6)
    d = ImageDraw.Draw(img)
    sketch(d, beanie, 6, seed=63, closed=True)
    sketch(d, band, 4, seed=64, closed=True)
    return img
