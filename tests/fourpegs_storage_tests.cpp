#include "save.hpp"
#include "test_paths.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
}
int main() {
    try {
        const std::filesystem::path directory = games_test::scratch_directory("games-fourpegs-");
        games_test::isolate_saves(directory);
        const std::filesystem::path legacy = directory / "four_pegs-v1.txt";
        const std::string sentinel = "Original Four Pegs save - preserve these bytes.\n";
        { std::ofstream output(legacy, std::ios::binary); output << sentinel; }
        require(fp::save_path(false) == directory / "four_pegs-v2.txt", "production v2 name");
        require(fp::save_path(true) == directory / "four_pegs-dev-v2.txt", "development v2 name");
        fp::Board board(42);
        fp::SaveData saved{};
        saved.board = board.state();
        saved.has_board = true;
        saved.settings.player_name = "RAINSTAR";
        saved.foiled = 3;
        const bool first_write = fp::write_save(fp::save_path(false), saved);
        require(first_write, "initial save");
        saved.foiled = 4;
        const bool replacement = fp::write_save(fp::save_path(false), saved);
        require(replacement, "replace an existing save");
        fp::SaveData restored{};
        const bool loaded = fp::load_save(fp::save_path(false), restored);
        require(loaded && restored.foiled == 4 && restored.settings.player_name == "RAINSTAR", "round-trip replacement");
        fp::Board next(1);
        const bool resumed = next.restore(restored.board);
        require(resumed && next.secret() == board.secret(), "restore secret and rules");
        const bool dev_write = fp::write_save(fp::save_path(true), saved);
        require(dev_write, "isolated development save");
        { std::ofstream output(fp::save_path(false), std::ios::binary | std::ios::app); output << "damage"; }
        const bool damaged = fp::load_save(fp::save_path(false), restored);
        require(!damaged, "corrupt save rejected");
        std::ifstream original(legacy, std::ios::binary);
        const std::string actual((std::istreambuf_iterator<char>(original)), std::istreambuf_iterator<char>());
        require(actual == sentinel, "v1 save never touched");
        original.close();
        std::filesystem::remove(legacy);
        std::filesystem::remove(fp::save_path(false));
        std::filesystem::remove(fp::save_path(true));
        std::filesystem::remove(directory);
        std::cout << "Four Pegs v2 save replacement and v1 isolation pass.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
