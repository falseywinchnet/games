#include "game_module.hpp"
#include "specimencabinet_view.hpp"

namespace games::modules {
std::unique_ptr<GameInstance> specimencabinet_create(ModuleContext& context) {
    const sc::Options options{.hosted = context.hosted, .dev = context.dev};
    return std::make_unique<HostedGameInstance<sc::SpecimenCabinetView>>(
        gf::make_control<sc::SpecimenCabinetView>(gf::StableId("specimencabinet.view"), options));
}
} // namespace games::modules
