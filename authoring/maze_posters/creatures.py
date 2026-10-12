"""Maze 95's residents, modelled as distance fields and rendered as glossy late-90s mascots.
Each model is a scene function and a shader; `portrait` renders one, cut out, for review."""
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

from kit import *


def rot_y(p, a, c=(0, 0, 0)):
    q = p - np.asarray(c, float)
    ca, sa = np.cos(a), np.sin(a)
    return np.stack([q[:, 0] * ca - q[:, 2] * sa, q[:, 1], q[:, 0] * sa + q[:, 2] * ca], -1) + np.asarray(c, float)


def rot_z(p, a, c=(0, 0, 0)):
    q = p - np.asarray(c, float)
    ca, sa = np.cos(a), np.sin(a)
    return np.stack([q[:, 0] * ca - q[:, 1] * sa, q[:, 0] * sa + q[:, 1] * ca, q[:, 2]], -1) + np.asarray(c, float)


def rot_x(p, a, c=(0, 0, 0)):
    q = p - np.asarray(c, float)
    ca, sa = np.cos(a), np.sin(a)
    return np.stack([q[:, 0], q[:, 1] * ca - q[:, 2] * sa, q[:, 1] * sa + q[:, 2] * ca], -1) + np.asarray(c, float)


def sd_cone(p, tip, base, r):
    """a cone from its base (radius r) to its tip"""
    tip, base = np.asarray(tip, float), np.asarray(base, float)
    ax = tip - base
    L = np.linalg.norm(ax)
    ax /= L
    q = p - base
    h = q @ ax
    radial = np.linalg.norm(q - h[:, None] * ax[None], axis=-1)
    rr = r * np.clip(1 - h / L, 0, 1)
    return np.maximum((radial - rr) * L / np.hypot(L, r), np.maximum(-h, h - L))


def eye(sc, p, centre, r, look=(0, 0, 1), mat_white=90, mat_pupil=91, pupil=.55):
    """a cartoon eye: a white ball, a black pupil set into it toward `look`"""
    c = np.asarray(centre, float)
    sc.add(sd_sphere(p, c, r), mat_white)
    lk = norm(np.asarray(look, float))
    sc.add(sd_sphere(p, c + lk * r * .62, r * pupil), mat_pupil)


def toon(alb, n, v, spec=.6):
    rim = np.clip(1 - np.abs(dot(n, -v)), 0, 1) ** 3
    return alb, np.zeros(len(n)), np.full(len(n), spec), rim[:, None] * .25


def eye_colours(mat, alb):
    alb = np.where((mat == 90)[:, None], hexc(0xFFFFFF)[None], alb)
    return np.where((mat == 91)[:, None], hexc(0x0A0A0E)[None], alb)


def portrait(scene, shade, eye_at, look_at, fov=.7, light=(.5, .8, .7), size=S):
    rgb, cov, _ = render(scene, shade, lambda d: np.zeros((len(d), 3)), eye_at, look_at, fov, light=light, reflect=False)
    return to_img(rgb, cov)


# ---------------------------------------------------------------- Such Maze Shiba (the doge pose)
HEAD = np.array([0, 1.3, .1])
NECK = np.array([0, 1.02, .05])
TURN = -.75  # the head turned away to our left; the eyes slide back to you


def head_frame(p):
    return rot_y(p, TURN, NECK)


def shiba_scene(p):
    sc = Scene(p)
    body = sd_ellipsoid(p, (0, .52, -.18), (.4, .46, .44))
    chest = sd_ellipsoid(p, (0, .72, .1), (.3, .36, .28))
    d = smin(body, chest, .12)
    for sx in (-1, 1):
        d = smin(d, sd_capsule(p, (sx * .15, .55, .2), (sx * .16, .08, .24), .085), .05)
        d = smin(d, sd_ellipsoid(p, (sx * .16, .05, .3), (.09, .055, .12)), .03)
        d = smin(d, sd_ellipsoid(p, (sx * .28, .2, -.12), (.15, .2, .26)), .06)
    tail = sd_torus(rot_x(p, 1.25, (0, .95, -.55)), (0, .95, -.55), .16, .07)
    d = np.minimum(d, tail)
    q = head_frame(p)
    h = HEAD
    skull = sd_ellipsoid(q, h, (.36, .3, .3))
    cheeks = sd_ellipsoid(q, h + (0, -.12, .1), (.45, .22, .26))         # big fluffy cheeks
    muzzle = sd_ellipsoid(q, h + (0, -.1, .34), (.14, .11, .17))
    head = smin(smin(skull, cheeks, .1), muzzle, .05)
    for sx in (-1, 1):
        head = smin(head, sd_cone(q, h + (sx * .25, .46, .02), h + (sx * .18, .17, .0), .15), .04)
    d = smin(d, head, .12)
    sc.add(d, 1)
    sc.add(sd_ellipsoid(q, h + (0, -.05, .5), (.06, .045, .035)), 2)              # the nose
    sc.add(sd_capsule(q, h + (-.1, -.17, .4), h + (.02, -.19, .45), .018), 2)     # the smug, half-open mouth
    sc.add(sd_capsule(q, h + (.02, -.19, .45), h + (.11, -.15, .4), .018), 2)
    sc.add(sd_ellipsoid(q, h + (0, -.2, .42), (.06, .02, .03)), 2)
    for sx in (-1, 1):  # round eyes, the whites showing as they look back at you
        c = h + (sx * .15, .07, .27)
        sc.add(sd_ellipsoid(q, c, (.06, .055, .035)), 90)
        sc.add(sd_ellipsoid(q, c + (.034, .004, .016), (.034, .036, .026)), 91)
        sc.add(sd_ellipsoid(q, h + (sx * .14, .18, .26), (.04, .022, .022)), 3)   # raised cream brows
    return sc


