#pragma once
#include "terminal_order.hpp"
#include "tiers.hpp"
#include <variant>
namespace ct::research {
struct GardenScore {
    int difficulty=0,quarter_turns=0;
    std::uint64_t seed=0;
    bool reflect=false;
    std::variant<TerminalOrderScore,SharedStageScore> construction;
    int pushes=0,minimum_width=0,minimum_goal_departures=0;
};
// All seed decisions select valid grammar parameters. No board has yet been
// allocated, and no solver/candidate rejection is involved in these choices.
GardenScore compose_garden_score(int difficulty,std::uint64_t seed);
Level compile_garden_score(const GardenScore&);
std::string garden_score_dot(const GardenScore&);
Grown composed_grow(int difficulty,std::uint64_t seed,CancellationToken stop={},int max_tries=40);
}
