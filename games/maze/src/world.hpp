#pragma once
// Draws the maze around the camera: carpet, ceiling, brick (or posters, gold,
// or the snail's paint), doors sinking into the floor as they open, pads on
// floor and ceiling, the portal squares, the elevator plate, and everything
// that lives in it: bulbs, flip stones, the marble, the snail, the reward,
// and whoever you happen to meet.
#include "maze.hpp"
#include "soft3d.hpp"

#include <vector>

namespace mz {

struct Mover {          // the marble or the snail, between squares
    bool alive = false;
    Pos at, to;
    double t = 0;       // 0..1 along the way from `at` to `to`
    int dir = 0;
    double roll = 0;    // the marble's accumulated roll
    V3 world() const;
};

struct Visitor {        // someone met in the corridors
    bool alive = false;
    Pos at;
    const Tex32* tex = nullptr;
    double appear = 0;  // 0..1 fading in (and out)
    double bob = 0;
    double w = .7, h = .9;
};

struct WorldState {
    std::vector<double> door_open;   // per door colour, 0 shut .. 1 sunk into the floor
    std::uint32_t pressed = 0;
    Mover marble, snail;
    Visitor visitor;
    const Tex32* reward = nullptr;
    int reward_floor = 0;
    double elevator_lift = -1;       // 0..1 while an elevator carries you up (or 1..0 down)
    Pos elevator_at{-1, 0, 0};
    int floor = 0;                   // the floor being walked
    std::vector<bool> bulb_spent;    // per thing (unused for flip stones)
};

V3 cell_center(Pos p, double z = 0);
void draw_world(Soft3D& r, const Level& lv, const WorldState& s, double t);
const Tex32& tex_bulb();
const Tex32& tex_snail();

}  // namespace mz
