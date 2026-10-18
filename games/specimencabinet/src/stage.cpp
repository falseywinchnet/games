#include "stage.hpp"

#include <algorithm>
#include <cmath>

namespace sc {

bool inside(const Box& box, double x, double y) {
    return x >= box.x && y >= box.y && x < box.x + box.w && y < box.y + box.h;
}

bool overlap(const Box& a, const Box& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

Layout compute_layout(double width, double height, int n) {
    Layout l;
    l.width = std::max(width, 1.0);
    l.height = std::max(height, 1.0);
    l.n = std::clamp(n, 1, largest);
    l.compact = l.width < 760 || l.height < 520;
    const double margin = l.compact ? 8 : 12;
    const double top = capsule_clearance;
    const double room = std::max(40.0, l.height - top - margin);
    // The cabinet: as tall as the room allows, leaving the felt about two fifths.
    const double size = std::max(40.0, std::min(room, (l.width - 3 * margin) * .6));
    l.cabinet = Box{margin, top + (room - size) * .5, size, size};
    l.cell = size / l.n;
    l.stone = l.cell * .84;
    const double column = std::max(40.0, l.width - size - 3 * margin);
    // The felt starts a little below the capsule's line: the drum's mouth hangs over it.
    const double head = l.compact ? 14 : 24;
    l.felt = Box{l.cabinet.x + size + margin, top + head, column, std::max(40.0, room - head)};
    l.drum = std::min(column * .25, l.cell * 1.3);
    l.mouth_x = l.felt.x + l.felt.w * .5;
    l.mouth_y = l.felt.y + 6;
    // The pile: just below the mouth, its stones heaped close.
    const double px = l.mouth_x;
    const double py = l.mouth_y + l.drum * .35 + l.stone * .65;
    static constexpr double spots[pile_size * 2] = {-.5, .2, .45, .25, 0, .4, -.05, -.05};
    for (int k = 0; k < pile_size; ++k) {
        l.pile[static_cast<std::size_t>(k * 2)] = px + spots[k * 2] * l.stone;
        l.pile[static_cast<std::size_t>(k * 2 + 1)] = py + spots[k * 2 + 1] * l.stone;
    }
    const double pw = std::min(l.width - 2 * margin, l.compact ? 340.0 : 420.0);
    const double ph = std::min(l.height - top - margin, l.compact ? 220.0 : 260.0);
    l.panel = Box{(l.width - pw) * .5, top + (l.height - top - ph) * .5, pw, ph};
    l.type = l.compact ? 12 : 14;
    l.title = l.compact ? 18 : 24;
    return l;
}

Box cell_box(const Layout& layout, int cell) {
    const int row = cell / layout.n;
    const int column = cell % layout.n;
    return Box{layout.cabinet.x + column * layout.cell, layout.cabinet.y + row * layout.cell, layout.cell, layout.cell};
}

int pick_cell(const Layout& layout, double x, double y) {
    if (!inside(layout.cabinet, x, y))
        return -1;
    const int column = std::clamp(static_cast<int>((x - layout.cabinet.x) / layout.cell), 0, layout.n - 1);
    const int row = std::clamp(static_cast<int>((y - layout.cabinet.y) / layout.cell), 0, layout.n - 1);
    return row * layout.n + column;
}

void felt_fraction(const Layout& layout, double x, double y, double& fx, double& fy) {
    const double half = layout.stone * .5;
    const double w = std::max(1.0, layout.felt.w - 2 * half);
    const double h = std::max(1.0, layout.felt.h - 2 * half);
    fx = std::clamp((x - layout.felt.x - half) / w, 0.0, 1.0);
    fy = std::clamp((y - layout.felt.y - half) / h, 0.0, 1.0);
}

void felt_point(const Layout& layout, double fx, double fy, double& x, double& y) {
    const double half = layout.stone * .5;
    x = layout.felt.x + half + std::clamp(fx, 0.0, 1.0) * std::max(1.0, layout.felt.w - 2 * half);
    y = layout.felt.y + half + std::clamp(fy, 0.0, 1.0) * std::max(1.0, layout.felt.h - 2 * half);
}

}  // namespace sc