def shiba_shade(mat, p, n, v):
    N = len(p)
    q = head_frame(p) - HEAD
    ginger = hexc(0xD48A40)[None] * np.ones((N, 1))
    cream = hexc(0xFFF2DE)[None] * np.ones((N, 1))
    on_head = q[:, 1] > -.36
    face = on_head & (q[:, 1] < -.03) & (q[:, 2] > .02)
    face |= on_head & (q[:, 1] < -.08) & (np.abs(q[:, 0]) > .22)
    chest = ~on_head & (p[:, 2] > .16) & (np.abs(p[:, 0]) < .17 + .05 * (p[:, 1] < .5))
    creamy = face | chest | (p[:, 1] < .2) | ((p[:, 2] < -.45) & (np.abs(p[:, 0]) < .05))
    t = np.clip((q[:, 1] + .08) / .1, 0, 1)  # soften where the cream meets the ginger on the face
    alb = np.where(creamy[:, None], cream, ginger)
    alb = np.where((on_head & (q[:, 2] > .02) & (np.abs(q[:, 1] + .03) < .05))[:, None], ginger * t[:, None] + cream * (1 - t[:, None]), alb)
    ear_in = (q[:, 1] > .24) & (n[:, 2] > .4) & (np.abs(q[:, 0]) < .27)
    alb = np.where(ear_in[:, None], cream, alb)
    alb = np.where((mat == 2)[:, None], hexc(0x141010)[None], alb)
    alb = np.where((mat == 3)[:, None], cream, alb)
    alb = np.where((mat == 91)[:, None], hexc(0x1A0E08)[None], alb)
    alb = np.where((mat == 90)[:, None], hexc(0xFFFFFF)[None], alb)
    return toon(alb, n, v, .35)


def shiba():
    return portrait(shiba_scene, shiba_shade, (-.2, 1.35, 3.1), (0, .92, 0), .6)


# ---------------------------------------------------------------- Ceiling Cat
def cat_scene(p):
    sc = Scene(p)
    body = sd_ellipsoid(p, (0, .5, -.2), (.34, .42, .42))
    chest = sd_ellipsoid(p, (0, .62, .08), (.26, .32, .24))
    d = smin(body, chest, .1)
    for sx in (-1, 1):
        d = smin(d, sd_capsule(p, (sx * .13, .5, .16), (sx * .13, .07, .2), .075), .05)
        d = smin(d, sd_ellipsoid(p, (sx * .13, .05, .26), (.085, .05, .1)), .03)
    head = sd_ellipsoid(p, (0, 1.12, .08), (.36, .3, .3))
    muzzle = sd_ellipsoid(p, (0, 1.03, .3), (.17, .1, .12))
    d = smin(d, smin(head, muzzle, .06), .1)
    for sx in (-1, 1):
        d = smin(d, sd_cone(p, (sx * .28, 1.52, .04), (sx * .2, 1.25, .06), .15), .04)
    tail = sd_capsule(p, (.25, .15, -.45), (.55, .55, -.35), .07)
    d = np.minimum(d, smin(tail, sd_capsule(p, (.55, .55, -.35), (.5, .85, -.2), .065), .05))
    sc.add(d, 1)
    sc.add(sd_ellipsoid(p, (0, 1.1, .4), (.045, .03, .03)), 2)          # the nose
    eye(sc, p, (-.13, 1.18, .3), .075, look=(0, -.2, 1), pupil=.62)
    eye(sc, p, (.13, 1.18, .3), .075, look=(0, -.2, 1), pupil=.62)
    return sc


def cat_shade(mat, p, n, v):
    N = len(p)
    stripes = (np.sin(p[:, 1] * 22 + np.sin(p[:, 0] * 6) * 2) > .35) & (p[:, 2] < .25)
    grey = np.where(stripes[:, None], hexc(0x5A5A64)[None], hexc(0x9A9AA6)[None]) * np.ones((N, 1))
    white = (p[:, 2] > .22) & (p[:, 1] < 1.06) | (p[:, 1] < .1)
    alb = np.where(white[:, None], hexc(0xF4F2EE)[None], grey)
    alb = np.where(((p[:, 1] > 1.3) & (n[:, 2] > .4))[:, None], hexc(0xF0B0B8)[None], alb)
    alb = np.where((mat == 2)[:, None], hexc(0xE88090)[None], alb)
    alb = eye_colours(mat, alb)
    alb = np.where((mat == 91)[:, None], hexc(0x0A0A0E)[None], alb)
    return toon(alb, n, v)


def ceiling_cat():
    img = portrait(cat_scene, cat_shade, (.6, 1.0, 3.2), (0, .75, 0), .6)
    d = ImageDraw.Draw(img)  # whiskers
    for sx in (-1, 1):
        for k in range(3):
            d.line([(256 + sx * 40, 186 + k * 6), (256 + sx * 120, 176 + k * 12)], fill=(250, 250, 250, 230), width=2)
    return img


