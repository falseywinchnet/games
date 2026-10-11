#pragma once
#include "port_graph.hpp"
#include <cstdint>
namespace ct::research {
struct OrchardFeatures { bool one_way=false,pit=false,raccoon=false; };
enum class OrchardLayout { spine,staircase,folded };
struct TreeOrchard {
    PortGraph space;
    std::vector<PushIntent> plan;
    std::vector<int> goal_parent;
    int start_junction=0,escape_cost=0,exact_pushes=0;
};
// A graph-first experimental family. Ordered goal trees plus a temporary
// preorder cycle. Its par is proved; aesthetic quality and tiering are not.
TreeOrchard tree_orchard(int pumpkins,std::uint64_t seed,OrchardFeatures features={},OrchardLayout layout=OrchardLayout::spine);
// Explicit construction domain for exhaustive geometry verification. Parents
// must be an ordered tree numbered in preorder, with parent[0] == 0.
TreeOrchard tree_orchard(const std::vector<int>& parents,int start,int horizontal_pitch,std::uint64_t schedule_seed,OrchardFeatures features={},OrchardLayout layout=OrchardLayout::spine);
// Spatial compiler shared by the tree and forest planners. Certificate and par
// obligations belong to those planners; drawing a graph alone proves neither.
// Diagnostic names do not participate in movement or embedding. Production
// may omit them; the inspection tools retain them by default.
PortGraph forest_space(const std::vector<int>& parents,int start,int horizontal_pitch,OrchardFeatures features={},OrchardLayout layout=OrchardLayout::spine,bool diagnostic_names=true);
// Folded geometry rotates the second bank of turnouts by half a turn. Push
// intentions remain in local turnout directions until the planner applies this.
int orchard_direction(int direction,int pumpkin,int count,OrchardLayout layout);
}
