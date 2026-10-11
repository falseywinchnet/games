#include "nested_grow.hpp"
#include "nested_score.hpp"
#include "nested_realization.hpp"
#include "garden_space.hpp"
#include <algorithm>
#include <stdexcept>
namespace ct::research {
Grown nested_grow(int difficulty,std::uint64_t seed,CancellationToken stop,int max_tries) {
    static_cast<void>(max_tries);
    if(difficulty<kEasy||difficulty>kHard||stop.stop_requested())return {};
    auto score=compose_nested_score(difficulty,seed);
    const auto& certificate=score.certificate;const auto& c=certificate.orchard.certificate;
    const bool pit=c.root_pit,thief=score.raccoon;
    if(stop.stop_requested())return {};
    int n=static_cast<int>(c.shape.parent.size());
    auto realized=realize_nested_score(score);
    if(stop.stop_requested())return {};
    auto level=expand_turnout_garden(std::move(realized.level),score.garden_seed,score.quarter_turns%2,realized.centers).level;
    if(stop.stop_requested())return {};
    Grown out;out.entry.level=std::move(level);out.entry.difficulty=difficulty;
    out.entry.seed="graph-v2 "+std::to_string(seed);out.entry.nodes=0;out.entry.par=c.exact_pushes;
    const char* tier=difficulty==kEasy?"Easy":difficulty==kMedium?"Medium":"Hard";
    out.entry.section=std::string("Endless ")+tier;
    out.entry.level.title=difficulty==kEasy?"The Returning Orchard":difficulty==kMedium?"The Borrowed Crossing":"The Unfinished Garden";
    // These are properties of the actual emitted board and checked push plan.
    // Replaying twice merely to recount them is unnecessary runtime work. Tests
    // independently compare every field with measure() through the Board rules.
    auto& m=out.metrics;m.pumpkins=n;m.burrows=n-static_cast<int>(pit);m.pushes=c.exact_pushes;
    m.moves=static_cast<int>(out.entry.level.solution.size());m.one_way=1;m.pits=pit;m.raccoons=thief;
    for(auto tile:out.entry.level.tiles)m.floors+=tile==Tile::floor;
    int previous=-1;for(auto p:c.plan){m.switches+=p.pumpkin!=previous;previous=p.pumpkin;}
    out.entry.switches=m.switches;out.ok=true;return out;
}
}
