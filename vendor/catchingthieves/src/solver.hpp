#pragma once
// A push-optimal solver: breadth-first over pushes, with the bear's position
// reduced to the region he can walk to, and squares no pumpkin can leave
// pruned. It is the generator's independent check, the source of every
// level's par, and the hint.
#include "level.hpp"

#include <string>
#include "cancellation.hpp"

namespace ct {

struct SolveResult {
    bool solved = false;
    bool exhausted = false;   // gave up at the node limit (unknown, not unsolvable)
    int pushes = 0;           // the fewest pushes possible
    std::string lurd;         // a solution with that many pushes
    long long nodes = 0;      // states explored
};

// From the board's current position. `node_limit` caps the search.
SolveResult solve(const Board& board, long long node_limit = 2000000, CancellationToken stop = {});
SolveResult solve(const Level& level, long long node_limit = 2000000, CancellationToken stop = {});

}  // namespace ct
