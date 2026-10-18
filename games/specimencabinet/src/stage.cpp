#include "stage.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

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
    const double ph = std::min(l.height - top - margin, l.compact ? 236.0 : 282.0);
    l.panel = Box{(l.width - pw) * .5, top + (l.height - top - ph) * .5, pw, ph};
    // The chooser: three sizes side by side, Cancel under them; the solved card's button.
    const double gap = l.compact ? 8 : 12;
    const double bw = (pw - 2 * 24 - gap * 2) / 3;
    const double bh = l.compact ? 70 : 86;
    const double by = l.panel.y + (l.compact ? 72 : 92);
    for (std::size_t k = 0; k < l.choose.size(); ++k)
        l.choose[k] = Box{l.panel.x + 24 + static_cast<double>(k) * (bw + gap), by, bw, bh};
    l.cancel = Box{l.panel.x + (pw - 120) * .5, by + bh + gap, 120, l.compact ? 28.0 : 32.0};
    l.again = Box{l.panel.x + (pw - 190) * .5, l.panel.y + (l.compact ? 120 : 146), 190, l.compact ? 30.0 : 36.0};
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

// Stones lie a half stone inside the felt, and clear of the counts at its foot.
double felt_foot(const Layout& layout) {
    return layout.type * 1.6;
}

void felt_fraction(const Layout& layout, double x, double y, double& fx, double& fy) {
    const double half = layout.stone * .5;
    const double w = std::max(1.0, layout.felt.w - 2 * half);
    const double h = std::max(1.0, layout.felt.h - 2 * half - felt_foot(layout));
    fx = std::clamp((x - layout.felt.x - half) / w, 0.0, 1.0);
    fy = std::clamp((y - layout.felt.y - half) / h, 0.0, 1.0);
}

void felt_point(const Layout& layout, double fx, double fy, double& x, double& y) {
    const double half = layout.stone * .5;
    x = layout.felt.x + half + std::clamp(fx, 0.0, 1.0) * std::max(1.0, layout.felt.w - 2 * half);
    y = layout.felt.y + half + std::clamp(fy, 0.0, 1.0) * std::max(1.0, layout.felt.h - 2 * half - felt_foot(layout));
}

void stone_point(const Layout& layout, const Session& session, int stone, double& x, double& y) {
    const Where& w = session.where[static_cast<std::size_t>(stone)];
    if (w.place == Place::cabinet) {
        const Box b = cell_box(layout, w.cell);
        x = b.x + b.w * .5;
        y = b.y + b.h * .5;
    } else if (w.place == Place::felt) {
        felt_point(layout, w.x, w.y, x, y);
    } else if (w.place == Place::pile) {
        const int spot = std::clamp(w.cell, 0, pile_size - 1);
        x = layout.pile[static_cast<std::size_t>(spot * 2)];
        y = layout.pile[static_cast<std::size_t>(spot * 2 + 1)];
    } else {
        x = layout.mouth_x;
        y = layout.mouth_y;
    }
}

int stone_under(const Layout& layout, const Session& session, double x, double y, const std::vector<int>& hidden) {
    // Drawn cabinet first, then the felt from the back, then the pile: the last drawn is on top.
    const int count = session.puzzle.n * session.puzzle.n;
    const double reach = layout.stone * .5;
    int best = -1;
    double best_rank = -1;
    for (int stone = 0; stone < count; ++stone) {
        const Where& w = session.where[static_cast<std::size_t>(stone)];
        if (w.place == Place::drum || !movable(session, stone) ||
            std::find(hidden.begin(), hidden.end(), stone) != hidden.end())
            continue;
        double sx = 0;
        double sy = 0;
        stone_point(layout, session, stone, sx, sy);
        if (std::hypot(x - sx, y - sy) > reach)
            continue;
        const double layer = w.place == Place::cabinet ? 0 : w.place == Place::felt ? 1 : 2;
        const double rank = layer * 100000 + sy;
        if (rank > best_rank) {
            best_rank = rank;
            best = stone;
        }
    }
    return best;
}

}  // namespace sc
