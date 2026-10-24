#include "game_module.hpp"
#include "fourpegs_view.hpp"
#include "fourpegs_cues.hpp"
namespace games::modules {
std::unique_ptr<GameInstance> fourpegs_create([[maybe_unused]] ModuleContext& context) {
    fp::register_sound_cues();  // the sound is rendered live, not read from files
    return std::make_unique<HostedGameInstance<fp::FourPegsView>>(gf::make_control<fp::FourPegsView>(gf::StableId("fourpegs.view"),fp::Options{.dev=context.dev,.hosted=context.hosted}));
}
}
