#pragma once
// What a stone is made of. A mineral is drawn for every stone on its own, never from its
// shape, so a family's shape cannot be learnt from its colour: any family may come as
// obsidian, walnut, bone, agate, amethyst or lapis. Each stone's mineral has its own
// colours, grain, clarity and polish, and is a solid: the pattern runs through the stone,
// so its two faces differ as a real stone's do.
//
// `MineralPicture` is a stone's mineral printed once over each face, so drawing it in any
// orientation reads the picture instead of working the pattern out again.
#include "shapes.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace ts {

enum class Mineral {
    obsidian, walnut, petrified, bone, agate, amethyst, lapis, sodalite, malachite, quartz, smoky,
    rose, jade, labradorite, jasper, amber, blue_lace, chrysocolla, candy, nacre, cone, seaglass,
    lava, dripstone
};
constexpr int mineral_count = 24;

[[nodiscard]] const char* mineral_name(Mineral mineral);

struct Linear {
    float r = 0, g = 0, b = 0;
};

// One stone's mineral: drawn from its seed.
struct Finish {
    Mineral mineral = Mineral::obsidian;
    std::uint32_t seed = 0;
    int variant = 0;                  // a mineral's kinds: snowflake obsidian, an agate's colours
    std::array<Linear, 5> palette{};  // linear colours, the mineral's own
    double axis[3] = {0, 0, 1};       // the bands' or grain's direction
    double centre[3] = {0, 0, 0};     // the middle of concentric bands
    double frequency = 4;             // bands per unit
    double warp = .3;                 // how much they wander
    double roughness = .1;            // 0 a mirror polish .. 1 satin
    double clarity = 0;               // 0 opaque .. 1 clear as glass
    Linear absorb{};                  // per unit of thickness, for the clear ones
    double sheen = 0;                 // a colour that comes and goes with the angle
    Linear sheen_colour{};
    double flecks = 0;                // glinting specks: pyrite, mica, crazed spangles
    Linear glitter{.95F, .72F, .32F}; // what they glint, the room's light tinted
    double scatter = .08;             // light scattered inside a clear one: a frosted glow
    double shine = .045;              // the polish's reflection face on: low for chalky bone
    bool rainbow = false;             // the sheen runs through the rainbow with the angle: nacre
};

// The mineral for stone `seed`: any mineral, weighted so each kind of material turns up.
[[nodiscard]] Finish draw_finish(std::uint32_t seed);
// A mineral's share of the draw, in thousandths of the whole (the rules draw with it).
[[nodiscard]] int mineral_weight(int mineral);
// A given mineral, its measures drawn from `seed`.
[[nodiscard]] Finish make_finish(Mineral mineral, std::uint32_t seed);

// The mineral at a point of the stone (object space): its colour, how clear it is there,
// and whether a metallic fleck lies there.
struct MineralSample {
    Linear albedo;
    float clarity = 0;
    float fleck = 0;
    float sheen = 0;   // 0..1, where the sheen can show
};
[[nodiscard]] MineralSample sample_mineral(const Finish& finish, double x, double y, double z);

// The mineral printed over both faces of one stone's shape: `side` x `side` texels a face
// over [-1, 1]^2, row 0 at y = 1.
struct MineralPicture {
    int side = 0;
    std::vector<std::uint32_t> colour;  // 2 faces; r | g << 8 | b << 16 | clarity << 24, sRGB-coded colour
    std::vector<std::uint16_t> extra;   // 2 faces; fleck | sheen << 8
};
void print_mineral(const Finish& finish, const Shape& shape, int side, MineralPicture& out);

// sRGB coding, both ways.
[[nodiscard]] float linear_from_srgb(float coded);
[[nodiscard]] float srgb_from_linear(float linear);

}  // namespace ts
