#include "restricted_garden.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>
namespace ct::research {
namespace {
// The tier grammar and router already certify this trace. Record its footprint
// without simulating all turn effects a second time, then perform the same
// spine/cut operation as WitnessSpace with bounded scratch storage.
int carve_known_performance(StageDrawing& drawing,std::string_view trace,std::uint64_t seed) {
    auto& g=drawing.graph;const int n=int(g.sites.size());
    if(n<1||n>64)throw std::invalid_argument("tier restriction graph exceeds its certified domain");
    auto bit=[](int v){return std::uint64_t{1}<<v;};std::uint64_t spine=bit(g.player);int at=g.player;
    for(char c:trace){int d=dir_of(c);if(d<0)throw std::logic_error("tier certificate direction invalid");int to=g.sites[at].port[d];
        if(to<0)throw std::logic_error("tier certificate leaves its graph");spine|=bit(to);
        if(c==kPush[d]){int end=g.sites[to].port[d];if(end<0)throw std::logic_error("tier push has no destination");spine|=bit(end);}at=to;}
    std::array<int,64> parent,queue,map;parent.fill(-1);map.fill(-1);int count=0;
    for(int v=0;v<n;++v)if(spine&bit(v)){queue[count++]=v;parent[v]=v;}
    for(int head=0;head<count;++head)for(int to:g.sites[queue[head]].port)if(to>=0&&parent[to]<0){parent[to]=queue[head];queue[count++]=to;}
    if(count!=n)throw std::logic_error("tier source disconnected");
    auto protect=[&](int v){while(!(spine&bit(v))){spine|=bit(v);v=parent[v];}};
    for(int v:g.pumpkins)protect(v);for(int v:g.raccoons)protect(v);
    for(int v=0;v<n;++v)if(g.sites[v].goal||g.sites[v].pit||g.sites[v].gate)protect(v);
    auto next=[&](){auto x=(seed+=0x9e3779b97f4a7c15ULL);x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;x=(x^(x>>27))*0x94d049bb133111ebULL;return x^(x>>31);};
    std::uint64_t blocked=0;for(int v=0;v<n;++v)if(!(spine&bit(v))&&(next()&1))blocked|=bit(v);
    std::uint64_t seen=bit(g.player);queue[0]=g.player;count=1;
    for(int head=0;head<count;++head)for(int to:g.sites[queue[head]].port)if(to>=0&&!((seen|blocked)&bit(to))){seen|=bit(to);queue[count++]=to;}
    if(spine&~seen)throw std::logic_error("tier restriction disconnected its spine");
    int kept=0;for(int v=0;v<n;++v)if(seen&bit(v))map[v]=kept++;
    for(int v=0;v<n;++v)if(map[v]>=0){auto site=g.sites[v];for(int& to:site.port)if(to>=0)to=map[to];g.sites[map[v]]=site;drawing.points[map[v]]=drawing.points[v];}
    g.sites.resize(kept);drawing.points.resize(kept);g.names.clear();g.player=map[g.player];
    for(int& v:g.pumpkins)v=map[v];for(int& v:g.raccoons)v=map[v];
    int guards=0;for(auto [bear,victim]:g.guards)if(map[bear]>=0&&map[victim]>=0)g.guards[guards++]={map[bear],map[victim]};g.guards.resize(guards);
    return n-kept;
}
}
RestrictedGardenScore restrict_garden_score(GardenScore source,std::uint64_t spatial_seed) {
    RestrictedGardenScore out;out.source=std::move(source);auto& s=out.source;
    std::vector<PushIntent> plan;
    if(auto terminal=std::get_if<TerminalOrderScore>(&s.construction)){out.drawing=draw_terminal_order(*terminal);plan=terminal->plan;}
    else{auto& stage=std::get<SharedStageScore>(s.construction);out.drawing=draw_shared_stage(stage,false);plan=stage.plan;}
    orient(out.drawing.graph,plan,s.quarter_turns,s.reflect);
    for(auto& [x,y]:out.drawing.points){if(s.reflect)x=-x;for(int i=0;i<s.quarter_turns;++i){int previous=x;x=-y;y=previous;}}
    out.trace=route_plan(out.drawing.graph,plan);out.removed=carve_known_performance(out.drawing,out.trace,spatial_seed);return out;
}
RestrictedGardenScore compose_restricted_garden(int difficulty,std::uint64_t seed) {
    return restrict_garden_score(compose_garden_score(difficulty,seed),seed^0x746965722d737061ULL);
}
Level compile_restricted_garden(const RestrictedGardenScore& s) {
    const auto& d=s.drawing;const auto& g=d.graph;
    int minx=d.points[0].first,maxx=minx,miny=d.points[0].second,maxy=miny;
    for(auto [x,y]:d.points){minx=std::min(minx,x);maxx=std::max(maxx,x);miny=std::min(miny,y);maxy=std::max(maxy,y);}
    Level lv;lv.w=maxx-minx+3;lv.h=maxy-miny+3;
    lv.tiles.assign(lv.w*lv.h,Tile::wall);lv.goal.assign(lv.tiles.size(),0);lv.pit.assign(lv.tiles.size(),0);lv.gate.assign(lv.tiles.size(),0);
    auto cell=[&](int site){auto [x,y]=d.points[site];return lv.idx(x-minx+1,y-miny+1);};
    for(int v=0;v<int(g.sites.size());++v){int c=cell(v);lv.tiles[c]=Tile::floor;lv.goal[c]=g.sites[v].goal;lv.pit[c]=g.sites[v].pit;lv.gate[c]=g.sites[v].gate;}
    lv.player=cell(g.player);for(int v:g.pumpkins)lv.boxes.push_back(cell(v));for(int v:g.raccoons)lv.raccoons.push_back(cell(v));lv.solution=s.trace;return lv;
}
Grown restricted_grow(int difficulty,std::uint64_t seed,CancellationToken stop,int max_tries) {
    static_cast<void>(max_tries);if(difficulty<kEasy||difficulty>kHard||stop.stop_requested())return {};
    auto score=compose_restricted_garden(difficulty,seed);if(stop.stop_requested())return {};
    Grown out;out.entry.level=compile_restricted_garden(score);out.entry.difficulty=difficulty;out.entry.seed="graph-v4 "+std::to_string(seed);
    out.entry.section=std::string("Endless ")+(difficulty==kEasy?"Easy":difficulty==kMedium?"Medium":"Hard");
    out.entry.level.title=difficulty==kEasy?"The Last Passage":difficulty==kMedium?"The Borrowed Path":"The Shared Returns";
    out.entry.par=score.source.pushes;auto& m=out.metrics;const auto& g=score.drawing.graph;
    m.floors=int(g.sites.size());m.pumpkins=int(g.pumpkins.size());m.raccoons=int(g.raccoons.size());
    for(auto site:g.sites){m.burrows+=site.goal;m.pits+=site.pit;m.one_way+=site.gate!=0;}
    m.pushes=score.source.pushes;m.moves=int(score.trace.size());int previous=-1;
    std::visit([&](const auto& source){for(auto push:source.plan){m.switches+=push.pumpkin!=previous;previous=push.pumpkin;}},score.source.construction);
    out.entry.switches=m.switches;if(stop.stop_requested())return {};out.ok=true;return out;
}
}
