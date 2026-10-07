#pragma once
// Draws the cube: a block of dark glass whose three playable faces carry mirrored tiles
// reflecting the lake, mossy stones, coloured endpoints, portal wells and the lines,
// which run over the surface and fold across the edges. Pure: it reads the rules and the
// motion and writes pixels, so the preview tool and the window draw the same frame.
#include "cube.hpp"
#include "raster3d.hpp"
#include "stage.hpp"

namespace ps_cube {

// The colour of a pair's line and endpoints.
[[nodiscard]] Rgba pair_color(int pair);

// Draws everything into `raster` (cleared first). `hover` is the cell under the pointer.
void draw_cube(Raster3D& raster, const Puzzle& puzzle, const Play& play, const Motion& motion, int hover);
// Where a cell's centre lands in the raster, false when its face is turned away.
[[nodiscard]] bool cell_point(const Puzzle& puzzle, const Motion& motion, int width, int height, int cell,
                              double& x, double& y);
// The cell under a raster pixel; grout between tiles belongs to the nearest tile.
[[nodiscard]] int pick(const Raster3D& raster, double x, double y);

}  // namespace ps_cube
