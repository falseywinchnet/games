#pragma once
#include "nested_score.hpp"
namespace ct::research {
struct NestedRealization {
    Level level;
    std::vector<int> centers; // original turnout centers in the emitted grid
};
// Lower a composed score, retaining the constructor's checked placement.
// Geometry is checked during construction; the routed trace is still checked
// through GraphState. Does not expand walking space or select/retry a seed.
NestedRealization realize_nested_score(const NestedScore&);
}
