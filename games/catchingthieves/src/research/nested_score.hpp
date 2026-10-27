#pragma once
#include "borrowed_goals.hpp"
namespace ct::research {
// Fully composed before spatial sites or Level cells exist. The certificate
// carries its mathematical bounds; rendering choices do not define the tier.
struct NestedScore {
    BorrowedGoals certificate;
    int difficulty=0;
    std::uint64_t seed=0,garden_seed=0;
    int quarter_turns=0;
    bool reflect=false,raccoon=false;
};
NestedScore compose_nested_score(int difficulty,std::uint64_t seed);
// Conditional actions and their access requirements, not an execution trace.
std::string score_graph_dot(const NestedScore&);
}
