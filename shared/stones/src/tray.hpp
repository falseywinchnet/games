#pragma once
// The tray: one board of dark varnished wood with a raised wall round its edge and a
// grid of rounded sockets carved into it, one for every stone, so each stone has its
// place whatever its outline. It is lit by the same room as the stones (room.hpp): the
// window and the ceiling lights in its varnish, the sockets' walls shading their floors.
// It never moves, so it is drawn once for a size and kept.
#include <cstdint>

namespace stones {

enum class Lining { wood, felt };

struct TrayPlan {
    int width = 0, height = 0;     // the whole picture, device pixels
    double scale = 1;              // device pixels a point
    double grid_x = 0, grid_y = 0; // the grid's top-left corner, device pixels
    double cell = 64;              // a socket's pitch, device pixels
    int columns = 10, rows = 10;
    Lining lining = Lining::wood;
    int wood = 0;                  // the wood: 0 the tray's, 1 walnut boards, 2 quartersawn, 3 figured
    double wall = 0;               // the raised edge's width round the picture, device pixels
};

// Rows [row0, row1) of the tray into BGRA (`bgra`) or RGBA pixels: rows can be shared out
// among threads, each writing its own.
void draw_tray(const TrayPlan& plan, std::uint32_t* pixels, int stride, bool bgra, int row0, int row1);

// A socket's floor below the tray's top, as a share of the pitch: where a stone rests.
constexpr double socket_depth = .13;

}  // namespace stones
