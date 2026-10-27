"""Maze 95 posters, part one: box art, flyers and ads for things that live in the maze."""
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

from kit import *
import props

HERE = os.path.dirname(os.path.abspath(__file__))
FOOT = [os.path.join(HERE, 'footage', f'shot{i}.png') for i in range(6)]
BARLOW = 'BarlowCondensed-Bold.ttf'


def stars(canvas, n=160, seed=9):
    st = layer()
    d = ImageDraw.Draw(st)
    rng = np.random.default_rng(seed)
    for _ in range(n):
        x, y, r = rng.uniform(0, S), rng.uniform(0, S), rng.uniform(.4, 1.6)
        d.ellipse([x - r, y - r, x + r, y + r], fill=(255, 255, 255, int(rng.uniform(90, 255))))
    canvas.alpha_composite(st)


def vgrad(top, bottom):
    t = np.linspace(0, 1, S)[:, None, None]
    return to_img(np.broadcast_to(hexc(top) * (1 - t) + hexc(bottom) * t, (S, S, 3))).convert('RGBA')


# ---------------------------------------------------------------- 1. the box
def box_art():
    canvas = Image.new('RGBA', (S, S), (6, 4, 24, 255))
    stars(canvas)
    canvas.alpha_composite(glow(props.golden_maze(), 14, .5))
    sl = layer()
    d = ImageDraw.Draw(sl)
    rng = np.random.default_rng(2)
    for _ in range(40):
        a = rng.uniform(0, 2 * np.pi)
        d.line([(256 + np.cos(a) * 60, 120 + np.sin(a) * 30), (256 + np.cos(a) * 420, 120 + np.sin(a) * 210)], fill=(255, 240, 140, 60), width=3)
    canvas.alpha_composite(sl)
    paste(canvas, resample(props.mascot(), 340, 340), (230, 250), ((-10, 10), 10, .6))
    canvas.alpha_composite(chrome_text('MAZE', font(BARLOW, 150), (230, 92), angle=6))
    canvas.alpha_composite(chrome_text('95', font(BARLOW, 120), (412, 112), angle=6, depth=6))
    canvas.alpha_composite(starburst(92, 250, 70, 14, (255, 230, 40, 255), (220, 20, 40, 255), 'ENDLESS\nMAZES!', font(BARLOW, 26)))
    canvas.alpha_composite(starburst(440, 230, 52, 12, (60, 230, 255, 255), (20, 40, 160, 255), 'IT\nPAINTS!', font(BARLOW, 22), angle=10, text_fill=(20, 20, 120, 255)))
    strip = layer()
    for i, k in enumerate((0, 2, 1)):
        frame_shot(strip, FOOT[k], (14 + i * 104, 410, 110 + i * 104, 476))
    ImageDraw.Draw(strip).text((14, 486), 'ACTUAL GAME FOOTAGE!', font=font(BARLOW, 18), fill=(255, 230, 40, 255))
    canvas.alpha_composite(shadowed(strip, (5, 5), 3, .5))
    canvas.alpha_composite(cd_badge(470, 462))
    return finish(grain(arr(canvas), .015))


