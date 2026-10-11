#include "port_graph.hpp"
#include <algorithm>
#include <bit>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace ct::research {
namespace {
void require(bool value,const char* message) { if(!value)throw std::logic_error(message); }
int transformed(int d,int turns,bool reflect) {
    if(reflect && (d==kLeft||d==kRight))d=(d+2)%4;
    return (d+turns)%4;
}
}
int PortGraph::add(std::string name) {
    add_unnamed();
    if(!name.empty()){names.resize(sites.size());names.back()=std::move(name);}
    return static_cast<int>(sites.size())-1;
}
const std::string& PortGraph::name(int site) const {
    static const std::string empty;
    return site>=0&&site<static_cast<int>(names.size())?names[site]:empty;
}
int PortGraph::find_site(std::string_view wanted) const {
    // Empty names are not identifiers, including implicit unnamed road sites.
    if(wanted.empty())return -1;
    auto it=std::find(names.begin(),names.end(),wanted);
    return it==names.end()?-1:static_cast<int>(it-names.begin());
}
void PortGraph::join(int from,int to,int d) {
    require(from>=0&&to>=0&&from<static_cast<int>(sites.size())&&to<static_cast<int>(sites.size())&&from!=to&&d>=0&&d<4,"invalid graph edge");
    require(sites[from].port[d]<0&&sites[to].port[(d+2)%4]<0,"occupied graph port");
    sites[from].port[d]=to;sites[to].port[(d+2)%4]=from;
}
int PortGraph::extend(int from,int d,std::string name) {
    int to=add(std::move(name));join(from,to,d);return to;
}
bool PortGraph::edge(int from,int d) const {
    if(from<0||from>=static_cast<int>(sites.size())||d<0||d>=4)return false;
    int to=sites[from].port[d];
    return to>=0&&(!sites[from].gate||sites[from].gate==d+1)&&(!sites[to].gate||sites[to].gate==d+1);
}
GraphState::GraphState(const PortGraph& g):graph(&g),player(g.player),pumpkins(g.pumpkins),occupied(g.sites.size(),-1),filled(g.sites.size()),departed(g.raccoons.size()) {
    for(std::size_t i=0;i<pumpkins.size();++i)occupied[pumpkins[i]]=static_cast<int>(i);
}
int GraphState::box_at(int site) const {
    return site>=0&&site<static_cast<int>(occupied.size())?occupied[site]:-1;
}
bool GraphState::sentry_at(int site) const {
    for(std::size_t i=0;i<graph->raccoons.size();++i)if(!departed[i]&&graph->raccoons[i]==site)return true;
    return false;
}
bool GraphState::walkable(int site) const {
    return site>=0&&(!graph->sites[site].pit||filled[site])&&box_at(site)<0&&!sentry_at(site);
}
bool GraphState::move(int d,bool* pushed) {
    if(pushed)*pushed=false;
    if(!graph->edge(player,d))return false;
    int next=graph->sites[player].port[d],box=box_at(next);
    if(sentry_at(next))return false;
    if(box>=0) {
        if(!graph->edge(next,d))return false;
        int to=graph->sites[next].port[d];
        if(box_at(to)>=0||sentry_at(to))return false;
        occupied[next]=-1;
        if(graph->sites[to].pit&&!filled[to]){pumpkins[box]=-1;filled[to]=1;}
        else {pumpkins[box]=to;occupied[to]=box;}
        if(pushed)*pushed=true;
    } else if(!walkable(next))return false;
    player=next;
    for(std::size_t r=0;r<graph->raccoons.size();++r)if(!departed[r]) {
        int sentry=graph->raccoons[r];
        for(int k=0;k<4;++k) {
            int site=graph->sites[sentry].port[k],victim=box_at(site);
            if(victim<0||graph->sites[site].goal)continue;
            if(std::find(graph->guards.begin(),graph->guards.end(),std::pair{player,site})!=graph->guards.end())continue;
            pumpkins[victim]=-2;occupied[site]=-1;departed[r]=1;break;
        }
    }
    return true;
}
bool GraphState::solved() const {
    for(std::size_t i=0;i<graph->sites.size();++i)if(graph->sites[i].goal&&box_at(static_cast<int>(i))<0)return false;
    return true;
}

