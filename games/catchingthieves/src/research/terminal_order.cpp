#include "terminal_order.hpp"
#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
namespace ct::research {
namespace {
void check(int row,int direction,int distance,int start) {
    if(row<0||row>2||(direction!=kLeft&&direction!=kDown&&direction!=kRight)||distance<1||distance>(direction==kLeft?2:3)||start<0||start>=distance)
        throw std::invalid_argument("terminal order needs row 0..2, a left/down/right branch, distance 1..2 left or 1..3 otherwise, and start 0..distance-1");
}
struct Drawing {PortGraph graph;std::vector<std::pair<int,int>> points;};
Drawing draw(const TerminalOrderScore& s,bool names) {
    check(s.terminal_row,s.branch_direction,s.distance,s.start);
    constexpr int w=6,h=7;std::array<bool,w*h> floor{};
    auto at=[](int x,int y){return y*w+x;};
    for(int y=0;y<3;++y)for(int x=(y==2?1:0);x<4;++x)floor[at(x,y)]=true;
    if(s.terminal_row==2){floor[at(0,0)]=false;floor[at(0,2)]=true;}
    for(int x=1;x<=3;++x)floor[at(x,3)]=true;
    if(s.branch_direction!=kDown)for(int x=1;x<=3;++x)floor[at(x,4)]=true;
    int dx=kDX[s.branch_direction],dy=kDY[s.branch_direction];
    for(int i=1;i<=s.distance;++i)floor[at(2+dx*i,3+dy*i)]=true;
    Drawing out;auto& g=out.graph;g.sites.reserve(24);g.pumpkins.reserve(2);out.points.reserve(24);
    std::array<int,w*h> sites;sites.fill(-1);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)if(floor[at(x,y)]) {
        int id=names?g.add("cell."+std::to_string(x)+"."+std::to_string(y)):g.add_unnamed();sites[at(x,y)]=id;out.points.emplace_back(x,y);
        if(x&&sites[at(x-1,y)]>=0)g.join(sites[at(x-1,y)],id,kRight);
        if(y&&sites[at(x,y-1)]>=0)g.join(sites[at(x,y-1)],id,kDown);
    }
    auto cell=[&](int x,int y){return sites[at(x,y)];};
    g.sites[cell(0,s.terminal_row)].goal=true;g.sites[cell(2+dx*s.distance,3+dy*s.distance)].goal=true;
    g.sites[cell(1,s.terminal_row)].gate=kLeft+1;
    // Keep the initial bear off the terminal burrow so both goals are visible.
    g.player=cell(0,s.terminal_row==1?0:1);g.pumpkins={cell(2,1),cell(2+dx*s.start,3+dy*s.start)};return out;
}
}
TerminalOrderScore compose_terminal_order(int row,int direction,int distance,int start) {
    check(row,direction,distance,start);TerminalOrderScore s;s.terminal_row=row;s.branch_direction=direction;s.distance=distance;s.start=start;
    s.plan.reserve(6);for(int i=start;i<distance;++i)s.plan.push_back({1,direction});
    s.events[0]={1,0,distance-start,0,"Finish the accessible branch"};
    int first=int(s.plan.size());if(row!=1)s.plan.push_back({0,row==0?kUp:kDown});
    s.plan.push_back({0,kLeft});s.plan.push_back({0,kLeft});
    s.events[1]={0,first,int(s.plan.size())-first,1,"Fill the terminal goal last"};s.pushes=int(s.plan.size());return s;
}
PortGraph realize_terminal_order(const TerminalOrderScore& s){return draw(s,true).graph;}
StageDrawing draw_terminal_order(const TerminalOrderScore& s,bool names){auto d=draw(s,names);return {std::move(d.graph),std::move(d.points)};}
Level compile_terminal_order(const TerminalOrderScore& s,int turns,bool reflect) {
    if(turns<0||turns>3)throw std::invalid_argument("terminal order orientation needs 0..3 quarter turns");
    auto drawing=draw(s,false);auto& g=drawing.graph;auto plan=s.plan;orient(g,plan,turns,reflect);
    int minx=0,miny=0,maxx=0,maxy=0;
    for(auto& [x,y]:drawing.points) {
        if(reflect)x=-x;for(int i=0;i<turns;++i){int old=x;x=-y;y=old;}
        minx=std::min(minx,x);miny=std::min(miny,y);maxx=std::max(maxx,x);maxy=std::max(maxy,y);
    }
    Level lv;lv.w=maxx-minx+3;lv.h=maxy-miny+3;
    lv.tiles.assign(lv.w*lv.h,Tile::wall);lv.goal.assign(lv.tiles.size(),0);lv.gate.assign(lv.tiles.size(),0);lv.pit.assign(lv.tiles.size(),0);
    auto cell=[&](int site){auto [x,y]=drawing.points[site];return lv.idx(x-minx+1,y-miny+1);};
    for(std::size_t i=0;i<g.sites.size();++i){int c=cell(int(i));lv.tiles[c]=Tile::floor;lv.goal[c]=g.sites[i].goal;lv.gate[c]=g.sites[i].gate;}
    lv.player=cell(g.player);lv.boxes.reserve(2);for(int p:g.pumpkins)lv.boxes.push_back(cell(p));lv.solution=route_plan(g,plan);return lv;
}
bool certifies_terminal_order(const Level& lv,int goal) {
    if(!lv.valid()||!lv.floor(goal)||!lv.goal[goal]||lv.pit_at(goal)||std::find(lv.boxes.begin(),lv.boxes.end(),goal)!=lv.boxes.end())return false;
    for(int d=0;d<4;++d) {
        int arrow=lv.step(goal,(d+2)%4),beyond=lv.step(goal,d);
        if(!lv.floor(arrow)||lv.gate_at(arrow)!=d+1||lv.floor(beyond)||!lv.edge(arrow,d))continue;
        bool only_entry=true;
        for(int other=0;other<4;++other)if(other!=d) {
            int from=lv.step(goal,(other+2)%4),support=lv.step(from,(other+2)%4);
            if(lv.floor(from)&&lv.floor(support)&&lv.edge(support,other)&&lv.edge(from,other))only_entry=false;
        }
        if(only_entry)return true;
    }
    return false;
}
std::string terminal_order_dot(const TerminalOrderScore& s) {
    std::ostringstream out;out<<"digraph terminal_order {\n  rankdir=LR;\n  label=\""<<s.pushes<<" pushes; minimum width 1; no goal departures\";\n"
        <<"  branch [shape=box,label=\"Finish accessible branch\\n"<<s.events[0].push_count<<" pushes\"];\n"
        <<"  terminal [shape=box,label=\"Finish terminal goal\\n"<<s.events[1].push_count<<" pushes\"];\n"
        <<"  branch -> terminal [label=\"terminal fill permits no later move\"];\n}\n";return out.str();
}
}
