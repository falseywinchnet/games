#pragma once
// Autosave: the game in progress, top scores, and settings. A bounded,
// versioned, checksummed text file replaced atomically.
#include "puzzle.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace sbx {

struct TopScore {
    std::string name;
    int steps = 0;        // flips taken to crack one combination (fewer is better)
    double when = 0;      // unix seconds
};

struct Settings {
    bool sound = true, music = true, reduced_motion = false;
    std::string player_name;
};

struct SaveData {
    PuzzleState puzzle;
    bool has_puzzle = false;
    int cracked = 0;                 // combinations solved, ever
    std::vector<TopScore> scores;    // best ten, fewest steps first
    Settings settings;
};

std::filesystem::path save_path(bool dev);
bool load_save(const std::filesystem::path& path, SaveData& out);
bool write_save(const std::filesystem::path& path, const SaveData& data);
// Inserts if it ranks among the best ten; returns its place (0-based) or -1.
int add_score(std::vector<TopScore>& scores, const TopScore& s);
bool qualifies(const std::vector<TopScore>& scores, int steps);

}  // namespace sbx
