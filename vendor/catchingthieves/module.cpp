#include "game_module.hpp"
#include "src/thieves_view.hpp"
namespace games::modules {
std::unique_ptr<GameInstance> catchingthieves_create(ModuleContext& context) {
    return std::make_unique<HostedGameInstance<ct::ThievesView>>(
        gf::make_control<ct::ThievesView>(gf::StableId("catchingthieves.view"),
                                        ct::Options{.hosted = context.hosted, .dev = context.dev}));
}
} // namespace games::modules
