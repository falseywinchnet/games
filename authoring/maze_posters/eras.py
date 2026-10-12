"""Maze 95 posters, part two: the Windows years. 95's desktop and multimedia boxes, 98's web
pages and candy plastic, and the millennium's gel, brushed metal and dot-coms. Every product
and brand on them is made up."""
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

from kit import *
import props
from ads import FOOT, BARLOW, vgrad, stars


def star4(d, cx, cy, s):
    """Rainstar's own mark: a waving four-colour star"""
    cols = ((240, 72, 72), (72, 192, 96), (72, 144, 240), (240, 208, 64))
    for k in range(4):
        a = k * np.pi / 2 + .3
        d.polygon([(cx, cy), (cx + np.cos(a) * s, cy + np.sin(a) * s), (cx + np.cos(a + .8) * s * .45, cy + np.sin(a + .8) * s * .45)], fill=cols[k] + (255,))


def window(canvas, w, h, title, xy, gradient=False, rot=0):
    win = win95(w, h, title, gradient=gradient)
    if rot:
        win = win.rotate(rot, expand=True, resample=Image.BICUBIC)
    paste(canvas, win, xy, ((6, 6), 5, .45))


# ---------------------------------------------------------------- 95: press start
def press_start():
    canvas = clouds_sky(3)
    lay = layer()
    d = ImageDraw.Draw(lay)
    d.rectangle([60, 210, 452, 330], fill=(192, 192, 192, 255))
    for k in range(4):
        d.line([(60 + k, 210 + k), (452 - k, 210 + k)], fill=(255, 255, 255, 255))
        d.line([(60 + k, 210 + k), (60 + k, 330 - k)], fill=(255, 255, 255, 255))
        d.line([(452 - k, 210 + k), (452 - k, 330 - k)], fill=(64, 64, 64, 255))
        d.line([(60 + k, 330 - k), (452 - k, 330 - k)], fill=(64, 64, 64, 255))
    star4(d, 140, 270, 46)
    d.text((300, 270), 'Start', font=font('Carlito-Bold.ttf', 96), anchor='mm', fill=(0, 0, 0, 255))
    paste(canvas, lay, shadow=((10, 10), 6, .5))
    bar = layer()
    d = ImageDraw.Draw(bar)
    d.rectangle([0, 476, S, S], fill=(192, 192, 192, 255))
    d.line([(0, 477), (S, 477)], fill=(255, 255, 255, 255), width=2)
    button95(d, (4, 482, 86, 506), 'Start', font('Carlito-Bold.ttf', 16))
    d.rectangle([420, 482, 506, 506], fill=(192, 192, 192, 255), outline=(128, 128, 128, 255))
    d.text((463, 494), '9:51 AM', font=font('Carlito-Regular.ttf', 15), anchor='mm', fill=(0, 0, 0, 255))
    canvas.alpha_composite(bar)
    canvas.alpha_composite(outlined('Rainstar 95', font('Carlito-Bold.ttf', 64), (256, 92), (255, 255, 255, 255), 4, (0, 0, 128, 255), shadow=(4, 4)))
    canvas.alpha_composite(outlined('Start here.', font('Carlito-Bold.ttf', 34), (256, 400), (255, 255, 255, 255), 3, (0, 0, 128, 255)))
    return finish(arr(canvas))


# ---------------------------------------------------------------- 95: the office party
def maze_day():
    canvas = Image.new('RGBA', (S, S), (252, 252, 246, 255))
    d = ImageDraw.Draw(canvas)
    for k in range(0, S, 32):  # a clip-art border of stars
        for x, y in ((k + 16, 12), (k + 16, 500), (12, k + 16), (500, k + 16)):
            d.regular_polygon((x, y, 9), 5, rotation=18, fill=(255, 200, 0, 255), outline=(0, 0, 0, 255))
    rng = np.random.default_rng(5)
    for x, y, c in ((70, 330, (240, 40, 40)), (112, 350, (40, 120, 240)), (400, 340, (40, 200, 60)), (444, 322, (250, 200, 0))):
        d.line([(x, y + 34), (x + rng.uniform(-10, 10), y + 110)], fill=(60, 60, 60, 255), width=1)
        d.ellipse([x - 24, y - 30, x + 24, y + 34], fill=c + (255,), outline=(0, 0, 0, 255), width=2)
        d.ellipse([x - 12, y - 18, x - 4, y - 6], fill=(255, 255, 255, 200))
    canvas.alpha_composite(wordart('MAZE DAY!', font('ComicNeue-Bold.ttf', 84), (256, 110), bend=34, outline=3, shadow=(5, 5)))
    t = layer()
    d = ImageDraw.Draw(t)
    f = font('ComicNeue-Bold.ttf', 20)
    for i, line in enumerate(('Friday at 3:00, Room B', 'Bring a dish!', 'Prizes for whoever gets', 'the most lost!')):
        d.text((256, 220 + i * 32), line, font=f, anchor='mm', fill=(20, 20, 140, 255))
    d.text((256, 360), 'Turn left at the copier.', font=font('ComicNeue-Regular.ttf', 18), anchor='mm', fill=(160, 0, 0, 255))
    d.text((256, 470), 'Please RSVP to Linda in Accounts (ext. 214)', font=font('ComicNeue-Regular.ttf', 14), anchor='mm', fill=(60, 60, 60, 255))
    canvas.alpha_composite(t)
    hat = layer()
    d = ImageDraw.Draw(hat)
    d.polygon([(220, 440), (256, 400), (292, 440)], fill=(200, 60, 200, 255), outline=(0, 0, 0, 255))
    for k in range(3):
        d.ellipse([236 + k * 12, 418 + (k % 2) * 6, 242 + k * 12, 424 + (k % 2) * 6], fill=(255, 240, 0, 255))
    d.ellipse([250, 392, 262, 404], fill=(255, 120, 0, 255))
    canvas.alpha_composite(hat)
    return finish(arr(canvas))


