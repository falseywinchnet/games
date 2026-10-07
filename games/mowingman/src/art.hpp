#pragma once
// Everything in the garden that is not grass, seen a little from the front as well as
// from above: the ground is drawn from straight up and squashed by `tilt`, and anything
// with height stands up the screen by `rise` of its height. Things are built as stacks
// of their ground shape from the bottom up, so they have sides, then a top. The light
// is from the upper left (shadows fall down and to the right). Positions are lawn
// metres; a Frame maps them to raster pixels.
#include "garden.hpp"
#include "platform/raster.hpp"
#include "sim.hpp"

namespace mm {

struct Frame {
    double ppm{};         // pixels per metre along the ground
    double ox{};          // raster position of the lawn's origin
    double oy{};
    double tilt{0.78};    // the ground's depth is drawn this much shorter than its width
    double rise{0.62};    // pixels, per ppm, a metre of height stands up the screen
    [[nodiscard]] double px(double x) const {
        return ox + x * ppm;
    }
    [[nodiscard]] double py(double y) const {
        return oy + y * ppm * tilt;
    }
    [[nodiscard]] double up(double height) const {
        return height * ppm * rise;
    }
};

constexpr double wall_height = 1.5;  // the garden walls, metres
constexpr double trunk_height = 1.3; // where a tree's canopy begins, metres

// The world outside the lawn: the sky, a brick wall along the back and down each
// side, a paved terrace in front. Drawn once.
void draw_surround(Canvas& canvas, const Frame& frame);
// Into a transparent layer laid over the lawn: a bed's edging stones, a prop (shed,
// greenhouse, fountain, birdbath, grill, sandbox, pool, lawn chair), a tree's trunk.
void draw_bed(Canvas& canvas, const Frame& frame, const Bed& bed);
void draw_prop(Canvas& canvas, const Frame& frame, const Prop& prop);
void draw_tree_foot(Canvas& canvas, const Frame& frame, const Tree& tree);
// Every frame: what moves on a prop.
void draw_prop_live(Canvas& canvas, const Frame& frame, const Prop& prop, double time);

struct MowerPose {
    double x{};
    double y{};
    double heading{};
    double shake{};      // engine vibration, 0..1
    double eggy_look{};  // Eggy's head turn, radians
    bool held{};         // someone has the controls (Eggy hangs on)
};
void draw_mower(Canvas& canvas, const Frame& frame, const MowerPose& pose, Livery livery, double time);
void draw_gnome(Canvas& canvas, const Frame& frame, const Gnome& gnome, double time);
void draw_granny(Canvas& canvas, const Frame& frame, const Granny& granny);
void draw_bee(Canvas& canvas, const Frame& frame, const Bee& bee, double time);
void draw_bird(Canvas& canvas, const Frame& frame, const Bird& bird);
// While the old lady is after the mower: how long is left to dodge her, as a stopwatch
// at the top of the lawn and a shrinking clock over her head.
void draw_chase_timer(Canvas& canvas, const Frame& frame, const Granny& granny, double time);
void draw_particles(Canvas& canvas, const Frame& frame, const std::vector<Particle>& particles);

} // namespace mm
