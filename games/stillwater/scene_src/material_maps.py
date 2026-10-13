"""Stillwater's material maps at 256 x 256, reduced with Plan Paint's CONV (authoring/conv).

A colour map is reduced in linear light and encoded back to sRGB. A normal map holds
vectors, not colours: its texels are decoded to directions, CONV averages their components,
each is made unit length again, and they are encoded back. Both are rounded only at the end."""
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "authoring" / "conv"))
import conv  # noqa: E402

TEXTURE_SIZE = 256


def material_map(path: Path, normal_map: bool, size: int = TEXTURE_SIZE) -> np.ndarray:
    """The map as size x size x 3 bytes."""
    with Image.open(path) as source:
        rgb = np.asarray(source.convert("RGB")).astype(float) / 255.0
    if normal_map:
        vectors = rgb * 2.0 - 1.0
        stack = np.concatenate([vectors, np.ones(vectors.shape[:2] + (1,))], -1)
        small = conv.resize_area(stack, size, size)[..., :3]
        small /= np.maximum(np.linalg.norm(small, axis=-1, keepdims=True), 1e-9)
        encoded = (small + 1.0) * 0.5
    else:
        stack = np.concatenate([conv.to_linear(rgb), np.ones(rgb.shape[:2] + (1,))], -1)
        small = conv.resize_area(stack, size, size)[..., :3]
        encoded = conv.to_srgb(small)
    return np.clip(np.round(encoded * 255.0), 0, 255).astype(np.uint8)


def texture_record(path: Path, normal_map: bool) -> bytes:
    """A texture as an ambient archive stores it: width, height, kind, then the bytes."""
    data = material_map(path, normal_map)
    return struct.pack("<HHI", TEXTURE_SIZE, TEXTURE_SIZE, 1 if normal_map else 0) + data.tobytes()
