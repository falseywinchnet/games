#pragma once
// The clipped hedges round the garden, after box and yew: a mass of small leaves at
// many depths, cut flat on top with soft shoulders and sides that lean in a little,
// darker in the hollows and at the foot, lighter where the outer leaves catch the sun,
// a twiggy gap here and there, and the colour drifting along the run. One continuous
// shell is built for the whole hedge, so runs join, corners turn and inside corners
// close without seams. The shell, its colours and the shade it casts on the ground are
// made once per garden and season; a frame only draws them.
#include "garden.hpp"
#include "platform/render.hpp"

#include <cstdint>
#include <vector>

namespace ct {

struct HedgeCache {
    // what it was made for
    int w = 0;
    int h = 0;
    std::vector<Tile> tiles;
    Season season = Season::spring;
    bool coloured = false;
    // the shell, before colour: one entry per triangle corner
    struct Corner {
        V3 p;            // world position
        V3 n;            // smooth normal
        double s = 0;    // texture coordinates, world-locked
        double t = 0;
        float shade = 1; // ambient occlusion and hollows: 0 black .. 1 open
        float tone = 0;  // the colour's drift along the run: -1 .. 1
        float up = 0;    // how much the surface faces the sky, for snow: 0 .. 1
        float foot = 0;  // how near the ground: 1 at the foot .. 0 a hand's height up
    };
    std::vector<Corner> top;   // the clipped top and its shoulders
    std::vector<Corner> side;  // the sides down to the ground
    std::vector<Corner> fringe;  // sprigs the shears missed, standing along the top's edges
    std::vector<Vtx> top_v;    // coloured for the season
    std::vector<Vtx> side_v;
    std::vector<Vtx> snow_v;   // the flat of the top under snow
    std::vector<Vtx> fringe_v;
    // the shade on the ground: contact darkness at the foot and the shadow the sun casts
    Tex shadow;
    double shadow_x0 = 0, shadow_y0 = 0, shadow_span_x = 1, shadow_span_y = 1;  // world rectangle the texture covers
    std::vector<int> shadow_cells;  // cells (grid index in the shadow's own grid) with any shade, to draw
    int shadow_cols = 0;
    int shadow_rows = 0;
};

}  // namespace ct
