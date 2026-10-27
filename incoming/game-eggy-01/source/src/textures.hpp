#pragma once
// Procedurally painted, tileable pixel-art textures (generated at start-up;
// no image files, nothing to license).
#include "r3d.hpp"

#include <array>

namespace eggy {

struct Textures {
    Tex grass, trail, grass_alpine, forest, autumn, moss, path, rock, gravel, snow, ice, sand, cliff, dirt;
    std::array<Tex, 4> water;
    Tex bark, wood_end, leaves, leaves_autumn, pine, fluff, belly, acorn, beak, eye;
    Tex tuft, fern, clover, flower[4], mushroom_cap, star, glow, puff, leaf_sprite[4], shadow, lily, sparkle, flagcloth, canvas;
    void build();
};

const Textures& textures();

}  // namespace eggy
