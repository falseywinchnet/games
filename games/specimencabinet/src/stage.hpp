#pragma once
// Where everything goes. Pure geometry: no pixels, no text, no clock of its own, so the
// tests can sweep every window size and prove the layout holds before a window opens.
//
// All measurements are in points (the window's logical unit). The scene multiplies by
// the device scale when it draws.
//
// The cabinet stands at the left. At the right, a strip of felt runs down the window; the
// tumbler's mouth hangs over its head, the drum running off beyond the window's top, and
// the stones it drops lie in a small pile just below the mouth.
#include "rules.hpp"

#include <array>
#include <vector>

namespace sc {

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
    int n = 5;
    bool compact = false; // a small window: tighter margins, smaller type
    Box cabinet;          // the sockets' square
    double cell = 64;     // a socket's pitch
    Box felt;             // the strip of felt
    double mouth_x = 0;   // the tumbler's mouth, its centre
    double mouth_y = 0;
    double drum = 40;     // and its radius
    double stone = 50;    // a stone's size, across
    std::array<double, pile_size * 2> pile{};  // where the pile's stones lie, x then y
    Box panel;            // the chooser and the solved card, centred over everything
    std::array<Box, sizes.size()> choose{};  // the chooser's buttons, one a size
    Box cancel;           // the chooser's way back to the cabinet under way
    Box again;            // the solved card's button for another cabinet
    double type = 13;     // body text size in points
    double title = 20;    // heading size in points
};

// The smallest surface a hosted game is given: the 600 x 420 window less the 50 point rail.
inline constexpr double minimum_width = 600;
inline constexpr double minimum_height = 370;
// The capsule and Help float over the top of the surface: the layout keeps below them.
inline constexpr double capsule_clearance = 54;

// Lays the game out for any surface and cabinet size. Sizes below the minimum still
// produce a usable, in-bounds layout.
[[nodiscard]] Layout compute_layout(double width, double height, int n);
[[nodiscard]] Box cell_box(const Layout& layout, int cell);
// The socket under a point, or -1.
[[nodiscard]] int pick_cell(const Layout& layout, double x, double y);
// A point on the felt as fractions of the strip (0..1 each), and back, keeping a stone's
// half width inside it.
void felt_fraction(const Layout& layout, double x, double y, double& fx, double& fy);
void felt_point(const Layout& layout, double fx, double fy, double& x, double& y);
// Where a stone lies, its centre in points: in its socket, on the felt or on the pile. A
// stone in the tumbler is at its mouth.
void stone_point(const Layout& layout, const Session& session, int stone, double& x, double& y);
// The stone a press at (x, y) would take: the topmost movable stone there, or -1. Stones
// in `hidden` (falling, still on their way) are passed over.
[[nodiscard]] int stone_under(const Layout& layout, const Session& session, double x, double y,
                              const std::vector<int>& hidden);

}  // namespace sc
