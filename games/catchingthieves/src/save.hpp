#pragma once
// Autosave: where the player is (and the moves made there, so a garden
// resumes exactly), what they have cleared and how well, the endless garden
// in play, and settings. A bounded, versioned, checksummed text file
// replaced atomically.
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace ct {

struct LevelRecord {
    int best_moves = 0, best_pushes = 0;  // 0: never cleared
};

struct Settings {
    bool sound = true, music = true;
};

struct SaveData {
    int level = 0;                         // campaign index in play (-1: the endless garden)
    std::string history;                   // LURD of the moves made in it so far
    std::map<int, LevelRecord> records;    // campaign index -> best
    std::string endless_xsb;               // the endless garden in play (rows joined by '|')
    std::string endless_solution;
    int endless_season = 0, endless_tier = 0, endless_cleared = 0;
    Settings settings;
};

std::filesystem::path save_path(bool dev);
bool load_save(const std::filesystem::path& path, SaveData& out);
bool write_save(const std::filesystem::path& path, const SaveData& data);

}  // namespace ct