# ---------------------------------------------------------------- Philosoraptor
def raptor_scene(p, pose=0):
    """standing half upright, a big thoughtful head on an S of a neck, a claw at the chin"""
    sc = Scene(p)
    hip = np.array([0, .95, -.15])
    body = sd_ellipsoid(rot_x(p, -.7, hip), hip + (0, .2, .1), (.27, .3, .48))
    d = body
    pts = [hip + (0, 0, -.25), hip + (0, -.12, -.7), hip + (.08, -.3, -1.15), hip + (.2, -.48, -1.5)]
    for i in range(3):
        d = smin(d, sd_capsule(p, pts[i], pts[i + 1], .17 - i * .05), .1)
    n0, n1, n2 = hip + (0, .5, .4), hip + (0, .72, .48), hip + (0, .82, .64)   # the neck, hunched forward in thought
    d = smin(d, sd_capsule(p, n0, n1, .12), .1)
    d = smin(d, sd_capsule(p, n1, n2, .11), .08)
    head = n2 + (0, .12, .12)
    skull = sd_ellipsoid(p, head, (.19, .18, .24))
    snout = sd_ellipsoid(rot_x(p, .2, head), head + (0, -.04, .3), (.13, .11, .27))
    jaw = sd_ellipsoid(rot_x(p, .12, head), head + (0, -.13, .24), (.11, .06, .24))
    d = smin(d, smin(smin(skull, snout, .06), jaw, .04), .08)
    for sx in (-1, 1):  # legs
        th = hip + (sx * .19, -.08, .08)
        knee = hip + (sx * .23, -.46, .3)
        ankle = hip + (sx * .21, -.8, -.02)
        d = smin(d, sd_ellipsoid(p, th + (0, -.14, .1), (.14, .26, .19)), .08)
        d = smin(d, sd_capsule(p, knee, ankle, .075), .05)
        d = smin(d, sd_capsule(p, ankle, ankle + (0, -.13, .24), .055), .03)
        d = np.minimum(d, sd_capsule(p, ankle + (0, -.08, .1), ankle + (0, .03, .21), .028))
    for sx in (-1, 1):  # arms: the right claw under the chin (pose 0) or both folded (pose 1)
        sh = hip + (sx * .17, .5, .38)
        if pose == 0 and sx == 1:
            el, wr = sh + (.14, -.12, .2), head + (.06, -.2, .16)
        elif pose == 1:
            el, wr = sh + (sx * .1, -.2, .16), hip + (-sx * .08, .36, .52)
        else:
            el, wr = sh + (sx * .06, -.24, .12), sh + (sx * .02, -.32, .3)
        d = smin(d, sd_capsule(p, sh, el, .065), .07)
        d = smin(d, sd_capsule(p, el, wr, .052), .05)
        for k in (-1, 0, 1):
            d = np.minimum(d, sd_capsule(p, wr, wr + (k * .03, .06, .06), .018))
    sc.add(d, 1)
    for sx in (-1, 1):
        eye(sc, p, head + (sx * .13, .07, .08), .05, look=(.5, .25, .7), pupil=.45)
    sc.add(sd_ellipsoid(rot_z(p, -.2, head), head + (0, .15, .07), (.2, .028, .11)), 3)  # a furrowed, thoughtful brow
    return sc


def raptor_shade(mat, p, n, v):
    N = len(p)
    belly = n[:, 1] < -.25
    stripes = (np.sin(p[:, 2] * 12 + p[:, 1] * 5) > .7) & (n[:, 1] > .25)
    alb = np.where(belly[:, None], hexc(0xDCE4A8)[None], hexc(0x4E9E44)[None]) * np.ones((N, 1))
    alb = np.where(stripes[:, None], hexc(0x2E6E2A)[None], alb)
    alb = np.where((mat == 3)[:, None], hexc(0x2A5A20)[None], alb)
    alb = eye_colours(mat, alb)
    alb = np.where((mat == 90)[:, None], hexc(0xF2D24A)[None], alb)
    return toon(alb, n, v, .5)


def philosoraptor(pose=0):
    return portrait(lambda p: raptor_scene(p, pose), raptor_shade, (2.5, 1.7, 2.6), (0, 1.1, 0), .74, light=(.4, .9, .8))


def sheet(out):
    imgs = [('Such Maze Shiba', shiba()), ('Ceiling Cat (seen upright)', ceiling_cat()), ('Philosoraptor, thinking', philosoraptor(0)),
            ('Philosoraptor, arms folded', philosoraptor(1))]
    W = 320
    s = Image.new('RGB', (len(imgs) * (W + 10) + 10, W + 40), (40, 40, 46))
    d = ImageDraw.Draw(s)
    from PIL import ImageFont
    f = ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc', 16)
    for i, (name, im) in enumerate(imgs):
        bg = Image.new('RGBA', (S, S), (70, 70, 80, 255))
        bg.alpha_composite(im)
        s.paste(bg.convert('RGB').resize((W, W), Image.LANCZOS), (10 + i * (W + 10), 34))
        d.text((10 + i * (W + 10), 8), name, fill=(240, 240, 240), font=f)
    s.save(out)


if __name__ == '__main__':
    import sys
    sheet(sys.argv[1])


# ---------------------------------------------------------------- Apu, looking for his mommy
def wobbly(d, pts, fill=None, outline=(0, 0, 0, 255), width=5, seed=1, closed=True):
    """a shape drawn the way a mouse draws it: the outline a little unsteady"""
    rng = np.random.default_rng(seed)
    pts = [(x + rng.uniform(-1.2, 1.2), y + rng.uniform(-1.2, 1.2)) for x, y in pts]
    if fill is not None:
        d.polygon(pts, fill=fill)
    d.line(pts + ([pts[0]] if closed else []), fill=outline, width=width, joint='curve')


