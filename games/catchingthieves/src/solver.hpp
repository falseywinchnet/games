#pragma once
// Independent push-optimal solver used by hints and certificate validation.
// Classic and pit-only boards collapse effect-free walking regions. Gates and
// sentries use exact step transitions with walks costing zero and pushes one.
// Constructive generators can establish par without invoking this solver.
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

// From the board's current position. `node_limit` caps the search; a stop request ends it
// as exhausted (unknown, not unsolvable).
SolveResult solve(const Board& board, long long node_limit = 2000000, CancellationToken stop = {});
SolveResult solve(const Level& level, long long node_limit = 2000000, CancellationToken stop = {});

}  // namespace ct
