#pragma once
#include "shared_stage.hpp"
namespace ct::research {
struct TerminalOrderScore {
    int terminal_row=0,branch_direction=kDown,distance=1,start=0;
    std::vector<PushIntent> plan;
    std::array<StageEvent,2> events{};
    int pushes=0;
};
// A two-actor ordering rule. Filling the terminal goal traps the bear behind
// its one-way square, so the other goal must already be complete. Exact par
// attains the two-goal Manhattan assignment lower bound; width is one.
TerminalOrderScore compose_terminal_order(int terminal_row,int branch_direction,int distance,int start=0);
PortGraph realize_terminal_order(const TerminalOrderScore&);
StageDrawing draw_terminal_order(const TerminalOrderScore&,bool names=false);
Level compile_terminal_order(const TerminalOrderScore&,int quarter_turns=0,bool reflect=false);
// Sufficient local proof that filling this goal is the final successful move
// of any win. It does not itself claim the initial puzzle is solvable.
bool certifies_terminal_order(const Level&,int goal);
std::string terminal_order_dot(const TerminalOrderScore&);
}