# ---------------------------------------------------------------- 2. the arcade flyer
def arcade_flyer():
    canvas = vgrad(0x0A1A6A, 0x2A4AC0)
    bolt = layer()
    for k, (p0, p1) in enumerate((((20, 0), (120, 260)), ((500, 30), (380, 300)), ((260, 0), (300, 120)), ((0, 300), (90, 512)))):
        lightning(bolt, p0, p1, seed=k + 3)
    canvas.alpha_composite(glow(bolt, 8, 1.2))
    plate = brushed_metal((40, 26, 472, 176), 0x6A6E9A)
    bolts(plate, [(52, 38), (460, 38), (52, 164), (460, 164)])
    paste(canvas, plate, shadow=((6, 6), 5, .6))
    inner = layer()
    ImageDraw.Draw(inner).rounded_rectangle([62, 46, 450, 156], 10, fill=(30, 10, 60, 255), outline=(200, 200, 230, 255), width=2)
    canvas.alpha_composite(inner)
    canvas.alpha_composite(neon_text('MAZE 95', font(BARLOW, 74), (256, 86), (255, 70, 200)))
    canvas.alpha_composite(neon_text('TURBO', font(BARLOW, 40), (256, 136), (60, 230, 255), width=2, glow_r=8))
    canvas.alpha_composite(outlined("Get a Grip on Rainstar's Electrifying Maze!", font('Carlito-Bold.ttf', 19), (256, 202), (255, 90, 190, 255), 2, (40, 0, 60, 255)))
    # the cabinet
    cab = layer()
    d = ImageDraw.Draw(cab)
    d.polygon([(318, 250), (440, 236), (452, 490), (330, 500)], fill=(236, 236, 236, 255))
    d.polygon([(440, 236), (476, 254), (480, 480), (452, 490)], fill=(180, 180, 186, 255))
    d.polygon([(322, 236), (442, 222), (444, 252), (320, 262)], fill=(40, 40, 50, 255))
    d.text((382, 242), 'MAZE 95', font=font(BARLOW, 16), fill=(255, 70, 200, 255), anchor='mm')
    shot = resample(Image.open(FOOT[3]).convert('RGB'), 100, 76)
    cab.paste(shot, (334, 276))
    d.rectangle([332, 274, 435, 353], outline=(20, 20, 20, 255), width=3)
    d.polygon([(326, 368), (444, 360), (446, 396), (328, 404)], fill=(200, 40, 40, 255))
    d.ellipse([350, 372, 372, 390], fill=(30, 30, 30, 255))
    d.ellipse([400, 368, 414, 382], fill=(250, 220, 40, 255))
    d.rectangle([360, 440, 410, 470], fill=(200, 200, 200, 255), outline=(90, 90, 90, 255))
    canvas.alpha_composite(shadowed(cab, (10, 8), 8, .5))
    # captioned screenshots
    sh = layer()
    small = font('Carlito-Regular.ttf', 13)
    lines = [("Steer through endless mazes.\nDon't touch the snail!", FOOT[1]), ("Easy, Medium and Hard.\nNow with ceilings!", FOOT[4])]
    for i, (cap, path) in enumerate(lines):
        y = 236 + i * 118
        frame_shot(sh, path, (36, y, 150, y + 78))
        ImageDraw.Draw(sh).multiline_text((162, y + 18), cap, font=small, fill=(255, 255, 255, 255), spacing=3)
    canvas.alpha_composite(sh)
    canvas.alpha_composite(outlined('Great for all locations!', font('Carlito-Bold.ttf', 18), (160, 484), (255, 255, 255, 255), 2, (0, 0, 60, 255)))
    return finish(grain(arr(canvas), .015))


# ---------------------------------------------------------------- 3. snail slime
def snail_slime():
    ys, xs = np.mgrid[0:S, 0:S] / S
    bg = hexc(0xFF2AB0)[None, None] * (1 - xs[..., None]) + hexc(0x8AF020)[None, None] * xs[..., None]
    bg = bg * (.85 + .15 * ((np.floor(xs * 16) + np.floor(ys * 16)) % 2))[..., None]
    canvas = to_img(bg).convert('RGBA')
    sp = layer()
    for k, (x, y, r, c) in enumerate(((80, 120, 60, (60, 240, 60, 255)), (440, 90, 50, (40, 200, 255, 255)), (420, 380, 70, (255, 120, 20, 255)),
                                      (90, 420, 46, (180, 60, 255, 255)))):
        slime(sp, x, y, r, c, seed=k + 1)
    canvas.alpha_composite(shadowed(sp, (4, 6), 4, .35))
    # the tub
    tub = layer()
    d = ImageDraw.Draw(tub)
    d.rounded_rectangle([180, 210, 340, 410], 24, fill=(230, 255, 230, 120), outline=(255, 255, 255, 255), width=4)
    d.rounded_rectangle([186, 270, 334, 404], 20, fill=(70, 245, 80, 255))
    slime(tub, 260, 268, 46, (70, 245, 80, 255), seed=7, drips=0)
    d.rounded_rectangle([170, 190, 350, 222], 10, fill=(255, 220, 30, 255), outline=(200, 40, 40, 255), width=3)
    d.rectangle([196, 300, 324, 360], fill=(255, 255, 255, 255), outline=(200, 40, 40, 255), width=3)
    d.text((260, 318), 'SNAIL', font=font('ComicNeue-Bold.ttf', 28), fill=(220, 20, 60, 255), anchor='mm')
    d.text((260, 344), 'SLIME', font=font('ComicNeue-Bold.ttf', 28), fill=(30, 90, 220, 255), anchor='mm')
    canvas.alpha_composite(shadowed(tub, (8, 8), 6, .45))
    paste(canvas, resample(props.mascot(), 170, 170), (330, 250), ((4, 6), 4, .4))
    # the title, every letter its own colour and angle
    title = 'SNAIL SLIME!'
    cols = [(255, 240, 40), (40, 220, 255), (255, 80, 40), (120, 255, 60), (255, 255, 255), (0, 0, 0), (255, 60, 200), (60, 255, 160), (255, 200, 40), (90, 120, 255),
            (255, 90, 60), (255, 255, 0)]
    f = font('ComicNeue-Bold.ttf', 70)
    x = 24
    for i, ch in enumerate(title):
        if ch == ' ':
            x += 18
            continue
        g = outlined(ch, f, (x + 18, 70 + 8 * np.sin(i * 1.3)), cols[i % len(cols)] + (255,), 5, (30, 0, 40, 255), angle=(i % 3 - 1) * 10)
        canvas.alpha_composite(g)
        x += f.getlength(ch) + 2
    canvas.alpha_composite(starburst(96, 270, 62, 14, (255, 240, 40, 255), (220, 20, 40, 255), 'NOW IN\n18\nCOLOURS!', font(BARLOW, 20)))
    canvas.alpha_composite(starburst(440, 200, 46, 10, (255, 255, 255, 255), (40, 200, 60, 255), 'IT\nGLOWS!', font(BARLOW, 20), angle=8, text_fill=(30, 140, 40, 255)))
    canvas.alpha_composite(outlined("It's gooey! It's gross! It's on the walls!", font('ComicNeue-Bold.ttf', 20), (256, 460), (255, 255, 255, 255), 3, (60, 0, 80, 255)))
    canvas.alpha_composite(outlined('Ages 5 & up. Do not eat the slime.', font('Carlito-Regular.ttf', 12), (256, 492), (255, 255, 255, 255), 1, (0, 0, 0, 255)))
    return finish(grain(arr(canvas), .012))