def oval(cx, cy, rx, ry, n=40, squash=None):
    out = []
    for k in range(n):
        a = k * 2 * np.pi / n
        r = 1 + (squash(a) if squash else 0)
        out.append((cx + np.cos(a) * rx * r, cy + np.sin(a) * ry * r))
    return out


def apu():
    img = layer()
    d = ImageDraw.Draw(img)
    green, dark = (110, 168, 74, 255), (70, 120, 46, 255)
    blue = (60, 110, 210, 255)
    # legs and feet
    for sx in (-1, 1):
        wobbly(d, oval(256 + sx * 34, 452, 30, 16), green, seed=3 + sx)
    # the bear, clutched at his left (our right)
    for (cx, cy, rx, ry) in ((360, 362, 42, 46), (360, 300, 34, 32), (334, 272, 12, 12), (386, 272, 12, 12)):
        wobbly(d, oval(cx, cy, rx, ry), (150, 100, 60, 255), seed=int(cx + cy))
    for x in (348, 372):
        d.ellipse([x - 4, 292, x + 4, 300], fill=(0, 0, 0, 255))
    d.ellipse([354, 304, 366, 314], fill=(60, 30, 20, 255))
    wobbly(d, oval(360, 372, 20, 22), (190, 150, 110, 255), width=3, seed=8)
    # the blue shirt
    wobbly(d, [(196, 312), (316, 312), (330, 360), (318, 440), (194, 440), (182, 360)], blue, seed=4)
    # arms: one round the bear, one up with the spatula
    wobbly(d, [(312, 330), (350, 350), (344, 368), (306, 352)], green, width=4, seed=5)
    wobbly(d, [(200, 330), (150, 300), (140, 316), (192, 350)], green, width=4, seed=6)
    d.line([(146, 304), (112, 214)], fill=(60, 50, 40, 255), width=9)           # the spatula's handle
    wobbly(d, [(92, 214), (132, 214), (128, 158), (96, 158)], (190, 196, 204, 255), width=4, seed=7)
    for k in range(3):
        d.line([(102 + k * 10, 166), (102 + k * 10, 204)], fill=(120, 126, 134, 255), width=3)
    # the head: wide and lumpy, the big eyes set into the face itself, not perched on top
    wobbly(d, oval(256, 238, 118, 92, squash=lambda a: .07 * np.sin(2 * a) + .04 * np.cos(3 * a)), green, seed=9)
    for sx, rx, ry, px, py in ((-1, 44, 40, 10, -6), (1, 38, 42, 4, -8)):  # uneven, as a mouse draws them
        cx, cy = 256 + sx * 46, 214
        wobbly(d, oval(cx, cy, rx, ry), (255, 255, 255, 255), width=5, seed=12 + sx)
        d.ellipse([cx + px - 11, cy + py - 11, cx + px + 11, cy + py + 11], fill=(10, 10, 10, 255))   # small pupils, looking up at you
    d.line([(214, 286), (230, 280), (252, 283), (270, 279), (296, 287)], fill=(30, 50, 20, 255), width=5, joint='curve')  # a small, worried mouth
    return img


# ---------------------------------------------------------------- Keyboard Cat
def keyboard_cat_scene(p):
    sc = cat_scene(p)
    sc.add(sd_box(p, (0, .32, .55), (.62, .06, .2), .03), 4)                 # the keyboard on its stand
    sc.add(sd_capsule(p, (-.5, .26, .55), (-.5, -.2, .55), .03), 5)
    sc.add(sd_capsule(p, (.5, .26, .55), (.5, -.2, .55), .03), 5)
    for sx in (-1, 1):                                                         # paws on the keys
        sc.add(sd_capsule(p, (sx * .2, .62, .2), (sx * .22, .42, .52), .07), 6)
    return sc


def keyboard_cat_shade(mat, p, n, v):
    N = len(p)
    stripes = np.sin(p[:, 1] * 22 + np.sin(p[:, 0] * 6) * 2) > .35
    orange = np.where(stripes[:, None], hexc(0xC86A1E)[None], hexc(0xF0A050)[None]) * np.ones((N, 1))
    shirt = (p[:, 1] < .92) & (p[:, 1] > .25) & (mat == 1)
    alb = np.where(shirt[:, None], hexc(0x3A6AD0)[None], orange)
    alb = np.where(((p[:, 1] > 1.3) & (n[:, 2] > .4))[:, None], hexc(0xF0B0B8)[None], alb)
    keys = (mat == 4) & (n[:, 1] > .5)
    white_key = (np.floor((p[:, 0] + 1) * 14) % 2 == 0) | (p[:, 2] > .62)
    alb = np.where((mat == 4)[:, None], hexc(0x202024)[None], alb)
    alb = np.where((keys & white_key)[:, None], hexc(0xF4F4F0)[None], alb)
    alb = np.where((mat == 5)[:, None], hexc(0x606068)[None], alb)
    alb = np.where((mat == 6)[:, None], hexc(0xF0A050)[None], alb)
    alb = np.where((mat == 2)[:, None], hexc(0xE88090)[None], alb)
    return toon(eye_colours(mat, alb), n, v)


def keyboard_cat():
    return portrait(keyboard_cat_scene, keyboard_cat_shade, (.5, 1.4, 3.3), (0, .7, .2), .66)


