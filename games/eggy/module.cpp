#include "game_module.hpp"
#include "eggy_cues.hpp"
#include "eggy_view.hpp"
namespace games::modules {
std::unique_ptr<GameInstance> eggy_create([[maybe_unused]] ModuleContext& context) {
    eggy::register_sound_cues();  // effects, beds and music are rendered live, not read from files
    return std::make_unique<HostedGameInstance<eggy::EggyView>>(gf::make_control<eggy::EggyView>(gf::StableId("collection.eggy"),eggy::Options{.hosted=context.hosted,.dev=context.dev}));
}
}
