#pragma once
#include "game.hpp"
#include "scores.hpp"
#include <filesystem>
namespace games {
struct Cabinet {
    std::array<Game, 4> games{};
    std::array<bool, 4> started{};
    std::array<bool, 4> result_recorded{};
    TopScores top_scores{};
    std::string player_name = "Player";
    int active = 0, back = 0;
    bool reduced = false, sound = true, music = true;
};
// Bounded versioned text snapshot with corruption detection and atomic replacement.
// Failed reads leave the destination untouched. No addresses or resource paths are saved.
bool load_cabinet(const std::filesystem::path& path, Cabinet& destination);
bool save_cabinet(const std::filesystem::path& path, const Cabinet& cabinet);
std::filesystem::path cabinet_path();
std::string asset_directory();
} // namespace games