# ---------------------------------------------------------------- 4. collect all six
def buddies_scene(p):
    sc = Scene(p)
    sc.add(p[:, 1], 1)
    sc.add(sd_sphere(p, (-1.3, .32, -.2), .32), 2)                                   # the marble
    sc.add(sd_sphere(p, (-.45, .42, .5), .22), 3)                                    # a bulb
    sc.add(sd_cyl(p, (-.45, .14, .5), .1, .12), 4)
    sc.add(sd_cyl(p, (.35, .05, -.3), .32, .05), 5)                                  # a pad
    sc.add(sd_box(p, (1.2, .45, -.4), (.26, .45, .06), .02), 6)                      # a door
    sc.add(sd_cyl(p, (.6, .2, .6), .26, .06), 7)                                     # the flip stone, on its edge? flat
    sc.add(sd_box(p, (-.2, .18, -.9), (.18, .18, .18), .04), 8)                      # the reward's crate
    return sc


def buddies_shade(mat, p, n, v):
    N = len(p)
    cols = {1: checker(p, .35, hexc(0xF040A0), hexc(0x2A1050)), 2: np.ones((N, 3)) * hexc(0x9A8A7A), 3: np.ones((N, 3)) * hexc(0xFFF4C0),
            4: np.ones((N, 3)) * hexc(0x8A8A90), 5: np.ones((N, 3)) * hexc(0x30C060), 6: np.ones((N, 3)) * hexc(0xE04040),
            7: np.ones((N, 3)) * hexc(0x40A0F0), 8: np.ones((N, 3)) * hexc(0xF0C030)}
    alb = np.zeros((N, 3))
    for k, c in cols.items():
        alb = np.where((mat == k)[:, None], c, alb)
    emit = np.where((mat == 3)[:, None], hexc(0xFFE080)[None] * .6, 0)
    mirror = np.select([mat == 1, mat == 2], [.3, .2], 0)
    return alb, mirror, np.full(N, .7), emit


def collect_all():
    rgb, cov, _ = render(buddies_scene, buddies_shade, sky_gradient([(-1, 0xFFD040), (1, 0xFF7020)]), (0, 1.6, 3.6), (0, .2, 0), 1.0, light=(-.4, 1, .6))
    canvas = to_img(rgb).convert('RGBA')
    paste(canvas, resample(props.mascot(), 200, 200), (300, 170), ((4, 6), 4, .4))
    canvas.alpha_composite(wordart('COLLECT', font(BARLOW, 74), (256, 60), bend=18, outline=3))
    canvas.alpha_composite(wordart('ALL 7!', font(BARLOW, 90), (256, 136), stops=[(0, 0xFFFFFF), (1, 0x40E0FF)], horizontal=False, outline=4, shadow=(5, 5)))
    labels = [('MARBLE', 70, 380), ('BULB', 175, 300), ('PAD', 270, 420), ('DOOR', 448, 330), ('STONE', 345, 430), ('CRATE', 230, 248), ('SNAIL', 420, 190)]
    for txt, x, y in labels:
        canvas.alpha_composite(outlined(txt, font(BARLOW, 18), (x, y), (255, 255, 255, 255), 2, (40, 0, 60, 255)))
    canvas.alpha_composite(starburst(80, 220, 50, 12, (255, 255, 255, 255), (220, 20, 40, 255), 'MAZE\nBUDDIES', font(BARLOW, 18)))
    canvas.alpha_composite(outlined('Each sold separately. Snail may wander off.', font('Carlito-Regular.ttf', 13), (256, 494), (255, 255, 255, 255), 1, (0, 0, 0, 255)))
    return finish(grain(arr(canvas), .012))


