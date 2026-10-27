#pragma once
#include "tiers.hpp"
namespace ct::research {
// Retained graph-v2 engine for regression and timing comparisons. Public grow
// now uses garden_score.hpp. No candidate rejection or puzzle-state search.
Grown nested_grow(int difficulty,std::uint64_t seed,CancellationToken stop={},int max_tries=40);
}
