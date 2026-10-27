#pragma once
// An exhaustive solver for Nature Cube boards and the difficulty measures built on it.
// It plays by the rules in cube.hpp: lines step between neighbours or hop through a
// free portal pair, never share a cell, and need not fill the board.
//
// Counting every way to draw a line through open space would count wiggles, so the
// solver counts lines without needless detours: a line never runs beside an earlier
// part of itself (a path with no chords). Two solutions differ when some line takes a
// different route. A board whose count is one has exactly one sensible solution.
#include "cube.hpp"

#include <vector>

namespace ps_cube {

struct SolveLimits {
    long node_budget = 400000;   // steps tried before giving up; the count is then a floor
    int solution_cap = 64;       // stop counting here
    bool portals_as_stones = false;  // solve as if the portals were not there
    int keep = 1;                // solutions to record in SolveResult::found
    // False gives the plain solver used to rate boards: it tries steps in a fixed order
    // and notices a blocked pair only when it reaches it, as a person guessing would.
    bool lookahead = true;
    // Nonzero: take the pairs in an order and from ends drawn from this seed, instead of
    // shortest first from the tighter end.
    std::uint64_t order_seed = 0;
};

struct SolveResult {
    int solutions = 0;
    bool exact = false;     // the search finished: `solutions` is the full count (or the cap)
    long nodes = 0;         // steps tried
    long nodes_to_first = -1;  // steps tried before the first solution, -1 if none found
    std::vector<std::vector<int>> first;  // the first solution found, per pair
    std::vector<std::vector<std::vector<int>>> found;  // up to `keep` solutions, first first
};

[[nodiscard]] SolveResult solve(const Puzzle& puzzle, const SolveLimits& limits);

// What makes a board hard, measured.
struct Metrics {
    int solutions = 0;         // distinct solutions, up to the cap
    bool solutions_exact = false;
    // Wrong turns: steps a solver that sees blocked pairs takes before its first solution,
    // beyond the steps that solution needs. The median over several pair orders, as a
    // person might start anywhere.
    long backtracks = 0;
    int forced_starts = 0;     // endpoints with exactly one way out on the empty board
    int total_length = 0;      // cells covered by the witness lines
    int longest = 0;           // the witness's longest line, in cells
    int crossings = 0;         // witness steps that fold over an edge or hop through a portal
    int detour = 0;            // witness length beyond each pair's shortest route, summed
    int conflicts = 0;         // pairs whose shortest route runs into another pair's
    int stones = 0;
    int pairs = 0;
    int portals = 0;
    bool portal_needed = false;  // no solution exists if the portals are ignored
};

[[nodiscard]] Metrics measure(const Puzzle& puzzle, const SolveLimits& limits);
// The same, given the board's solution count and whether its portals are needed, already
// established under `limits`.
[[nodiscard]] Metrics measure_known(const Puzzle& puzzle, const SolveLimits& limits,
                                    const SolveResult& count, bool portal_needed);

// The shortest route between a pair's endpoints on the empty board (stones and other
// endpoints block; a portal is entered from a neighbour and left from its partner), or
// empty if there is none.
[[nodiscard]] std::vector<int> shortest_route(const Puzzle& puzzle, int pair);

}  // namespace ps_cube
