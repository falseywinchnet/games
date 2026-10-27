#pragma once
// The grass itself: the one seam where the lawn's look is decided. The mowing, the
// stripes, the cut edge and everything standing on the lawn are built on these
// layers, so better grass replaces make_grass_art() and nothing more.
//
//   tall         uncut grass with the garden's flowers and mushrooms grown into it
//   mown_dark    short turf laid towards the sun (the darker stripe, with the dew)
//   mown_light   the same turf laid away from the sun (the lighter stripe)
//   beds         shredded bark with the beds' flowers growing in it
//   mulch        the same bark bare, shown where a bed has been driven over
//   crowns       the trees' canopies, cut out, drawn over whatever is beneath them
//
// Each covers the whole lawn at `ppm` pixels per metre (no tiling, so no repeats),
// opaque, 0xAARRGGBB, row-major. The generator is the shared grass engine (shared/grass).
#include "lawn.hpp"

#include <atomic>
#include <cstdint>
#include <vector>

namespace mm {

struct Garden;

using grass::Cutout;
using grass::Layer;
using grass::sample_clamped;

struct GrassArt {
    Layer tall{};
    Layer mown_dark{};
    Layer mown_light{};
    Layer mown_quarter{};        // laid a quarter turn from the sun (see LawnLook)
    Layer mown_three_quarter{};
    Layer beds{};
    Layer mulch{};
    std::vector<Cutout> crowns{};  // one canopy for each of the garden's trees, in order
    double ppm{};  // pixels per metre of every layer
    [[nodiscard]] bool empty() const {
        return tall.empty() || mown_dark.empty() || mown_light.empty() || mown_quarter.empty() || mown_three_quarter.empty() || beds.empty() || mulch.empty();
    }
};

// Grows the grass for one garden, around its beds and with its flowers. A few tenths
// of a second the first time (the kits of plants are made once), about a tenth after;
// `cancel` is checked between layers.
[[nodiscard]] GrassArt make_grass_art(const Garden& garden, const std::atomic<bool>* cancel);

} // namespace mm
