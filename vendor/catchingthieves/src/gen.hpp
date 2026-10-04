#pragma once
// Level generation, after Taylor and Parberry's method: carve a garden, put
// every pumpkin on a burrow, then search backwards by *pulling* pumpkins.
// Every position that search reaches can be solved by pushing them back, so a
// level is solvable by construction. The most interesting far position becomes
// the start, and the forward solver independently confirms it, measures its
// par and records a solution, which is replayed through the real rules.
#include "level.hpp"

#include <cstdint>
#include "cancellation.hpp"

namespace ct {

struct GenParams {
    int w = 7, h = 7;            // inner size of the garden (hedges excluded)
    int boxes = 3;
    int min_pushes = 10, max_pushes = 999;
    std::uint64_t seed = 1;
    long long reverse_budget = 250000;  // states explored backwards per attempt
    long long solve_budget = 3000000;
    int attempts = 300;
};

struct GenResult {
    bool ok = false;
    Level level;                 // carries its solution
    int pushes = 0;              // par: the fewest pushes possible
    int moves = 0;               // moves in the recorded solution
    int box_lines = 0;           // times the solution switches pumpkin (a difficulty sign)
    long long solver_nodes = 0;  // effort the forward solver needed
    int attempts = 0;
    int carve_rejects = 0, shallow = 0, unverified = 0;  // why attempts failed
    int deepest = 0;                                    // the deepest backward search seen
};

GenResult generate(const GenParams& p, CancellationToken stop = {});

// How often the solution switches from one pumpkin to another.
int box_switches(const Level& level, const std::string& lurd);

}  // namespace ct
