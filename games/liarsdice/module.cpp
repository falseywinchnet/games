#include "game_module.hpp"
#include "dice_view.hpp"
namespace games::modules {
std::unique_ptr<GameInstance> liarsdice_create([[maybe_unused]] ModuleContext& context) {
    return std::make_unique<HostedGameInstance<ld::DiceView>>(gf::make_control<ld::DiceView>(gf::StableId("liarsdice.view"),ld::Options{.hosted=context.hosted,.dev=context.dev}));
}
}
