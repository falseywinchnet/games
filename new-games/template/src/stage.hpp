#pragma once
// Where everything goes, and what is moving. Pure geometry and easing: no
// pixels, no text, no clock of its own. Because it is pure, the tests can sweep
// every window size and prove the layout holds before anyone opens a window.
//
// All measurements are in points (the window's logical unit). The scene
// multiplies by the device scale when it draws.
#include "rules.hpp"

#include <vector>

namespace tg {

struct Box {
    double x = 0;
    double y = 0;
    double w = 0;
    double h = 0;
};

[[nodiscard]] bool inside(const Box& box, double x, double y);
[[nodiscard]] bool overlap(const Box& a, const Box& b);

struct Layout {
    double width = 0;     // the surface, in points
    double height = 0;
    bool compact = false; // a small window: slimmer strips, smaller type
    Box status;           // the strip above the board: the game's facts
    Box board;            // the square the lamps sit in
    Box message;          // one line under the board: what to do next
    Box panel;            // the help card, centred over everything
    double type = 13;     // body text size in points
    double title = 20;    // heading size in points
};

// The smallest surface a hosted game is given: the 600 x 420 window less the 50 point rail.
inline constexpr double minimum_width = 600;
inline constexpr double minimum_height = 370;

// Lays the game out for any surface. Sizes below the minimum still produce a usable,
// in-bounds layout (the open command capsule can leave as little as 600 x 320).
[[nodiscard]] Layout compute_layout(double width, double height);
[[nodiscard]] Box cell_box(const Layout& layout, int side, int cell);
// The lamp under a point, or -1.
[[nodiscard]] int pick_cell(const Layout& layout, int side, double x, double y);

// Everything that animates. The view owns one and advances it from its timer.
struct Visual {
    std::vector<double> glow;  // per lamp, 0 dark to 1 lit, easing toward the board
    double won = 0;            // 0 to 1 as the solved banner arrives
    int hover = -1;            // lamp under the pointer
    int cursor = -1;           // lamp chosen with the keyboard
    bool help = false;         // the help card is open
};

// Makes the visual state agree with a board at once (a new game, a load, reduced motion).
void snap(Visual& visual, const Board& board);
// Moves the visual state toward the board by `seconds`. Returns true while anything is
// still moving. When it returns false the picture is final and the view may stop its
// timer: this is what keeps an untouched game at zero CPU.
[[nodiscard]] bool advance(Visual& visual, const Board& board, double seconds, bool reduced);

}  // namespace tg
