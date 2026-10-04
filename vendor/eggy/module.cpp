#include "game_module.hpp"
#include "eggy_view.hpp"
namespace games::modules {
std::unique_ptr<GameInstance> eggy_create([[maybe_unused]] ModuleContext& context) {
    return std::make_unique<HostedGameInstance<eggy::EggyView>>(gf::make_control<eggy::EggyView>(gf::StableId("collection.eggy"),eggy::Options{.hosted=context.hosted,.dev=context.dev}));
}
}
