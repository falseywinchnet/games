#pragma once
// Whole-lawn pictures of grass and mulch, generated from the garden's layout: long grass with the garden's own
// flowers and mushrooms grown into it and lit with it, short grass laid each of the two ways a mower can leave
// it, and shredded bark mulch. Grass thins and stops in a bare strip at whatever the scene says stands in it.
//
// Placement comes first (the garden decides what is where), generation second (here), display third (the yard).
#include "grass_art.hpp"

#include <cstdint>
#include <vector>

namespace mm {

// 0 clover, 1 buttercup, 2 dandelion, 3 daisy, 4 a small blue flower, 5 mushroom cap, 6 dandelion clock
struct LawnFlower {
    int kind{};
    double x{};  // lawn metres
    double y{};
};

// One plant in a bed: kind is the garden's Flower (0 crocus .. 8 daisy), tone one of its three colours.
struct LawnPlant {
    int kind{};
    int tone{};
    double x{};
    double y{};
    std::uint32_t seed{};
};

struct LawnScene {
    int width{};   // grid nodes across and down, covering the lawn edge to edge
    int height{};
    std::vector<float> distance{};  // lawn metres to the nearest thing grass stops at; rows top to bottom
    std::vector<LawnFlower> flowers{};
    std::vector<LawnPlant> plants{};  // shown in the beds look
};

// The mown looks are laid four ways: towards the sun (dark, with the dew), a quarter turn
// round from it, away from it (light), and the other quarter turn.
enum class LawnLook : std::uint8_t { tall, mown_dark, mown_light, mulch, beds, mown_quarter, mown_three_quarter };

// One opaque layer covering metres_w x metres_h at pixels_per_metre. mown_dark is laid towards the sun (upper
// left) and carries the dew; mown_light is laid away from it. The first call for a look makes its kit (some
// tenths of a second for long grass); later calls reuse it. Uses every core. `beds` is `mulch` with the scene's
// plants growing in it: the same bark, piece for piece.
[[nodiscard]] Layer render_lawn(LawnLook look, const LawnScene& scene, std::uint64_t seed, double metres_w, double metres_h, double pixels_per_metre);

// A tree's canopy as a cut-out, lit like the lawn, to be drawn over whatever passes beneath.
// kind: 0 green, 1 pink blossom, 2 white blossom, 3 copper. crown: the canopy's radius, metres.
[[nodiscard]] Cutout render_tree(int kind, double crown, std::uint64_t seed, double pixels_per_metre);

} // namespace mm
