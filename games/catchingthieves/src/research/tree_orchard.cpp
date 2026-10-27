#include "tree_orchard.hpp"
#include <algorithm>
#include <deque>
#include <functional>
#include <stdexcept>
namespace ct::research {
PortGraph folded_forest_space(const std::vector<int>&,int,int,OrchardFeatures,bool);
int orchard_direction(int direction,int pumpkin,int count,OrchardLayout layout) {
    return layout==OrchardLayout::folded&&pumpkin>=(count+1)/2?(direction+2)%4:direction;
}
namespace {
struct Random {
    std::uint64_t s;
    std::uint64_t next(){s+=0x9e3779b97f4a7c15ULL;auto z=s;z=(z^(z>>30))*0xbf58476d1ce4e5b9ULL;z=(z^(z>>27))*0x94d049bb133111ebULL;return z^(z>>31);}
};
void connect(PortGraph& g,int from,int to,const std::vector<int>& directions,[[maybe_unused]] const std::string& name) {
    if(directions.empty())throw std::logic_error("empty orchard road");
    for(std::size_t i=0;i+1<directions.size();++i)from=g.extend(from,directions[i],{});
    g.join(from,to,directions.back());
}
void repeat(std::vector<int>& out,int direction,int count){out.insert(out.end(),count,direction);}
}
TreeOrchard tree_orchard(int count,std::uint64_t seed,OrchardFeatures features,OrchardLayout layout) {
    if(count<1||count>6)throw std::invalid_argument("tree orchard requires 1..6 pumpkins");
    Random rng{seed};std::vector<int> parents(count),stack{0};
    // Choose an ordered tree constructively, maintaining a preorder ancestor
    // stack. Every value selects a legal parent; no board or seed is rejected.
    for(int v=1;v<count;++v) {
        stack.resize(1+rng.next()%stack.size());parents[v]=stack.back();stack.push_back(v);
    }
    int start=count==1?0:1+static_cast<int>(rng.next()%(count-1));
    int pitch=4+static_cast<int>(rng.next()%3);
    return tree_orchard(parents,start,pitch,rng.next(),features,layout);
}
TreeOrchard tree_orchard(const std::vector<int>& parents,int start,int pitch,std::uint64_t schedule_seed,OrchardFeatures features,OrchardLayout layout) {
    int count=static_cast<int>(parents.size());
    if(count<1||count>6||parents[0]!=0||start<0||start>=count||pitch<4||pitch>6)throw std::invalid_argument("invalid orchard construction domain");
    if((features.pit&&(count<2||start==0))||(features.raccoon&&!features.pit))throw std::invalid_argument("sacrificial root requires a non-root start and a pit");
    std::vector<int> stack{0};
    for(int v=1;v<count;++v) {
        auto it=std::find(stack.begin(),stack.end(),parents[v]);
        if(it==stack.end())throw std::invalid_argument("goal tree is not in preorder");
        stack.erase(it+1,stack.end());stack.push_back(v);
    }
    TreeOrchard out;out.goal_parent=parents;out.start_junction=start;Random rng{schedule_seed};
    std::vector<std::vector<int>> children(count);
    for(int v=1;v<count;++v)children[out.goal_parent[v]].push_back(v);
    // The graph certificate is constructed before any spatial graph or map.
    const int inf=100000;std::vector<int> distance(count,inf),previous(count,-1),mode(count),setting(count);
    std::deque<int> queue{out.start_junction};distance[out.start_junction]=0;
    while(!queue.empty()) {
        int u=queue.front();queue.pop_front();
        auto visit=[&](int v,int m,int cost) {
            if(distance[u]+cost>=distance[v])return;
            distance[v]=distance[u]+cost;previous[v]=u;mode[v]=m;
            if(cost)queue.push_back(v);else queue.push_front(v);
        };
        visit((u+1)%count,1,1);
        for(int v:children[u])visit(v,2,0);
    }
    out.escape_cost=distance[0];out.exact_pushes=count+2*out.escape_cost-(features.pit?1:0);
    std::vector<int> path;
    for(int v=0;v!=out.start_junction;v=previous[v]) {
        if(previous[v]<0)throw std::logic_error("temporary cycle failed to reach root");path.push_back(v);
    }
    std::reverse(path.begin(),path.end());
    auto select=[&](int v,int m) {
        if(setting[v]==m)return;
        int pushes=setting[v]?2:1;for(int p=0;p<pushes;++p)out.plan.push_back({v,m==1?kRight:kLeft});setting[v]=m;
    };
    for(int v:path)select(v,mode[v]);
    if(features.pit)out.plan.push_back({0,kRight}); // Any ordinary pumpkin can fill this pit.
    else select(0,2);
    std::function<void(int)> finish=[&](int p) {
        auto order=children[p];
        for(int i=static_cast<int>(order.size())-1;i>0;--i)std::swap(order[i],order[rng.next()%(i+1)]);
        for(int v:order){select(v,2);finish(v);}
    };
    finish(0);
    if(static_cast<int>(out.plan.size())!=out.exact_pushes)throw std::logic_error("orchard cost certificate mismatch");

    out.space=forest_space(parents,start,pitch,features,layout);
    for(auto& push:out.plan)push.direction=orchard_direction(push.direction,push.pumpkin,count,layout);
    return out;
}
// Internal source drawing may exceed the game rectangle: folded_space copies
// its local kernels and constructs entirely new roads before embedding.
PortGraph orchard_source_space(const std::vector<int>& parents,int start,int pitch,OrchardFeatures features,OrchardLayout layout,int limit) {
    int count=static_cast<int>(parents.size());
    if(count<1||count>limit||start<0||start>=count||pitch<4||pitch>6)throw std::invalid_argument("invalid forest layout domain");
    if((features.pit&&(count<2||start==0))||(features.raccoon&&!features.pit))throw std::invalid_argument("invalid forest hazard domain");
    std::vector<std::vector<int>> children(count);
    for(int v=0;v<count;++v) {
        if(parents[v]<0||parents[v]>v)throw std::invalid_argument("goal predecessor must be earlier or self");
        if(parents[v]!=v)children[parents[v]].push_back(v);
        for(int w=v+1;w<count;++w)if(parents[v]<parents[w]&&parents[w]<v)throw std::invalid_argument("crossing goal roads");
    }

    // Realize this outerplanar family as a port graph. Inputs have terminal
    // support cells approached from above, so pumpkins cannot leave a turnout.
    // A final self-loop needs two more columns than a leaf turnout. Reserve
    // those columns when choosing spacing, before constructing any geometry.
    const int dx=count==6&&parents.back()==count-1?std::min(pitch,5):pitch,dy=6;
    const bool spine=layout==OrchardLayout::spine;
    PortGraph g;g.sites.reserve(count*80);g.pumpkins.reserve(count);
    struct Ports {int front,back,fork,goal,initial,park,back_cell,back_stem,continuation;};std::vector<Ports> ports;
    for(int v=0;v<count;++v) {
        auto name="turnout."+std::to_string(v)+".";
        int front=g.add(name+"temporary_support");
        int goal=g.extend(front,kRight,name+"goal");g.sites[goal].goal=!(v==0&&features.pit);
        int initial=g.extend(goal,kRight,name+"initial");g.pumpkins.push_back(initial);
        int park=g.extend(initial,kRight,name+"park");
        int back=g.extend(park,kRight,name+"goal_support");
        if(v==0&&features.pit)g.sites[back].pit=true;
        int fstem=g.extend(front,kUp,name+"temporary_stem");int fhub=g.extend(fstem,kUp,name+"temporary_input");
        int bstem=g.extend(back,kUp,name+"goal_stem");int bhub=g.extend(bstem,kUp,name+"goal_input");
        int continuation=g.extend(initial,kDown,name+"continuation");int fork=g.extend(continuation,kDown,name+"junction");
        ports.push_back({fhub,bhub,fork,goal,initial,park,back,bstem,continuation});
    }
    g.player=ports[start].fork;
    for(int v=0;v+1<count;++v) {
        std::vector<int> road;repeat(road,kDown,2);repeat(road,spine?kLeft:kRight,spine?2:dx-2);
        connect(g,ports[v].fork,ports[v+1].front,road,"temporary."+std::to_string(v));
    }
    std::vector<int> outside;repeat(outside,kDown,2);repeat(outside,kLeft,spine?4:dx*(count-1)+4);repeat(outside,kUp,dy*count);repeat(outside,kRight,2);
    connect(g,ports.back().fork,ports.front().front,outside,"temporary.wrap");
    if(features.one_way)for(const auto& p:ports)g.sites[g.sites[p.fork].port[kDown]].gate=kDown+1;
    for(int p=0;p<count;++p) {
        // Nesting gives a crossing-free arc layout: an enclosing interval has
        // a larger span, hence a farther track. Disjoint intervals reuse tracks.
        auto track=[&](int child){return spine?4+(pitch-2)*(child-p):dx*(child-p)+2;};
        int length=children[p].empty()?0:track(children[p].back());if(parents[p]==p)length=std::max(length,4);
        std::vector<int> highway(length+1,-1);highway[0]=ports[p].fork;
        int begin=1;
        if(p==0&&features.pit){highway[2]=g.add("goal.fork.0.2");begin=3;} // A wall at offset 1 makes the pit an access cut.
        for(int i=begin;i<=length;++i)highway[i]=g.extend(highway[i-1],kRight,{});
        for(int child:children[p]) {
            std::vector<int> road(dy*(child-p)-4,kDown);
            if(spine)repeat(road,kLeft,track(child)-2);
            connect(g,highway[track(child)],ports[child].back,road,"goal.branch."+std::to_string(child));
        }
        if(parents[p]==p) {
            std::vector<int> loop(4,kUp);repeat(loop,kLeft,2);
            connect(g,highway[4],ports[p].back,loop,"goal.root_loop."+std::to_string(p));
        }
    }
    if(features.raccoon) {
        const auto& p=ports[0];int r=g.extend(p.park,kDown,"root.raccoon");g.join(p.continuation,r,kRight);g.raccoons.push_back(r);
        // Declare geometry-sensitive guarding before embedding. The compiler
        // checks this exact relation, including diagonals across walls.
        for(int bear:{p.initial,p.park,p.back_cell,p.back_stem,p.continuation,r})g.guards.emplace_back(bear,p.park);
        for(int bear:{p.goal,p.initial,p.park,p.continuation,p.fork,r})g.guards.emplace_back(bear,p.continuation);
    }
    return g;
}
PortGraph forest_space(const std::vector<int>& parents,int start,int pitch,OrchardFeatures features,OrchardLayout layout,bool diagnostic_names) {
    if(layout==OrchardLayout::folded)return folded_forest_space(parents,start,pitch,features,diagnostic_names);
    auto graph=orchard_source_space(parents,start,pitch,features,layout,6);
    if(!diagnostic_names)graph.names.clear();
    return graph;
}
}
