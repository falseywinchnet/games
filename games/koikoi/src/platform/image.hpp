#pragma once
// PNG card artwork into canvases, and high-quality reduction to the size it's drawn at.
#include "render.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace kk {

// The prepared runtime stores the deck as 8-bit RGBA, non-interlaced PNG (tools/prepare_portable_assets.py
// writes exactly that). Decoding uses the ambient engine's zlib decoder and premultiplies with exact
// rounding, (c * a + 127) / 255, so the canvas is byte-identical to the preparation's own premultiplication.
bool decode_png(std::span<const std::uint8_t> file, Canvas& out);  // BGRA premultiplied, like every Canvas
bool load_png(const std::string& path, Canvas& out);
// Loads every path into the canvas at the same index, decoding on several threads; a failed load leaves its
// canvas empty. Returns the number loaded.
int load_pngs(const std::vector<std::string>& paths, const std::vector<Canvas*>& out);
void reduce(const Canvas& src, int w, int h, Canvas& out); // area-averaged downscale (or bilinear upscale)

}  // namespace kk
