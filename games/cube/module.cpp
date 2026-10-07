#include "game_module.hpp"
#include "legacy_game_module.hpp"
#include "src/cube_score.hpp"
namespace games::modules {
std::unique_ptr<GameInstance> cube_create([[maybe_unused]] ModuleContext& context) {
    // Nature Cube's music and effects are synthesized live (src/glass_music.hpp).
    if (std::shared_ptr<LiveScore> score = ps_cube::make_score())
        live_score("nature_cube", std::move(score));
    return std::make_unique<HostedGameInstance<PuzzleView>>(gf::make_control<PuzzleView>(gf::StableId("collection.puzzle.1"),PuzzleKind::cube));
}
}
