#pragma once
// A file of levels: XSB boards separated by blank lines, each preceded by
// "; key: value" lines (id, tier, lesson, title, section, par, switches,
// nodes, solution, seed).
#include "level.hpp"

#include <string>
#include <vector>

namespace ct {

struct LevelEntry {
    Level level;
    std::string section;   // e.g. "Spring Sprouts"
    int par = 0;           // fewest pushes
    int switches = 0;
    std::string seed;      // how it was generated (empty for hand-made)
    int id = -1;           // permanent: saves record bests by it (the old book's index)
    std::string tier;      // "tutorial", "easy", "medium" or "hard"
    std::string lesson;    // a tutorial garden's one mechanism, in a sentence
    long long nodes = 0;   // the solver's effort, measured when the table was made
};

bool load_levels(const std::string& text, std::vector<LevelEntry>& out, std::string* error = nullptr);
std::string save_levels(const std::vector<LevelEntry>& levels);

}  // namespace ct
