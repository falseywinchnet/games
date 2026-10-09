#pragma once
// The lake behind the cube: the painted panorama decoded from its PNG, and laid over a
// frame of any size the way a cover photo is, cropped rather than stretched, a little
// darkened so the cube stands out. PNG decoding uses the ambient engine's inflate, so the
// game needs no image library. Only 8-bit RGB or RGBA, non-interlaced, is accepted.
#include "target.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace ps_cube {

struct Picture {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgb;  // width * height * 3, row-major
};

// False, leaving `out` empty, for anything but an intact 8-bit RGB or RGBA PNG.
[[nodiscard]] bool decode_png(std::span<const std::uint8_t> file, Picture& out);
// Fills a target with the picture covering it (opaque, in the target's byte order).
void cover(const Picture& picture, const render::Target& target);

}  // namespace ps_cube
