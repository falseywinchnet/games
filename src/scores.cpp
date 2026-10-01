#include "scores.hpp"
#include <algorithm>
namespace games {
int score_profile(const State& s) {
    if (s.kind == Kind::solitaire)
        return s.draw_count == 3 ? 1 : 0;
    if (s.kind == Kind::spider)
        return s.spider_suits == 4 ? 4 : s.spider_suits == 2 ? 3 : 2;
    return s.kind == Kind::freecell ? 5 : 6;
}
int final_score(const State& s) {
    if (s.kind == Kind::solitaire)
        return s.score;
    return s.kind == Kind::hearts ? s.totals[0] : s.moves;
}
bool score_is_better(int profile, int left, int right) {
    return profile < 2 ? left > right : left < right;
}
std::string score_profile_name(const State& s) {
    std::string title = game_name(s.kind);
    if (s.kind == Kind::solitaire)
        title += " · Draw " + std::to_string(s.draw_count);
    if (s.kind == Kind::spider)
        title += " · " + std::to_string(s.spider_suits) + " suit";
    return title;
}
std::string score_unit(const State& s) {
    return s.kind == Kind::solitaire ? "points · higher is better"
           : s.kind == Kind::hearts  ? "points · lower is better"
                                     : "moves · fewer is better";
}
bool valid_score_name(const std::string& name) {
    if (name.empty() || name.size() > 96)
        return false;
    bool visible = false;
    for (unsigned char c : name) {
        if (c < 32 || c == 127)
            return false;
        visible = visible || c != ' ';
    }
    return visible;
}
bool qualifies(const TopScores& scores, const State& s) {
    if (!s.over)
        return false;
    int profile = score_profile(s);
    const std::vector<TopScore>& list = scores[profile];
    return list.size() < 10 || score_is_better(profile, final_score(s), list.back().value);
}
bool add_top_score(TopScores& scores, const State& s, const std::string& name) {
    if (!valid_score_name(name) || !qualifies(scores, s))
        return false;
    int profile = score_profile(s);
    std::vector<TopScore>& list = scores[profile];
    TopScore entry{name, final_score(s)};
    std::vector<TopScore>::iterator position = list.begin();
    while (position != list.end() && !score_is_better(profile, entry.value, (*position).value))
        ++position;
    list.insert(position, entry);
    if (list.size() > 10)
        list.pop_back();
    return true;
}
} // namespace games
