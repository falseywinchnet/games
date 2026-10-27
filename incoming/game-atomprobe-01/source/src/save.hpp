#pragma once
// Autosave: the box in play, top scores and settings. A bounded, versioned,
// checksummed text file replaced atomically.
#include "box.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace ap {

struct TopScore {
    std::string name;
    int points = 0;      // ports lit to find all four atoms (fewer is better)
    double when = 0;     // unix seconds
};

struct Settings {
    bool sound = true, music = true;
    std::string player_name;
};

struct SaveData {
    BoxState box;
    bool has_box = false;
    int solved = 0, opened = 0;     // boxes solved and boxes opened, ever
    std::vector<TopScore> scores;   // best ten, fewest points first
    Settings settings;
};

std::filesystem::path save_path(bool dev);
bool load_save(const std::filesystem::path& path, SaveData& out);
bool write_save(const std::filesystem::path& path, const SaveData& data);
int add_score(std::vector<TopScore>& scores, const TopScore& s);  // its place (0-based), or -1
bool qualifies(const std::vector<TopScore>& scores, int points);

}  // namespace ap
