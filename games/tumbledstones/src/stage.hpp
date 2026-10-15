#pragma once
// Where things are and how they move: the layout of the tray, its counts and its panels
// for a surface size, and the stones' movements between one position and the next (lifted
// out when pulled, falling into gaps, dropping in from the top, tumbling in a juggle). Pure
// geometry and easing in points and seconds: no pixels, no clock.
#include "rules.hpp"

#include <cstdint>
#include <vector>

namespace ts {

struct Box {
    double x = 0, y = 0, w = 0, h = 0;
    [[nodiscard]] bool contains(double px, double py) const {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
};

struct Layout {
    double width = 0, height = 0;   // the surface, points
    double cell = 40;               // a socket's pitch
    double grid_x = 0, grid_y = 0;  // the grid's top-left corner
    Box counts;                     // the column of counts beside the tray's grid
    double text = 15;               // the counts' type size
    // The panel for choosing a pull and the one for a win, and their buttons.
    Box panel;
    Box fewer, more, begin, cancel;
};

// The capsule floats over the top-left corner and Help over the top-right: the grid keeps
// clear of the right-hand corner and the counts start below the capsule.
[[nodiscard]] Layout compute_layout(double width, double height);
// The socket under a point, or -1.
[[nodiscard]] int pick_cell(const Layout& layout, double x, double y);
// A socket's centre.
void cell_centre(const Layout& layout, int cell, double& x, double& y);

// A stone as it is shown: where (in cells: column + 0.5, row + 0.5), how high off the
// wood, how large, how solid, turned how far in the tray's plane, squashed how far across
// (a stone turning over in flight), and which look it has.
struct Piece {
    std::uint32_t serial = 0;
    Stone look;                 // mineral and orientation shown
    Family family;              // and its family's shape
    Stone landing_look;         // what it shows once a juggle's halfway (a new orientation, or family)
    Family landing_family;
    double x = 0, y = 0;
    double lift = 0;            // 0 resting .. 1 held
    double scale = 1;
    double opacity = 1;
    double spin = 0;            // radians
    double squash = 1;          // 1 flat on, 0 edge on
    bool moving = false;        // drawn over the tray, not in the kept picture of it
    bool gone = false;          // pulled and faded out
    // The plan: up to three legs, one after another.
    struct Leg {
        int kind = 0;           // 0 none, 1 pull, 2 fall, 3 juggle
        double start = 0, end = 0;
        double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        double spin = 0;        // turns in flight
    };
    Leg legs[3];
    int leg_count = 0;
    bool landed = true;         // its last leg has ended (a click may sound)
};

// The stones on their way from one position to the next.
class Motion {
public:
    // Everything at rest where the board says.
    void settle(const Board& board);
    // A move's movements: the board `before` the move (with the stone set down or the
    // swap made as the player made it), what the rules said happened, and the board after.
    // Reduced motion settles at once.
    void plan(const Board& before, const Outcome& outcome, const Board& after, bool reduced);
    // Advances by `seconds`; true while anything still moves.
    bool advance(double seconds);
    [[nodiscard]] bool moving() const {
        return clock_ < finish_;
    }
    [[nodiscard]] const std::vector<Piece>& pieces() const {
        return pieces_;
    }
    // Sounds due since the last call: stones landing, the tumble starting; drained.
    struct Cue {
        int kind = 0;           // 1 a pull begins, 2 a stone lands, 3 a juggle begins, 4 stones settle after one
        int size = 0;           // a pull's size
        double x = 0;           // across the tray, 0..1, for the stereo
    };
    [[nodiscard]] std::vector<Cue> take_cues();
    // The serials whose pictures change as they move: for drawing and for the kept tray.
    [[nodiscard]] double clock() const {
        return clock_;
    }

private:
    std::vector<Piece> pieces_;
    std::vector<Cue> cues_;
    std::vector<double> pull_starts_;
    std::vector<int> pull_sizes_;
    double clock_ = 0;
    double finish_ = 0;
    double juggle_start_ = -1;
    double juggle_end_ = -1;
    bool juggle_cued_ = false;
    bool juggle_landed_ = false;
    std::size_t pulls_cued_ = 0;
    void place(Piece& piece) const;
};

// Timings, seconds.
inline constexpr double pull_time = .34;
inline constexpr double juggle_rise = .32;
inline constexpr double juggle_fly = .72;
inline constexpr double juggle_fall = .26;

}  // namespace ts
