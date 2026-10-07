#include "game_module.hpp"
#include "sheep_view.hpp"
namespace games::modules {
std::unique_ptr<GameInstance> penthesheep_create([[maybe_unused]] ModuleContext& context) {
    return std::make_unique<HostedGameInstance<sh::SheepView>>(gf::make_control<sh::SheepView>(gf::StableId("penthesheep.view"),sh::Options{.hosted=context.hosted,.dev=context.dev}));
}
}
