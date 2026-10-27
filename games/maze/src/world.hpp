#pragma once
// Draws the maze around the camera: carpet, ceiling, brick (or posters, gold,
// with the snail's paint over them), doors sinking into the floor as they open, pads on
// floor and ceiling, the portal squares, the elevator plate, and everything
// that lives in it: bulbs, flip stones, the marble, the snail, the reward,
// and whoever you happen to meet.
#include "maze.hpp"
#include "residents.hpp"
#include "snail.hpp"
#include "soft3d.hpp"

#include <vector>

namespace mz {

struct Mover {          // the marble, between squares
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
    double lift = 0;    // how far above the floor it hovers
};

struct Dweller {        // a resident who keeps to one square
    bool alive = false;
    Pos at;
    double wait = 0;    // seconds until it next makes itself heard
    int pose = 0;
};

struct WorldState {
    std::vector<double> door_open;   // per door colour, 0 shut .. 1 sunk into the floor
    std::uint32_t pressed = 0;
    Mover marble;
    Snail snail;
    Visitor visitor;
    const Tex32* reward = nullptr;
    int reward_floor = 0;
    double elevator_lift = -1;       // 0..1 while an elevator carries you up (or 1..0 down)
    Pos elevator_at{-1, 0, 0};
    int floor = 0;                   // the floor being walked
    std::vector<bool> bulb_spent;    // per thing (unused for flip stones)
    Patches patches;                 // the snail's paint, surface by surface
    Ball ball = Ball::rock;          // what the giant marble is this level
    Dweller ceiling_cat, goose;      // upside down on the ceiling
    Dweller keyboard_cat;            // just beyond a portal
    Dweller raptor;                  // in the reward room, staring at the trophy
    bool monke_rides = false;        // Monke is on this elevator ride
};

V3 cell_center(Pos p, double z = 0);
void draw_world(Soft3D& r, const Level& lv, const WorldState& s, double t);
const Tex32& tex_bulb();

}  // namespace mz