Embedding embed(const PortGraph& g,int turns,bool reflect) {
    require(turns>=0&&turns<4&&!g.sites.empty(),"invalid embedding request");
    for(const auto& site:g.sites)require(site.gate>=0&&site.gate<=4,"invalid graph gate direction");
    // The game breaks simultaneous theft ties in world U/R/D/L order. Rotating
    // a sentry graph without rebuilding its transition certificate is unsound.
    require(g.raccoons.empty()||(!turns&&!reflect),"sentry tie order needs a new certificate after rotation");
    using Point=std::pair<int,int>;
    std::vector<Point> point(g.sites.size());std::vector<unsigned char> placed(g.sites.size());
    std::vector<int> queue{0};queue.reserve(g.sites.size());placed[0]=true;
    // A valid 40x40 garden has offsets in [-39,39] relative to any of its
    // sites. Direct indexing avoids a tree lookup for every geometric edge.
    constexpr int side=83,origin=41;
    std::array<int,side*side> owner;owner.fill(-1);
    auto slot=[](int x,int y){require(x>=-40&&x<=40&&y>=-40&&y<=40,"graph exceeds garden dimensions");return (y+origin)*side+x+origin;};
    owner[slot(0,0)]=0;
    for(std::size_t head=0;head<queue.size();++head) {
        int from=queue[head];auto [x,y]=point[from];
        for(int d=0;d<4;++d)if(int to=g.sites[from].port[d];to>=0) {
            require(to<static_cast<int>(g.sites.size())&&g.sites[to].port[(d+2)%4]==from,"nonreciprocal graph edge");
            int td=transformed(d,turns,reflect);Point p{x+kDX[td],y+kDY[td]};
            if(placed[to])require(point[to]==p,"inconsistent graph cycle geometry");
            else {
                int index=slot(p.first,p.second);require(owner[index]<0,"distinct graph sites collide");
                point[to]=p;placed[to]=true;queue.push_back(to);owner[index]=to;
            }
        }
    }
    require(queue.size()==g.sites.size(),"disconnected spatial graph");
    int minx=0,miny=0,maxx=0,maxy=0;
    for(std::size_t i=0;i<point.size();++i) {
        auto [x,y]=point[i];minx=std::min(minx,x);miny=std::min(miny,y);maxx=std::max(maxx,x);maxy=std::max(maxy,y);
        for(int d=0;d<4;++d) {
            int td=transformed(d,turns,reflect);
            int actual=owner[slot(x+kDX[td],y+kDY[td])];
            require(actual==g.sites[i].port[d],"geometry introduced an undeclared adjacency");
        }
    }
    // Theft guarding includes diagonals even when the two intervening cells are
    // walls. Ordinary walking adjacency cannot represent this relation.
    std::vector<std::pair<int,int>> expected;
    for(int r:g.raccoons) {
        require(r>=0&&r<static_cast<int>(g.sites.size()),"invalid sentry site");
        for(int victim:g.sites[r].port)if(victim>=0)
            for(std::size_t bear=0;bear<point.size();++bear)
                if(std::abs(point[bear].first-point[victim].first)<=1&&std::abs(point[bear].second-point[victim].second)<=1)
                    expected.emplace_back(static_cast<int>(bear),victim);
    }
    auto normalize=[](auto& pairs){std::sort(pairs.begin(),pairs.end());pairs.erase(std::unique(pairs.begin(),pairs.end()),pairs.end());};
    auto guards=g.guards;normalize(expected);normalize(guards);
    require(expected==guards,"geometry changed the guarding relation");
    Embedding e;auto& lv=e.level;lv.w=maxx-minx+3;lv.h=maxy-miny+3;
    require(lv.w<=40&&lv.h<=40,"graph exceeds garden dimensions");
    lv.tiles.assign(lv.w*lv.h,Tile::wall);lv.goal.assign(lv.tiles.size(),0);lv.pit.assign(lv.tiles.size(),0);lv.gate.assign(lv.tiles.size(),0);
    e.cell_for_site.reserve(point.size());int goals=0,pits=0;
    for(std::size_t i=0;i<point.size();++i) {
        auto [x,y]=point[i];int c=lv.idx(x-minx+1,y-miny+1);e.cell_for_site.push_back(c);
        lv.tiles[c]=Tile::floor;lv.goal[c]=g.sites[i].goal;lv.pit[c]=g.sites[i].pit;
        require(!(g.sites[i].goal&&g.sites[i].pit),"pit overlaps a goal");
        goals+=g.sites[i].goal;pits+=g.sites[i].pit;
        if(g.sites[i].gate)lv.gate[c]=transformed(g.sites[i].gate-1,turns,reflect)+1;
    }
    auto cell=[&](int s){require(s>=0&&s<static_cast<int>(point.size()),"invalid actor site");return e.cell_for_site[s];};
    // Finish Level::valid's obligations while visiting the sparse actors.
    // Dimensions, vector sizes, boundary walls and overlay ranges were already
    // checked or established above; rescanning the full rectangle is redundant.
    std::fill(placed.begin(),placed.end(),0);
    auto actor=[&](int site) {
        int c=cell(site);
        require(!g.sites[site].pit&&!placed[site],"invalid or overlapping graph actor");
        placed[site]=1;return c;
    };
    lv.player=actor(g.player);
    lv.boxes.reserve(g.pumpkins.size());for(int b:g.pumpkins)lv.boxes.push_back(actor(b));
    lv.raccoons.reserve(g.raccoons.size());for(int r:g.raccoons) {
        int c=actor(r);require(!g.sites[r].goal&&!g.sites[r].gate,"sentry overlaps an incompatible overlay");lv.raccoons.push_back(c);
    }
    require(goals>0&&static_cast<int>(lv.boxes.size())>=goals&&lv.boxes.size()<=static_cast<std::size_t>(goals+pits)+lv.raccoons.size(),"invalid graph goal inventory");
    return e;
}

