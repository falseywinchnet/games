#include "storage.hpp"
#include "sudoku.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
int main(int argc, char** argv) {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("sudoku-test-" + std::to_string(getpid()));
    for (int difficulty = 0; difficulty < 3; ++difficulty) {
        games::SudokuJob job{games::asset_directory(),
                             "native-integration-" + std::to_string(difficulty), difficulty};
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        games::Sudoku game = job();
        double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        require(game.invariant(), "native generated board validates");
        games::Sudoku again = job();
        require(again.puzzle == game.puzzle && again.solution == game.solution,
                "native seed determinism");
        if (argc > 1) {
            std::filesystem::create_directories(argv[1]);
            require(game.save(std::filesystem::path(argv[1]) /
                              ("native-" + std::to_string(difficulty) + ".txt")),
                    "export native generation for independent oracle");
        }
        int cell = 0;
        while (game.puzzle[cell])
            ++cell;
        int correct = game.solution[cell], wrong = correct % 9 + 1;
        require(game.set(cell, wrong, false) && game.errors == 1, "mistake counted");
        require(game.undo() && game.errors == 1 && game.grid.values[cell] == 0,
                "undo does not erase errors");
        require(game.set(cell, correct, true) && game.grid.notes[cell] == (1 << (correct - 1)),
                "right-click note model");
        require(game.set(cell, correct, false) && game.grid.notes[cell] == 0,
                "setting digit replaces notes");
        game.dark = true;
        require(game.save(root / "save.txt"), "autosave");
        games::Sudoku loaded;
        require(loaded.load(root / "save.txt") && loaded.errors == 1 && loaded.dark &&
                    loaded.grid.values == game.grid.values,
                "resume exact state and errors");
        for (int i = 0; i < 81; ++i)
            if (!game.puzzle[i])
                game.set(i, game.solution[i], false);
        require(game.over && game.qualifies(), "completion qualifies");
        require(game.record("Native Solver"), "named result");
        require(!game.record("Again"), "no duplicate submission");
        require(game.undo() && game.set(80, game.solution[80], false) == !game.puzzle[80],
                "undo state available");
        // Refinish whichever square undo removed; the independent result latch survives.
        for (int i = 0; i < 81; ++i)
            if (!game.puzzle[i])
                game.set(i, game.solution[i], false);
        require(game.over && !game.qualifies(), "rewinning cannot record same puzzle twice");
        std::cout
            << "Native difficulty " << difficulty << ": " << ms
            << " ms; deterministic generation, error/undo, notes, saves and top scores pass.\n";
    }
    std::filesystem::remove_all(root);
}
