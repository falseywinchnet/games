#pragma once
// Draws the cube: a block of dark glass whose three playable faces carry mirrored tiles
// reflecting the lake, mossy stones, coloured endpoints, portal wells and the lines,
// which run over the surface and fold across the edges. Pure: it reads the rules and the
// motion and writes pixels through the shared renderer, so the preview tool and the
// window draw the same frame.
//
// It is drawn straight into the picture at its final size. Near surfaces go first (tiles,
// stones and lines before the glass body under them), so depth turns away what they hide
// before it is shaded, and everything is scissored to the rectangle being repaired.
#include "cube.hpp"
#include "r3d.hpp"
#include "stage.hpp"

#include <cstdint>
#include <vector>

namespace ps_cube {

// A colour with straight alpha, channels 0..255.
struct Rgba {
    double r = 0;
    double g = 0;
    double b = 0;
    double a = 255;
};

// The colour of a pair's line and endpoints.
[[nodiscard]] Rgba pair_color(int pair);

// Where and how the cube is drawn this frame.
struct Drawing {
    render::r3d::Pass pass;                            // the picture, its buffers and the scissor
    const render::r3d::Panorama* panorama = nullptr;   // the lake the tiles mirror; none: plain glass
    Box board;                                         // the board square, in the picture's pixels
    bool draft = false;                                // nearest reflections and coarser tiles, for slow computers
    // Set by draw_cube from the motion: how bright the cube is (the finale darkens it)
    // and how far it is washed toward white (the finale's flash).
    double dim = 1;
    double flash = 0;
};

// Draws everything that falls inside the pass's scissor at full opacity. Cell ids go to
// the buffers. `hover` is the highlighted cell.
void draw_cube(Drawing& drawing, const Puzzle& puzzle, const Play& play, const Motion& motion, int hover);
// The same at the pose's opacity: while the cube fades in or out it is drawn alone into
// `scratch` (kept between frames, the target's size) and laid over the target.
void show_cube(Drawing& drawing, std::vector<std::uint32_t>& scratch, const Puzzle& puzzle, const Play& play,
               const Motion& motion, int hover);
// Every pixel the cube can touch in this pose, bevels and standing stones included.
[[nodiscard]] render::Rect cube_bounds(const Motion& motion, const Box& board);
// The pixels the given cells (and lines and rings lying on them) can touch.
[[nodiscard]] render::Rect cell_bounds(const Puzzle& puzzle, const Motion& motion, const Box& board,
                                       const std::vector<int>& cells);
// Where a cell's centre lands, false when its face is turned away.
[[nodiscard]] bool cell_point(const Puzzle& puzzle, const Motion& motion, const Box& board, int cell, double& x,
                              double& y);
// The cell under a pixel; grout between tiles belongs to the nearest tile.
[[nodiscard]] int pick(const render::r3d::Buffers& buffers, const Box& board, double x, double y);

}  // namespace ps_cube
