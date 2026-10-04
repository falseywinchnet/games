#include "game_module.hpp"
#include "switchbox_view.hpp"
namespace games::modules {
std::unique_ptr<GameInstance> switchbox_create([[maybe_unused]] ModuleContext& context) {
    return std::make_unique<HostedGameInstance<sbx::SwitchboxView>>(gf::make_control<sbx::SwitchboxView>(gf::StableId("switchbox.view"),sbx::Options{.hosted=context.hosted,.dev=context.dev}));
}
}
