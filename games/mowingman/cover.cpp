#include "shelf_art.hpp"

namespace games::modules {

// A striped lawn seen from above, the orange mower at work, and a red gnome hat
// peeking out of the uncut grass.
void mowingman_cover(gf::Painter& painter, gf::Rect bounds) {
    using shelf_art::disc;
    using shelf_art::rgb;
    const double size = std::min(bounds.width, bounds.height) * 0.86;
    const double x = bounds.x + bounds.width * 0.5;
    const double y = bounds.y + bounds.height * 0.5;
    const double left = x - size * 0.5;
    const double top = y - size * 0.42;
    // Mown stripes below, long grass above.
    for (int stripe = 0; stripe < 4; ++stripe)
        painter.fill_rect({left, top + size * (0.36 + stripe * 0.12), size, size * 0.12},
                          stripe % 2 == 0 ? rgb(124, 178, 76) : rgb(104, 158, 62));
    painter.fill_rect({left, top, size, size * 0.36}, rgb(56, 98, 36));
    for (int blade = 0; blade < 26; ++blade) {
        const double bx = left + size * (0.02 + blade * 0.038);
        const double lean = (blade % 3 - 1) * size * 0.02;
        painter.draw_line({bx, top + size * 0.37}, {bx + lean, top + size * (0.22 + (blade * 7 % 5) * 0.02)},
                          rgb(84, 132, 50), std::max(1.0, size * 0.012));
    }
    // The gnome's hat in the long grass.
    paint_polygon(painter, {{x + size * 0.22, top + size * 0.26}, {x + size * 0.3, top + size * 0.08}, {x + size * 0.36, top + size * 0.27}},
                  rgb(210, 52, 44));
    disc(painter, x + size * 0.29, top + size * 0.28, size * 0.035, rgb(242, 238, 228));
    // The mower: deck, orange body, seat, and Eggy's yellow head and acorn cap.
    const double mx = x - size * 0.08;
    const double my = top + size * 0.58;
    painter.fill_rounded_rect({mx - size * 0.24, my - size * 0.2, size * 0.48, size * 0.3}, size * 0.06, rgb(48, 50, 52));
    painter.fill_rounded_rect({mx - size * 0.16, my - size * 0.12, size * 0.5, size * 0.24}, size * 0.05, rgb(242, 103, 34));
    painter.fill_rounded_rect({mx + size * 0.2, my - size * 0.12, size * 0.14, size * 0.24}, size * 0.04, rgb(184, 75, 18));
    painter.fill_rounded_rect({mx - size * 0.24, my - size * 0.24, size * 0.14, size * 0.07}, size * 0.02, rgb(27, 27, 27));
    painter.fill_rounded_rect({mx - size * 0.24, my + size * 0.17, size * 0.14, size * 0.07}, size * 0.02, rgb(27, 27, 27));
    painter.fill_rounded_rect({mx + size * 0.02, my - size * 0.09, size * 0.13, size * 0.18}, size * 0.05, rgb(30, 30, 30));
    disc(painter, mx + size * 0.08, my, size * 0.07, rgb(237, 199, 91));
    disc(painter, mx - size * 0.01, my, size * 0.05, rgb(251, 221, 125));
    disc(painter, mx + 0.0, my, size * 0.036, rgb(132, 104, 64));
    paint_polygon(painter, {{mx - size * 0.05, my - size * 0.02}, {mx - size * 0.09, my}, {mx - size * 0.05, my + size * 0.02}},
                  rgb(214, 128, 52));
}

} // namespace games::modules
