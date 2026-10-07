#pragma once
// Nature Cube's rules: the board on three faces of a cube, the stones, the coloured
// endpoints, the portal pairs, and the paths the player draws. Pure and deterministic:
// no pixels, no clock, no files. The generator and solver live in their own files and
// use only what is declared here, so the rules they prove are the rules the player meets.
//
// A board has three visible faces of side x side cells, numbered face * side * side +
// row * side + column. Faces are the front (z = +1), the left (x = -1) and the top
// (y = -1) of a cube spanning -1..1, matching the original four-by-four game's layout.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ps_cube {

inline constexpr int faces = 3;
inline constexpr int minimum_side = 3;
inline constexpr int maximum_side = 7;
inline constexpr int maximum_pairs = 10;
inline constexpr int maximum_portals = 3;

struct Point3 {
    double x = 0;
    double y = 0;
    double z = 0;
};

// The cells and their neighbours for one face size. Cheap to build; boards keep one.
struct Geometry {
    int side = 4;
    int cells = 48;                            // faces * side * side
    std::vector<std::array<int, 4>> neighbours; // per cell, -1 where the board ends
    std::vector<int> neighbour_count;           // per cell
};
[[nodiscard]] Geometry make_geometry(int side);
// The centre of a cell on the cube's surface, and the face's outward normal.
[[nodiscard]] Point3 cell_center(int side, int cell);
[[nodiscard]] Point3 face_normal(int face);
// The face's in-plane axes: column grows along u, row along v.
void face_axes(int face, Point3& u, Point3& v);
[[nodiscard]] bool adjacent(const Geometry& geometry, int a, int b);

// What a cell holds before anyone draws.
enum class Tile : std::uint8_t { open, stone, endpoint, portal };

struct Puzzle {
    int side = 4;
    int level = 0;                 // 0 Easy, 1 Medium, 2 Hard
    std::uint64_t seed = 1;
    std::vector<Tile> tiles;       // per cell
    std::vector<int> pair_of;      // per cell: the pair whose endpoint it is, or -1
    std::vector<int> portal_of;    // per cell: the portal pair it belongs to, or -1
    std::vector<std::array<int, 2>> ends;    // per pair, its two endpoint cells
    std::vector<std::array<int, 2>> portals; // per portal pair, its two cells
    // One complete solution, per pair from ends[0] to ends[1]. A portal hop appears as
    // its two portal cells in a row. Every generated board carries one.
    std::vector<std::vector<int>> witness;
    Geometry geometry;
};

// Builds the per-cell tables from side, ends, portals and the stone list. Returns false
// when anything overlaps or lies outside the board.
[[nodiscard]] bool assemble(Puzzle& puzzle, const std::vector<int>& stones);
[[nodiscard]] int partner(const Puzzle& puzzle, int portal_cell);

// The player's lines. paths[pair] runs from the endpoint where it was started.
struct Play {
    std::vector<std::vector<int>> paths;
    int active = -1;  // the pair being traced, or -1
    int strokes = 0;  // presses that started or resumed a line: the score, fewer is better
    bool won = false;
};

[[nodiscard]] Play fresh_play(const Puzzle& puzzle);
// The pair whose line occupies a cell, or -1.
[[nodiscard]] int owner(const Play& play, int cell);
// The pair that has claimed a portal pair (its line runs through it), or -1.
[[nodiscard]] int portal_owner(const Puzzle& puzzle, const Play& play, int portal);
[[nodiscard]] bool complete(const Puzzle& puzzle, const std::vector<int>& path, int pair);
[[nodiscard]] bool all_connected(const Puzzle& puzzle, const Play& play);

// A press on a cell. An endpoint starts its pair's line afresh; a cell of an existing
// line cuts the line back to it and continues from there. Returns false, changing
// nothing, for anything else.
bool press(const Puzzle& puzzle, Play& play, int cell);
// The pointer moved onto a cell while tracing. Steps the active line onto it, through a
// portal if it is one, or cuts the line back if the cell is already on it. Returns
// false, changing nothing, when the step is not allowed.
bool extend(const Puzzle& puzzle, Play& play, int cell);
// Stops tracing. The line stays as drawn.
void release(Play& play);
// Removes a pair's line entirely.
bool erase(Play& play, int pair);

// A line is legal on the board: starts at the pair's endpoint, steps between
// neighbours or hops between the two cells of a portal pair (entered from a
// neighbour, left to a neighbour), never visits a cell twice, a stone, another
// endpoint, or a portal whose partner it does not take next.
[[nodiscard]] bool path_legal(const Puzzle& puzzle, const std::vector<int>& path, int pair);
// The whole play is consistent: legal lines that never share a cell.
[[nodiscard]] bool play_legal(const Puzzle& puzzle, const Play& play);
// The board is well formed and its witness is a complete solution under the real rules
// (replayed through press and extend).
[[nodiscard]] bool witness_valid(const Puzzle& puzzle);

// A splitmix64 step: integer-only, so every platform deals the same boards.
[[nodiscard]] std::uint64_t next_random(std::uint64_t& state);
[[nodiscard]] int random_below(std::uint64_t& state, int limit);

}  // namespace ps_cube