# ---------------------------------------------------------------- 5. the marble's own game
def track_scene(p):
    sc = Scene(p)
    q = p.copy()
    q[:, 0] -= np.sin(p[:, 2] * .5) * 1.2
    q[:, 1] -= np.sin(p[:, 2] * .3) * .6
    sc.add(sd_box(q, (0, -.1, -6), (.9, .1, 10), .02), 1)
    sc.add(sd_sphere(p, (.6, .42, .2), .42), 2)
    return sc


def track_shade(mat, p, n, v):
    q = p.copy()
    q[:, 0] -= np.sin(p[:, 2] * .5) * 1.2
    alb = np.where((mat == 1)[:, None], checker(q, .45, hexc(0xF4F4F8), hexc(0x2A60E0)), hexc(0x202020)[None])
    mirror = np.select([mat == 2, mat == 1], [.95, .15], 0)
    return alb, mirror, np.full(len(p), 1.0), np.zeros((len(p), 3))


def marble_game():
    sky = clouds_sky(5, 0x1A5AE0, 0xB8E0FF)
    rgb, cov, _ = render(track_scene, track_shade, lambda d: np.zeros((len(d), 3)), (0, 1.6, 4.2), (0, .3, -1), .95, light=(.3, 1, .5))
    canvas = sky.copy()
    canvas.alpha_composite(to_img(rgb, cov))
    rays = layer()
    d = ImageDraw.Draw(rays)
    for k in range(16):
        a = k * np.pi / 8
        d.polygon([(256, 130), (256 + np.cos(a) * 400, 130 + np.sin(a) * 400), (256 + np.cos(a + .18) * 400, 130 + np.sin(a + .18) * 400)], fill=(255, 220, 60, 60))
    canvas.alpha_composite(rays)
    canvas.alpha_composite(gold_text('THE', font(BARLOW, 54), (256, 52)))
    canvas.alpha_composite(gold_text('MARBLE', font(BARLOW, 128), (256, 128), outline=6, depth=6))
    canvas.alpha_composite(outlined('IT ROLLS.', font(BARLOW, 40), (256, 214), (255, 255, 255, 255), 4, (20, 40, 140, 255), angle=-4))
    canvas.alpha_composite(starburst(430, 400, 56, 14, (255, 220, 40, 255), (200, 100, 0, 255), '100%\nROUND!', font(BARLOW, 22), text_fill=(160, 40, 0, 255)))
    canvas.alpha_composite(outlined('Featuring the giant marble from MAZE 95', font('Carlito-Bold.ttf', 15), (200, 490), (255, 255, 255, 255), 2, (0, 0, 80, 255)))
    return finish(grain(arr(canvas), .012))


# ---------------------------------------------------------------- 6. the render software
def terrain_h(x, z):
    pts = np.stack([x * .35, z * .35, np.full(len(x), 3.0)], -1)
    return (fbm3(pts, 5, 4) - .35) * 3.2 * np.clip((-z - 2) / 4, 0, 1)


def vista_scene(p):
    sc = Scene(p)
    h = terrain_h(p[:, 0], p[:, 2])
    sc.add((p[:, 1] - h) * .5, 1)
    sc.add(p[:, 1] + .05, 2)                                    # the water
    sc.add(sd_sphere(p, (-.8, .9, -1.5), .6), 3)                # chrome
    pyr = np.maximum(np.abs(p[:, 0] - 1.2) + (p[:, 1] - .0) * .7 - .8, np.abs(p[:, 2] + 1.2) + (p[:, 1]) * .7 - .8)
    pyr = np.maximum(pyr, -p[:, 1])
    sc.add(pyr * .7, 4)                                         # a glass pyramid
    return sc