# ---------------------------------------------------------------- the Goose
def goose_scene(p):
    sc = Scene(p)
    body = sd_ellipsoid(rot_x(p, .25, (0, .55, 0)), (0, .55, -.1), (.32, .3, .55))
    tail = sd_cone(p, (0, .78, -.78), (0, .62, -.45), .16)
    d = smin(body, tail, .08)
    for sx in (-1, 1):  # folded wings
        d = smin(d, sd_ellipsoid(rot_x(p, .2, (sx * .26, .64, -.15)), (sx * .26, .64, -.15), (.1, .2, .45)), .05)
    neck = [(0, .7, .3), (0, 1.05, .38), (0, 1.35, .3)]
    d = smin(d, sd_capsule(p, neck[0], neck[1], .1), .08)
    d = smin(d, sd_capsule(p, neck[1], neck[2], .085), .05)
    head = np.array([0, 1.42, .34])
    d = smin(d, sd_ellipsoid(p, head, (.1, .1, .14)), .04)
    sc.add(d, 1)
    sc.add(sd_cone(p, head + (0, -.03, .32), head + (0, -.02, .1), .055), 2)   # the beak, open a little for the honk
    sc.add(sd_cone(p, head + (0, -.07, .28), head + (0, -.06, .1), .035), 2)
    for sx in (-1, 1):
        eye(sc, p, head + (sx * .075, .03, .07), .028, look=(sx * .3, 0, 1), pupil=.7)
        sc.add(sd_capsule(p, (sx * .12, .3, 0), (sx * .12, .02, .03), .025), 3)   # legs
        sc.add(sd_ellipsoid(p, (sx * .12, .015, .1), (.09, .015, .11)), 3)       # webbed feet
    return sc


def goose_shade(mat, p, n, v):
    N = len(p)
    alb = np.where((mat == 1)[:, None], hexc(0xF6F4EE)[None], hexc(0xF08A20)[None]) * np.ones((N, 1))
    alb = np.where(((mat == 1) & (np.abs(p[:, 0]) > .2) & (p[:, 1] > .5))[:, None], hexc(0xD8D6D0)[None], alb)
    return toon(eye_colours(mat, alb), n, v)


def goose():
    return portrait(goose_scene, goose_shade, (1.8, 1.3, 2.6), (0, .75, 0), .7)


# ---------------------------------------------------------------- Monke (the chimp, hand held out)
MHEAD = np.array([0, 1.2, .12])


def monke_scene(p):
    sc = Scene(p)
    body = sd_ellipsoid(rot_x(p, .3, (0, .7, 0)), (0, .7, 0), (.42, .5, .34))   # hunched forward
    d = body
    for sx in (-1, 1):
        d = smin(d, sd_ellipsoid(p, (sx * .4, .95, .05), (.2, .17, .2)), .1)     # big shoulders
        d = smin(d, sd_capsule(p, (sx * .22, .35, .05), (sx * .26, .05, .25), .1), .06)   # legs, squatting
    # the left arm hangs; the right reaches out to you, palm up
    d = smin(d, sd_capsule(p, (-.45, .95, .05), (-.55, .45, .2), .09), .06)
    d = smin(d, sd_capsule(p, (-.55, .45, .2), (-.5, .1, .3), .08), .05)
    d = smin(d, sd_capsule(p, (.45, .95, .05), (.42, .6, .45), .09), .06)
    d = smin(d, sd_capsule(p, (.42, .6, .45), (.2, .55, .85), .08), .05)
    hand = np.array([.12, .55, .95])
    d = smin(d, sd_ellipsoid(p, hand, (.13, .04, .11)), .04)
    for k in range(4):
        d = smin(d, sd_capsule(p, hand + (-.08 + k * .05, 0, .08), hand + (-.09 + k * .055, .07, .16), .022), .015)
    d = smin(d, sd_capsule(p, hand + (.1, 0, -.02), hand + (.17, .06, .04), .024), .015)
    head = sd_ellipsoid(p, MHEAD, (.3, .3, .28))
    for sx in (-1, 1):
        head = smin(head, sd_ellipsoid(p, MHEAD + (sx * .32, .02, -.04), (.07, .11, .05)), .03)    # ears, standing out
    d = smin(d, head, .1)
    sc.add(d, 1)
    face = sd_ellipsoid(p, MHEAD + (0, -.04, .2), (.21, .24, .12))
    brow = sd_capsule(p, MHEAD + (-.15, .1, .27), MHEAD + (.15, .1, .27), .055)                       # the heavy brow
    muzzle = sd_ellipsoid(p, MHEAD + (0, -.15, .28), (.15, .12, .1))
    sc.add(smin(smin(face, brow, .04), muzzle, .05), 2)
    for sx in (-1, 1):
        eye(sc, p, MHEAD + (sx * .08, .03, .31), .032, look=(0, 0, 1), pupil=.75)
        sc.add(sd_sphere(p, MHEAD + (sx * .025, -.08, .38), .015), 3)                               # nostrils
    sc.add(sd_capsule(p, MHEAD + (-.07, -.2, .37), MHEAD + (.07, -.2, .37), .012), 3)               # a pursed, knowing mouth
    return sc


