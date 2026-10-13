#!/usr/bin/env python3
"""Rebuild the tanks' six material maps from the source photographs and swap them into every
ambient archive (riverscape, reef and pool), leaving every other byte as it was.

The maps are reduced with Plan Paint's CONV (material_maps.py). The source photographs are
Stillwater's own (`assets/materials` in the pinned Stillwater checkout); the archives keep them
in this order.

    python3 retexture.py SOURCE_MATERIALS_DIR
"""
import sys
from pathlib import Path

from ambient_archive import Texture, read_archive, write_archive
from material_maps import TEXTURE_SIZE, material_map

HERE = Path(__file__).resolve().parent
SCENES = HERE.parent / "assets/scene"
MAPS = [("sand_01_diff", False), ("sand_01_nor_gl", True), ("rock_boulder_dry_diff", False),
        ("rock_boulder_dry_nor_gl", True), ("rough_wood_diff", False), ("rough_wood_nor_gl", True)]


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    source = Path(sys.argv[1])
    maps = [Texture(TEXTURE_SIZE, TEXTURE_SIZE, normal, material_map(source / f"{name}.jpg", normal).tobytes())
            for name, normal in MAPS]
    for scene in ("riverscape", "reef", "pool"):
        path = SCENES / f"{scene}.ambient"
        archive = read_archive(path)
        if [(t.width, t.height, t.normal_map) for t in archive.textures] != [(TEXTURE_SIZE, TEXTURE_SIZE, n) for _, n in MAPS]:
            raise SystemExit(f"{path.name}: its material maps are not the six expected")
        archive.textures = list(maps)
        write_archive(archive, path)
        print(f"{path.name}: six material maps rebuilt")


if __name__ == "__main__":
    main()