# ---------------------------------------------------------------- 95: the helper
def helper():
    canvas = teal_desktop()
    window(canvas, 360, 240, 'MAZE95.EXE', (90, 40))
    inner = layer()
    shot = Image.open(FOOT[0]).convert('RGB').resize((350, 210))
    inner.paste(shot, (95, 68))
    canvas.alpha_composite(inner)
    # the helper: a folded paper map with googly eyes (no relation to any paper clip)
    hp = layer()
    d = ImageDraw.Draw(hp)
    pts = [(300, 330), (350, 316), (400, 332), (450, 318), (452, 430), (402, 444), (352, 428), (302, 442)]
    d.polygon(pts, fill=(250, 240, 200, 255), outline=(80, 60, 30, 255))
    for x in (350, 400):
        d.line([(x, 318), (x + 2, 430)], fill=(170, 150, 110, 255), width=2)
    d.line([(310, 400), (340, 370), (370, 392), (420, 360), (440, 390)], fill=(220, 40, 40, 255), width=3)
    d.text((440, 400), 'X', font=font(BARLOW, 20), fill=(220, 40, 40, 255), anchor='mm')
    for x in (350, 395):
        d.ellipse([x - 18, 336, x + 18, 372], fill=(255, 255, 255, 255), outline=(0, 0, 0, 255), width=2)
        d.ellipse([x - 4, 348, x + 10, 362], fill=(0, 0, 0, 255))
    canvas.alpha_composite(shadowed(hp, (5, 5), 4, .4))
    bal = layer()
    d = ImageDraw.Draw(bal)
    d.rounded_rectangle([24, 296, 300, 470], 12, fill=(255, 255, 225, 255), outline=(0, 0, 0, 255), width=2)
    d.polygon([(290, 360), (330, 370), (290, 384)], fill=(255, 255, 225, 255), outline=(0, 0, 0, 255))
    d.line([(290, 361), (290, 383)], fill=(255, 255, 225, 255), width=3)
    d.multiline_text((38, 306), "It looks like you're lost\nin a maze.\nWould you like help?", font=font('Carlito-Bold.ttf', 18), fill=(0, 0, 0, 255), spacing=2)
    for i, opt in enumerate(('Get help finding the exit', 'Just keep turning left', "Don't show me this tip again")):
        y = 384 + i * 26
        d.ellipse([40, y, 54, y + 14], fill=(255, 255, 255, 255), outline=(0, 0, 0, 255))
        if i == 1:
            d.ellipse([44, y + 4, 50, y + 10], fill=(0, 0, 0, 255))
        d.text((62, y - 1), opt, font=font('Carlito-Regular.ttf', 15), fill=(0, 0, 160, 255))
    canvas.alpha_composite(shadowed(bal, (4, 4), 3, .35))
    return finish(arr(canvas))


