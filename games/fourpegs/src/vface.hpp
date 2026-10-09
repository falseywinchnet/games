#pragma once
// The villain's face: a painted texture over the front of his head, so his
// expressions are drawings swapped instantly. Painted once per combination
// and cached.
#include "platform/render.hpp"

#include <cstdint>

namespace fp {

enum class VEyes : std::uint8_t { menace, half, closed, wide, furious, squint, glint, laugh, twitch };
enum class VBrow : std::uint8_t { flat, arched, furious, worried, quizzical };
enum class VMouth : std::uint8_t { smirk, grin, sneer, flat, laugh, shout, gasp, frown, grimace, purse };

struct VFace {
    VEyes eyes = VEyes::menace;
    VBrow brow = VBrow::flat;
    VMouth mouth = VMouth::smirk;
    int look_x = 0;   // -2..2, + is screen right
    int look_y = 0;   // -1 down .. 1 up
    std::uint32_t key() const {
        return static_cast<std::uint32_t>(eyes) | static_cast<std::uint32_t>(brow) << 4 | static_cast<std::uint32_t>(mouth) << 8 |
               static_cast<std::uint32_t>(look_x + 2) << 12 | static_cast<std::uint32_t>(look_y + 1) << 16;
    }
};

// Texture coordinates of the face patch: s = .5 + x * kVFaceK, t = .5 - (z - kVFaceZ) * kVFaceK
// for a point (x, y, z) on the unit head sphere (the face looks toward -y).
constexpr double kVFaceK = .58, kVFaceZ = -.08;

const Tex& vface_texture(const VFace& f);
Col villain_skin();

}  // namespace fp
