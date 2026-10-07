#pragma once
// Her face is a painted texture over the front of the head, so expressions
// are 2D drawings swapped instantly, the way anime faces work. Each
// combination is painted once on demand and cached.
#include "platform/r3d.hpp"

#include <cstdint>

namespace sbx {

enum class Eyes : std::uint8_t { open, half, happy, squeeze, wide, angry, blink, sparkle, teary, swirl };
enum class Brow : std::uint8_t { neutral, raised, angry, worried };
enum class Mouth : std::uint8_t { smile, cat, open_smile, grin_tongue, pout, frown, wavy, shout, o_small, flat, bleh };

struct Face {
    Eyes eyes = Eyes::open;
    Brow brow = Brow::neutral;
    Mouth mouth = Mouth::smile;
    int blush = 1;     // 0 none, 1 rosy, 2 flustered (hatched)
    int look_x = 0;    // iris offset, -2..2 (her right .. her left as seen by the player: + is screen right)
    int look_y = 0;    // -1 down .. 1 up
    std::uint32_t key() const {
        return static_cast<std::uint32_t>(eyes) | static_cast<std::uint32_t>(brow) << 4 | static_cast<std::uint32_t>(mouth) << 8 |
               static_cast<std::uint32_t>(blush) << 12 | static_cast<std::uint32_t>(look_x + 2) << 16 | static_cast<std::uint32_t>(look_y + 1) << 20;
    }
};

// Texture coordinates of the face patch: s = .5 + x * kFaceK, t = .5 - (z - kFaceZ) * kFaceK
// for a point (x, y, z) on the unit head sphere (the face looks toward -y).
constexpr double kFaceK = .6, kFaceZ = -.1;

const Tex& face_texture(const Face& f);

// Skin colour shared by the head mesh and the face paint so the patch is seamless.
Col skin_col();

}  // namespace sbx
