#include "shared_stage.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
namespace ct::research {
namespace {
void check_passage(bool outer,StagePassage p) {
    if(p.span<0||p.span>3||p.start<0||p.start>p.span||p.goal<0||p.goal>p.span||(!outer&&(p.span||p.start||p.goal)))
        throw std::invalid_argument("shared-stage passage needs span 0..3 and endpoints 0..span, or zeros without an outer passage");
}
}
SharedStageScore compose_shared_stage(bool pit,bool outer,bool raccoon,StagePassage passage,bool opening_return) {
    check_passage(outer,passage);
    if(!opening_return&&pit&&(!outer||passage.start==passage.goal))
        throw std::invalid_argument("the shorter pit score needs an initially empty outer goal");
    SharedStageScore s;s.pit=pit;s.outer=outer;s.raccoon=raccoon;s.passage=passage;s.opening_return=opening_return;
    s.plan.reserve(25);
    auto event=[&](int actor,std::initializer_list<int> moves,unsigned needs,const char* label) {
        actor-=!opening_return;
        int id=s.event_count++;s.events[id]={actor,int(s.plan.size()),int(moves.size()),needs,label};
        for(int d:moves)s.plan.push_back({actor,d});return 1u<<id;
    };
    unsigned a=opening_return?event(0,{kDown,kDown},0,"A lends the opening support"):0;
    unsigned c=event(2,{kRight},a,opening_return?"C lends its occupied goal":"C clears the middle approach");
    unsigned e=pit?event(3,{kDown},c,"E is consumed; its stage stays open"):
        event(3,{kDown,kDown},c,"E lends the lower passage");
    auto repeated=[&](int direction,int count,unsigned needs,const char* label) {
        int actor=4-int(!opening_return);
        int id=s.event_count++;s.events[id]={actor,int(s.plan.size()),count,needs,label};
        for(int i=0;i<count;++i)s.plan.push_back({actor,direction});return 1u<<id;
    };
    const bool early_stage_return=!opening_return&&!pit&&outer&&passage.start>0;
    if(early_stage_return)e=event(3,{kUp},e,"E returns through the already open lower approach");
    unsigned f=0;
    if(outer) {
        if(passage.goal>passage.start) {
            f=repeated(kRight,passage.goal-passage.start,e,"F temporarily fills the transit goal");
            f=repeated(kRight,2+passage.span-passage.goal,f,"F vacates that goal to open the approach");
        } else f=repeated(kRight,2+passage.span-passage.start,e,
            passage.goal==passage.start?"F lends its goal and the outer passage":"F moves away from its goal to open the approach");
    }
    if(!opening_return&&!pit&&!early_stage_return)e=event(3,{kUp},e|f,"E returns before the upper exchange");
    unsigned restored=event(2,{kLeft},outer?f:e,opening_return?"C restores its goal to change approach":"C returns to change approach");
    if(!opening_return&&outer)f=repeated(kLeft,2+passage.span-passage.goal,f|e|(pit?0:restored),"F returns to its goal");
    unsigned b=event(1,{kLeft},restored,opening_return?"B borrows A's starting square":"B clears C's finishing support");
    c=event(2,{kDown},b|e,"C takes E's original goal");
    if(opening_return) {
        b=event(1,{kRight,kDown},c,"B returns and takes C's original goal");
        a=event(0,{kUp,kUp,kUp,kLeft},b,"A returns through the cleared opening");
        if(!pit)e=event(3,{kUp},a|e,"E closes the lower passage on its new goal");
        // At goal offset zero F occupies E's returning access. At a positive
        // offset that support stays open, so their final blocks may commute.
        if(outer)repeated(kLeft,2+passage.span-passage.goal,f|(!pit&&passage.goal==0?e:a),"F closes the outer passage");
    } else {
        unsigned finish=event(1,{kUp,kLeft},pit?b:c,"B finishes the upper goal");
        if(pit) {
            // C's final downward push opens B's upper pushing support from
            // inside. Until then B needs the still-open outer return route.
            s.order_choices[0]={int(std::countr_zero(c)),int(std::countr_zero(finish)),
                int(std::countr_zero(finish)),int(std::countr_zero(f)),
                "C finishes before B, or B finishes before F closes the passage"};
            s.order_choice_count=1;
        }
    }
    s.pushes=static_cast<int>(s.plan.size());s.minimum_width=opening_return?4-int(pit)+int(outer):2+int(outer&&!pit&&passage.start==0);
    s.minimum_goal_departures=1+int(opening_return)+int(outer&&passage.goal>=passage.start);return s;
}
bool accepts_stage_order(const SharedStageScore& s,std::span<const int> order) {
    if(s.event_count<1||s.event_count>int(s.events.size())||order.size()!=std::size_t(s.event_count)||
       s.order_choice_count<0||s.order_choice_count>int(s.order_choices.size()))return false;
    std::array<int,12> position{};unsigned done=0;
    for(int i=0;i<s.event_count;++i) {
        int event=order[i];
        if(event<0||event>=s.event_count||(done&(1u<<event))||(s.events[event].prerequisites&~done))return false;
        position[event]=i;done|=1u<<event;
    }
    for(int i=0;i<s.order_choice_count;++i) {
        const auto& c=s.order_choices[i];
        for(int e:{c.before,c.after,c.or_before,c.or_after})if(e<0||e>=s.event_count)return false;
        if(!(position[c.before]<position[c.after]||position[c.or_before]<position[c.or_after]))return false;
    }
    return true;
}
std::vector<PushIntent> stage_push_order(const SharedStageScore& s,std::span<const int> order) {
    if(!accepts_stage_order(s,order))throw std::invalid_argument("event order violates the shared-stage resource contract");
    std::vector<PushIntent> plan;plan.reserve(s.plan.size());
    for(int id:order) {
        const auto& e=s.events[id];
        if(e.first_push<0||e.push_count<1||std::size_t(e.first_push)>s.plan.size()||
           std::size_t(e.push_count)>s.plan.size()-std::size_t(e.first_push))
            throw std::invalid_argument("stage event has an invalid push block");
        for(int i=0;i<e.push_count;++i) {
            auto push=s.plan[e.first_push+i];
            if(push.pumpkin!=e.actor)throw std::invalid_argument("stage event actor differs from its push block");
            plan.push_back(push);
        }
    }
    return plan;
}
StageFootprint footprint_shared_stage(const SharedStageScore& s) {
    check_passage(s.outer,s.passage);
    if(!s.opening_return&&s.pit&&(!s.outer||s.passage.start==s.passage.goal))
        throw std::invalid_argument("the shorter pit score needs an initially empty outer goal");
    StageFootprint out;auto& cells=out.cells;
    auto at=[](int x,int y){return y*StageFootprint::width+x;};
    const int gap=s.passage.span;auto shifted=[&](int x){return x+(x>=4?gap:0);};
    for(int y=0;y<6;++y)for(int x=0;x<5;++x)cells[at(shifted(x),y)].floor=y-x>=-2&&y-x<=1;
    for(int t:{2,3})for(auto [x,y]:{std::pair{t,t+2},std::pair{t-1,t+2},std::pair{t-1,t+1}})cells[at(shifted(x),y)].floor=true;
    cells[at(3,3)].floor=cells[at(shifted(4),5)].floor=false;
    if(!s.pit)cells[at(2,6)].floor=cells[at(3,6)].floor=true;
    if(s.outer) {
        for(auto [x,y]:{std::pair{5,3},std::pair{6,3},std::pair{5,4},std::pair{6,4}})cells[at(shifted(x),y)].floor=true;
        for(int x=4;x<4+gap;++x)for(int y:{2,4})cells[at(x,y)].floor=true;
    }
    for(auto [x,y]:{std::pair{0,0},std::pair{2,3}})cells[at(x,y)].goal=true;
    cells[at(2,2)].goal=s.opening_return;
    if(s.pit)cells[at(2,4)].pit=true;else cells[at(2,4)].goal=true;
    out.player=at(0,1);
    if(s.opening_return)out.pumpkins[out.pumpkin_count++]=at(1,1);
    for(auto [x,y]:{std::pair{2,1},std::pair{2,2},std::pair{2,3}})out.pumpkins[out.pumpkin_count++]=at(x,y);
    if(s.outer) {
        cells[at(5+gap,3)].gate=kRight+1;cells[at(3+s.passage.goal,4)].goal=true;
        out.pumpkins[out.pumpkin_count++]=at(3+s.passage.start,4);
    }
    if(s.raccoon){out.raccoon=at(0,2);cells[out.raccoon].floor=true;}
    return out;
}
StageDrawing draw_shared_stage(const SharedStageScore& s,bool names) {
    const auto footprint=footprint_shared_stage(s);
    constexpr int width=StageFootprint::width,height=StageFootprint::height;
    StageDrawing drawing;auto& g=drawing.graph;g.sites.reserve(34);g.pumpkins.reserve(5);drawing.points.reserve(34);
    std::array<int,width*height> site;site.fill(-1);
    for(int i=0;i<width*height;++i) {
        auto c=footprint.cells[i];if(!c.floor)continue;int x=i%width,y=i/width;
        int id=names?g.add("cell."+std::to_string(x)+"."+std::to_string(y)):g.add_unnamed();site[i]=id;
        drawing.points.emplace_back(x,y);g.sites[id].goal=c.goal;g.sites[id].pit=c.pit;g.sites[id].gate=c.gate;
        if(x&&site[i-1]>=0)g.join(site[i-1],id,kRight);
        if(y&&site[i-width]>=0)g.join(site[i-width],id,kDown);
    }
    g.player=site[footprint.player];
    for(int i=0;i<footprint.pumpkin_count;++i)g.pumpkins.push_back(site[footprint.pumpkins[i]]);
    if(footprint.raccoon>=0) {
        g.raccoons.push_back(site[footprint.raccoon]);
        for(auto [vx,vy]:{std::pair{0,1},std::pair{1,2}})for(int i=0;i<width*height;++i)
            if(site[i]>=0&&std::abs(i%width-vx)<=1&&std::abs(i/width-vy)<=1)g.guards.emplace_back(site[i],site[vy*width+vx]);
    }
    return drawing;
}
PortGraph realize_shared_stage(const SharedStageScore& s) {
    return draw_shared_stage(s,true).graph;
}
Level compile_shared_stage(const SharedStageScore& s,int turns,bool reflect) {
    if(turns<0||turns>3)throw std::invalid_argument("shared-stage orientation needs 0..3 quarter turns");
    auto drawing=draw_shared_stage(s,false);auto& g=drawing.graph;
    auto plan=s.plan;orient(g,plan,turns,reflect);
    int minx=0,miny=0,maxx=0,maxy=0;
    for(auto& [x,y]:drawing.points) {
        if(reflect)x=-x;
        for(int i=0;i<turns;++i){int old=x;x=-y;y=old;}
        minx=std::min(minx,x);miny=std::min(miny,y);maxx=std::max(maxx,x);maxy=std::max(maxy,y);
    }
    Level lv;lv.w=maxx-minx+3;lv.h=maxy-miny+3;
    lv.tiles.assign(lv.w*lv.h,Tile::wall);lv.goal.assign(lv.tiles.size(),0);lv.pit.assign(lv.tiles.size(),0);lv.gate.assign(lv.tiles.size(),0);
    auto cell=[&](int site){auto [x,y]=drawing.points[site];return lv.idx(x-minx+1,y-miny+1);};
    for(std::size_t i=0;i<g.sites.size();++i) {
        int c=cell(int(i));const auto& site=g.sites[i];lv.tiles[c]=Tile::floor;lv.goal[c]=site.goal;lv.pit[c]=site.pit;lv.gate[c]=site.gate;
    }
    lv.player=cell(g.player);lv.boxes.reserve(g.pumpkins.size());for(int p:g.pumpkins)lv.boxes.push_back(cell(p));
    for(int r:g.raccoons)lv.raccoons.push_back(cell(r));
    lv.solution=route_plan(g,plan);return lv;
}
Level compile_shared_stage_order(const SharedStageScore& s,std::span<const int> order,int turns,bool reflect) {
    auto reordered=s;reordered.plan=stage_push_order(s,order);return compile_shared_stage(reordered,turns,reflect);
}
}