namespace {
int departure_count(const GraphState& state) {
    return int(std::count(state.departed.begin(),state.departed.end(),1));
}
bool reaction(GraphState& state,const ActionIntent& action,std::string& result) {
    if(action.kind==ActionIntent::Kind::push)return false;
    if(action.kind==ActionIntent::Kind::walk) {
        require(action.direction>=0&&action.direction<4&&action.pumpkin==-1&&action.raccoon==-1,"invalid plain walk intent");
        const int departed=departure_count(state);
        bool pushed=false;
        require(state.move(action.direction,&pushed)&&!pushed,"plain walk must be a legal walk");
        require(departure_count(state)==departed,"plain walk caused an unprescribed effect");
        result+=kWalk[action.direction];return true;
    }
    require(action.kind==ActionIntent::Kind::theft_walk,"invalid action kind");
    require(action.direction>=0&&action.direction<4&&action.pumpkin>=0&&
        action.pumpkin<int(state.pumpkins.size())&&action.raccoon>=0&&
        action.raccoon<int(state.departed.size()),"invalid theft intent");
    require(state.pumpkins[action.pumpkin]>=0&&!state.departed[action.raccoon],"theft resource already consumed");
    const int departed=departure_count(state);
    bool pushed=false;
    require(state.move(action.direction,&pushed)&&!pushed,"theft action must be a legal walk");
    // Without a push, only theft changes resources. Each theft removes one
    // live pumpkin and monotonically departs one squirrel. Exactly one new
    // departure plus the named victim/squirrel therefore checks the full effect.
    require(state.pumpkins[action.pumpkin]==-2&&state.departed[action.raccoon]&&
        departure_count(state)==departed+1,"theft action has different consequences");
    result+=kWalk[action.direction];return true;
}
template<class Intent> void prescribed_push(GraphState& state,const Intent& action,std::string& result) {
    // Legacy push plans deliberately allow push-induced theft. Explicit action
    // scores separate pushes from reactions and reject such hidden effects.
    int departed=0;
    if constexpr(std::is_same_v<Intent,ActionIntent>)departed=departure_count(state);
    bool pushed;require(state.move(action.direction,&pushed)&&pushed,"push certificate failed");
    if constexpr(std::is_same_v<Intent,ActionIntent>)require(departure_count(state)==departed,"push caused an unprescribed theft");
    result+=kPush[action.direction];
}
// Fixed-size masks keep the 64-site specialization scalar and use two native
// words for larger gardens. Shifts always have a count in [0,63], including
// exact 64/128-site boundaries. No compiler-specific 128-bit integer is needed.
template<int Words> struct SiteMask {
    std::array<std::uint64_t,Words> words{};
    void set(int site) {words[site/64]|=std::uint64_t(1)<<(site%64);}
    void clear(int site) {words[site/64]&=~(std::uint64_t(1)<<(site%64));}
    static SiteMask bit(int site) {SiteMask out;out.set(site);return out;}
    bool contains(int site) const {return words[site/64]&(std::uint64_t(1)<<(site%64));}
    explicit operator bool() const {for(auto word:words)if(word)return true;return false;}
    SiteMask& operator|=(SiteMask rhs) {for(int i=0;i<Words;++i)words[i]|=rhs.words[i];return *this;}
    SiteMask& operator&=(SiteMask rhs) {for(int i=0;i<Words;++i)words[i]&=rhs.words[i];return *this;}
    friend SiteMask operator|(SiteMask a,SiteMask b) {return a|=b;}
    friend SiteMask operator&(SiteMask a,SiteMask b) {return a&=b;}
    friend SiteMask operator~(SiteMask a) {for(auto& word:a.words)word=~word;return a;}
    template<class Visit> void each(Visit visit) const {
        for(int i=0;i<Words;++i)for(auto left=words[i];left;left&=left-1)visit(64*i+std::countr_zero(left));
    }
};
// Orthogonal spaces up to 128 sites fit in one or two words. Compile edges
// once; only pumpkin occupancy, filled pits and departed sentries vary during
// routing. Layered reverse BFS preserves the existing U/R/D/L shortest-walk tie
// order. Pushes and threatened walks still use the full GraphState semantics.
template<int Words,class Intent> std::string route_compact(const PortGraph& g,const std::vector<Intent>& plan) {
    using Mask=SiteMask<Words>;constexpr int capacity=Words*64;
    const int n=int(g.sites.size());
    require(n>0&&n<=capacity,"invalid compact space size");
    require(g.player>=0&&g.player<n,"invalid graph player");
    for(int p:g.pumpkins)require(p>=0&&p<n,"invalid graph pumpkin");
    for(int r:g.raccoons)require(r>=0&&r<n,"invalid graph sentry");
    std::array<Mask,capacity> incoming{};
    std::array<unsigned char,capacity> directions{};
    // Retain BFS frontiers as distance layers. A lexicographic descent needs
    // membership in the previous layer, not a distance write for every site.
    std::array<Mask,capacity> layers;
    Mask pits{},all{};
    for(int from=0;from<n;++from) {
        all.set(from);
        if(g.sites[from].pit)pits.set(from);
        for(int d=0;d<4;++d) {
            int to=g.sites[from].port[d];if(to<0)continue;
            require(to<n,"graph edge outside compact space");
            if((!g.sites[from].gate||g.sites[from].gate==d+1)&&(!g.sites[to].gate||g.sites[to].gate==d+1)) {
                incoming[to].set(from);directions[from]|=1u<<d;
            }
        }
    }
    GraphState state(g);std::string result;result.reserve(plan.size()*32);std::array<char,capacity> walk{};
    auto occupied_mask=[&]() {
        Mask blocked{};
        for(int p:state.pumpkins)if(p>=0)blocked.set(p);
        pits.each([&](int p){if(!state.filled[p])blocked.set(p);});
        for(std::size_t r=0;r<g.raccoons.size();++r)if(!state.departed[r])blocked.set(g.raccoons[r]);
        return blocked;
    };
    Mask blocked=occupied_mask();
    for(auto action:plan) {
        if constexpr(std::is_same_v<Intent,ActionIntent>) {
            if(reaction(state,action,result)) {blocked=occupied_mask();continue;}
        }
        require(action.pumpkin>=0&&action.pumpkin<int(state.pumpkins.size())&&action.direction>=0&&action.direction<4,"invalid push intent");
        int from=state.pumpkins[action.pumpkin];require(from>=0,"cannot push a consumed pumpkin");
        int support=g.sites[from].port[(action.direction+2)%4];require(support>=0,"push has no support site");
        if(support!=state.player) {
            if constexpr(!std::is_same_v<Intent,ActionIntent>)blocked=occupied_mask();
            const auto open=all&~blocked,player=Mask::bit(state.player);
            Mask frontier=Mask::bit(support),seen=frontier;
            require(bool(frontier&open),"push plan requires blocked support");
            layers[0]=frontier;int layer=0;
            while(!(frontier&player)) {
                Mask next{};frontier.each([&](int site){next|=incoming[site];});
                next&=open&~seen;require(bool(next),"push plan requires inaccessible support");
                layers[++layer]=next;
                seen|=next;frontier=next;
            }
            int cell=state.player,length=0;
            while(cell!=support) {
                int next=-1,chosen=-1;
                for(int d=0;d<4;++d)if(directions[cell]&(1u<<d)) {
                    int to=g.sites[cell].port[d];if(layers[layer-1].contains(to)){next=to;chosen=d;break;}
                }
                require(next>=0,"support distance has no descending edge");walk[length++]=kWalk[chosen];cell=next;--layer;
            }
            bool threatened=false;
            for(std::size_t r=0;r<g.raccoons.size();++r)if(!state.departed[r])
                for(int site:g.sites[g.raccoons[r]].port)if(state.box_at(site)>=0&&!g.sites[site].goal)threatened=true;
            if(threatened) {
                const auto before=state.pumpkins;
                for(int i=0;i<length;++i){bool pushed;require(state.move(dir_of(walk[i]),&pushed)&&!pushed,"walking certificate failed");}
                require(state.pumpkins==before,"walking certificate exposed an unattended pumpkin");
            } else state.player=support;
            result.append(walk.data(),length);
        }
        prescribed_push(state,action,result);
        if constexpr(std::is_same_v<Intent,ActionIntent>) {
            // Explicit pushes cannot cause theft. Exactly the old pumpkin site
            // and its destination can change occupancy (including a sealed pit).
            const int to=g.sites[from].port[action.direction];
            blocked.clear(from);blocked.clear(to);
            if(state.pumpkins[action.pumpkin]>=0)blocked.set(to);
        }
    }
    require(state.solved(),"push plan does not complete its graph");return result;
}
template<class Intent> std::string route_general(const PortGraph& g,const std::vector<Intent>& plan) {
    GraphState state(g);std::string result,walk;result.reserve(plan.size()*32);walk.reserve(g.sites.size());
    std::vector<int> distance(g.sites.size()),queue;queue.reserve(g.sites.size());
    for(auto action:plan) {
        if constexpr(std::is_same_v<Intent,ActionIntent>)if(reaction(state,action,result))continue;
        require(action.pumpkin>=0&&action.pumpkin<static_cast<int>(state.pumpkins.size())&&action.direction>=0&&action.direction<4,"invalid push intent");
        int from=state.pumpkins[action.pumpkin];require(from>=0,"cannot push a consumed pumpkin");
        int support=g.sites[from].port[(action.direction+2)%4];require(support>=0,"push has no support site");
        if(support==state.player) {
            prescribed_push(state,action,result);continue;
        }
        require(state.walkable(support),"push plan requires blocked support");
        // Distances to the support, on reversed directed walking edges. The
        // support is often a dead-end stem; expanding from it avoids unrelated
        // branches which a forward search from the bear would explore first.
        std::fill(distance.begin(),distance.end(),-1);queue.clear();queue.push_back(support);distance[support]=0;
        for(std::size_t head=0;head<queue.size()&&distance[state.player]<0;++head) {
            int to=queue[head];
            for(int d=0;d<4;++d) {
                int from=g.sites[to].port[(d+2)%4];
                if(from>=0&&distance[from]<0&&g.edge(from,d)&&state.walkable(from)) {
                    distance[from]=distance[to]+1;queue.push_back(from);
                }
            }
        }
        require(distance[state.player]>=0,"push plan requires inaccessible support");
        // All smaller-distance layers are discovered when the bear is found.
        // Greedy U/R/D/L descent therefore gives exactly the old forward BFS's
        // lexicographically first shortest walk, including ties around loops.
        walk.clear();int cell=state.player;
        while(cell!=support) {
            int next=-1,chosen=-1;
            for(int d=0;d<4;++d)if(g.edge(cell,d)) {
                int to=g.sites[cell].port[d];
                if(distance[to]==distance[cell]-1&&state.walkable(to)){next=to;chosen=d;break;}
            }
            require(next>=0,"support distance has no descending edge");walk+=kWalk[chosen];cell=next;
        }
        bool threatened=false;
        for(std::size_t r=0;r<g.raccoons.size();++r)if(!state.departed[r])
            for(int site:g.sites[g.raccoons[r]].port)if(state.box_at(site)>=0&&!g.sites[site].goal)threatened=true;
        if(threatened) {
            const auto before=state.pumpkins;
            for(char c:walk){bool pushed;require(state.move(dir_of(c),&pushed)&&!pushed,"walking certificate failed");}
            require(state.pumpkins==before,"walking certificate exposed an unattended pumpkin");
        } else {
            // BFS already checked every directed edge, occupancy and pit.
            // Walking cannot create a theft candidate when no pumpkin moves.
            state.player=support;
        }
        result+=walk;prescribed_push(state,action,result);
    }
    require(state.solved(),"push plan does not complete its graph");return result;
}
}
std::string route_plan(const PortGraph& g,const std::vector<PushIntent>& plan) {
    if(!g.sites.empty()&&g.sites.size()<=64)return route_compact<1>(g,plan);
    if(g.sites.size()>64&&g.sites.size()<=128)return route_compact<2>(g,plan);
    return route_general(g,plan);
}
std::string route_actions(const PortGraph& g,const std::vector<ActionIntent>& plan) {
    if(!g.sites.empty()&&g.sites.size()<=64)return route_compact<1>(g,plan);
    if(g.sites.size()>64&&g.sites.size()<=128)return route_compact<2>(g,plan);
    return route_general(g,plan);
}
void orient_actions(PortGraph& g,std::vector<ActionIntent>& plan,int turns,bool reflect) {
    std::vector<PushIntent> empty;orient(g,empty,turns,reflect);
    for(auto& a:plan){require(a.direction>=0&&a.direction<4,"invalid action direction");a.direction=transformed(a.direction,turns,reflect);}
}
void orient(PortGraph& g,std::vector<PushIntent>& plan,int turns,bool reflect) {
    require(turns>=0&&turns<4,"invalid graph orientation");
    for(auto& s:g.sites) {
        auto old=s.port;
        for(int d=0;d<4;++d)s.port[transformed(d,turns,reflect)]=old[d];
        if(s.gate)s.gate=transformed(s.gate-1,turns,reflect)+1;
    }
    for(auto& a:plan)a.direction=transformed(a.direction,turns,reflect);
}
std::string transform_trace(const std::string& trace,int turns,bool reflect) {
    require(turns>=0&&turns<4,"invalid trace transform");std::string out;
    for(char c:trace){int d=dir_of(c);require(d>=0,"invalid trace letter");d=transformed(d,turns,reflect);out+=(c>='A'&&c<='Z')?kPush[d]:kWalk[d];}
    return out;
}
std::string graph_dot(const PortGraph& g) {
    std::ostringstream out;out<<"graph spaces {\n";
    for(std::size_t i=0;i<g.sites.size();++i) {
        std::string name;for(char c:g.name(static_cast<int>(i))){if(c=='\"'||c=='\\')name+='\\';name+=c;}
        if(name.empty())name=std::to_string(i);
        out<<"  n"<<i<<" [label=\""<<name<<"\"];\n";
        for(int d=0;d<4;++d)if(g.sites[i].port[d]>static_cast<int>(i))out<<"  n"<<i<<" -- n"<<g.sites[i].port[d]<<" [label=\""<<kPush[d]<<"\"];\n";
    }
    return out.str()+"}\n";
}
}
