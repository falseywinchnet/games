#pragma once
#include "port_graph.hpp"
#include <span>
namespace ct::research {
struct StageEvent {
    int actor=0,first_push=0,push_count=0;
    unsigned prerequisites=0; // Events required before this action block.
    const char* label="";
};
struct StageOrderChoice {
    // Either before precedes after, OR or_before precedes or_after.
    // A resource window cannot generally be encoded by one precedence DAG.
    int before=0,after=0,or_before=0,or_after=0;
    const char* label="";
};
struct StagePassage {
    // Offset of the outer approach, then the resident's initial/final offsets
    // along its track. All are constructor parameters, not selected boards.
    int span=0,start=0,goal=0;
};
struct SharedStageScore {
    bool pit=false,outer=false,raccoon=false;
    bool opening_return=true;
    StagePassage passage;
    std::vector<PushIntent> plan;
    std::array<StageEvent,12> events{};
    int event_count=0;
    std::array<StageOrderChoice,1> order_choices{};
    int order_choice_count=0;
    int pushes=0,minimum_width=0,minimum_goal_departures=0;
};
// Compose a persistent or consumed staging obligation, optionally nested
// inside a second borrowed passage. The score is independent of geometry.
// Without opening_return, B finishes the upper goal and the outer resident
// can settle earlier. A pit then requires an initially empty outer goal.
SharedStageScore compose_shared_stage(bool pit=false,bool outer=false,bool raccoon=false,StagePassage passage={},bool opening_return=true);
// Orders contain every event exactly once. Blocks remain indivisible and each
// pumpkin keeps its prescribed pushes. These APIs do not enumerate or search
// puzzle states; callers can compile any order admitted by the score contract.
bool accepts_stage_order(const SharedStageScore&,std::span<const int>);
std::vector<PushIntent> stage_push_order(const SharedStageScore&,std::span<const int>);
struct StageCell {bool floor=false,goal=false,pit=false;unsigned char gate=0;};
struct StageFootprint {
    static constexpr int width=10,height=7;
    std::array<StageCell,width*height> cells{};
    std::array<int,5> pumpkins{};
    int pumpkin_count=0,player=-1,raccoon=-1;
};
// A freshly constructed annotated footprint, before allocating a movement
// graph. Compositions can identify cells/actors and then build adjacency once.
StageFootprint footprint_shared_stage(const SharedStageScore&);
struct StageDrawing {
    PortGraph graph;
    std::vector<std::pair<int,int>> points;
};
// Retained coordinates let composition identify interfaces without parsing
// diagnostic site names. names=false is the construction path.
StageDrawing draw_shared_stage(const SharedStageScore&,bool names=false);
PortGraph realize_shared_stage(const SharedStageScore&);
// Retains the constructor's coordinates rather than recovering them from the
// graph. The general graph embedding remains an independent reference check.
Level compile_shared_stage(const SharedStageScore&,int quarter_turns=0,bool reflect=false);
Level compile_shared_stage_order(const SharedStageScore&,std::span<const int>,int quarter_turns=0,bool reflect=false);
std::string shared_stage_dot(const SharedStageScore&);
}