# ---------------------------------------------------------------- 95: the back of the box
def box_back():
    canvas = vgrad(0x101060, 0x000018)
    stars(canvas, 80, 3)
    d = ImageDraw.Draw(canvas)
    feats = ['Endless 3D mazes!', 'Over 600 MB of corridors!', 'Full Motion Snail!', 'Stereo sound!', 'Three difficulty levels!']
    for i, ftxt in enumerate(feats):
        y = 34 + i * 30
        d.line([(26, y + 12), (32, y + 19), (42, y + 4)], fill=(80, 255, 80, 255), width=4)
        d.text((48, y), ftxt, font=font('Carlito-Bold.ttf', 20), fill=(255, 255, 255, 255))
    shots = layer()
    for i, k in enumerate((2, 4, 3)):
        frame_shot(shots, FOOT[k], (290, 30 + i * 62, 488, 82 + i * 62), (255, 220, 40, 255))
    canvas.alpha_composite(shots)
    box = layer()
    d = ImageDraw.Draw(box)
    d.rectangle([20, 230, 330, 420], fill=(255, 255, 255, 255), outline=(0, 0, 0, 255), width=2)
    d.rectangle([20, 230, 330, 256], fill=(0, 0, 0, 255))
    d.text((175, 243), 'SYSTEM REQUIREMENTS', font=font(BARLOW, 20), anchor='mm', fill=(255, 255, 255, 255))
    req = ['486DX/66 MHz or faster', '8 MB RAM (16 MB recommended)', 'Double-speed CD-ROM drive', 'SVGA, 256 colours', 'Sound card, speakers or headphones', 'Mouse', 'Rainstar 95']
    for i, r in enumerate(req):
        d.text((32, 264 + i * 22), '• ' + r, font=font('Carlito-Regular.ttf', 16), fill=(0, 0, 0, 255))
    canvas.alpha_composite(box)
    mpc = layer()
    d = ImageDraw.Draw(mpc)
    d.rounded_rectangle([352, 240, 492, 330], 8, fill=(255, 255, 255, 255), outline=(0, 0, 0, 255), width=2)
    d.multiline_text((422, 285), 'MULTIMEDIA\nMAZE', font=font(BARLOW, 24), anchor='mm', align='center', fill=(200, 0, 0, 255))
    d.rectangle([352, 350, 420, 420], fill=(255, 255, 255, 255), outline=(0, 0, 0, 255), width=2)
    d.text((386, 376), 'K-A', font=font(BARLOW, 30), anchor='mm', fill=(0, 0, 0, 255))
    d.text((386, 406), 'KIDS TO ADULTS', font=font('Carlito-Bold.ttf', 8), anchor='mm', fill=(0, 0, 0, 255))
    d.rectangle([432, 350, 492, 420], fill=(255, 255, 255, 255))
    rng = np.random.default_rng(1)
    x = 436
    while x < 488:
        w = int(rng.integers(1, 4))
        d.rectangle([x, 354, x + w - 1, 408], fill=(0, 0, 0, 255))
        x += w + int(rng.integers(1, 3))
    canvas.alpha_composite(mpc)
    canvas.alpha_composite(outlined('Rainstar Entertainment. Made for Rainstar 95.', font('Carlito-Regular.ttf', 13), (256, 470), (200, 200, 255, 255), 0, (0, 0, 0, 0)))
    canvas.alpha_composite(cd_badge(470, 470, 22))
    return finish(arr(canvas))


# ---------------------------------------------------------------- 98: the free trial disc
def free_mazes():
    canvas = vgrad(0x0A2A9A, 0x0A6AE0)
    ys, xs = np.mgrid[0:S, 0:S].astype(float)
    cx, cy, R = 256, 300, 170
    r = np.hypot(xs - cx, ys - cy)
    ang = np.arctan2(ys - cy, xs - cx)
    hue = (ang / (2 * np.pi) * 3 + r / 90) % 1
    sheen = np.stack([.75 + .25 * np.cos(2 * np.pi * (hue + k / 3)) for k in range(3)], -1)
    silver = .78 + .2 * np.cos(ang * 2)
    disc = sheen * .5 + silver[..., None] * .5
    a = ((r < R) & (r > 26)).astype(float)
    disc_l = to_img(disc, a)
    canvas.alpha_composite(shadowed(disc_l, (10, 12), 8, .5))
    lab = layer()
    d = ImageDraw.Draw(lab)
    d.pieslice([cx - R + 30, cy - R + 30, cx + R - 30, cy + R - 30], 20, 160, fill=(255, 255, 255, 230))
    d.text((cx, cy + 92), 'RAINSTAR ONLINE', font=font(BARLOW, 24), anchor='mm', fill=(0, 40, 160, 255))
    canvas.alpha_composite(lab)
    canvas.alpha_composite(chrome_text('500 FREE', font(BARLOW, 92), (256, 66), outline=5, depth=4, sky=0xC08000, ground=0x5A2A00, horizon=0xFFF080, extrude=(120, 40, 0)))
    canvas.alpha_composite(chrome_text('MAZES!', font(BARLOW, 92), (256, 150), outline=5, depth=4, sky=0xC08000, ground=0x5A2A00, horizon=0xFFF080, extrude=(120, 40, 0)))
    canvas.alpha_composite(starburst(440, 250, 56, 14, (255, 230, 40, 255), (220, 20, 40, 255), 'INSERT\nDISC\nNOW!', font(BARLOW, 20)))
    canvas.alpha_composite(outlined('No credit card needed!*', font('Carlito-Bold.ttf', 22), (256, 484), (255, 255, 255, 255), 2, (0, 0, 80, 255)))
    canvas.alpha_composite(outlined('*first 500 mazes only. 56K modem recommended.', font('Carlito-Regular.ttf', 11), (256, 504), (200, 220, 255, 255), 0, (0, 0, 0, 0)))
    return finish(arr(canvas))


