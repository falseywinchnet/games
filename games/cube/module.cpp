#include "cube_score.hpp"
#include "cube_view.hpp"
#include "game_module.hpp"
namespace games::modules {
std::unique_ptr<GameInstance> cube_create(ModuleContext& context) {
    // Nature Cube's music and effects are synthesized live (src/glass_music.hpp).
    if (std::shared_ptr<LiveScore> score = ps_cube::make_score()) {
        live_score("nature_cube", std::move(score));
    }
    const ps_cube::Options options{.hosted = context.hosted, .dev = context.dev};
    return std::make_unique<HostedGameInstance<ps_cube::CubeView>>(
        gf::make_control<ps_cube::CubeView>(gf::StableId("cube.view"), options));
}
} // namespace games::modules
