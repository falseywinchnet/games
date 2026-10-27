#pragma once
// Where the cube sits and how it moves: pure geometry and easing, no pixels and no
// clock of its own. The view advances a Motion by elapsed seconds; the scene reads it.
//
//   Arrival     the cube turns in from the side and grows to its place, then its
//               stones rise out of the glass and the coloured ends and portals open.
//   Tracing     each new step of a line grows out of the last cell; a joined pair
//               sends a ripple of light along its line.
//   Completion  the cube flashes and its coloured ends pulse, every line lights in
//               turn, then the lines unwind back into the squares they began from, the
//               glass goes dark, and the next pattern comes up in the same cube.
//
// With reduced motion every change lands at once; a finished board rests lit for a
// moment (one timed wake, no frames) before the next one replaces it.
#include "cube.hpp"

#include <cstdint>
#include <vector>

namespace ps_cube {

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
    double top_clear = 0; // a margin above the cube (the capsule has its own rail)
    Box board;            // the square the cube is drawn in
    Box panel;            // the top scores card
};
[[nodiscard]] Layout compute_layout(double width, double height);

// The cube's resting tilt and the range the pointer may lean it through, in radians.
inline constexpr double rest_yaw = .75;
inline constexpr double rest_pitch = -.56;

struct Pose {
    double yaw = rest_yaw;
    double pitch = rest_pitch;
    double scale = 1;   // of the cube, 1 at rest
    double lift = 0;    // vertical offset, in board heights
    double drift = 0;   // horizontal offset, in board widths
    double opacity = 1;
};

enum class Phase { resting, arriving, lighting, unwinding, darkening, departing, holding, departed };

struct Motion {
    Phase phase = Phase::resting;
    double clock = 0;           // seconds into the current phase
    double yaw = rest_yaw;      // the tilt the pointer has eased the cube to
    double pitch = rest_pitch;
    double target_yaw = rest_yaw;
    double target_pitch = rest_pitch;
    bool departed = false;      // the finished cube has left; the view brings the next
    bool dark = false;          // it ended dark, not gone: the next pattern comes up in it
    bool in_place = false;      // this arrival is in the cube already standing there
    std::vector<int> drawn;     // per pair, line length last seen
    std::vector<double> grow;   // per pair, 0..1, how far the newest step has grown
    std::vector<double> ripple; // per pair, seconds since it was joined, or -1
    std::vector<std::uint8_t> joined;  // per pair, joined when last seen
};

// Starts the arrival of a board (or shows it at once with reduced motion).
void begin_arrival(Motion& motion, const Puzzle& puzzle, bool reduced);
// Starts the finale: lines light, then the cube spins away.
void begin_completion(Motion& motion, bool reduced);
// The cube leaves at once (a new board was asked for).
void begin_departure(Motion& motion, bool reduced);
// Notes the lines as drawn now, so a new step grows and a joined pair ripples.
void note_lines(Motion& motion, const Puzzle& puzzle, const Play& play, bool reduced);
// Points the cube's tilt; it eases there as it advances.
void lean(Motion& motion, double yaw, double pitch);
// Holds the tilt where it is (while the player traces).
void hold_tilt(Motion& motion);
// Moves everything on by `seconds`. True while anything is still moving: when it
// returns false the picture is final and the view may stop its timer.
[[nodiscard]] bool advance(Motion& motion, const Puzzle& puzzle, const Play& play, double seconds,
                           bool reduced);
// Seconds until a timed change with nothing moving meanwhile, or -1.
[[nodiscard]] double seconds_until_event(const Motion& motion);

// What the scene asks.
[[nodiscard]] Pose pose(const Motion& motion);
// 0 sunk into the glass, 1 standing; a stone rises with a small bounce.
[[nodiscard]] double stone_rise(const Motion& motion, const Puzzle& puzzle, int cell);
// 0 closed, 1 open: coloured ends and portals bloom after the stones.
[[nodiscard]] double tile_bloom(const Motion& motion, const Puzzle& puzzle, int cell);
// How much of a pair's line still shows, 0..1, as the finale unwinds it toward its start.
[[nodiscard]] double line_shown(const Motion& motion, int pair);
// The finale's flash of the whole cube, 0..1.
[[nodiscard]] double cube_flash(const Motion& motion);
// How far the glass has gone dark, 0..1: at the end of the finale, and lifting as the next
// pattern comes up.
[[nodiscard]] double cube_dark(const Motion& motion);
// The coloured ends pulsing when the board is solved, 0..1.
[[nodiscard]] double end_flash(const Motion& motion);
// How bright a line glows at a point `along` it (0 start, 1 end): the ripple of a pair
// just joined, or the finale's light.
[[nodiscard]] double line_glow(const Motion& motion, int pair, double along);
[[nodiscard]] double growth(const Motion& motion, int pair);

// Durations, in seconds.
inline constexpr double arrival_seconds = 1.7;
inline constexpr double lighting_seconds = 1.5;
inline constexpr double departure_seconds = .75;
inline constexpr double hold_seconds = 1.2;
inline constexpr double step_seconds = .09;
inline constexpr double ripple_seconds = .7;
inline constexpr double unwind_seconds = .6;    // each line; the pairs start a little apart
inline constexpr double unwind_stagger = .08;
inline constexpr double dark_seconds = .6;
inline constexpr double undark_seconds = .9;

}  // namespace ps_cube
