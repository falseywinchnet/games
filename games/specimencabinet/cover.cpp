#include "shelf_art.hpp"

namespace games::modules {
// The box's emblem: a walnut cabinet of three by three sockets holding a whole small
// square, three shapes in three colours, no row or column repeating either: the shapes
// (row + column) and the colours (2 row + column), both mod 3.
void specimencabinet_cover(gf::Painter& painter, gf::Rect bounds) {
    const double size = std::min(bounds.width, bounds.height);
    const double cx = bounds.x + bounds.width * .5;
    const double cy = bounds.y + bounds.height * .5;
    const double pitch = size * .27;
    painter.fill_rounded_rect({cx - pitch * 1.62, cy - pitch * 1.62, pitch * 3.24, pitch * 3.24}, size * .08,
                              shelf_art::rgb(94, 60, 36));
    const gf::Color colours[3] = {shelf_art::rgb(52, 104, 178), shelf_art::rgb(226, 179, 90),
                                  shelf_art::rgb(58, 140, 98)};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            const double sx = cx + (column - 1) * pitch;
            const double sy = cy + (row - 1) * pitch;
            painter.fill_rounded_rect({sx - pitch * .44, sy - pitch * .44, pitch * .88, pitch * .88}, pitch * .18,
                                      shelf_art::rgb(52, 32, 18));
            const int shape = (row + column) % 3;
            const gf::Color colour = colours[(2 * row + column) % 3];
            double w = pitch * .3;
            double h = pitch * .3;
            double corner = w;
            if (shape == 1) {
                corner = pitch * .08;
                w = pitch * .27;
                h = pitch * .27;
            } else if (shape == 2) {
                w = pitch * .36;
                h = pitch * .2;
                corner = h;
            }
            painter.fill_rounded_rect({sx - w, sy - h, w * 2, h * 2}, corner, colour);
            shelf_art::disc(painter, sx - w * .4, sy - h * .45, std::min(w, h) * .22, shelf_art::rgb(255, 244, 220, 160));
        }
    }
}
}  // namespace games::modules
