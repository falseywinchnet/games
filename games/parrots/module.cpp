#include "game_module.hpp"
#include "parrot_cues.hpp"
#include "table_view.hpp"
namespace games::modules {
std::unique_ptr<GameInstance> parrots_create([[maybe_unused]] ModuleContext& context) {
    pt::register_sound_cues();  // the sounds are rendered live, not read from files
    return std::make_unique<HostedGameInstance<pt::TableView>>(gf::make_control<pt::TableView>(gf::StableId("parrots.view"),pt::Options{.hosted=context.hosted,.dev=context.dev}));
}
}
