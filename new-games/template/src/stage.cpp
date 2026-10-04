#include "stage.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace tg {
namespace {

// Eases `value` toward `target`; lands exactly on it once the gap cannot be seen.
// Returns true while still moving.
bool ease(double& value, double target, double seconds, double rate) {
    if (value == target) {
        return false;
    }
    const double blend = 1 - std::exp(-seconds * rate);
    value += (target - value) * blend;
    if (std::abs(target - value) < .004) {
        value = target;
    }
    return value != target;
}

}  // namespace

bool inside(const Box& box, double x, double y) {
    return x >= box.x && y >= box.y && x < box.x + box.w && y < box.y + box.h;
}

bool overlap(const Box& a, const Box& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

Layout compute_layout(double width, double height) {
    Layout layout;
    layout.width = std::max(width, 120.0);
    layout.height = std::max(height, 120.0);
    layout.compact = layout.height < 440 || layout.width < 520;
    layout.type = layout.compact ? 11.5 : 13;
    layout.title = layout.compact ? 16 : 20;
    const double margin = layout.compact ? 10 : 18;
    const double strip = layout.compact ? 26 : 36;
    const double line = layout.compact ? 24 : 32;
    layout.status = {margin, margin * .5, layout.width - 2 * margin, strip};
    layout.message = {margin, layout.height - line - margin * .5, layout.width - 2 * margin, line};
    const double top = layout.status.y + layout.status.h + margin * .5;
    const double bottom = layout.message.y - margin * .5;
    const double room_h = std::max(bottom - top, 40.0);
    const double room_w = layout.width - 2 * margin;
    const double edge = std::floor(std::min(room_w, room_h));
    layout.board = {std::floor((layout.width - edge) * .5), std::floor(top + (room_h - edge) * .5),
                    edge, edge};
    const double panel_w = std::min(layout.width - 2 * margin, 520.0);
    const double panel_h = std::min(layout.height - 2 * margin, 330.0);
    layout.panel = {std::floor((layout.width - panel_w) * .5),
                    std::floor((layout.height - panel_h) * .5), panel_w, panel_h};
    return layout;
}

Box cell_box(const Layout& layout, int side, int cell) {
    const double pitch = layout.board.w / side;
    const int row = cell / side;
    const int column = cell % side;
    Box box;
    box.x = layout.board.x + column * pitch;
    box.y = layout.board.y + row * pitch;
    box.w = pitch;
    box.h = pitch;
    return box;
}

int pick_cell(const Layout& layout, int side, double x, double y) {
    if (!inside(layout.board, x, y)) {
        return -1;
    }
    const double pitch = layout.board.w / side;
    const int column = std::clamp(static_cast<int>((x - layout.board.x) / pitch), 0, side - 1);
    const int row = std::clamp(static_cast<int>((y - layout.board.y) / pitch), 0, side - 1);
    // Only the lamp's disc is a target, so a click between lamps does nothing.
    const double centre_x = layout.board.x + (column + .5) * pitch;
    const double centre_y = layout.board.y + (row + .5) * pitch;
    const double reach = pitch * .46;
    const double dx = x - centre_x;
    const double dy = y - centre_y;
    if (dx * dx + dy * dy > reach * reach) {
        return -1;
    }
    return row * side + column;
}

void snap(Visual& visual, const Board& board) {
    visual.glow.assign(board.lit.size(), 0);
    for (std::size_t index = 0; index < board.lit.size(); ++index) {
        visual.glow[index] = board.lit[index] != 0 ? 1 : 0;
    }
    visual.won = solved(board) ? 1 : 0;
}

bool advance(Visual& visual, const Board& board, double seconds, bool reduced) {
    if (visual.glow.size() != board.lit.size() || reduced) {
        // Reduced motion: the picture simply becomes the board. Nothing eases.
        const Visual before = visual;
        snap(visual, board);
        const bool changed = before.glow != visual.glow || before.won != visual.won;
        return changed;
    }
    bool moving = false;
    for (std::size_t index = 0; index < board.lit.size(); ++index) {
        const double target = board.lit[index] != 0 ? 1 : 0;
        const bool lamp_moving = ease(visual.glow[index], target, seconds, 14);
        moving = moving || lamp_moving;
    }
    const double won_target = solved(board) ? 1 : 0;
    const bool banner_moving = ease(visual.won, won_target, seconds, 6);
    moving = moving || banner_moving;
    return moving;
}

}  // namespace tg
