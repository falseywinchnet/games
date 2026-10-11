#include "garden_score.hpp"
#include <stdexcept>
namespace ct::research {
namespace {
struct Random {
    std::uint64_t state;
    std::uint64_t next(){state+=0x9e3779b97f4a7c15ULL;auto z=state;z=(z^(z>>30))*0xbf58476d1ce4e5b9ULL;z=(z^(z>>27))*0x94d049bb133111ebULL;return z^(z>>31);}
};
}
GardenScore compose_garden_score(int difficulty,std::uint64_t seed) {
    if(difficulty<kEasy||difficulty>kHard)throw std::invalid_argument("invalid garden score tier");
    GardenScore out;out.difficulty=difficulty;out.seed=seed;Random random{seed};
    if(difficulty==kEasy) {
        constexpr int directions[]{kLeft,kDown,kRight};
        int row=int(random.next()%3),direction=directions[random.next()%3];
        int distance=1+int(random.next()%(direction==kLeft?2:3));int start=int(random.next()%distance);
        auto s=compose_terminal_order(row,direction,distance,start);out.pushes=s.pushes;out.minimum_width=1;out.minimum_goal_departures=0;
        out.construction=std::move(s);
    } else {
        StagePassage p;
        if(difficulty==kMedium) {
            p.span=1+int(random.next()%3);p.start=1+int(random.next()%p.span);p.goal=int(random.next()%p.start);
        } else {
            p.span=int(random.next()%4);p.start=int(random.next()%(p.span+1));p.goal=int(random.next()%(p.span+1));
        }
        auto s=compose_shared_stage(true,true,difficulty==kHard,p,difficulty==kHard);
        out.pushes=s.pushes;out.minimum_width=s.minimum_width;out.minimum_goal_departures=s.minimum_goal_departures;out.construction=std::move(s);
    }
    out.quarter_turns=int(random.next()%4);out.reflect=random.next()%2;return out;
}
Level compile_garden_score(const GardenScore& s) {
    if(auto simple=std::get_if<TerminalOrderScore>(&s.construction))return compile_terminal_order(*simple,s.quarter_turns,s.reflect);
    return compile_shared_stage(std::get<SharedStageScore>(s.construction),s.quarter_turns,s.reflect);
}
std::string garden_score_dot(const GardenScore& s) {
    if(auto simple=std::get_if<TerminalOrderScore>(&s.construction))return terminal_order_dot(*simple);
    return shared_stage_dot(std::get<SharedStageScore>(s.construction));
}
Grown composed_grow(int difficulty,std::uint64_t seed,CancellationToken stop,int max_tries) {
    static_cast<void>(max_tries);
    if(difficulty<kEasy||difficulty>kHard||stop.stop_requested())return {};
    auto score=compose_garden_score(difficulty,seed);if(stop.stop_requested())return {};
    auto lv=compile_garden_score(score);if(stop.stop_requested())return {};
    Grown out;out.entry.level=std::move(lv);out.entry.difficulty=difficulty;out.entry.seed="graph-v3 "+std::to_string(seed);
    const char* tier=difficulty==kEasy?"Easy":difficulty==kMedium?"Medium":"Hard";
    out.entry.section=std::string("Endless ")+tier;out.entry.par=score.pushes;
    if(difficulty==kEasy)out.entry.level.title="The Last Burrow";
    else if(difficulty==kMedium)out.entry.level.title="The Borrowed Crossing";
    else {
        const auto& p=std::get<SharedStageScore>(score.construction).passage;
        out.entry.level.title=p.goal>p.start?"The Goal You Must Reopen":p.goal<p.start?"Away Before Home":"The Borrowed Garden";
    }
    auto& m=out.metrics;m.pumpkins=int(out.entry.level.boxes.size());m.pits=difficulty>=kMedium;m.raccoons=difficulty==kHard;
    m.burrows=m.pumpkins-m.pits;m.pushes=score.pushes;m.moves=int(out.entry.level.solution.size());m.one_way=1;
    for(auto tile:out.entry.level.tiles)m.floors+=tile==Tile::floor;
    int previous=-1;
    std::visit([&](const auto& s){for(auto p:s.plan){m.switches+=p.pumpkin!=previous;previous=p.pumpkin;}},score.construction);
    out.entry.switches=m.switches;if(stop.stop_requested())return {};out.ok=true;return out;
}
}
