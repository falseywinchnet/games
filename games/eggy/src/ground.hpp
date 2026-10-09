#pragma once
#include "render.hpp"

#include <array>

namespace eggy {

constexpr int kGroundCount = 40;

// Ground ids (looks). The world assigns one per tile; neighbouring looks are
// splat-blended per pixel so regions meet in organic, noisy edges.
enum GroundId : std::uint8_t {
    g_lush, g_flowery, g_clover, g_dry, g_alpine, g_alpine_stony, g_mossy_grass, g_wet_grass,
    g_needles, g_litter, g_autumn, g_autumn_red, g_moss, g_fern_floor, g_roots,
    g_trail, g_mud, g_clay, g_sand, g_riverbed,
    g_granite, g_slate, g_sandstone, g_basalt, g_lichen_rock, g_limestone, g_scree, g_gravel, g_shale,
    g_snow, g_snow_carved, g_snow_crust, g_snow_grass, g_snow_rock, g_firn,
    g_ice, g_ice_blue, g_ice_frost,
    g_streambed, g_silt
};

struct Ground {
    std::array<Tex, kGroundCount> tex;
    Tex splat;
};

const Ground& ground();

}  // namespace eggy