def monke_shade(mat, p, n, v):
    N = len(p)
    q = p - MHEAD
    fur = hexc(0x1E1A1A)[None] * (.85 + .3 * fbm3(p * 30, 2)[:, None])
    skin = hexc(0xD8A088)[None] * np.ones((N, 1))
    ears = (mat == 1) & (np.abs(np.abs(q[:, 0]) - .32) < .08) & (np.abs(q[:, 1]) < .13)
    palm = (mat == 1) & (p[:, 2] > .8)
    alb = np.where((ears | palm)[:, None], skin, fur)
    beard = (mat == 2) & (q[:, 1] < -.2)
    alb = np.where((mat == 2)[:, None], np.where(beard[:, None], hexc(0xE8E2DA)[None], skin), alb)
    alb = np.where((mat == 3)[:, None], hexc(0x3A1E18)[None], alb)
    alb = eye_colours(mat, alb)
    alb = np.where((mat == 90)[:, None], hexc(0x5A3420)[None], alb)
    return toon(alb, n, v, .25)


def monke():
    return portrait(monke_scene, monke_shade, (.5, 1.25, 3.3), (0, .8, .2), .66)


# ---------------------------------------------------------------- the frog on the penny-farthing
def farthing_scene(p, spin=0.0):
    sc = Scene(p)
    big_c, small_c = np.array([0, 1.0, .25]), np.array([0, .28, -1.15])
    wheel = sd_torus(rot_z(p, np.pi / 2, big_c), big_c, .98, .03)
    sc.add(wheel, 1)
    sc.add(sd_torus(rot_z(p, np.pi / 2, small_c), small_c, .26, .025), 1)
    q = p - big_c
    ang = np.arctan2(q[:, 1], q[:, 2]) + spin
    spokes = np.where((np.abs(((ang * 16 / (2 * np.pi)) % 1) - .5) > .45) & (np.abs(q[:, 0]) < .01) & (np.hypot(q[:, 1], q[:, 2]) < .96), -1.0, 1.0)
    sc.add(np.maximum(np.abs(q[:, 0]) - .006, np.hypot(q[:, 1], q[:, 2]) - .96) * .5 + (spokes > 0) * .02, 2)
    sc.add(sd_capsule(p, big_c + (0, .98, 0), small_c + (0, .22, .05), .025), 3)            # the backbone
    sc.add(sd_capsule(p, big_c + (0, .98, 0), big_c + (0, 1.1, .12), .02), 3)             # handlebars' stem
    sc.add(sd_capsule(p, big_c + (-.25, 1.1, .12), big_c + (.25, 1.1, .12), .02), 3)
    # the frog, perched high, in his finery
    seat = big_c + (0, 1.1, -.25)
    body = sd_ellipsoid(p, seat + (0, .22, .02), (.32, .34, .28))
    head = seat + (0, .62, .07)
    d = smin(body, sd_ellipsoid(p, head, (.3, .22, .24)), .1)
    for sx in (-1, 1):
        d = smin(d, sd_sphere(p, head + (sx * .14, .18, .08), .1), .05)                   # eye bumps
        d = smin(d, sd_capsule(p, seat + (sx * .2, .3, .05), big_c + (sx * .25, 1.1, .12), .05), .04)   # arms to the bars
        knee = seat + (sx * .2, -.25, .45)
        pedal = big_c + (sx * .14, .12 * sx, .18 * sx)
        d = smin(d, sd_capsule(p, seat + (sx * .15, .05, .05), knee, .07), .05)                      # thigh
        d = np.minimum(d, sd_capsule(p, knee, pedal, .045))                                          # long shin down to the pedal
        d = np.minimum(d, sd_ellipsoid(p, pedal + (0, -.02, .06), (.06, .03, .12)))                  # a webbed foot
    sc.add(d, 4)
    for sx in (-1, 1):
        eye(sc, p, head + (sx * .14, .2, .14), .06, look=(0, 0, 1), pupil=.5)
    sc.add(sd_torus(rot_x(p, np.pi / 2, head + (.14, .2, .21)), head + (.14, .2, .21), .075, .01), 5)   # the monocle
    sc.add(sd_capsule(p, head + (.2, .14, .21), head + (.25, -.3, .15), .004), 5)                   # its chain
    sc.add(sd_cyl(p, head + (0, .38, -.02), .16, .14), 6)                                            # a top hat
    sc.add(sd_cyl(p, head + (0, .25, -.02), .26, .015), 6)
    return sc


def farthing_shade(mat, p, n, v):
    N = len(p)
    cols = {1: 0x1A1A1A, 2: 0xB0B4BC, 3: 0x2A2A30, 4: 0x5AA84A, 5: 0xE0B840, 6: 0x18181E}
    alb = np.zeros((N, 3))
    for k, c in cols.items():
        alb = np.where((mat == k)[:, None], hexc(c)[None], alb)
    belly = (mat == 4) & (n[:, 2] > .5) & (n[:, 1] < .3)
    alb = np.where(belly[:, None], hexc(0xE8E0A0)[None], alb)
    return toon(eye_colours(mat, alb), n, v, .8)


def farthing_frog():
    return portrait(farthing_scene, farthing_shade, (4.6, 2.2, 2.4), (0, 1.55, -.35), .78)


# ---------------------------------------------------------------- the Thinking Face
def thinker_scene(p):
    sc = Scene(p)
    sc.add(sd_sphere(p, (0, 0, 0), 1.0), 1)
    hand = np.array([.15, -.78, .7])
    d = sd_ellipsoid(p, hand, (.32, .2, .18))
    d = smin(d, sd_capsule(p, hand + (.15, .05, .05), hand + (.42, .3, -.05), .09), .06)       # the finger to the chin
    d = smin(d, sd_capsule(p, hand + (-.2, -.05, 0), hand + (-.25, -.5, -.05), .14), .06)      # the wrist going down
    sc.add(d, 1)
    for sx, h in ((-1, .3), (1, .36)):
        sc.add(sd_ellipsoid(p, (sx * .33, h, .9), (.1, .14, .08)), 2)                         # eyes
    sc.add(sd_capsule(p, (-.5, .62, .8), (-.15, .7, .92), .04), 2)                             # one brow raised
    sc.add(sd_capsule(p, (.2, .6, .9), (.52, .5, .78), .04), 2)
    sc.add(sd_capsule(p, (-.2, -.32, .93), (.3, -.38, .9), .04), 2)                           # a doubtful mouth
    return sc


