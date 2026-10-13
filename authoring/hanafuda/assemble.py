"""Assemble reviewed generated panels inside the deck's labeled frames.

    python3 authoring/hanafuda/assemble.py [--frames DIR]

The frames are the delivered deck's (incoming/game-koikoi-01, read-only) unless --frames
names a folder made by games/koikoi/tools/make_hanafuda.py. Panels are fitted into the
frames' insets with Plan Paint's CONV (authoring/conv), in linear light."""
import sys
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "authoring" / "conv"))
import conv  # noqa: E402


def main():
    original = ROOT / "incoming/game-koikoi-01/assets/cards"
    if "--frames" in sys.argv:
        original = Path(sys.argv[sys.argv.index("--frames") + 1])
    destination = ROOT / "assets/cards"
    for sheet in range(3):
        with Image.open(HERE / f"months-{sheet * 4 + 1:02}-{sheet * 4 + 4:02}.png") as atlas:
            for index in range(16):
                col, row = index % 4, index // 4
                # Trim grid lines, then fit the existing inset exactly. Labels and
                # card identity are retained from the delivered deck, never generated.
                box = (round(col * atlas.width / 4) + 2,
                       round(row * atlas.height / 4) + 2,
                       round((col + 1) * atlas.width / 4) - 2,
                       round((row + 1) * atlas.height / 4) - 2)
                panel = conv.resize_srgb_image(atlas.crop(box).convert("RGBA"), 308, 420)
                name = f"hana_{sheet * 16 + index:02}.png"
                with Image.open(original / name) as frame:
                    card = frame.convert("RGBA")
                card.paste(panel, (26, 24))
                card.save(destination / name, optimize=True)
    print("Assembled 48 labeled hanafuda faces")


if __name__ == "__main__":
    main()
