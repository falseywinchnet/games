#include "shelf_art.hpp"

namespace games::modules {

// The box's emblem: a curtain of white water over a dark cliff under a bright sky,
// trees on either side, the pool at its foot.
void canopyfalls_cover(gf::Painter& painter, gf::Rect bounds) {
    using shelf_art::disc;
    using shelf_art::rgb;
    const double size = std::min(bounds.width, bounds.height) * .86;
    const double x = bounds.x + bounds.width * .5;
    const double y = bounds.y + bounds.height * .5;
    const double left = x - size * .5, top = y - size * .5;
    // Sky and sun.
    painter.fill_rounded_rect({left, top, size, size * .42}, size * .06, rgb(122, 168, 214));
    disc(painter, x + size * .14, top + size * .12, size * .07, rgb(255, 250, 232));
    disc(painter, x + size * .14, top + size * .12, size * .12, rgb(255, 246, 214, 70));
    // The cliff.
    paint_polygon(painter,
                  {{left, top + size * .34}, {left + size, top + size * .3}, {left + size, top + size * .86},
                   {left, top + size * .86}},
                  rgb(54, 58, 56));
    // The curtain: white, streaked, with a glowing brink.
    paint_polygon(painter,
                  {{x - size * .26, top + size * .33}, {x + size * .26, top + size * .32}, {x + size * .3, top + size * .8},
                   {x - size * .3, top + size * .8}},
                  rgb(236, 240, 242));
    for (int i = 0; i < 7; ++i) {
        const double sx = x - size * .22 + i * size * .075;
        painter.draw_line({sx, top + size * .36}, {sx + size * .01 * (i % 3 - 1), top + size * .78}, rgb(178, 196, 208), size * .012);
    }
    painter.draw_line({x - size * .26, top + size * .33}, {x + size * .26, top + size * .32}, rgb(246, 222, 150), size * .02);
    // The pool and its spray.
    painter.fill_rounded_rect({left, top + size * .78, size, size * .22}, size * .06, rgb(58, 120, 112));
    disc(painter, x, top + size * .8, size * .22, rgb(240, 244, 246, 140));
    // Trees either side.
    disc(painter, left + size * .1, top + size * .3, size * .2, rgb(40, 92, 52));
    disc(painter, left + size * .04, top + size * .55, size * .2, rgb(30, 76, 42));
    disc(painter, left + size * .9, top + size * .32, size * .2, rgb(44, 98, 56));
    disc(painter, left + size * .96, top + size * .58, size * .2, rgb(32, 80, 44));
}

} // namespace games::modules
