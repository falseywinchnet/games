#pragma once
// The paint snail. It crawls over every surface of its floor of the maze, the carpet, the
// walls and the ceiling, never in a straight line, and paints a stripe a little wider than
// itself wherever it goes, with a line of slime down the middle that glistens and dries. It
// pays you no mind until you come close; then it pulls into its shell, and peeks out again
// once you have gone. It is drawn in the round, facing the way it crawls.
#include "maze.hpp"
#include "soft3d.hpp"

#include <cstdint>
#include <map>
#include <vector>

namespace mz {

// A unit square of surface: a square's floor or ceiling, or one of its wall faces.
struct Surface {
    int f = 0, x = 0, y = 0;
    int side = 0;  // 0 floor, 1 ceiling, 2 + d the wall on side d
    bool operator==(const Surface& o) const { return f == o.f && x == o.x && y == o.y && side == o.side; }
    bool operator<(const Surface& o) const {
        return f != o.f ? f < o.f : y != o.y ? y < o.y : x != o.x ? x < o.x : side < o.side;
    }
};

// Where a surface lies: its corner, the unit axes u and v run along, and its normal into the open square.
// On a wall u runs left to right as seen from the square and v from the floor up.
struct Frame {
    V3 o, u, v, n;
};
Frame surface_frame(const Surface& s);

constexpr int kPatch = 64;  // paint texels across a surface
struct Patch {               // what the snail has left on one surface
    std::vector<std::uint8_t> paint = std::vector<std::uint8_t>(kPatch * kPatch, 0);  // a paint's index + 1; 0 is bare
    std::vector<float> slime = std::vector<float>(kPatch * kPatch, -1e9f);           // when slime was laid (snail time)
    mutable Tex32 shown;                                                              // composed for drawing
    mutable double shown_t = -1;
};
using Patches = std::map<Surface, Patch>;

struct Snail {
    bool alive = false;
    Surface on;
    double u = .5, v = .5;   // where on its surface
    double heading = 0;      // which way, in radians in the surface's u, v plane
    double hide = 0;         // 0 out and crawling .. 1 shut in its shell
    double away = 0;         // how long the viewer has kept their distance
    double clock = 0;        // its own time
    double paint_t = 0;      // when it next changes paint
    double squelch_t = 0;    // when it next squelches
    int paint = 0;           // the paint it carries
    std::uint32_t rng = 1;
    double turn1 = 0, turn2 = 0;  // where its meander starts
    V3 world() const;
    V3 forward() const;
    V3 normal() const;
};

struct SnailNews {
    bool painted = false, squelched = false, hid = false, peeked = false;
};

// Sets the snail down on the floor of `at`, facing any way.
void snail_place(Snail& s, Pos at, std::uint32_t seed);
// One step of its life: crawls and paints, or hides from `viewer` when it comes close.
SnailNews snail_step(Snail& s, const Level& lv, Patches& patches, V3 viewer, double dt);

void draw_snail(Soft3D& r, const Snail& s);
// The paint on the surfaces of floor `f`, composed afresh for time `t` (the snail's clock dries the slime).
void draw_patches(Soft3D& r, const Patches& patches, int f, double t, double snail_clock);

}  // namespace mz
