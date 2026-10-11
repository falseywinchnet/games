#include "tree_orchard.hpp"
#include "nested_realization.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace ct::research {
namespace {
using Point=std::pair<int,int>;
void require(bool b,const char* why){if(!b)throw std::logic_error(why);}
struct Drawing {
    PortGraph graph;
    std::vector<Point> point;
    std::vector<int> owner,partition;
    int components=0,minx=0,miny=0,maxx=0,maxy=0;
    std::array<int,83*83> cell;
    explicit Drawing(int estimate) {cell.fill(-1);graph.sites.reserve(estimate);point.reserve(estimate);owner.reserve(estimate);partition.reserve(estimate);}
    int& at(Point p) {
        require(p.first>=-40&&p.first<=40&&p.second>=-40&&p.second<=40,"folded drawing exceeds coordinate bounds");
        return cell[(p.second+41)*83+p.first+41];
    }
    int add(Point p,Site site,int region=-1,std::string name={}) {
        require(at(p)<0,"folded kernel collision");
        int id=static_cast<int>(point.size());at(p)=id;point.push_back(p);owner.push_back(region);
        partition.push_back(-1);++components;
        minx=std::min(minx,p.first);maxx=std::max(maxx,p.first);miny=std::min(miny,p.second);maxy=std::max(maxy,p.second);
        site.port.fill(-1);int added=graph.add_unnamed(site);require(added==id,"folded site index mismatch");
        if(!name.empty()){graph.names.resize(graph.sites.size());graph.names.back()=std::move(name);}
        return id;
    }
    void join(int from,int to,int direction) {
        graph.join(from,to,direction);
        while(partition[from]>=0)from=partition[from];
        while(partition[to]>=0)to=partition[to];
        if(from==to)return;
        if(partition[from]>partition[to])std::swap(from,to);
        partition[from]+=partition[to];partition[to]=from;--components;
    }
    void road(int from,int to,const std::vector<int>& steps,int region) {
        for(std::size_t i=0;i<steps.size();++i) {
            int d=steps[i];auto [x,y]=point[from];Point p{x+kDX[d],y+kDY[d]};int next;
            if(i+1==steps.size()){require(point[to]==p,"folded road has wrong destination");next=to;}
            else if(at(p)>=0) {
                next=at(p);require(owner[next]==region,"folded roads cross or enter a kernel");
            } else next=add(p,{},region);
            if(graph.sites[from].port[d]<0)join(from,next,d);
            else require(graph.sites[from].port[d]==next,"folded road port conflict");
            from=next;
        }
    }
    void check_contacts() {
        for(std::size_t i=0;i<point.size();++i)for(int d=0;d<4;++d) {
            auto [x,y]=point[i];int j=at({x+kDX[d],y+kDY[d]});
            int declared=graph.sites[i].port[d];
            if(declared>=0){require(declared==j&&graph.sites[declared].port[(d+2)%4]==static_cast<int>(i),"folded adjacency mismatch");continue;}
            if(j<0)continue;
            // Branches with the same logical source may form a wider walking
            // junction. Contacts with another source or a kernel are forbidden.
            require(owner[i]!=-1&&owner[i]==owner[j],"folded drawing introduces a shortcut");
            join(static_cast<int>(i),j,d);
        }
        require(components==1,"disconnected folded drawing");
        // Check the Chebyshev guarding relation against the retained atlas.
        // Orthogonal edges alone do not account for diagonal guarding.
        std::vector<std::pair<int,int>> expected;
        for(int sentry:graph.raccoons)for(int victim:graph.sites[sentry].port)if(victim>=0) {
            auto [vx,vy]=point[victim];
            for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)
                if(int bear=at({vx+x,vy+y});bear>=0)expected.emplace_back(bear,victim);
        }
        auto normalize=[](auto& pairs){std::sort(pairs.begin(),pairs.end());pairs.erase(std::unique(pairs.begin(),pairs.end()),pairs.end());};
        auto guards=graph.guards;normalize(expected);normalize(guards);
        require(expected==guards,"folded drawing changed the guarding relation");
    }
    Embedding emit(int turns,bool reflect) {
        require(turns>=0&&turns<4,"invalid retained-placement orientation");
        auto transform=[&](Point p) {
            auto [x,y]=p;if(reflect)x=-x;
            if(turns==1)return Point{-y,x};if(turns==2)return Point{-x,-y};if(turns==3)return Point{y,-x};return Point{x,y};
        };
        auto lo=transform({minx,miny}),hi=lo;
        for(Point corner:{Point{minx,maxy},Point{maxx,miny},Point{maxx,maxy}}) {
            auto p=transform(corner);lo.first=std::min(lo.first,p.first);lo.second=std::min(lo.second,p.second);
            hi.first=std::max(hi.first,p.first);hi.second=std::max(hi.second,p.second);
        }
        Embedding e;auto& lv=e.level;lv.w=hi.first-lo.first+3;lv.h=hi.second-lo.second+3;
        require(lv.w<=40&&lv.h<=40,"folded drawing exceeds garden dimensions");
        lv.tiles.assign(lv.w*lv.h,Tile::wall);lv.goal.assign(lv.tiles.size(),0);lv.pit.assign(lv.tiles.size(),0);lv.gate.assign(lv.tiles.size(),0);
        e.cell_for_site.reserve(point.size());int goals=0,pits=0;
        for(std::size_t i=0;i<point.size();++i) {
            auto [x,y]=transform(point[i]);int c=lv.idx(x-lo.first+1,y-lo.second+1);const auto& site=graph.sites[i];
            require(site.gate>=0&&site.gate<=4&&!(site.goal&&site.pit),"invalid folded overlay");
            e.cell_for_site.push_back(c);lv.tiles[c]=Tile::floor;lv.goal[c]=site.goal;lv.pit[c]=site.pit;lv.gate[c]=site.gate;
            goals+=site.goal;pits+=site.pit;
        }
        // Reuse the region scratch after construction to check actor overlap.
        std::fill(owner.begin(),owner.end(),0);
        auto actor=[&](int site) {
            require(site>=0&&site<static_cast<int>(point.size())&&!owner[site]&&!graph.sites[site].pit,"invalid folded actor");
            owner[site]=1;return e.cell_for_site[site];
        };
        lv.player=actor(graph.player);lv.boxes.reserve(graph.pumpkins.size());
        for(int box:graph.pumpkins)lv.boxes.push_back(actor(box));
        for(int sentry:graph.raccoons) {
            int c=actor(sentry);require(!graph.sites[sentry].goal&&!graph.sites[sentry].gate,"invalid folded sentry");lv.raccoons.push_back(c);
        }
        require(goals>0&&static_cast<int>(lv.boxes.size())>=goals&&lv.boxes.size()<=static_cast<std::size_t>(goals+pits)+lv.raccoons.size(),"invalid folded inventory");
        return e;
    }
};
void step(std::vector<int>& path,int d,int n){require(n>=0,"negative folded road length");path.insert(path.end(),n,d);}
Drawing draw_folded_forest(const std::vector<int>& parents,int start,int pitch,OrchardFeatures features,bool diagnostic_names) {
    const int n=static_cast<int>(parents.size()),m=(n+1)/2;
    if(n<1||n>10||start<0||start>=n||pitch<4||pitch>6)throw std::invalid_argument("invalid folded forest domain");
    if((features.pit&&(n<2||start==0))||(features.raccoon&&!features.pit))throw std::invalid_argument("invalid folded hazard domain");
    for(int v=0;v<n;++v) {
        if(parents[v]<0||parents[v]>v)throw std::invalid_argument("invalid goal predecessor");
        for(int w=0;w<v;++w)if(parents[w]<parents[v]&&parents[v]<w)throw std::invalid_argument("crossing goal roads");
    }
    // Same-bank nesting needs one distinct track per enclosed source, not one
    // per unit of geometric span. Sources share their own road junctions.
    std::vector<int> reach(n),track(n,4);
    for(int v=0;v<n;++v)reach[v]=v;
    for(int v=0;v<n;++v)if((v<m)==(parents[v]<m))reach[parents[v]]=std::max(reach[parents[v]],v);
    for(int p=n-1;p>=0;--p)for(int q=p+1;q<reach[p];++q)
        if((q<m)==(p<m)&&(reach[q]>q||parents[q]==q))track[p]=std::max(track[p],track[q]+2);
    int left=4,right=4,crossing=0;std::vector<int> rank(n,-1);
    for(int p=0;p<n;++p)if(reach[p]>p||parents[p]==p)(p<m?left:right)=std::max(p<m?left:right,track[p]);
    for(int v=m;v<n;++v)if(parents[v]<m)rank[parents[v]]=0;
    for(int p=0;p<n;++p)if(rank[p]==0)rank[p]=crossing++;
    int width=std::max(10,left+right+2*crossing+2);
    auto center=[&](int v)->Point{return v<m?Point{0,6*v}:Point{width,6*(m-1-(v-m))};};
    // Emit the same checked kernel directly. No discarded spine graph, string
    // lookup, or coordinate reconstruction is needed during generation.
    Drawing out(n*60);if(diagnostic_names)out.graph.names.reserve(n*11+1);out.graph.pumpkins.reserve(n);if(features.raccoon)out.graph.guards.reserve(12);
    enum Port {front,goal,initial,park,back,front_stem,front_input,back_stem,back_input,continuation,junction};
    constexpr std::array<Point,11> offsets{{{-2,0},{-1,0},{0,0},{1,0},{2,0},{-2,-1},{-2,-2},{2,-1},{2,-2},{0,1},{0,2}}};
    constexpr std::array<const char*,11> names{{"temporary_support","goal","initial","park","goal_support","temporary_stem","temporary_input","goal_stem","goal_input","continuation","junction"}};
    auto port=[](int v,Port p){return 11*v+static_cast<int>(p);};
    for(int v=0;v<n;++v) {
        auto [cx,cy]=center(v);int sign=v<m?1:-1;auto prefix=diagnostic_names?"turnout."+std::to_string(v)+".":std::string{};
        for(int p=0;p<11;++p){auto [x,y]=offsets[p];out.add({cx+sign*x,cy+sign*y},{},-1,diagnostic_names?prefix+names[p]:std::string{});}
        auto join=[&](Port a,Port b,int d){out.join(port(v,a),port(v,b),orchard_direction(d,v,n,OrchardLayout::folded));};
        join(front,goal,kRight);join(goal,initial,kRight);join(initial,park,kRight);join(park,back,kRight);
        join(front,front_stem,kUp);join(front_stem,front_input,kUp);join(back,back_stem,kUp);join(back_stem,back_input,kUp);
        join(initial,continuation,kDown);join(continuation,junction,kDown);
        out.graph.sites[port(v,goal)].goal=!(features.pit&&v==0);
        out.graph.pumpkins.push_back(port(v,initial));
    }
    if(features.pit)out.graph.sites[port(0,back)].pit=true;
    if(features.raccoon) {
        int r=out.add({1,1},{},-1,diagnostic_names?"root.raccoon":"");out.graph.raccoons.push_back(r);
        out.join(port(0,park),r,kDown);out.join(port(0,continuation),r,kRight);
        for(int bear:{port(0,initial),port(0,park),port(0,back),port(0,back_stem),port(0,continuation),r})out.graph.guards.emplace_back(bear,port(0,park));
        for(int bear:{port(0,goal),port(0,initial),port(0,park),port(0,continuation),port(0,junction),r})out.graph.guards.emplace_back(bear,port(0,continuation));
    }
    out.graph.player=port(start,junction);
    // Reuse scratch across roads; each road is consumed before the next starts.
    std::vector<int> road;road.reserve(160);
    for(int v=0;v<n;++v) {
        road.clear();int next=(v+1)%n;
        if(v+1==n) {
            auto [x,y]=out.point[port(v,junction)];
            if(v<m){step(road,kDown,2);step(road,kLeft,4);step(road,kUp,y+4);step(road,kRight,2);}
            else {step(road,kUp,y+4);step(road,kLeft,width+4);step(road,kDown,2);step(road,kRight,2);}
        } else if(v+1==m) {
            step(road,kDown,2);step(road,kRight,width);step(road,kUp,2);step(road,kRight,2);
        } else if(v<m){step(road,kDown,2);step(road,kLeft,2);}
        else {step(road,kUp,2);step(road,kRight,2);}
        out.road(port(v,junction),port(next,front_input),road,-2-v);
    }
    // A pit root's goal roads start at the disconnected highway stub. The self
    // loop reaches this stub only after the pit's support has become walkable.
    int root_stub=-1;
    if(features.pit)root_stub=out.add({2,2},{},0);
    for(int v=0;v<n;++v) {
        int p=parents[v];
        int from=p==0&&features.pit?root_stub:port(p,junction);
        auto [fx,fy]=out.point[from];auto [tx,ty]=out.point[port(v,back_input)];
        int column;
        if((p<m)==(v<m))column=p<m?track[p]:width-track[p];
        else {
            int order=ty>=fy?crossing-1-rank[p]:rank[p];
            column=left+2+2*order;
        }
        road.clear();
        step(road,column>=fx?kRight:kLeft,std::abs(column-fx));
        step(road,ty>=fy?kDown:kUp,std::abs(ty-fy));
        step(road,tx>=column?kRight:kLeft,std::abs(tx-column));
        out.road(from,port(v,back_input),road,p);
    }
    if(features.one_way)for(int v=0;v<n;++v)out.graph.sites[port(v,front_stem)].gate=orchard_direction(kDown,v,n,OrchardLayout::folded)+1;
    out.check_contacts();return out;
}
}
PortGraph folded_forest_space(const std::vector<int>& parents,int start,int pitch,OrchardFeatures features,bool diagnostic_names) {
    auto drawing=draw_folded_forest(parents,start,pitch,features,diagnostic_names);return std::move(drawing.graph);
}
NestedRealization realize_nested_score(const NestedScore& score) {
    const auto& c=score.certificate.orchard.certificate;int n=static_cast<int>(c.shape.parent.size());
    auto drawing=draw_folded_forest(c.shape.parent,c.shape.start,4,{false,c.root_pit,score.raccoon},false);
    auto& graph=drawing.graph;auto centers=place_borrowed_goals(graph,score.certificate,OrchardLayout::folded);auto plan=c.plan;
    require(!plan.empty(),"nested score has no push plan");
    for(auto& action:plan) {
        require(action.pumpkin>=0&&action.pumpkin<n&&action.direction>=0&&action.direction<4,"invalid nested push intent");
        action.direction=orchard_direction(action.direction,action.pumpkin,n,OrchardLayout::folded);
    }
    int last=plan.back().pumpkin,right=orchard_direction(kRight,last,n,OrchardLayout::folded);
    int support=graph.sites[centers[last]].port[right];
    if(support>=0)support=graph.sites[support].port[right];
    int gate=support<0?-1:graph.sites[support].port[orchard_direction(kUp,last,n,OrchardLayout::folded)];
    require(gate>=0,"missing final support stem");graph.sites[gate].gate=orchard_direction(kDown,last,n,OrchardLayout::folded)+1;
    // Only overlays, actor sites and a rigid global orientation change after
    // the placement check. No unchecked caller can change these private ports.
    orient(graph,plan,score.quarter_turns,score.reflect);
    auto trace=route_plan(graph,plan);auto e=drawing.emit(score.quarter_turns,score.reflect);
    NestedRealization out;out.level=std::move(e.level);out.level.solution=std::move(trace);
    out.centers.reserve(centers.size());for(int site:centers)out.centers.push_back(e.cell_for_site[site]);
    return out;
}
}
