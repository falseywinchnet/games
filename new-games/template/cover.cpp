#include "shelf_art.hpp"

namespace games::modules {
void templategame_cover(gf::Painter& painter, gf::Rect bounds) {
    const double size = std::min(bounds.width, bounds.height);
    const double cx = bounds.x + bounds.width * .5;
    const double cy = bounds.y + bounds.height * .5;
    for (int row = 0; row < 2; ++row) {
        for (int column = 0; column < 2; ++column) {
            const gf::Color color = row != column ? gf::Color::rgba(255, 215, 122) : gf::Color::rgba(110, 96, 84);
            shelf_art::disc(painter, cx + (column - .5) * size * .46,
                            cy + (row - .5) * size * .46, size * .19, color);
        }
    }
}
} // namespace games::modules
