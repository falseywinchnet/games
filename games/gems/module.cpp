#include "game_module.hpp"
#include "legacy_game_module.hpp"
namespace games::modules {
std::unique_ptr<GameInstance> gems_create([[maybe_unused]] ModuleContext& context) {
    return std::make_unique<HostedGameInstance<PuzzleView>>(gf::make_control<PuzzleView>(gf::StableId("collection.puzzle.0"),PuzzleKind::gems));
}
}
