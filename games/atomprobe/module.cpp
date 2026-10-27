#include "game_module.hpp"
#include "atomprobe_view.hpp"
namespace games::modules {
std::unique_ptr<GameInstance> atomprobe_create([[maybe_unused]] ModuleContext& context) {
    return std::make_unique<HostedGameInstance<ap::AtomProbeView>>(gf::make_control<ap::AtomProbeView>(gf::StableId("atomprobe.view"),ap::Options{.dev=context.dev,.hosted=context.hosted}));
}
}
