#pragma once
#include "scores.hpp"
#include <array>
#include <filesystem>
namespace games {
struct SudokuGrid {
    std::array<int, 81> values{};
    std::array<int, 81> notes{};
};
struct Sudoku {
    std::array<int, 81> puzzle{}, solution{};
    SudokuGrid grid;
    std::vector<SudokuGrid> history;
    std::array<std::vector<TopScore>, 3> scores;
    std::string seed, player_name = "Player", message;
    int difficulty = 0, errors = 0;
    bool dark = false, over = false, recorded = false;
    bool set(int cell, int digit, bool note);
    bool undo();
    bool complete_unit(int unit) const;
    bool invariant() const;
    bool qualifies() const;
    bool record(const std::string& name);
    bool save(const std::filesystem::path& path) const;
    bool load(const std::filesystem::path& path);
};
struct SudokuJob {
    std::string asset_path, seed;
    int difficulty = 0;
    Sudoku operator()() const;
};
} // namespace games