namespace ct::research {
std::string shared_stage_dot(const SharedStageScore& s) {
    std::ostringstream out;
    out<<"digraph shared_stage {\n  rankdir=LR;\n  labelloc=t;\n  label=\"Event blocks with resource-conditioned ordering; not the full solution graph\\n"
       <<s.pushes<<" pushes; minimum unfinished width "<<s.minimum_width<<"; "<<s.minimum_goal_departures<<" goal departures\";\n";
    std::array<int,5> last;last.fill(-1);
    for(int i=0;i<s.event_count;++i) {
        const auto& e=s.events[i];
        out<<"  e"<<i<<" [shape=box,label=\""<<e.label<<"\\n"<<e.push_count<<" pushes\"];\n";
        for(int j=0;j<i;++j)if(e.prerequisites&(1u<<j))out<<"  e"<<j<<" -> e"<<i<<";\n";
        if(last[e.actor]>=0)out<<"  e"<<last[e.actor]<<" -> e"<<i<<" [style=dashed,color=blue,constraint=false,label=\"same pumpkin\"];\n";
        last[e.actor]=i;
    }
    for(int i=0;i<s.order_choice_count;++i) {
        const auto& c=s.order_choices[i];
        out<<"  choice"<<i<<" [shape=diamond,color=darkorange,label=\""<<c.label<<"\\n(e"<<c.before<<" before e"<<c.after
           <<") OR (e"<<c.or_before<<" before e"<<c.or_after<<")\"];\n";
        for(int e:{c.before,c.after,c.or_after})out<<"  choice"<<i<<" -> e"<<e<<" [style=dotted,dir=none,constraint=false,color=darkorange];\n";
    }
    return out.str()+"}\n";
}
}
