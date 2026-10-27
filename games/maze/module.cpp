#include "game_module.hpp"
#include "maze_view.hpp"
namespace games::modules {
std::unique_ptr<GameInstance> maze_create(ModuleContext& context) {
 return std::make_unique<HostedGameInstance<mz::MazeView>>(gf::make_control<mz::MazeView>(gf::StableId("maze.view"),mz::Options{.hosted=context.hosted,.dev=context.dev}));
}
}
