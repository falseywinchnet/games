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
        const std::filesystem::path directory = games_test::scratch_directory("games-atomprobe-");
        games_test::isolate_saves(directory);
        const std::filesystem::path legacy = directory / "atom_probe-v1.txt";
        const std::string sentinel = "Original Atom Probe save - preserve these bytes.\n";
        { std::ofstream output(legacy, std::ios::binary); output << sentinel; }
        require(ap::save_path(false) == directory / "atom_probe-v2.txt", "production v2 name");
        require(ap::save_path(true) == directory / "atom_probe-dev-v2.txt", "development v2 name");
        ap::Box board(42);
        board.new_box();
        ap::SaveData saved{};
        saved.box = board.state();
        saved.has_box = true;
        saved.settings.player_name = "RAINSTAR";
        saved.solved = 3;
        const bool first_write = ap::write_save(ap::save_path(false), saved);
        require(first_write, "initial save");
        saved.solved = 4;
        const bool replacement = ap::write_save(ap::save_path(false), saved);
        require(replacement, "replace an existing save");
        ap::SaveData restored{};
        const bool loaded = ap::load_save(ap::save_path(false), restored);
        require(loaded && restored.solved == 4 && restored.settings.player_name == "RAINSTAR", "round-trip replacement");
        ap::Box next(1);
        const bool resumed = next.restore(restored.box);
        require(resumed && next.atoms() == board.atoms(), "restore atoms and rules");
        const bool dev_write = ap::write_save(ap::save_path(true), saved);
        require(dev_write, "isolated development save");
        { std::ofstream output(ap::save_path(false), std::ios::binary | std::ios::app); output << "damage"; }
        const bool damaged = ap::load_save(ap::save_path(false), restored);
        require(!damaged, "corrupt save rejected");
        std::ifstream original(legacy, std::ios::binary);
        const std::string actual((std::istreambuf_iterator<char>(original)), std::istreambuf_iterator<char>());
        require(actual == sentinel, "v1 save never touched");
        original.close();
        std::filesystem::remove(legacy);
        std::filesystem::remove(ap::save_path(false));
        std::filesystem::remove(ap::save_path(true));
        std::filesystem::remove(directory);
        std::cout << "Atom Probe v2 save replacement and v1 isolation pass.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