def vista_shade(mat, p, n, v):
    N = len(p)
    rock = hexc(0x8A6A4A)[None] * (.7 + .3 * fbm3(p * 4, 3)[:, None])
    snow = (n[:, 1] > .75) & (p[:, 1] > 1.2)
    rock = np.where(snow[:, None], hexc(0xF0F4FF)[None], rock)
    alb = np.where((mat == 1)[:, None], rock, hexc(0x0A2A4A)[None])
    alb = np.where((mat == 4)[:, None], hexc(0x80E0D0)[None], alb)
    alb = np.where((mat == 3)[:, None], hexc(0x101010)[None], alb)
    mirror = np.select([mat == 3, mat == 2, mat == 4], [.95, .7, .5], 0)
    return alb, mirror, np.select([mat == 3, mat == 4], [1.0, 1.0], .2), np.zeros((N, 3))


def render_box():
    sky = sky_gradient([(-1, 0xFFB060), (0, 0xFF9050), (.15, 0xF8C080), (.5, 0x4A80D0), (1, 0x10204A)])
    rgb, cov, _ = render(vista_scene, vista_shade, sky, (0, 1.0, 4.0), (0, .7, -2), 1.0, light=(-.6, .5, -.6))
    canvas = to_img(rgb).convert('RGBA')
    canvas.alpha_composite(lens_flare(120, 200, .8))
    band = brushed_metal((0, 0, S, 120), 0xC8CCD4)
    canvas.alpha_composite(band)
    canvas.alpha_composite(outlined('VistaForge', font('LibreBaskerville-Bold.ttf', 58), (236, 56), (40, 50, 80, 255), 0, (0, 0, 0, 0)))
    canvas.alpha_composite(outlined('3D', font(BARLOW, 60), (440, 56), (200, 40, 40, 255), 2, (255, 255, 255, 255)))
    canvas.alpha_composite(outlined('Create photorealistic worlds on your PC!', font('Carlito-Bold.ttf', 18), (256, 100), (40, 50, 80, 255), 0, (0, 0, 0, 0)))
    canvas.alpha_composite(sticker(440, 430, 50, None, 'VERSION\n2.0', font(BARLOW, 20)))
    canvas.alpha_composite(outlined('Includes 300 textures!', font('Carlito-Bold.ttf', 20), (150, 480), (255, 255, 255, 255), 2, (0, 0, 0, 255)))
    return finish(grain(arr(canvas), .01))


# ---------------------------------------------------------------- 7. the binder
def binder():
    canvas = Image.new('RGBA', (S, S), (8, 4, 20, 255))
    grid = layer()
    d = ImageDraw.Draw(grid)
    for k in range(-12, 13):
        d.line([(256 + k * 18, 300), (256 + k * 90, S)], fill=(255, 40, 200, 200), width=2)
    for k in range(10):
        y = 300 + (k / 9) ** 2 * 212
        d.line([(0, y), (S, y)], fill=(255, 40, 200, 200), width=2)
    canvas.alpha_composite(glow(grid, 4, 1))
    shapes = layer()
    d = ImageDraw.Draw(shapes)
    rng = np.random.default_rng(4)
    neon = [(40, 240, 255), (255, 240, 40), (255, 60, 200), (120, 255, 80)]
    for k in range(14):
        x, y, r = rng.uniform(30, 480), rng.uniform(30, 290), rng.uniform(14, 34)
        c = neon[k % 4] + (255,)
        kind = k % 3
        if kind == 0:
            d.polygon([(x, y - r), (x + r, y + r * .8), (x - r, y + r * .8)], outline=c, width=3)
        elif kind == 1:
            d.rectangle([x - r, y - r, x + r * .4, y + r * .4], outline=c, width=3)
            d.rectangle([x - r * .4, y - r * .4, x + r, y + r], outline=c, width=3)
            for dx, dy in ((-r, -r), (r * .4, -r), (-r, r * .4), (r * .4, r * .4)):
                d.line([(x + dx, y + dy), (x + dx + r * .6, y + dy + r * .6)], fill=c, width=3)
        else:
            d.line([(x - r + i * r / 3, y + (r / 3 if i % 2 else -r / 3)) for i in range(7)], fill=c, width=3)
    canvas.alpha_composite(glow(shapes, 6, 1.1))
    bd = layer()
    d = ImageDraw.Draw(bd)
    d.rounded_rectangle([150, 250, 362, 470], 16, fill=(20, 20, 30, 255), outline=(255, 255, 255, 255), width=3)
    d.rounded_rectangle([150, 330, 362, 380], 8, fill=(40, 40, 60, 255))
    d.ellipse([240, 340, 272, 370], fill=(200, 200, 210, 255))
    for k in range(5):
        d.line([(170 + k * 40, 270), (200 + k * 30, 310)], fill=neon[k % 4] + (255,), width=4)
    canvas.alpha_composite(shadowed(bd, (8, 8), 6, .5))
    canvas.alpha_composite(chrome_text('MAZE', font(BARLOW, 92), (256, 70), angle=-4, sky=0xC020A0, extrude=(80, 0, 90)))
    canvas.alpha_composite(chrome_text('KEEPER', font(BARLOW, 92), (256, 160), angle=-4, sky=0x20A0C0, extrude=(0, 60, 90)))
    canvas.alpha_composite(outlined('Keeps your maps together!', font('ComicNeue-Bold.ttf', 22), (256, 494), (255, 255, 255, 255), 3, (120, 0, 140, 255)))
    return finish(grain(arr(canvas), .012))


