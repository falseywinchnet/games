#pragma once
// Autosave: the game in progress, his current plan, top scores and settings.
// A bounded, versioned, checksummed text file replaced atomically.
#include "board.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace fp {

struct TopScore {
    std::string name;
    int guesses = 0;     // guesses taken to name the code (fewer is better)
    double when = 0;     // unix seconds
};

struct Settings {
    bool sound = true, music = true;
    std::string player_name;
};

struct SaveData {
    BoardState board;
    bool has_board = false;
    std::string plan;               // what he means to do this game
    int foiled = 0, triumphs = 0;   // games won and lost, ever
    std::vector<TopScore> scores;   // best ten, fewest guesses first
    Settings settings;
};

std::filesystem::path save_path(bool dev);
bool load_save(const std::filesystem::path& path, SaveData& out);
bool write_save(const std::filesystem::path& path, const SaveData& data);
int add_score(std::vector<TopScore>& scores, const TopScore& s);  // its place (0-based), or -1
bool qualifies(const std::vector<TopScore>& scores, int guesses);

}  // namespace fp