# ---------------------------------------------------------------- 98: the homepage
def homepage():
    canvas = Image.new('RGBA', (S, S), (0, 0, 0, 255))
    window(canvas, S, S, 'Welcome to my MAZE page!! - Rainstar Navigator', (0, 0), gradient=True)
    page = layer()
    d = ImageDraw.Draw(page)
    d.rectangle([4, 28, 507, 50], fill=(192, 192, 192, 255))
    d.text((8, 31), 'Address:', font=font('Carlito-Regular.ttf', 14), fill=(0, 0, 0, 255))
    d.rectangle([62, 30, 500, 48], fill=(255, 255, 255, 255), outline=(128, 128, 128, 255))
    d.text((66, 31), 'http://members.rainstar.net/~mazefan/', font=font('Carlito-Regular.ttf', 14), fill=(0, 0, 0, 255))
    for y in range(52, 508, 24):  # the tiled starry background
        for x in range(4, 508, 24):
            d.rectangle([x, y, x + 23, y + 23], fill=(0, 0, 40, 255))
            d.point([(x + 5, y + 7), (x + 17, y + 15), (x + 11, y + 20)], fill=(255, 255, 160, 255))
    page.alpha_composite(outlined('Welcome to my', font('LibreBaskerville-Bold.ttf', 28), (256, 80), (255, 255, 0, 255), 0, (0, 0, 0, 0)))
    page.alpha_composite(outlined('MAZE page!!', font('LibreBaskerville-Bold.ttf', 40), (256, 120), (255, 60, 60, 255), 0, (0, 0, 0, 0)))
    d = ImageDraw.Draw(page)
    for x in range(20, 492, 12):  # the flame divider
        h = 10 + 6 * np.sin(x * .7)
        d.polygon([(x, 170), (x + 6, 170 - h), (x + 12, 170)], fill=(255, 120, 0, 255))
        d.polygon([(x + 3, 170), (x + 6, 170 - h * .55), (x + 9, 170)], fill=(255, 240, 0, 255))
    page.alpha_composite(stripes((60, 182, 452, 222)))
    d = ImageDraw.Draw(page)
    d.rectangle([150, 190, 362, 214], fill=(0, 0, 0, 255))
    d.text((256, 202), 'UNDER CONSTRUCTION', font=font(BARLOW, 20), anchor='mm', fill=(255, 220, 0, 255))
    fl = font('LibreBaskerville-Regular.ttf', 15)
    lines = ['This page is all about MAZES. I have been', 'lost in Maze 95 since 1996 and I LOVE IT!!!', 'Here are my favourite walls:']
    for i, ln in enumerate(lines):
        d.text((256, 244 + i * 20), ln, font=fl, anchor='mm', fill=(255, 255, 255, 255))
    for i, k in enumerate((1, 3, 5)):
        frame_shot(page, FOOT[k], (40 + i * 152, 312, 168 + i * 152, 386), (192, 192, 192, 255))
    d = ImageDraw.Draw(page)
    d.text((256, 410), 'You are visitor number', font=fl, anchor='mm', fill=(255, 255, 255, 255))
    for i, ch in enumerate('000137'):
        d.rectangle([196 + i * 20, 424, 214 + i * 20, 450], fill=(0, 0, 0, 255), outline=(120, 120, 120, 255))
        d.text((205 + i * 20, 437), ch, font=font('Cousine-Bold.ttf', 20), anchor='mm', fill=(80, 255, 80, 255))
    d.text((256, 470), 'Sign my guestbook!   <<  Maze Ring  >>', font=font('LibreBaskerville-Bold.ttf', 14), anchor='mm', fill=(80, 160, 255, 255))
    d.line([(150, 479), (238, 479)], fill=(80, 160, 255, 255))
    d.text((256, 494), 'Best viewed at 800x600', font=font('Carlito-Regular.ttf', 12), anchor='mm', fill=(200, 200, 200, 255))
    env = layer()
    de = ImageDraw.Draw(env)
    de.rectangle([440, 80, 488, 112], fill=(255, 255, 255, 255), outline=(0, 0, 0, 255))
    de.line([(440, 80), (464, 98), (488, 80)], fill=(0, 0, 0, 255), width=2)
    page.alpha_composite(env)
    canvas.alpha_composite(page)
    return finish(arr(canvas))


# ---------------------------------------------------------------- 98: the 3D card
def turbo3d():
    canvas = vgrad(0x100000, 0x000000)
    fire = layer()
    d = ImageDraw.Draw(fire)
    rng = np.random.default_rng(8)
    for _ in range(90):
        x = rng.uniform(0, S)
        h = rng.uniform(80, 260)
        w = rng.uniform(10, 30)
        c = (255, int(rng.uniform(60, 220)), 0, 160)
        d.polygon([(x - w, S), (x + rng.uniform(-20, 20), S - h), (x + w, S)], fill=c)
    canvas.alpha_composite(fire.filter(ImageFilter.GaussianBlur(4)))
    card = layer()
    d = ImageDraw.Draw(card)
    d.polygon([(80, 240), (420, 200), (450, 330), (110, 380)], fill=(20, 110, 50, 255), outline=(200, 200, 200, 255))
    for k in range(9):
        d.line([(100 + k * 36, 236 - k * 4), (126 + k * 36, 372 - k * 6)], fill=(200, 170, 60, 255), width=1)
    d.polygon([(170, 260), (250, 250), (260, 320), (180, 330)], fill=(30, 30, 30, 255))
    d.ellipse([290, 240, 380, 320], fill=(60, 60, 70, 255), outline=(180, 180, 190, 255), width=3)
    for k in range(7):
        a = k * 2 * np.pi / 7
        d.line([(335, 280), (335 + np.cos(a) * 40, 280 + np.sin(a) * 40)], fill=(150, 150, 160, 255), width=6)
    d.polygon([(110, 380), (450, 330), (452, 344), (112, 394)], fill=(220, 180, 40, 255))
    canvas.alpha_composite(shadowed(card, (10, 10), 8, .6))
    bolt = layer()
    lightning(bolt, (40, 120), (200, 250), (180, 220, 255), seed=4)
    lightning(bolt, (480, 120), (380, 220), (180, 220, 255), seed=5)
    canvas.alpha_composite(glow(bolt, 8, 1.2))
    canvas.alpha_composite(chrome_text('TURBO 3D', font(BARLOW, 110), (256, 76), angle=4, sky=0x8A1010, ground=0x200000, horizon=0xFFC060, extrude=(90, 0, 0)))
    canvas.alpha_composite(outlined('SEE EVERY WALL IN 16 MILLION COLOURS!', font(BARLOW, 22), (256, 150), (255, 230, 60, 255), 2, (0, 0, 0, 255)))
    canvas.alpha_composite(starburst(430, 430, 56, 14, (255, 230, 40, 255), (200, 0, 0, 255), '8 MB\nOF\nRAM!', font(BARLOW, 22)))
    canvas.alpha_composite(outlined('3D ACCELERATED', font(BARLOW, 30), (180, 460), (255, 255, 255, 255), 3, (160, 0, 0, 255), angle=3))
    return finish(grain(arr(canvas), .015))


