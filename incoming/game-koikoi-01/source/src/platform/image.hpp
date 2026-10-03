#pragma once
// PNG card artwork into canvases (macOS ImageIO), and high-quality reduction to the size it's drawn at.
#include "raster.hpp"

#include <string>

namespace kk {

bool load_png(const std::string& path, Canvas& out);      // BGRA premultiplied, like every Canvas
void reduce(const Canvas& src, int w, int h, Canvas& out); // area-averaged downscale (or bilinear upscale)

}  // namespace kk
