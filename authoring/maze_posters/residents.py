"""Writes Maze 95's residents as sprites into games/maze/assets/residents: RGBA, square and a
power of two (256), the figure standing on the bottom edge and centred, as the game's sprites
expect. The doge, Apu and Monke are the owner's crayon drawings (residents_src), cut out;
Wojak, Chudjak and the Doomer are drawn to match them (jaks.py); the rest are rendered
(creatures.py). The two WANTED posters are here too;
make.py paints them at the end of the poster list."""
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

import creatures
import jaks
from kit import S, font, fbm3, hexc, to_img

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', '..', 'games', 'maze', 'assets', 'residents')


def fit(img, size=256, pad=4):
    """cropped to the figure, scaled to fit, standing on the bottom edge"""
    a = np.asarray(img)[..., 3]
    ys, xs = np.nonzero(a > 8)
    img = img.crop((xs.min(), ys.min(), xs.max() + 1, ys.max() + 1))
    img.thumbnail((size - 2 * pad, size - pad), Image.LANCZOS)
    out = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    out.alpha_composite(img, ((size - img.width) // 2, size - img.height))
    return out


def cut(name):
    return Image.open(os.path.join(HERE, 'residents', name + '.png')).convert('RGBA')


def frog_frames(n=4):
    frames = []
    for k in range(n):
        spin = k * 2 * np.pi / 16 / n  # one spoke's turn, over the frames
        img = creatures.portrait(lambda p, s=spin: creatures.farthing_scene(p, s), creatures.farthing_shade, (4.6, 2.2, 2.4), (0, 1.55, -.35), .78)
        frames.append(fit(img))
    return frames


def googly():
    img = Image.new('RGBA', (256, 128), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    for cx in (70, 186):
        d.ellipse([cx - 56, 8, cx + 56, 120], fill=(255, 255, 255, 255), outline=(0, 0, 0, 255), width=6)
        d.ellipse([cx - 22, 66, cx + 26, 114], fill=(10, 10, 10, 255))  # the pupils, slid to the bottom
        d.ellipse([cx - 40, 22, cx - 22, 40], fill=(255, 255, 255, 255))
    return img


def wanted(face, name, crime, reward):
    ys, xs = np.mgrid[0:S, 0:S].astype(float)
    paper = hexc(0xE8D6A8) * (1 - .14 * fbm3(np.stack([xs.ravel() / 50, ys.ravel() / 50, np.full(S * S, 3.0)], -1), 3).reshape(S, S)[..., None])
    paper *= (1 - .25 * np.clip(np.hypot(xs - 256, ys - 256) / 360, 0, 1) ** 2)[..., None]
    img = to_img(paper).convert('RGBA')
    d = ImageDraw.Draw(img)
    d.text((256, 56), 'WANTED', font=font('LibreBaskerville-Bold.ttf', 76), anchor='mm', fill=(60, 30, 10, 255))
    d.rectangle([110, 102, 402, 362], fill=(250, 246, 236, 255), outline=(60, 30, 10, 255), width=4)
    f = face.copy()
    f.thumbnail((286, 256), Image.LANCZOS)
    img.alpha_composite(f, (256 - f.width // 2, 360 - f.height))
    d.rectangle([110, 102, 402, 362], outline=(60, 30, 10, 255), width=4)
    d.text((256, 390), name, font=font('LibreBaskerville-Bold.ttf', 30), anchor='mm', fill=(60, 30, 10, 255))
    d.text((256, 426), crime, font=font('LibreBaskerville-Regular.ttf', 18), anchor='mm', fill=(60, 30, 10, 255))
    d.text((256, 468), reward, font=font('LibreBaskerville-Bold.ttf', 24), anchor='mm', fill=(120, 20, 10, 255))
    return img.resize((256, 256), Image.LANCZOS).convert('RGB')


def wanted_wojak():
    return wanted(jaks.wojak_crayon(), 'WOJAK', 'for feeling it, in the corridors', 'REWARD: ONE HUG')


def wanted_chudjak():
    return wanted(jaks.chudjak_crayon(), 'CHUDJAK', 'for shouting at walls', 'REWARD: 500 MAZES')


def main():
    os.makedirs(OUT, exist_ok=True)
    sprites = {
        'shiba': fit(cut('doge')), 'apu': fit(cut('apu')), 'monke': fit(cut('monke')),
        'doomer': fit(jaks.doomer_crayon()),
        'ceiling_cat': fit(creatures.ceiling_cat()), 'keyboard_cat': fit(creatures.keyboard_cat()), 'goose': fit(creatures.goose()),
        'raptor_think': fit(creatures.philosoraptor(0)), 'raptor_fold': fit(creatures.philosoraptor(1)),
        'thinker': fit(creatures.thinking_face()), 'hamster': fit(creatures.hamster()),
    }
    for k, f in enumerate(frog_frames()):
        sprites[f'frog_{k}'] = f
    sprites['googly'] = googly()
    for name, img in sprites.items():
        img.save(os.path.join(OUT, name + '.png'), optimize=True)
        print('resident', name, flush=True)


if __name__ == '__main__':
    main()
