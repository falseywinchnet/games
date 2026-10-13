"""Python side of authoring/conv: Plan Paint's CONV area resize for offline assets.

resize_area(a, width, height) -- a float array (H x W, or H x W x 4 premultiplied RGBA).
resize_srgb_image(image, width, height) -- a PIL image, resized in linear light with
premultiplied alpha and rounded to 8 bits only at the end.
The tool is built on first use into .build/conv_tool."""
import os
import subprocess
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
TOOL = os.path.join(ROOT, '.build', 'conv_tool')


def _tool():
    sources = [os.path.join(HERE, 'conv.cpp'), os.path.join(HERE, 'conv_tool.cpp')]
    if not os.path.exists(TOOL) or any(os.path.getmtime(s) > os.path.getmtime(TOOL) for s in sources):
        os.makedirs(os.path.dirname(TOOL), exist_ok=True)
        subprocess.run(['c++', '-std=c++20', '-O2', *sources, '-o', TOOL], check=True)
    return TOOL


def resize_area(a, width, height):
    a = np.asarray(a, float)
    channels = 1 if a.ndim == 2 else a.shape[2]
    if channels not in (1, 4):
        raise ValueError('one channel, or four (premultiplied RGBA)')
    with tempfile.TemporaryDirectory() as tmp:
        src, dst = os.path.join(tmp, 'in.f64'), os.path.join(tmp, 'out.f64')
        np.ascontiguousarray(a, '<f8').tofile(src)
        subprocess.run([_tool(), src, str(a.shape[1]), str(a.shape[0]), str(width), str(height), dst, str(channels)],
                       check=True)
        out = np.fromfile(dst, '<f8')
    return out.reshape(height, width) if channels == 1 else out.reshape(height, width, 4)


def to_linear(c):
    c = np.asarray(c, float)
    return np.where(c <= .04045, c / 12.92, ((c + .055) / 1.055) ** 2.4)


def to_srgb(x):
    x = np.clip(x, 0, 1)
    return np.where(x <= .0031308, x * 12.92, 1.055 * np.power(x, 1 / 2.4) - .055)


def resize_srgb_image(image, width, height):
    """A PIL image resized by CONV in linear light, premultiplied; RGBA or RGB out as given."""
    from PIL import Image
    mode = image.mode
    rgba = np.asarray(image.convert('RGBA')).astype(float) / 255.0
    alpha = rgba[..., 3:4]
    pre = np.concatenate([to_linear(rgba[..., :3]) * alpha, alpha], -1)
    small = resize_area(pre, width, height)
    a = np.clip(small[..., 3:4], 0, 1)
    colour = np.where(a > 1e-9, small[..., :3] / np.maximum(a, 1e-9), 0)
    out = np.concatenate([to_srgb(colour), a], -1)
    img = Image.fromarray(np.clip(np.round(out * 255), 0, 255).astype(np.uint8), 'RGBA')
    return img if mode == 'RGBA' else img.convert(mode)
