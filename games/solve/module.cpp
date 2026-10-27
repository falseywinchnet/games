#include "game_module.hpp"
#include "legacy_game_module.hpp"
namespace games::modules {
std::unique_ptr<GameInstance> solve_create(ModuleContext& context) {
    std::shared_ptr<PuzzleView> view =
        gf::make_control<PuzzleView>(gf::StableId("collection.puzzle.6"), PuzzleKind::solve);
    (*view).development = context.dev;
    return std::make_unique<HostedGameInstance<PuzzleView>>(view);
}
}