# ---------------------------------------------------------------- 8. the dark floor (airbrushed)
def dark_scene(p):
    sc = Scene(p)
    sc.add(p[:, 1], 1)
    for sx in (-1, 1):
        sc.add(sd_box(p, (sx * 1.9, 3, -3.5), (.9, 3, 5.5)), 2)
    sc.add(sd_box(p, (0, 3, -9.5), (3, 3, .4)), 2)
    sc.add(sd_capsule(p, (0, .12, -1.2), (0, .42, -1.2), .085), 3)  # the hero, small at the mouth of it
    sc.add(sd_sphere(p, (0, .6, -1.2), .085), 3)
    return sc


def dark_shade(mat, p, n, v):
    N = len(p)
    alb = np.where((mat == 1)[:, None], hexc(0x1A1428)[None], hexc(0x2A1A40)[None])
    glow_lines = (mat == 2) & (np.abs(n[:, 2]) < .5) & ((np.abs(((p[:, 1] * 2) % 1) - .5) > .46) | (np.abs(((p[:, 2] * 1.5) % 1) - .5) > .47))
    emit = np.where(glow_lines[:, None], hexc(0x30F0FF)[None] * 1.2, 0) + np.where((mat == 2)[:, None], hexc(0x301060)[None] * .3, 0)
    alb = np.where((mat == 3)[:, None], hexc(0x101018)[None], alb)
    return alb, np.where(mat == 1, .35, 0), np.full(N, .3), emit


def dark_floor():
    rgb, cov, _ = render(dark_scene, dark_shade, sky_gradient([(-1, 0x000000), (1, 0x100828)]), (0, .5, 2.4), (0, 1.5, -4), 1.1, light=(0, 1, -1))
    img = rgb
    ys, xs = np.mgrid[0:S, 0:S]
    rays = np.clip(1 - np.hypot(xs - 256, ys - 230) / 300, 0, 1) ** 2
    img = img + rays[..., None] * hexc(0x40A0FF) * .4
    canvas = to_img(img).convert('RGBA')
    rim = layer()
    ImageDraw.Draw(rim).ellipse([244, 330, 268, 420], outline=(120, 240, 255, 200), width=2)
    canvas.alpha_composite(glow(rim.filter(ImageFilter.GaussianBlur(3)), 10, .8))
    canvas = Image.fromarray(np.asarray(canvas.filter(ImageFilter.GaussianBlur(1.2))))  # the airbrush
    canvas.alpha_composite(gold_text('MAZE 95', font(BARLOW, 96), (256, 70)))
    canvas.alpha_composite(outlined('THE DARK FLOOR', font(BARLOW, 44), (256, 140), (120, 240, 255, 255), 3, (0, 20, 60, 255), angle=-3))
    canvas.alpha_composite(outlined('Some lights are best left on.', font('LibreBaskerville-Regular.ttf', 18), (256, 486), (220, 230, 255, 255), 1, (0, 0, 0, 255)))
    return finish(grain(arr(canvas), .02))


