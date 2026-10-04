#include "game_module.hpp"
#include "zen_view.hpp"
namespace games::modules {
std::unique_ptr<GameInstance> zenconstruction_create([[maybe_unused]] ModuleContext& context) {
    return std::make_unique<HostedGameInstance<zc::ZenView>>(gf::make_control<zc::ZenView>(gf::StableId("zenconstruction.view"),zc::Options{.hosted=context.hosted,.dev=context.dev}));
}
}
