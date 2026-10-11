#pragma once
#include "level.hpp"
#include <cstdint>
#include <vector>
namespace ct::research {
struct GardenSpace {
    Level level;
    // -1 means rigid/unassigned. A nonnegative value names a walking component
    // of the original grid outside every potential pumpkin's support halo.
    std::vector<int> region;
    int added_floor=0;
};
// Expand walking-only regions without changing the available push sequences.
// Special squares and every possible guarding position stay rigid. Expanded
// regions are undirected, pumpkin-free, uniformly unguarded walking space.
// The bounded growth process never rejects or retries a seed.
GardenSpace expand_garden(const Level&,std::uint64_t seed,int rounds=4);
// Independent postcondition: checks region connectivity and boundary incidence,
// unchanged mechanics, and isolation of every possible pumpkin trajectory.
bool check_expansion(const Level& original,const GardenSpace&);
// Specialized emitter for the proved five-cell turnout family. Every pumpkin
// has an explicit siding center, and `vertical` describes the global siding
// axis. Omit centers only when all pumpkins start at their centers. It uses that construction certificate instead of recomputing possible
// pumpkin trajectories. The generic checker remains an independent audit.
GardenSpace expand_turnout_garden(const Level&,std::uint64_t seed,bool vertical,
    const std::vector<int>& centers={});
GardenSpace expand_turnout_garden(Level&&,std::uint64_t seed,bool vertical,
    const std::vector<int>& centers={});
}
