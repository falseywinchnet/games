#include "game_module.hpp"
#include "legacy_game_module.hpp"
#include "src/sudoku_score.hpp"
namespace games::modules {
std::unique_ptr<GameInstance> sudoku_create([[maybe_unused]] ModuleContext& context) {
    // The music is a koto garden synthesized live (src/sudoku_music.hpp), by day and by night.
    if (std::shared_ptr<LiveScore> day = ps_sudoku::make_score(false))
        live_score("sudoku_day", std::move(day));
    if (std::shared_ptr<LiveScore> night = ps_sudoku::make_score(true))
        live_score("sudoku_night", std::move(night));
    return std::make_unique<HostedGameInstance<SudokuView>>(gf::make_control<SudokuView>(gf::StableId("collection.sudoku")));
}
}
