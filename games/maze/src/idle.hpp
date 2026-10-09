#pragma once
#include "world.hpp"

namespace mz {
// Conservative visibility against the last rendered camera and depth buffer.
// False means the whole bound is offscreen or behind opaque geometry.
bool visible_box(const Soft3D& renderer, V3 minimum, V3 maximum);
bool visible_world_animation(const Soft3D& renderer, const Level& level, const WorldState& world);
}
