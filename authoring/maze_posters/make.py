"""Paints the Maze 95 posters and art into games/maze/assets/posters: poster_00.png on (256 x 256
RGB) and art_00.png on (256 x 256 RGBA, framed; the alpha cuts out taped corners and round
plastic), each in the order below (a wall keeps a picture's number, so add new ones at the end):
python3 make.py [OUT_DIR] [name ...]. With --names, each file is named after its poster too (for review)."""
import os
import sys
import time

from PIL import Image

import ads
import eras
import gallery_a
import gallery_b
import residents
from frames import framed

POSTERS = ads.ALL + eras.ALL + [residents.wanted_wojak, residents.wanted_chudjak]
ART = gallery_a.ALL + gallery_b.ALL  # (painting, frame)


def main():
    args = [a for a in sys.argv[1:] if a != '--names']
    named = '--names' in sys.argv
    here = os.path.dirname(os.path.abspath(__file__))
    out = args[0] if args else os.path.join(here, '..', '..', 'games', 'maze', 'assets', 'posters')
    names = set(args[1:])
    os.makedirs(out, exist_ok=True)
    for i, make in enumerate(POSTERS):
        if names and make.__name__ not in names:
            continue
        t0 = time.time()
        name = f'{i:02d}_{make.__name__}.png' if named else f'poster_{i:02d}.png'
        make().convert('RGB').save(os.path.join(out, name), optimize=True)
        print(f'poster {i:02d} {make.__name__} {time.time() - t0:.1f}s', flush=True)
    for i, (make, frame) in enumerate(ART):
        if names and make.__name__ not in names:
            continue
        t0 = time.time()
        name = f'art_{i:02d}_{make.__name__}.png' if named else f'art_{i:02d}.png'
        framed(make(), frame).resize((256, 256), Image.LANCZOS).save(os.path.join(out, name), optimize=True)
        print(f'art {i:02d} {make.__name__} {time.time() - t0:.1f}s', flush=True)

if __name__ == '__main__':
    main()
