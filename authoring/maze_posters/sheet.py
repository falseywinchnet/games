"""A labelled contact sheet of the posters: python3 sheet.py OUT.png [DIR] [cols]."""
import glob
import os
import sys

from PIL import Image, ImageDraw, ImageFont

out = sys.argv[1]
src = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), 'out')
cols = int(sys.argv[3]) if len(sys.argv) > 3 else 6
files = sorted(glob.glob(os.path.join(src, '*.png')))
T, pad, lab = 256, 10, 24
rows = (len(files) + cols - 1) // cols
sheet = Image.new('RGB', (cols * (T + pad) + pad, rows * (T + lab + pad) + pad), (28, 28, 32))
d = ImageDraw.Draw(sheet)
f = ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc', 16)
for i, p in enumerate(files):
    x, y = pad + (i % cols) * (T + pad), pad + (i // cols) * (T + lab + pad)
    name = os.path.basename(p)[:-4].replace('_', ' ')
    d.text((x, y + 3), name, fill=(235, 235, 235), font=f)
    sheet.paste(Image.open(p), (x, y + lab))
sheet.save(out)
