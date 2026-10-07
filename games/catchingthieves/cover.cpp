#include "shelf_art.hpp"
namespace games::modules {
void catchingthieves_cover(gf::Painter& painter, gf::Rect bounds) {
    using shelf_art::disc;
    using shelf_art::rgb;
    const double x = bounds.x + bounds.width * .5;
    const double y = bounds.y + bounds.height * .53;
    const double size = std::min(bounds.width, bounds.height) * .82;
    // A mischievous masked face peeking over a ribbed pumpkin.
    disc(painter, x - size * .2, y - size * .29, size * .11, rgb(114, 107, 100));
    disc(painter, x + size * .2, y - size * .29, size * .11, rgb(114, 107, 100));
    disc(painter, x, y - size * .18, size * .26, rgb(166, 157, 143));
    painter.fill_rounded_rect({x - size * .22, y - size * .26, size * .44, size * .13}, size * .06, rgb(43, 39, 35));
    disc(painter, x - size * .10, y - size * .2, size * .032, rgb(255, 239, 172));
    disc(painter, x + size * .10, y - size * .2, size * .032, rgb(255, 239, 172));
    disc(painter, x, y - size * .10, size * .036, rgb(35, 30, 25));
    disc(painter, x, y + size * .14, size * .32, rgb(220, 102, 19));
    disc(painter, x - size * .16, y + size * .14, size * .22, rgb(240, 130, 28));
    disc(painter, x + size * .16, y + size * .14, size * .22, rgb(232, 115, 23));
    painter.fill_rounded_rect({x - size * .13, y - size * .12, size * .26, size * .53}, size * .13, rgb(250, 153, 40));
    painter.draw_line({x - size * .02, y - size * .08}, {x + size * .03, y - size * .24}, rgb(84, 111, 37), size * .06);
}
} // namespace games::modules
