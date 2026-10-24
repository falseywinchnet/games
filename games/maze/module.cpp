#include "game_module.hpp"
#include "maze_cues.hpp"
#include "maze_view.hpp"
namespace games::modules {
std::unique_ptr<GameInstance> maze_create(ModuleContext& context) {
 mz::register_sound_cues();  // the effects and jingles are rendered live, not read from files
 return std::make_unique<HostedGameInstance<mz::MazeView>>(gf::make_control<mz::MazeView>(gf::StableId("maze.view"),mz::Options{.hosted=context.hosted,.dev=context.dev}));
}
}