# ---------------------------------------------------------------- 98: Y2K
def y2k():
    ys, xs = np.mgrid[0:S, 0:S].astype(float)
    paper = hexc(0xF4E8C8) * (1 - .12 * fbm3(np.stack([xs.ravel() / 40, ys.ravel() / 40, np.zeros(S * S)], -1), 3).reshape(S, S)[..., None])
    canvas = to_img(paper).convert('RGBA')
    d = ImageDraw.Draw(canvas)
    for k, w in ((14, 6), (26, 2)):
        d.rectangle([k, k, S - k, S - k], outline=(120, 80, 20, 255), width=w)
    for x, y in ((30, 30), (482, 30), (30, 482), (482, 482)):
        d.regular_polygon((x, y, 14), 4, rotation=45, fill=(160, 110, 30, 255))
    d.text((256, 76), 'Certificate', font=font('LibreBaskerville-Bold.ttf', 46), anchor='mm', fill=(80, 40, 10, 255))
    d.text((256, 120), 'of Y2K Compliance', font=font('LibreBaskerville-Regular.ttf', 26), anchor='mm', fill=(80, 40, 10, 255))
    body = 'This certifies that\nMAZE 95\nwill continue to be a maze\nafter December 31, 1999.'
    d.multiline_text((256, 230), body, font=font('LibreBaskerville-Regular.ttf', 20), anchor='mm', align='center', fill=(40, 20, 0, 255), spacing=8)
    d.line([(60, 400), (220, 400)], fill=(40, 20, 0, 255), width=1)
    d.line([(292, 400), (452, 400)], fill=(40, 20, 0, 255), width=1)
    d.text((140, 414), 'Chief Maze Officer', font=font('Carlito-Regular.ttf', 13), anchor='mm', fill=(40, 20, 0, 255))
    d.text((372, 414), 'Date: 11/12/99', font=font('Carlito-Regular.ttf', 13), anchor='mm', fill=(40, 20, 0, 255))
    d.line([(70, 392), (100, 380), (120, 394), (150, 376), (180, 392), (210, 384)], fill=(20, 20, 120, 255), width=2)  # a signature
    canvas.alpha_composite(sticker(256, 450, 48, None, 'Y2K\nREADY', font(BARLOW, 22), col=(200, 30, 30), holo=True))
    seal = layer()
    sd_ = ImageDraw.Draw(seal)
    sd_.regular_polygon((430, 340, 40), 16, fill=(190, 20, 30, 255))
    sd_.ellipse([400, 310, 460, 370], fill=(220, 40, 50, 255), outline=(150, 0, 10, 255), width=2)
    sd_.text((430, 340), 'Y2K', font=font(BARLOW, 20), anchor='mm', fill=(255, 220, 120, 255))
    canvas.alpha_composite(shadowed(seal, (3, 3), 2, .4))
    return finish(arr(canvas))


# ---------------------------------------------------------------- 98: candy plastic
def candy():
    canvas = Image.new('RGBA', (S, S), (250, 250, 252, 255))
    flavours = [((60, 150, 220), 'Blueberry'), ((240, 80, 110), 'Snailberry'), ((140, 220, 60), 'Lime'), ((150, 80, 200), 'Grape'), ((250, 150, 40), 'Tangerine')]
    for i, (c, name) in enumerate(flavours):
        x = 52 + i * 102 - (i % 2) * 0
        y = 220 + (i % 2) * 40
        m = layer()
        d = ImageDraw.Draw(m)
        d.rounded_rectangle([x - 44, y - 50, x + 44, y + 46], 26, fill=c + (150,), outline=c + (255,), width=2)
        d.rounded_rectangle([x - 32, y - 38, x + 32, y + 10], 6, fill=(20, 20, 30, 255))
        shot = Image.open(FOOT[i % 6]).convert('RGB').resize((60, 44))
        m.paste(shot, (x - 30, y - 36))
        for k in range(4):
            d.line([(x - 30 + k * 18, y + 18), (x - 20 + k * 18, y + 38)], fill=tuple(int(v * .7) for v in c) + (120,), width=2)
        d.ellipse([x - 30, y - 46, x - 8, y - 40], fill=(255, 255, 255, 160))
        d.polygon([(x - 20, y + 46), (x + 20, y + 46), (x + 28, y + 70), (x - 28, y + 70)], fill=c + (120,))
        canvas.alpha_composite(shadowed(m, (4, 8), 6, .25))
        canvas.alpha_composite(outlined(name, font('Carlito-Regular.ttf', 15), (x, y + 88), (90, 90, 100, 255), 0, (0, 0, 0, 0)))
    canvas.alpha_composite(outlined('Get lost in colour.', font('Carlito-Bold.ttf', 40), (256, 76), (40, 40, 50, 255), 0, (0, 0, 0, 0)))
    canvas.alpha_composite(outlined('The MazeBox. Now in five flavours.', font('Carlito-Regular.ttf', 22), (256, 118), (110, 110, 120, 255), 0, (0, 0, 0, 0)))
    d = ImageDraw.Draw(canvas)
    star4(d, 256, 470, 20)
    return finish(arr(canvas))


