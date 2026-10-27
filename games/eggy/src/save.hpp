#pragma once
// Autosave: a bounded, versioned, checksummed text file replaced atomically.
#include "sim.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace eggy {

struct TopScore {
    std::string name;
    double seconds = 0;   // time to complete (wall-clock, includes time away)
    int stars = 0;
    int stars_total = 0;
};

struct Settings {
    bool sound = true, music = true, reduced_motion = false;
    double zoom = 1.0;
    int pixel = 3;               // logical points per game pixel
    bool offline_climbing = true;
    std::string player_name = "EGGY FAN";
};

struct SaveData {
    std::uint64_t seed = 0;
    double u = 0, v = 0, z = 0, breath = 1, elapsed = 0, day_offset = .28, best_v = 0;
    double start_wall = 0, last_wall = 0, finish_seconds = 0;
    bool finished = false, recorded = false, intro_done = false;
    int last_milestone = 0;
    std::vector<int> collected;
    std::vector<TopScore> scores;
    Settings settings;
};

std::filesystem::path save_path(bool dev);
bool load_save(const std::filesystem::path& path, SaveData& out);
bool write_save(const std::filesystem::path& path, const SaveData& data);
void capture(const Sim& s, SaveData& d);
void restore(Sim& s, const SaveData& d);
bool add_score(std::vector<TopScore>& scores, const TopScore& s);   // keeps the best ten
std::string format_duration(double seconds);

}  // namespace eggy
