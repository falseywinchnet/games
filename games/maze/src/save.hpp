#pragma once
// Autosave: which maze you are on (each one is rebuilt from its seed), the
// trophy shelf, a few tallies, and settings. A bounded, versioned,
// checksummed text file replaced atomically.
#include <cstdint>
#include <filesystem>
#include <string>

namespace mz {

struct SaveData {
    int level = 1;
    std::uint64_t run_seed = 0;     // 0: not yet chosen
    std::uint64_t collected = 0;    // one bit per reward on the shelf
    int cleared = 0;                // mazes finished
    long long steps = 0;            // steps taken, ever
    int perfect = 0;                // mazes finished in the fewest possible steps
    int difficulty = 0;             // 0 Easy (the course), 1 Medium, 2 Hard
    int level_medium = 1, level_hard = 1;  // the maze reached at each; `level` is Easy's
    std::uint64_t met = 0;          // one bit for each trick already explained
    bool sound = true, music = true;
};

std::filesystem::path save_path(bool dev);
bool load_save(const std::filesystem::path& path, SaveData& out);
bool write_save(const std::filesystem::path& path, const SaveData& data);

}  // namespace mz