# ---------------------------------------------------------------- 2000: the millennium edition
def maze2000():
    canvas = brushed_metal((0, 0, S, S), 0xA8B0BC, circular=(256, 300))
    circ = layer()
    d = ImageDraw.Draw(circ)
    rng = np.random.default_rng(2)
    for _ in range(26):  # the techno circuit
        x, y = rng.uniform(0, S), rng.uniform(260, S)
        pts = [(x, y)]
        for _ in range(4):
            if rng.random() < .5:
                x += rng.uniform(-60, 60)
            else:
                y += rng.uniform(-40, 40)
            pts.append((x, y))
        d.line(pts, fill=(40, 120, 255, 160), width=2)
        d.ellipse([x - 3, y - 3, x + 3, y + 3], fill=(120, 200, 255, 220))
    canvas.alpha_composite(glow(circ, 4, .8))
    sw = layer()
    d = ImageDraw.Draw(sw)
    for k in range(3):
        d.arc([-200, 180 + k * 20, 700, 600 + k * 20], 200, 340, fill=(30, 90, 255, 160 - k * 40), width=10 - k * 2)
    canvas.alpha_composite(sw.filter(ImageFilter.GaussianBlur(2)))
    canvas.alpha_composite(chrome_text('MAZE', font(BARLOW, 120), (256, 90), sky=0x1040C0, extrude=(10, 30, 90)))
    canvas.alpha_composite(chrome_text('2000', font(BARLOW, 120), (256, 200), sky=0x1040C0, extrude=(10, 30, 90)))
    canvas.alpha_composite(gel((96, 268, 416, 314), 0x1A5AE0, text='MILLENNIUM EDITION', fnt=font(BARLOW, 28)))
    for k, (x, y, c) in enumerate(((80, 420, 0xE04040), (180, 450, 0x40C040), (330, 450, 0xF0B020), (430, 420, 0x6040E0))):
        orb = gel((x - 30, y - 30, x + 30, y + 30), c, radius=30)
        canvas.alpha_composite(orb)
    canvas.alpha_composite(lens_flare(420, 60, .7))
    canvas.alpha_composite(outlined('Y2K READY', font(BARLOW, 22), (256, 496), (20, 40, 120, 255), 0, (0, 0, 0, 0)))
    return finish(arr(canvas))


# ---------------------------------------------------------------- 2000: the dot-com
def emaze():
    canvas = Image.new('RGBA', (S, S), (255, 255, 255, 255))
    sw = layer()
    d = ImageDraw.Draw(sw)
    d.ellipse([-300, 360, 820, 760], fill=(0, 70, 170, 255))
    d.ellipse([-300, 380, 820, 780], fill=(255, 255, 255, 255))
    d.ellipse([-300, 400, 820, 800], fill=(255, 140, 0, 255))
    d.ellipse([-300, 420, 820, 820], fill=(0, 70, 170, 255))
    canvas.alpha_composite(sw)
    gl = layer()
    d = ImageDraw.Draw(gl)
    d.ellipse([330, 150, 470, 290], fill=(60, 140, 230, 255), outline=(0, 60, 160, 255), width=3)
    for k in range(-2, 3):
        d.arc([330 + abs(k) * 18, 150, 470 - abs(k) * 18, 290], 0, 360, fill=(200, 230, 255, 255), width=1)
        d.line([(332, 220 + k * 26), (468, 220 + k * 26)], fill=(200, 230, 255, 255), width=1)
    d.arc([300, 190, 500, 250], 190, 520, fill=(255, 140, 0, 255), width=4)
    canvas.alpha_composite(shadowed(gl, (4, 6), 6, .3))
    team = layer()  # the team, pointing at the screen
    d = ImageDraw.Draw(team)
    for i, (x, c) in enumerate(((70, (40, 40, 70)), (130, (90, 30, 30)), (190, (30, 60, 40)))):
        d.ellipse([x - 16, 220, x + 16, 252], fill=(230, 190, 150, 255))
        d.rounded_rectangle([x - 26, 254, x + 26, 340], 12, fill=c + (255,))
        d.line([(x + 20, 270), (x + 60, 236 - i * 8)], fill=c + (255,), width=9)
        d.polygon([(x - 6, 254), (x + 6, 254), (x, 290)], fill=(240, 240, 240, 255))
    canvas.alpha_composite(team)
    d = ImageDraw.Draw(canvas)
    d.text((40, 60), 'e', font=font('Carlito-Bold.ttf', 96), anchor='lm', fill=(255, 140, 0, 255))
    d.text((92, 66), 'Maze', font=font('Carlito-Bold.ttf', 80), anchor='lm', fill=(0, 70, 170, 255))
    d.text((290, 70), '.com', font=font('Carlito-Regular.ttf', 52), anchor='lm', fill=(120, 120, 130, 255))
    d.multiline_text((40, 124), 'B2B wayfinding solutions\nfor the new economy.', font=font('Carlito-Regular.ttf', 22), fill=(60, 60, 70, 255), spacing=4)
    canvas.alpha_composite(gel((60, 360, 230, 398), 0xFF8A00, text='Click here!', fnt=font('Carlito-Bold.ttf', 20)))
    canvas.alpha_composite(outlined('Powered by e-synergy', font('Carlito-Regular.ttf', 14), (400, 380), (100, 100, 110, 255), 0, (0, 0, 0, 0)))
    return finish(arr(canvas))


