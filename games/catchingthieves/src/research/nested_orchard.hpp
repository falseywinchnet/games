#pragma once
#include "forest_refine.hpp"
#include <cstdint>
namespace ct::research {
struct NestedOrchard {
    ForestCertificate certificate;
    int minimum_width=0;
    int components=0;
};
// Positive multiplicities, indexed outermost root to innermost root. Roots
// precede their children around the temporary cycle; child groups occur in
// reverse root order. The start is `unfinished` positions before the cyclic
// root-zero entrance; -1 starts at the first child. This sets minimum lifetime
// width directly, without a difficulty search.
// This constructor has no subset planner, puzzle solver, retry, or catalogue.
NestedOrchard nested_orchard(const std::vector<int>& multiplicity,std::uint64_t seed,bool root_pit=false,int unfinished=-1);
}
