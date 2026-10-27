"""The two Koi-Koi decks' backs, from the owner's chrysanthemum print.

Hanafuda come as a pair of decks, black and red, used in turn, one each deal. Both backs
are chrysanthemum_print.webp (a red print on black, 1024 x 1536, supplied by the owner)
cropped inside its own frame to the card's 5:7, its red and black remapped in linear
light, and given a fine double frame on the card's shape (360 x 504, a rounded
rectangle 2 px in, as the faces):

  hana_back.png      the black deck: lacquer red on black
  hana_back_red.png  the red deck: black print on lacquer red

  python3 tools/make_backs.py <output-directory>
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

W, H = 360, 504
SOURCE = Path(__file__).with_name("chrysanthemum_print.webp")
BLACK = (15, 10, 11)
LACQUER = (172, 30, 34)


def print_amount() -> np.ndarray:
    """0 where the print is black ground, 1 where it is red."""
    image = Image.open(SOURCE).convert("RGB")
    left, top, width = 12, 68, 1000  # inside the source's frame; its height trimmed to 5:7
    image = image.crop((left, top, left + width, top + width * 7 // 5))
    pixels = np.asarray(image).astype(np.float32)
    return np.clip((pixels[..., 0] - 12.0) / (197.0 - 12.0), 0, 1)


def coloured(amount: np.ndarray, ground, ink) -> Image.Image:
    def linear(colour):
        return (np.array(colour, np.float32) / 255.0) ** 2.2

    mixed = linear(ground) * (1 - amount[..., None]) + linear(ink) * amount[..., None]
    return Image.fromarray(np.clip(mixed ** (1 / 2.2) * 255 + .5, 0, 255).astype(np.uint8))


def back(amount: np.ndarray, ground, ink) -> Image.Image:
    scale = 4
    inset = 16
    card = Image.new("RGB", (W, H), ground)
    art = coloured(amount, ground, ink).resize((W - 2 * inset, H - 2 * inset), Image.LANCZOS)
    card.paste(art, (inset, inset))
    frame = Image.new("L", (W * scale, H * scale), 0)
    draw = ImageDraw.Draw(frame)
    draw.rounded_rectangle([9 * scale, 9 * scale, (W - 10) * scale, (H - 10) * scale], radius=12 * scale,
                           outline=255, width=round(2.2 * scale))
    draw.rounded_rectangle([14 * scale, 14 * scale, (W - 15) * scale, (H - 15) * scale], radius=8 * scale,
                           outline=255, width=scale)
    card.paste(Image.new("RGB", (W, H), ink), (0, 0), frame.resize((W, H), Image.LANCZOS))
    shape = Image.new("L", (W * scale, H * scale), 0)
    ImageDraw.Draw(shape).rounded_rectangle([2 * scale, 2 * scale, (W - 3) * scale, (H - 3) * scale],
                                            radius=20 * scale, fill=255)
    out = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    out.paste(card, (0, 0), shape.resize((W, H), Image.LANCZOS))
    return out


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "assets/cards")
    out.mkdir(parents=True, exist_ok=True)
    amount = print_amount()
    back(amount, BLACK, LACQUER).save(out / "hana_back.png")
    back(amount, LACQUER, BLACK).save(out / "hana_back_red.png")
    print("wrote hana_back.png and hana_back_red.png to", out)


if __name__ == "__main__":
    main()