# ---------------------------------------------------------------- 2000: System Restore
def restore():
    canvas = vgrad(0x3A6EA5, 0x0A246A)
    win = win95(470, 440, 'System Restore', gradient=True)
    d = ImageDraw.Draw(win)
    d.rectangle([4, 26, 465, 96], fill=(255, 255, 255))
    for x in range(4, 466):
        t = (x - 4) / 462
        d.line([(x, 26), (x, 96)], fill=(int(255 - 120 * t), int(255 - 60 * t), 255))
    d.text((20, 40), 'Restore your maze to an earlier time', font=font('Carlito-Bold.ttf', 20), fill=(0, 0, 80))
    d.text((20, 66), 'Return to the start of the maze?', font=font('Carlito-Regular.ttf', 16), fill=(0, 0, 80))
    d.rectangle([20, 116, 230, 300], fill=(255, 255, 255), outline=(128, 128, 128))
    d.rectangle([20, 116, 230, 136], fill=(10, 36, 106))
    d.text((125, 126), 'October 1996', font=font('Carlito-Bold.ttf', 14), anchor='mm', fill=(255, 255, 255))
    for i, day in enumerate('SMTWTFS'):
        d.text((36 + i * 29, 146), day, font=font('Carlito-Bold.ttf', 12), anchor='mm', fill=(0, 0, 0))
    for k in range(31):
        c, r_ = (k + 2) % 7, (k + 2) // 7
        bold = k + 1 in (3, 9, 12, 21)
        x, y = 36 + c * 29, 168 + r_ * 24
        if k + 1 == 12:
            d.rectangle([x - 12, y - 10, x + 12, y + 10], fill=(10, 36, 106))
        d.text((x, y), str(k + 1), font=font('Carlito-Bold.ttf' if bold else 'Carlito-Regular.ttf', 13), anchor='mm', fill=(255, 255, 255) if k + 1 == 12 else (0, 0, 0))
    d.rectangle([248, 116, 450, 300], fill=(255, 255, 255), outline=(128, 128, 128))
    pts = ['9:41 AM  Entered the maze', '9:44 AM  Turned left', '9:45 AM  Turned left', '9:46 AM  Met a snail', '9:52 AM  Lost']
    for i, p in enumerate(pts):
        if i == 0:
            d.rectangle([250, 122 + i * 22, 448, 140 + i * 22], fill=(10, 36, 106))
        d.text((256, 124 + i * 22), p, font=font('Carlito-Regular.ttf', 13), fill=(255, 255, 255) if i == 0 else (0, 0, 0))
    for i, opt in enumerate(('Restore my maze to an earlier time', 'Stay lost')):
        y = 322 + i * 26
        d.ellipse([22, y, 36, y + 14], fill=(255, 255, 255), outline=(0, 0, 0))
        if i == 0:
            d.ellipse([26, y + 4, 32, y + 10], fill=(0, 0, 0))
        d.text((44, y - 1), opt, font=font('Carlito-Regular.ttf', 15), fill=(0, 0, 0))
    for i, b in enumerate(('< Back', 'Next >', 'Cancel')):
        button95(d, (200 + i * 88, 396, 280 + i * 88, 422), b, font('Carlito-Regular.ttf', 14))
    paste(canvas, win, (20, 30), ((8, 8), 6, .5))
    return finish(arr(canvas))