def thinker_shade(mat, p, n, v):
    alb = np.where((mat == 1)[:, None], hexc(0xFFC83A)[None], hexc(0x5A3A10)[None]) * np.ones((len(p), 1))
    a, m, s_, e = toon(alb, n, v, .9)
    return a, m, s_, e + np.where((mat == 1)[:, None], hexc(0xFFB0D0)[None] * .25, 0)


def thinking_face():
    img = portrait(thinker_scene, thinker_shade, (0, .1, 4.2), (0, -.1, 0), .7)
    halo = layer()
    ImageDraw.Draw(halo).ellipse([60, 60, 452, 452], fill=(255, 190, 220, 120))
    out = glow(halo, 40, 1.4)
    out.alpha_composite(img)
    return out


# ---------------------------------------------------------------- Mazeball
def mazeball_scene(p):
    sc = Scene(p)
    sc.add(sd_sphere(p, (0, 0, 0), 1.0), 1)
    for sx in (-1, 1):
        c = np.array([sx * .32, .35, .9])
        sc.add(sd_sphere(p, c, .26), 90)
        sc.add(sd_sphere(p, c + (sx * .06, -.1, .17), .12), 91)          # googly: the pupils slid to the bottom
    return sc


def mazeball_shade(mat, p, n, v):
    N = len(p)
    u = np.arctan2(p[:, 0], p[:, 2]) * 4
    w = p[:, 1] * 5
    row = np.floor(w)
    brick = np.abs(((u + (row % 2) * .5) % 1) - .5) > .45
    mortar = np.abs((w % 1) - .5) > .42
    alb = np.where((brick | mortar)[:, None], hexc(0xB0A898)[None], hexc(0xB0563A)[None] * (.85 + .15 * np.sin(row * 3.1 + np.floor(u)))[:, None])
    return toon(eye_colours(mat, alb), n, v, .3)


def mazeball():
    return portrait(mazeball_scene, mazeball_shade, (.8, .5, 3.6), (0, 0, 0), .72)


# ---------------------------------------------------------------- the dancing hamster (a reward)
def hamster_scene(p):
    sc = Scene(p)
    d = sd_ellipsoid(p, (0, .55, 0), (.42, .5, .4))
    head = np.array([0, 1.08, .05])
    d = smin(d, sd_ellipsoid(p, head, (.36, .32, .32)), .15)
    for sx in (-1, 1):
        d = smin(d, sd_sphere(p, head + (sx * .25, .28, -.02), .1), .03)                       # ears
        d = smin(d, sd_capsule(p, (sx * .3, .75, .15), (sx * .55, 1.1, .2), .07), .05)         # arms up, dancing
        d = smin(d, sd_ellipsoid(p, (sx * .2, .07, .12), (.12, .07, .15)), .04)                # feet
        d = smin(d, sd_ellipsoid(p, head + (sx * .2, -.1, .2), (.16, .12, .12)), .06)          # stuffed cheeks
    sc.add(d, 1)
    for sx in (-1, 1):
        eye(sc, p, head + (sx * .12, .06, .27), .06, look=(0, 0, 1), pupil=.8)
    sc.add(sd_sphere(p, head + (0, -.04, .34), .04), 2)
    return sc


def hamster_shade(mat, p, n, v):
    N = len(p)
    q = p - np.array([0, 1.08, .05])
    white = ((q[:, 2] > .1) & (q[:, 1] < .0)) | ((p[:, 2] > .2) & (p[:, 1] < .9))
    alb = np.where(white[:, None], hexc(0xFFF4E6)[None], hexc(0xE0A060)[None]) * np.ones((N, 1))
    alb = np.where((mat == 2)[:, None], hexc(0xF08090)[None], alb)
    return toon(eye_colours(mat, alb), n, v)


def hamster():
    return portrait(hamster_scene, hamster_shade, (.4, 1.0, 3.2), (0, .7, 0), .62)


# ---------------------------------------------------------------- the jaks: plain line drawings
SKIN, INK = (244, 232, 222, 255), (20, 20, 20, 255)


