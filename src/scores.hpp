#pragma once
#include "game.hpp"
#include <array>
#include <string>
#include <vector>
namespace games {
struct TopScore {
    std::string name;
    int value = 0;
};
// One bounded list per rules profile; deliberately no game history or statistics.
using TopScores = std::array<std::vector<TopScore>, 7>;
int score_profile(const State& state);
int final_score(const State& state);
bool score_is_better(int profile, int left, int right);
std::string score_profile_name(const State& state);
std::string score_unit(const State& state);
bool qualifies(const TopScores& scores, const State& state);
bool add_top_score(TopScores& scores, const State& state, const std::string& name);
bool valid_score_name(const std::string& name);
} // namespace games
