#include "game_module.hpp"
#include "switchbox_babble_cues.hpp"
#include "switchbox_view.hpp"
namespace games::modules {
std::unique_ptr<GameInstance> switchbox_create([[maybe_unused]] ModuleContext& context) {
    sbx::register_babble_cues();  // the clap and babble are rendered live, not read from files
    return std::make_unique<HostedGameInstance<sbx::SwitchboxView>>(gf::make_control<sbx::SwitchboxView>(gf::StableId("switchbox.view"),sbx::Options{.hosted=context.hosted,.dev=context.dev}));
}
}
