#pragma once
// The rules of the maze: one or two stacked floors of block walls and carpeted
// corridors. Doors stay shut until their coloured pad is pressed; pads on the
// ceiling can only be pressed while the world is flipped. Elevator tiles carry
// you between the floors, portals jump you across the maze, flip stones roll
// the world over, light bulbs switch the lights. The reward waits in a room at
// the end. Generation is seeded, and every level is proven solvable by a search
// over the whole state (floor, square, flipped or not, pads pressed). Pure and
// deterministic: no UI, no clock.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace mz {

enum Dir { kN = 0, kE = 1, kS = 2, kW = 3 };
constexpr int kDX[4] = {0, 1, 0, -1}, kDY[4] = {1, 0, -1, 0};  // +y is north

enum class Block : std::uint8_t { wall, open, door };

struct Cell {
    Block block = Block::wall;
    std::int8_t door = -1;     // a door's colour
    std::int8_t pad = -1;      // a floor pad's colour
    std::int8_t cpad = -1;     // a ceiling pad's colour
    std::int8_t portal = -1;   // a portal's pair
    bool elevator = false;
    bool goal = false;         // part of the reward room
    std::array<std::uint8_t, 4> face{};  // per side, the wall face's look: 0 brick, 1..63 a poster (id+1), 64+ a snail paint
};

struct Floor {
    int w = 0, h = 0;
    std::vector<Cell> cells;
    bool in(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }
    Cell& at(int x, int y) { return cells[static_cast<size_t>(y * w + x)]; }
    const Cell& at(int x, int y) const { return cells[static_cast<size_t>(y * w + x)]; }
    bool open(int x, int y) const { return in(x, y) && at(x, y).block == Block::open; }
};

struct Pos {
    int f = 0, x = 0, y = 0;
    bool operator==(const Pos& o) const { return f == o.f && x == o.x && y == o.y; }
};

enum class ThingKind : std::uint8_t { bulb, flip };
struct Thing {
    ThingKind kind;
    Pos at;
};

struct LevelParams {
    int cells = 6;          // the maze is cells x cells corridors (2*cells+1 blocks a side)
    int floors = 1;         // 2: an elevator joins a second maze directly above
    int doors = 0;
    int ceiling_doors = 0;  // of those, how many have their pad on the ceiling (needs a flip stone)
    int portals = 0;        // shortcut pairs
    bool sealed_portal = false;  // the way on is walled off: only a portal crosses
    int bulbs = 0;
    bool marble = false, snail = false;
    double braid = .25;     // how many dead ends are knocked through into loops
    std::uint64_t seed = 1;
};

struct Level {
    LevelParams params;
    std::vector<Floor> floors;
    Pos start;
    int start_dir = kN;
    Pos goal;                       // where the reward stands
    std::vector<std::pair<Pos, Pos>> portals;
    std::vector<Thing> things;
    Pos marble{-1, 0, 0}, snail{-1, 0, 0};
    int carpet = 0, ceiling = 0;
    int optimal_steps = 0;          // the fewest steps to the reward (proven by the search)
    bool has_portal(Pos p) const;
    Pos portal_twin(Pos p) const;
    int thing_at(Pos p) const;      // index into things, or -1
    const Cell& cell(Pos p) const { return floors[static_cast<size_t>(p.f)].at(p.x, p.y); }
    Cell& cell(Pos p) { return floors[static_cast<size_t>(p.f)].at(p.x, p.y); }
    bool open(Pos p) const { return p.f >= 0 && p.f < static_cast<int>(floors.size()) && floors[static_cast<size_t>(p.f)].open(p.x, p.y); }
};

struct Play;

// The level's parameters grow with its number: new things arrive one at a time, then mix.
LevelParams params_for(int number, std::uint64_t seed);
Level generate(const LevelParams& p);

// Shortest number of steps from the start to the reward, or -1 if it cannot be reached.
int solve_steps(const Level& lv);
// The world directions of one shortest route from `from` (in state `play`) to the reward; empty if none.
std::vector<int> solve_route(const Level& lv, const Play& from);

// The live state of play, shared by the view and the tests.
struct Play {
    Pos at;
    int dir = kN;
    bool flipped = false;
    std::uint32_t pressed = 0;   // pads pressed (bit per colour)
    bool blackout = false;
    int steps = 0;
    bool won = false;
    bool door_open(int color) const { return color >= 0 && (pressed >> color & 1); }
};

enum class StepResult { moved, blocked_wall, blocked_door };
struct Arrival {           // what happened on arriving at a square
    bool pressed = false;  int pad = -1;
    bool portal = false;   Pos portal_from;
    bool elevator = false; int from_floor = 0;
    bool flipped = false, bulb = false, won = false;
};
// Try to walk one square in world direction `d`. On success fills `arr` with the arrival's consequences.
StepResult step(const Level& lv, Play& play, int d, Arrival& arr);
bool passable(const Level& lv, const Play& play, Pos p);

}  // namespace mz