# ---------------------------------------------------------------- 9. the OS upgrade
def vista_os():
    ys, xs = np.mgrid[0:S, 0:S].astype(float)
    sky = hexc(0x1C78E8)[None, None] * (1 - ys[..., None] / 330) + hexc(0xC8ECFF)[None, None] * (ys[..., None] / 330)
    hill = 360 - 50 * np.sin(xs / S * np.pi * 1.2 + .4)
    grass = hexc(0x30B030)[None, None] * (1 + .25 * np.clip(1 - (ys - hill) / 60, 0, 1))[..., None]
    img = np.where((ys > hill)[..., None], grass, np.clip(sky, 0, 1))
    canvas = to_img(img).convert('RGBA')
    sw = layer()
    d = ImageDraw.Draw(sw)
    for k in range(4):
        pts = [(x, 250 + 60 * np.sin(x / 90 + k) - k * 18) for x in range(-20, S + 20, 8)]
        d.line(pts, fill=(255, 255, 255, 90 - k * 15), width=14 - k * 3)
    canvas.alpha_composite(sw.filter(ImageFilter.GaussianBlur(3)))
    bub = layer()
    d = ImageDraw.Draw(bub)
    rng = np.random.default_rng(6)
    for _ in range(14):
        x, y, r = rng.uniform(20, 490), rng.uniform(150, 420), rng.uniform(8, 34)
        d.ellipse([x - r, y - r, x + r, y + r], fill=(200, 240, 255, 60), outline=(255, 255, 255, 180), width=2)
        d.ellipse([x - r * .55, y - r * .7, x - r * .05, y - r * .3], fill=(255, 255, 255, 200))
    canvas.alpha_composite(bub)
    fb, fr = font('Carlito-Bold.ttf', 66), font('Carlito-Regular.ttf', 66)
    w1, w2 = fb.getlength('Rainstar'), fr.getlength(' Vista')
    x0 = 256 - (w1 + w2) / 2
    canvas.alpha_composite(outlined('Rainstar', fb, (x0, 92), (255, 255, 255, 255), 0, (0, 0, 0, 0), anchor='lm', shadow=(3, 4)))
    canvas.alpha_composite(outlined(' Vista', fr, (x0 + w1, 92), (200, 240, 255, 255), 0, (0, 0, 0, 0), anchor='lm', shadow=(3, 4)))
    sun = layer()
    ImageDraw.Draw(sun).ellipse([400, 170, 460, 230], fill=(255, 255, 230, 255))
    canvas.alpha_composite(glow(sun, 24, 1.4))
    canvas.alpha_composite(outlined('Now with more sky.', font('Carlito-Regular.ttf', 26), (256, 150), (255, 255, 255, 255), 0, (0, 0, 0, 0), shadow=(2, 2)))
    canvas.alpha_composite(gel((150, 430, 362, 478), 0x2A9A2A, text='Upgrade now', fnt=font('Carlito-Bold.ttf', 22)))
    return finish(arr(canvas))


