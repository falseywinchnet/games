#include "save.hpp"
#include "test_paths.hpp"
#include <cassert>
#include <fstream>
#include <iterator>

int main() {
    const std::filesystem::path directory = games_test::scratch_directory("games-switchbox-");
    games_test::isolate_saves(directory);
    const std::filesystem::path legacy = directory / "switchbox-v1.txt";
    const std::string sentinel = "Earlier Switchbox save: preserve these bytes.\n";
    { std::ofstream output(legacy, std::ios::binary); output << sentinel; }
    assert(sbx::save_path(false) == directory / "switchbox-v2.txt");
    assert(sbx::save_path(true) == directory / "switchbox-dev-v2.txt");
    sbx::Puzzle puzzle(42);
    puzzle.flip(puzzle.first());
    sbx::SaveData saved;
    saved.puzzle = puzzle.state();
    saved.has_puzzle = true;
    saved.settings.player_name = "RAINSTAR";
    saved.cracked = 3;
    bool written = sbx::write_save(sbx::save_path(false), saved);
    assert(written);
    saved.cracked = 4;
    written = sbx::write_save(sbx::save_path(false), saved);
    assert(written);
    sbx::SaveData restored;
    bool loaded = sbx::load_save(sbx::save_path(false), restored);
    assert(loaded);
    assert(restored.cracked == 4 && restored.settings.player_name == "RAINSTAR");
    sbx::Puzzle next(1);
    const bool restored_puzzle = next.restore(restored.puzzle);
    assert(restored_puzzle);
    assert(next.combination() == puzzle.combination() && next.progress() == 1);
    written = sbx::write_save(sbx::save_path(true), saved);
    assert(written);
    { std::ofstream output(sbx::save_path(false), std::ios::binary | std::ios::app); output << "damage"; }
    loaded = sbx::load_save(sbx::save_path(false), restored);
    assert(!loaded);
    std::ifstream input(legacy, std::ios::binary);
    const std::string actual((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    assert(actual == sentinel);
    input.close();
    std::filesystem::remove(legacy);
    std::filesystem::remove(sbx::save_path(false));
    std::filesystem::remove(sbx::save_path(true));
    std::filesystem::remove(directory);
}