# ---------------------------------------------------------------- 2000: the snail-shaped player skin
def skin():
    canvas = vgrad(0x101828, 0x000000)
    lay = layer()
    d = ImageDraw.Draw(lay)
    # the foot: the control bar
    d.rounded_rectangle([40, 330, 470, 420], 40, fill=(150, 210, 90, 255), outline=(40, 80, 20, 255), width=4)
    d.rounded_rectangle([60, 344, 300, 372], 6, fill=(10, 20, 10, 255))
    d.text((66, 350), '*** MAZE 95 - Snail Slime (Remix) *** 128kbps', font=font('Cousine-Regular.ttf', 11), fill=(80, 255, 80, 255))
    for i, sym in enumerate(('|<', '>', '[]', '>|')):
        x = 80 + i * 52
        d.ellipse([x - 18, 378, x + 18, 414], fill=(90, 160, 50, 255), outline=(30, 60, 10, 255), width=2)
        d.text((x, 396), sym, font=font('Cousine-Bold.ttf', 16), anchor='mm', fill=(255, 255, 255, 255))
    for k in range(6):
        x = 320 + k * 22
        d.line([(x, 346), (x, 410)], fill=(30, 60, 10, 255), width=3)
        d.rectangle([x - 6, 360 + (k * 13) % 40, x + 6, 368 + (k * 13) % 40], fill=(230, 230, 230, 255))
    # the shell: a round visualiser
    d.ellipse([150, 70, 410, 330], fill=(240, 100, 180, 255), outline=(110, 30, 90, 255), width=5)
    d.ellipse([174, 94, 386, 306], fill=(20, 10, 30, 255))
    for k in range(48):
        a = k * 2 * np.pi / 48
        h = 30 + 50 * abs(np.sin(k * .7) * np.cos(k * .23))
        r0 = 30 + k * .9
        c = tuple(int(127 + 127 * np.cos(a + j * 2.1)) for j in range(3)) + (255,)
        d.line([(280 + np.cos(a) * r0, 200 + np.sin(a) * r0), (280 + np.cos(a) * (r0 + h * .6), 200 + np.sin(a) * (r0 + h * .6))], fill=c, width=4)
    # eye stalks: the volume and balance knobs
    for x, lbl in ((420, 'VOL'), (466, 'BAL')):
        d.line([(x - 20, 330), (x, 250)], fill=(150, 210, 90, 255), width=8)
        d.ellipse([x - 20, 226, x + 20, 266], fill=(255, 255, 255, 255), outline=(0, 0, 0, 255), width=3)
        d.ellipse([x - 6, 238, x + 8, 252], fill=(0, 0, 0, 255))
        d.text((x, 280), lbl, font=font('Carlito-Bold.ttf', 11), anchor='mm', fill=(255, 255, 255, 255))
    canvas.alpha_composite(shadowed(glow(lay, 6, .4), (8, 8), 6, .5))
    canvas.alpha_composite(outlined('SnailAmp skin v2.1 - download free!', font('Carlito-Bold.ttf', 18), (256, 462), (255, 255, 255, 255), 0, (0, 0, 0, 0)))
    return finish(arr(canvas))


# ---------------------------------------------------------------- 2000: the desktop buddy
def buddy():
    canvas = vgrad(0x5A7EC8, 0x2A4A90)
    bd = layer()
    d = ImageDraw.Draw(bd)
    d.ellipse([110, 200, 290, 420], fill=(130, 60, 200, 255), outline=(60, 20, 110, 255), width=4)
    d.ellipse([140, 230, 200, 280], fill=(190, 140, 255, 160))
    for x in (165, 235):
        d.ellipse([x - 26, 250, x + 26, 302], fill=(255, 255, 255, 255), outline=(40, 0, 80, 255), width=3)
        d.ellipse([x - 8, 266, x + 14, 290], fill=(20, 0, 40, 255))
        d.ellipse([x - 4, 268, x + 4, 276], fill=(255, 255, 255, 255))
    d.arc([160, 300, 240, 360], 20, 160, fill=(40, 0, 80, 255), width=5)
    d.ellipse([92, 300, 126, 340], fill=(130, 60, 200, 255), outline=(60, 20, 110, 255), width=3)
    d.ellipse([276, 280, 310, 320], fill=(130, 60, 200, 255), outline=(60, 20, 110, 255), width=3)
    canvas.alpha_composite(shadowed(bd, (8, 8), 6, .4))
    bal = layer()
    d = ImageDraw.Draw(bal)
    d.rounded_rectangle([250, 60, 492, 230], 16, fill=(255, 255, 230, 255), outline=(0, 0, 0, 255), width=2)
    d.polygon([(300, 226), (250, 270), (330, 228)], fill=(255, 255, 230, 255), outline=(0, 0, 0, 255))
    d.line([(302, 227), (328, 227)], fill=(255, 255, 230, 255), width=3)
    d.multiline_text((262, 74), "Hi! I'm Mazey! :)\nWant me to help you\nbrowse the maze?\nI'll be your best friend!", font=font('ComicNeue-Bold.ttf', 18), fill=(40, 0, 80, 255), spacing=4)
    canvas.alpha_composite(shadowed(bal, (4, 4), 3, .35))
    canvas.alpha_composite(gel((290, 260, 380, 296), 0x30B030, text='Yes!', fnt=font('Carlito-Bold.ttf', 18)))
    canvas.alpha_composite(gel((390, 260, 480, 296), 0x30B030, text='Sure!', fnt=font('Carlito-Bold.ttf', 18)))
    canvas.alpha_composite(starburst(400, 400, 60, 14, (255, 240, 40, 255), (220, 20, 40, 255), 'FREE\nDOWN-\nLOAD!', font(BARLOW, 20)))
    canvas.alpha_composite(outlined('Mazey: your desktop pal', font('Carlito-Bold.ttf', 22), (180, 470), (255, 255, 255, 255), 2, (40, 0, 80, 255)))
    return finish(arr(canvas))


ALL = [press_start, maze_day, helper, box_back, free_mazes, homepage, turbo3d, y2k, candy, maze2000, emaze, restore, skin, buddy]
