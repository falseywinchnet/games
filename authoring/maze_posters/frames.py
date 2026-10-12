"""Frames for the art pieces, so a picture reads as art and not as an ad, kept slim: a narrow
gallery mat in black metal, a thin black edge, gilt, chrome, wood, the translucent candy
plastic of 1998, a beige monitor bezel for screensavers, a printout taped to the wall, and
plain canvas."""
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

from kit import *


def _bevel_ring(outer, inner, light, dark, base):
    """a moulding between two rectangles, lit from the upper left"""
    lay = layer()
    d = ImageDraw.Draw(lay)
    (x0, y0, x1, y1), (i0, j0, i1, j1) = outer, inner
    d.polygon([(x0, y0), (x1, y0), (i1, j0), (i0, j0)], fill=light)
    d.polygon([(x0, y0), (i0, j0), (i0, j1), (x0, y1)], fill=light)
    d.polygon([(x1, y0), (x1, y1), (i1, j1), (i1, j0)], fill=dark)
    d.polygon([(x0, y1), (i0, j1), (i1, j1), (x1, y1)], fill=dark)
    d.rectangle([x0, y0, x1, y1], outline=base, width=2)
    return lay


def framed(art, style='gallery'):
    """`art` (an S x S image) set into a frame; returns the S x S whole"""
    wall = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    if style == 'gallery':  # thin black metal, a wide white mat
        out = Image.new('RGBA', (S, S), (20, 20, 22, 255))
        d = ImageDraw.Draw(out)
        d.rectangle([7, 7, S - 8, S - 8], fill=(246, 244, 238, 255))
        opening = (30, 30, S - 30, S - 30)
        d.rectangle([opening[0] - 2, opening[1] - 2, opening[2] + 2, opening[3] + 2], fill=(200, 196, 188, 255))
    elif style == 'gilt':
        out = Image.new('RGBA', (S, S), (0, 0, 0, 255))
        out.alpha_composite(_bevel_ring((0, 0, S, S), (24, 24, S - 24, S - 24), (240, 200, 90, 255), (120, 80, 20, 255), (90, 60, 10, 255)))
        d = ImageDraw.Draw(out)
        for k in range(0, S, 12):  # the beading
            for x, y in ((k, 12), (k, S - 12), (12, k), (S - 12, k)):
                d.ellipse([x - 3, y - 3, x + 3, y + 3], fill=(255, 225, 120, 255), outline=(140, 95, 20, 255))
        opening = (24, 24, S - 24, S - 24)
    elif style == 'chrome':
        out = Image.new('RGBA', (S, S), (0, 0, 0, 255))
        out.alpha_composite(_bevel_ring((0, 0, S, S), (14, 14, S - 14, S - 14), (240, 244, 250, 255), (90, 96, 110, 255), (60, 64, 70, 255)))
        opening = (14, 14, S - 14, S - 14)
    elif style == 'wood':
        ys, xs = np.mgrid[0:S, 0:S].astype(float)
        g = .5 + .5 * np.sin((xs + ys) * .05 + 6 * fbm3(np.stack([xs.ravel() / 30, ys.ravel() / 200, np.zeros(S * S)], -1), 3).reshape(S, S))
        out = to_img(hexc(0x6A4020)[None, None] * (.75 + .3 * g[..., None])).convert('RGBA')
        out.alpha_composite(_bevel_ring((0, 0, S, S), (18, 18, S - 18, S - 18), (180, 120, 70, 90), (30, 15, 5, 120), (40, 20, 5, 255)))
        opening = (18, 18, S - 18, S - 18)
    elif style == 'plastic':  # 1998: translucent candy plastic
        out = Image.new('RGBA', (S, S), (0, 0, 0, 0))
        d = ImageDraw.Draw(out)
        d.rounded_rectangle([0, 0, S - 1, S - 1], 26, fill=(40, 170, 200, 200), outline=(20, 110, 140, 255), width=3)
        d.rounded_rectangle([6, 4, S - 7, 16], 8, fill=(255, 255, 255, 80))
        opening = (18, 18, S - 18, S - 18)
    elif style == 'monitor':  # a beige CRT round a screensaver
        out = Image.new('RGBA', (S, S), (214, 206, 186, 255))
        d = ImageDraw.Draw(out)
        d.rectangle([0, 0, S - 1, S - 1], outline=(150, 140, 120, 255), width=4)
        d.rounded_rectangle([16, 14, S - 16, S - 46], 18, fill=(60, 60, 64, 255))
        d.ellipse([S - 46, S - 32, S - 34, S - 20], fill=(60, 200, 80, 255))
        for k in range(5):
            d.line([(40 + k * 10, S - 30), (40 + k * 10, S - 20)], fill=(160, 150, 130, 255), width=3)
        opening = (28, 24, S - 28, S - 56)
    elif style == 'taped':  # a printout, a little crooked, taped up
        out = Image.new('RGBA', (S, S), (0, 0, 0, 0))
        opening = (40, 40, S - 40, S - 40)
    elif style == 'thin':  # a narrow black edge, nothing more
        out = Image.new('RGBA', (S, S), (16, 16, 18, 255))
        opening = (6, 6, S - 6, S - 6)
    else:  # a canvas wrapped round its stretcher
        out = Image.new('RGBA', (S, S), (0, 0, 0, 255))
        opening = (6, 6, S - 6, S - 6)
    x0, y0, x1, y1 = opening
    inner = art.convert('RGBA').resize((x1 - x0, y1 - y0), Image.LANCZOS)
    if style == 'taped':
        sheet = Image.new('RGBA', (x1 - x0 + 24, y1 - y0 + 24), (252, 252, 250, 255))
        sheet.alpha_composite(inner, (12, 12))
        sheet = sheet.rotate(-2.5, expand=True, resample=Image.BICUBIC)
        back = Image.new('RGBA', (S, S), (0, 0, 0, 0))
        back.alpha_composite(sheet, ((S - sheet.width) // 2, (S - sheet.height) // 2))
        out = shadowed(back, (6, 6), 6, .45)
        d = ImageDraw.Draw(out)
        for cx, cy, a in ((70, 40, 30), (S - 70, 40, -30), (70, S - 40, -30), (S - 70, S - 40, 30)):
            tape = Image.new('RGBA', (70, 22), (240, 236, 200, 170))
            tape = tape.rotate(a, expand=True)
            out.alpha_composite(tape, (cx - tape.width // 2, cy - tape.height // 2))
        wall.alpha_composite(out)
        return wall
    out.alpha_composite(inner, (x0, y0))
    if style == 'canvas':  # the wrap shades off at the edges
        a = np.asarray(out, float)
        ys, xs = np.mgrid[0:S, 0:S]
        edge = np.minimum(np.minimum(xs, S - 1 - xs), np.minimum(ys, S - 1 - ys))
        a[..., :3] *= np.clip(.55 + edge / 30, 0, 1)[..., None]
        out = Image.fromarray(a.astype(np.uint8))
    if style == 'plastic':
        d = ImageDraw.Draw(out)
        d.rectangle([x0, y0, x1, y1], outline=(20, 110, 140, 255), width=3)
    # glass, faintly catching the light
    glare = layer()
    ImageDraw.Draw(glare).polygon([(x0, y0), (x0 + (x1 - x0) * .45, y0), (x0, y0 + (y1 - y0) * .45)], fill=(255, 255, 255, 22))
    out.alpha_composite(glare)
    wall.alpha_composite(out)
    return wall
