"""Renders art pieces in their frames for review: python3 preview_art.py MODULE OUT_DIR [name ...]"""
import importlib
import os
import sys
import time

from frames import framed
from kit import resample

mod = importlib.import_module(sys.argv[1])
out = sys.argv[2]
names = set(sys.argv[3:])
os.makedirs(out, exist_ok=True)
for i, (make, style) in enumerate(mod.ALL):
    if names and make.__name__ not in names:
        continue
    t0 = time.time()
    resample(framed(make(), style), 256, 256).save(os.path.join(out, f'{sys.argv[1]}_{i:02d}_{make.__name__}.png'))
    print(f'{i:02d} {make.__name__} {time.time() - t0:.1f}s', flush=True)
