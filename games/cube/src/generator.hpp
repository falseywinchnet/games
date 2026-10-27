#pragma once
// Deals Nature Cube boards. Every board is cut from lines laid over the open cells, so it
// is born with a solution (the witness); the solver then measures it and the generator
// keeps the board that best fits its difficulty.
//
//   Easy    four cells to a face, four or five pairs, a stone or two.
//   Medium  five to a face, six pairs, two or three walls of stone, and often one
//           portal pair placed beside the endpoint whose line needs it.
//   Hard    six to a face, eight pairs, longer walls and blocked corners, often a
//           portal; the board must have one solution without detours.
#include "cube.hpp"
#include "solver.hpp"

#include <atomic>
#include <cstdint>

namespace ps_cube {

inline constexpr int levels = 3;

struct Tier {
    int side = 4;
    int pairs = 5;
    int walls_min = 0;       // stone sections
    int walls_max = 0;
    int wall_length_max = 1;
    int portal_percent = 0;  // chance of a portal pair
    int second_portal_percent = 0;
    int minimum_line = 3;    // cells in the shortest witness line
    int solution_limit = 64; // accept a board with at most this many solutions
    long node_budget = 60000;
    int attempts = 24;
};
[[nodiscard]] Tier tier(int level);

struct GenerateOptions {
    // A first portal board for a player who has not met portals: one pair, beside an end.
    bool gentle_portal = false;
    // -1 lets the tier decide; 0 forbids portals; 1 asks for one.
    int portals = -1;
    // Set by the caller to stop early (the board is no longer wanted). The result is
    // then still a valid board, though maybe not the hardest found.
    const std::atomic<bool>* cancel = nullptr;
};

// Always returns a valid board with a witness. Deterministic in (level, seed, options).
[[nodiscard]] Puzzle generate(int level, std::uint64_t seed, const GenerateOptions& options);

}  // namespace ps_cube
