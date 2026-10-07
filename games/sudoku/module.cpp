#include "game_module.hpp"
#include "legacy_game_module.hpp"
namespace games::modules {
std::unique_ptr<GameInstance> sudoku_create([[maybe_unused]] ModuleContext& context) {
    return std::make_unique<HostedGameInstance<SudokuView>>(gf::make_control<SudokuView>(gf::StableId("collection.sudoku")));
}
}
