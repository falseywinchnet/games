#include "shelf_art.hpp"

namespace games::modules {
// The box's emblem: a corner of the dark tray, four polished stones in their sockets, each
// a different mineral, each with the window's glint on it.
void tumbledstones_cover(gf::Painter& painter, gf::Rect bounds) {
    const double size = std::min(bounds.width, bounds.height);
    const double cx = bounds.x + bounds.width * .5;
    const double cy = bounds.y + bounds.height * .5;
    const double pitch = size * .36;
    painter.fill_rounded_rect({cx - pitch * 1.1, cy - pitch * 1.1, pitch * 2.2, pitch * 2.2}, size * .1,
                              shelf_art::rgb(46, 29, 18));
    const gf::Color stones[4] = {shelf_art::rgb(118, 64, 168), shelf_art::rgb(36, 128, 82),
                                 shelf_art::rgb(212, 128, 40), shelf_art::rgb(24, 22, 28)};
    for (int k = 0; k < 4; ++k) {
        const double sx = cx + (k % 2 == 0 ? -.5 : .5) * pitch;
        const double sy = cy + (k / 2 == 0 ? -.5 : .5) * pitch;
        painter.fill_rounded_rect({sx - pitch * .46, sy - pitch * .46, pitch * .92, pitch * .92}, pitch * .2,
                                  shelf_art::rgb(28, 17, 10));
        const double w = pitch * (k % 2 == 0 ? .36 : .32);
        const double h = pitch * (k / 2 == 0 ? .3 : .36);
        painter.fill_rounded_rect({sx - w, sy - h, w * 2, h * 2}, std::min(w, h) * .9, stones[k]);
        shelf_art::disc(painter, sx - w * .35, sy - h * .4, std::min(w, h) * .22, shelf_art::rgb(255, 244, 220, 170));
    }
}
}  // namespace games::modules
