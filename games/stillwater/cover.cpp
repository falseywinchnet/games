#include "shelf_art.hpp"

namespace games::modules {
namespace {

// One blade of grass: a tapering quadrilateral from a root to a leaning tip.
void blade(gf::Painter& painter, double x, double y, double height, double lean, double width, gf::Color color) {
    paint_polygon(painter,
                  {{x - width, y}, {x + width, y}, {x + lean + width * .2, y - height}, {x + lean - width * .2, y - height}},
                  color);
}

// A neon-silver tetra facing right: lens body, belly, blue sheen, red tail and an eye.
void tetra(gf::Painter& painter, double x, double y, double size) {
    using shelf_art::disc;
    using shelf_art::rgb;
    paint_polygon(painter, {{x - size * .55, y}, {x - size * .85, y - size * .2}, {x - size * .8, y + size * .2}},
                  rgb(190, 52, 34));
    paint_polygon(painter,
                  {{x - size * .6, y}, {x - size * .3, y - size * .17}, {x + size * .1, y - size * .2},
                   {x + size * .45, y - size * .08}, {x + size * .55, y}, {x + size * .45, y + size * .08},
                   {x + size * .1, y + size * .17}, {x - size * .3, y + size * .14}},
                  rgb(214, 222, 220));
    paint_polygon(painter,
                  {{x - size * .45, y + size * .03}, {x + size * .3, y + size * .02}, {x + size * .3, y + size * .12},
                   {x - size * .3, y + size * .1}},
                  rgb(236, 238, 232));
    painter.draw_line({x - size * .42, y - size * .04}, {x + size * .32, y - size * .06}, rgb(64, 168, 214), size * .06);
    paint_polygon(painter, {{x - size * .1, y - size * .18}, {x + size * .05, y - size * .32}, {x + size * .1, y - size * .19}},
                  rgb(176, 60, 40));
    disc(painter, x + size * .36, y - size * .02, size * .06, rgb(22, 26, 24));
    disc(painter, x + size * .37, y - size * .035, size * .02, rgb(230, 236, 232));
}

} // namespace

void stillwater_cover(gf::Painter& painter, gf::Rect bounds) {
    using shelf_art::disc;
    using shelf_art::rgb;
    const double size = std::min(bounds.width, bounds.height) * .84;
    const double x = bounds.x + bounds.width * .5;
    const double y = bounds.y + bounds.height * .5;
    const double floor = y + size * .40;
    // Light from the surface, then grass behind, the sand and stones, grass in front.
    disc(painter, x - size * .12, y - size * .32, size * .3, rgb(70, 140, 112, 70));
    blade(painter, x - size * .40, floor, size * .78, size * .10, size * .045, rgb(52, 120, 54));
    blade(painter, x - size * .28, floor, size * .62, -size * .12, size * .04, rgb(74, 150, 62));
    blade(painter, x + size * .32, floor, size * .80, -size * .10, size * .045, rgb(58, 132, 56));
    blade(painter, x + size * .42, floor, size * .55, size * .08, size * .035, rgb(84, 162, 70));
    painter.fill_rounded_rect({x - size * .5, floor - size * .02, size, size * .1}, size * .05, rgb(196, 176, 128));
    disc(painter, x - size * .24, floor - size * .01, size * .11, rgb(34, 36, 34));
    disc(painter, x + size * .18, floor + size * .01, size * .07, rgb(46, 48, 44));
    tetra(painter, x + size * .02, y - size * .10, size * .44);
    tetra(painter, x - size * .20, y + size * .14, size * .30);
    blade(painter, x - size * .10, floor, size * .36, size * .06, size * .03, rgb(98, 176, 74));
    blade(painter, x + size * .08, floor, size * .30, -size * .05, size * .028, rgb(88, 168, 70));
}

} // namespace games::modules
