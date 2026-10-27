"""The Nature Cube's environment picture, reduced offline with Plan Paint's CONV.

assets/nature-lake.png (1536 x 1024) becomes assets/nature-lake-environment.png (1024 x 512),
the size the cube's reflections sample, in linear light and never ringing (authoring/conv).
Packaging (prepare_portable_assets.py) turns it into nature-lake.gpix as it is.

    python3 tools/make_lake_environment.py
"""
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "authoring" / "conv"))
import conv  # noqa: E402


def main():
    with Image.open(ROOT / "assets" / "nature-lake.png") as original:
        reduced = conv.resize_srgb_image(original.convert("RGBA"), 1024, 512)
    reduced.save(ROOT / "assets" / "nature-lake-environment.png", optimize=True)
    print("assets/nature-lake-environment.png: 1024 x 512")


if __name__ == "__main__":
    main()
