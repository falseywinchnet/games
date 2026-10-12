#pragma once
// PNG pictures (the posters) into texel maps: 8-bit RGB or RGBA, not interlaced, the
// layout the authoring script writes. Decoded with the ambient engine's inflater.
#include "soft3d.hpp"

#include <cstdint>
#include <span>
#include <string>

namespace mz {

bool decode_png(std::span<const std::uint8_t> file, Tex32& out);  // false, and `out` untouched, for anything else
bool load_png(const std::string& path, Tex32& out);

}  // namespace mz
