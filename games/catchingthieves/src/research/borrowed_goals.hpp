#pragma once
#include "nested_orchard.hpp"
#include "tree_orchard.hpp"
namespace ct::research {
// A certificate transformation: chosen pumpkins begin on their ordinary goals,
// but every winning play must displace and restore each one. Eligibility is a
// graph theorem, not a solver result or a sampled-board filter.
struct BorrowedGoals {
    NestedOrchard orchard;
    std::vector<int> initial_goals;
    int minimum_goal_departures=0;
};
std::vector<int> borrowable_goals(const NestedOrchard&);
BorrowedGoals borrow_goals(NestedOrchard,const std::vector<int>& pumpkins);
// Apply before orienting the space. Returns original siding centers so the
// expansion compiler can retain the whole support halo after actors move.
std::vector<int> place_borrowed_goals(PortGraph&,const BorrowedGoals&,OrchardLayout);
}