def jak_head(d, cx, cy, s=1.0, mood='sad', stubble=False):
    """the bald, pale head every -jak is drawn on"""
    head = oval(cx, cy, 98 * s, 134 * s, n=60, squash=lambda a: .04 * np.cos(2 * a) - .07 * np.sin(a) * (np.sin(a) > 0))  # tall, the chin narrowing
    d.polygon(head, fill=SKIN)
    d.line(head + [head[0]], fill=INK, width=4, joint='curve')
    for sx in (-1, 1):  # ears
        d.arc([cx + sx * 104 * s - 18 * s, cy - 10 * s, cx + sx * 104 * s + 18 * s, cy + 40 * s], 90 - sx * 90 + (0 if sx > 0 else 0), 270 - sx * 90, fill=INK, width=3)
    ey = cy - 6 * s
    for sx in (-1, 1):
        ex = cx + sx * 40 * s
        if mood == 'angry':
            d.ellipse([ex - 20 * s, ey - 14 * s, ex + 20 * s, ey + 14 * s], fill=(255, 255, 255, 255), outline=INK, width=3)
            d.ellipse([ex - 6 * s, ey - 6 * s, ex + 6 * s, ey + 6 * s], fill=INK)
            d.line([(ex - sx * 26 * s, ey - 26 * s), (ex + sx * 22 * s, ey - 14 * s)], fill=INK, width=5)   # brows pinched in
        else:
            d.arc([ex - 22 * s, ey - 12 * s, ex + 22 * s, ey + 16 * s], 200, 340, fill=INK, width=3)       # heavy upper lids
            d.ellipse([ex - 5 * s, ey - 2 * s, ex + 5 * s, ey + 8 * s], fill=INK)
            d.arc([ex - 20 * s, ey + 2 * s, ex + 20 * s, ey + 24 * s], 20, 160, fill=INK, width=2)        # bags
            d.line([(ex - sx * 24 * s, ey - 22 * s), (ex + sx * 20 * s, ey - 34 * s)], fill=INK, width=3)   # brows lifted in the middle: sad
    d.line([(cx - 2 * s, cy + 4 * s), (cx - 14 * s, cy + 46 * s), (cx + 8 * s, cy + 50 * s)], fill=INK, width=3)   # the long nose
    if mood != 'angry':
        for k in range(3):  # a worried forehead
            d.arc([cx - 40 * s, cy - 92 * s + k * 10 * s, cx + 40 * s, cy - 62 * s + k * 10 * s], 200, 340, fill=(120, 110, 104, 255), width=2)
    if mood == 'angry':
        d.rectangle([cx - 30 * s, cy + 64 * s, cx + 30 * s, cy + 84 * s], fill=(255, 255, 255, 255), outline=INK, width=3)  # gritted teeth
        for k in range(1, 5):
            d.line([(cx - 30 * s + k * 12 * s, cy + 64 * s), (cx - 30 * s + k * 12 * s, cy + 84 * s)], fill=INK, width=2)
    else:
        d.line([(cx - 24 * s, cy + 82 * s), (cx - 8 * s, cy + 76 * s), (cx + 10 * s, cy + 77 * s), (cx + 24 * s, cy + 84 * s)], fill=INK, width=3)   # the mouth, turned down
    if stubble:
        rng = np.random.default_rng(int(cx))
        for _ in range(260):
            a = rng.uniform(.15, np.pi - .15)
            r = rng.uniform(.62, .98)
            x, y = cx + np.cos(a) * 100 * s * r, cy + 40 * s + np.sin(a) * 90 * s * r
            d.point((x, y), fill=(90, 80, 74, 255))


def doomer():
    img = layer()
    d = ImageDraw.Draw(img)
    d.polygon([(130, 512), (150, 380), (256, 350), (362, 380), (382, 512)], fill=(28, 28, 32, 255), outline=INK)   # the black hoodie
    d.polygon([(200, 360), (256, 420), (312, 360)], fill=(40, 40, 46, 255))
    jak_head(d, 256, 230, 1.0, 'sad', stubble=True)
    d.chord([146, 92, 366, 250], 180, 360, fill=(22, 22, 26, 255), outline=INK, width=3)    # the black beanie
    d.rectangle([146, 168, 366, 196], fill=(22, 22, 26, 255), outline=INK, width=3)
    d.line([(274, 312), (324, 298)], fill=(240, 240, 240, 255), width=7)                     # a cigarette
    d.line([(320, 299), (326, 297)], fill=(255, 120, 40, 255), width=7)
    for k in range(4):
        d.arc([316 + k * 6, 236 - k * 22, 348 + k * 6, 280 - k * 22], 200, 340, fill=(200, 200, 210, 160), width=3)   # its smoke
    return img


def wanted(name, mood, crime, reward):
    """a WANTED poster, on old paper"""
    ys, xs = np.mgrid[0:S, 0:S].astype(float)
    paper = hexc(0xE8D6A8) * (1 - .14 * fbm3(np.stack([xs.ravel() / 50, ys.ravel() / 50, np.full(S * S, 3.0)], -1), 3).reshape(S, S)[..., None])
    paper *= (1 - .25 * np.clip(np.hypot(xs - 256, ys - 256) / 360, 0, 1) ** 2)[..., None]
    img = to_img(paper).convert('RGBA')
    d = ImageDraw.Draw(img)
    d.text((256, 56), 'WANTED', font=font('LibreBaskerville-Bold.ttf', 76), anchor='mm', fill=(60, 30, 10, 255))
    d.rectangle([116, 104, 396, 360], outline=(60, 30, 10, 255), width=4)
    face = layer()
    jak_head(ImageDraw.Draw(face), 256, 238, .92, mood, stubble=mood == 'angry')
    img.alpha_composite(face)
    d.text((256, 388), name, font=font('LibreBaskerville-Bold.ttf', 30), anchor='mm', fill=(60, 30, 10, 255))
    d.text((256, 424), crime, font=font('LibreBaskerville-Regular.ttf', 18), anchor='mm', fill=(60, 30, 10, 255))
    d.text((256, 468), reward, font=font('LibreBaskerville-Bold.ttf', 24), anchor='mm', fill=(120, 20, 10, 255))
    return img


def wanted_wojak():
    return wanted('WOJAK', 'sad', 'for feeling it, in the corridors', 'REWARD: ONE HUG')


def wanted_chudjak():
    return wanted('CHUDJAK', 'angry', 'for shouting at walls', 'REWARD: 500 MAZES')