# ---------------------------------------------------------------- 10. the reward room, as a horror comic
def reward_horror():
    canvas = vgrad(0x1A0A30, 0x2A5A20)
    tone = layer()
    d = ImageDraw.Draw(tone)
    for y in range(0, S, 10):
        for x in range(0, S, 10):
            r = 1.2 + 2.6 * (y / S)
            d.ellipse([x + (y // 10 % 2) * 5 - r, y - r, x + (y // 10 % 2) * 5 + r, y + r], fill=(10, 30, 10, 120))
    canvas.alpha_composite(tone)
    door = layer()
    d = ImageDraw.Draw(door)
    d.rectangle([186, 250, 326, 470], fill=(255, 200, 40, 255))
    d.rectangle([200, 264, 312, 470], fill=(255, 250, 200, 255))
    canvas.alpha_composite(glow(door, 26, 1.2))
    sil = layer()
    d = ImageDraw.Draw(sil)
    d.ellipse([240, 330, 272, 362], fill=(10, 10, 10, 255))
    d.polygon([(232, 470), (244, 360), (268, 360), (280, 470)], fill=(10, 10, 10, 255))
    canvas.alpha_composite(sil)
    # dripping title
    title = layer()
    f = font(BARLOW, 82)
    m = mask_text('ENTER IF', f, (256, 70))
    m2 = mask_text('YOU DARE!', f, (256, 150))
    both = Image.fromarray(np.maximum(np.asarray(m), np.asarray(m2)))
    slimed = Image.new('RGBA', (S, S), (120, 255, 60, 255))
    slimed.putalpha(both)
    dd = ImageDraw.Draw(slimed)
    rng = np.random.default_rng(3)
    a = np.asarray(both)
    for _ in range(18):
        x = int(rng.uniform(60, 450))
        col = np.nonzero(a[:, x] > 128)[0]
        if len(col):
            y = col.max()
            L = rng.uniform(10, 40)
            dd.rounded_rectangle([x - 3, y - 2, x + 3, y + L], 3, fill=(120, 255, 60, 255))
            dd.ellipse([x - 5, y + L - 4, x + 5, y + L + 6], fill=(120, 255, 60, 255))
    ol = Image.new('RGBA', (S, S), (10, 40, 0, 255))
    ol.putalpha(slimed.split()[3].filter(ImageFilter.MaxFilter(7)))
    title.alpha_composite(ol)
    title.alpha_composite(slimed)
    canvas.alpha_composite(shadowed(title, (6, 6), 4, .6))
    bub = layer()
    d = ImageDraw.Draw(bub)
    d.ellipse([310, 200, 492, 270], fill=(255, 255, 255, 255), outline=(0, 0, 0, 255), width=3)
    d.polygon([(360, 262), (340, 300), (390, 266)], fill=(255, 255, 255, 255), outline=(0, 0, 0, 255))
    d.text((401, 236), "WHAT'S IN\nTHERE?!", font=font('ComicNeue-Bold.ttf', 20), anchor='mm', fill=(0, 0, 0, 255), align='center')
    canvas.alpha_composite(bub)
    canvas.alpha_composite(outlined('THE REWARD ROOM', font(BARLOW, 30), (256, 494), (255, 220, 40, 255), 3, (60, 0, 0, 255)))
    return finish(grain(arr(canvas), .02))


# ---------------------------------------------------------------- 11. the award
def award():
    ys, xs = np.mgrid[0:S, 0:S].astype(float)
    r = np.hypot(xs - 256, ys - 230) / 360
    canvas = to_img(hexc(0xC01830)[None, None] * (1.1 - r[..., None]) + hexc(0x300008)[None, None] * r[..., None]).convert('RGBA')
    sp = layer()
    d = ImageDraw.Draw(sp)
    rng = np.random.default_rng(12)
    for _ in range(40):
        x, y, s = rng.uniform(0, S), rng.uniform(0, S), rng.uniform(3, 9)
        d.polygon([(x, y - s), (x + s * .25, y - s * .25), (x + s, y), (x + s * .25, y + s * .25), (x, y + s), (x - s * .25, y + s * .25), (x - s, y), (x - s * .25, y - s * .25)],
                  fill=(255, 240, 200, 200))
    canvas.alpha_composite(glow(sp, 3, .8))
    med = layer()
    d = ImageDraw.Draw(med)
    d.ellipse([136, 70, 376, 310], fill=(220, 170, 40, 255), outline=(120, 80, 10, 255), width=6)
    d.ellipse([160, 94, 352, 286], fill=(250, 210, 80, 255), outline=(150, 100, 20, 255), width=3)
    for k in range(14):
        a = np.pi * .75 + k * np.pi * 1.5 / 13
        x, y = 256 + np.cos(a) * 84, 190 + np.sin(a) * 84
        d.ellipse([x - 9, y - 5, x + 9, y + 5], fill=(170, 120, 20, 255))
    d.multiline_text((256, 182), 'GAME\nOF THE\nYEAR', font=font(BARLOW, 34), anchor='mm', fill=(110, 60, 0, 255), align='center', spacing=0)
    d.text((256, 262), '1996', font=font(BARLOW, 22), anchor='mm', fill=(110, 60, 0, 255))
    canvas.alpha_composite(shadowed(med, (6, 8), 6, .5))
    canvas.alpha_composite(starburst(420, 330, 64, 16, (255, 240, 40, 255), (40, 40, 40, 255), 'MAZE\nMAYHEM!\n9/10', font(BARLOW, 22), text_fill=(30, 30, 30, 255)))
    q = layer()
    d = ImageDraw.Draw(q)
    quotes = [('"I got lost and loved it."', 'PC Wanderer'), ('"Snail of the year."', 'CD-ROM Monthly'), ('"Turn left. Then keep turning left."', 'GameShock')]
    for i, (txt, who) in enumerate(quotes):
        y = 352 + i * 50
        for k in range(5):
            d.regular_polygon((47 + k * 15, y + 7, 7), 5, rotation=18, fill=(255, 220, 40, 255))
        d.text((40, y + 16), txt, font=font('LibreBaskerville-Bold.ttf', 15), fill=(255, 255, 255, 255))
        d.text((60, y + 34), '- ' + who, font=font('Carlito-Regular.ttf', 12), fill=(255, 200, 200, 255))
    canvas.alpha_composite(q)
    canvas.alpha_composite(chrome_text('MAZE 95', font(BARLOW, 50), (256, 40), outline=4, depth=3))
    return finish(grain(arr(canvas), .012))


ALL = [box_art, arcade_flyer, snail_slime, collect_all, marble_game, render_box, binder, dark_floor, vista_os, reward_horror, award]
