#pragma once
// Whole-lawn pictures of grass and mulch, generated from a scene's layout: long grass with the scene's own
// flowers and mushrooms grown into it and lit with it, short grass laid each of the ways a mower can leave
// it, and shredded bark mulch. Grass thins and stops in a bare strip at whatever the scene says stands in it.
//
// Placement comes first (the game decides what is where), generation second (here), display third (the game).
// See ../README.md for the interface, its costs and its limits.
#include <cstdint>
#include <vector>

namespace grass {

// A picture: opaque unless it is a cut-out, 0xAARRGGBB, row-major.
struct Layer {
    int width{};
    int height{};
    std::vector<std::uint32_t> px{};
    [[nodiscard]] bool empty() const {
        return width <= 0 || height <= 0;
    }
};

// A picture with a soft edge (premultiplied) and the point in it that stands on the lawn.
struct Cutout {
    Layer image{};
    double ppm{};
    double root_x{};
    double root_y{};
};

// Bilinear sample of a layer at pixel coordinates, clamped at its edges.
[[nodiscard]] std::uint32_t sample_clamped(const Layer& layer, double x, double y);

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

// The time of year in the grass. All zero is a lawn in early summer (Mowing's); each amount runs 0 to 1.
struct LawnWeather {
    double fresh{};  // spring growth: younger, lighter, yellower leaves
    double dry{};    // late summer and autumn: more leaves dried to straw, tips first
    double frost{};  // winter: hoar frost and a dusting of snow on whatever faces the sky, and on the ground
};

// One opaque layer covering metres_w x metres_h at pixels_per_metre. mown_dark is laid towards the sun (upper
// left) and carries the dew; mown_light is laid away from it. The first call for a look and weather makes its
// kit (some tenths of a second for long grass); later calls reuse it. Uses every core. `beds` is `mulch` with
// the scene's plants growing in it: the same bark, piece for piece. Flowers, plants and the distance grid are
// placed in lawn metres from the picture's top-left corner, as its rows run.
[[nodiscard]] Layer render_lawn(LawnLook look, const LawnScene& scene, std::uint64_t seed, double metres_w, double metres_h, double pixels_per_metre,
                                const LawnWeather& weather = LawnWeather{});

// A tree's canopy as a cut-out, lit like the lawn, to be drawn over whatever passes beneath.
// kind: 0 green, 1 pink blossom, 2 white blossom, 3 copper. crown: the canopy's radius, metres.
[[nodiscard]] Cutout render_tree(int kind, double crown, std::uint64_t seed, double pixels_per_metre);

} // namespace grass
