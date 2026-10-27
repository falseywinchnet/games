#include "game_module.hpp"
#include "falls_view.hpp"

namespace games::modules {
std::unique_ptr<GameInstance> canopyfalls_create(ModuleContext& context) {
    const ambient::ViewOptions options{.hosted = context.hosted, .dev = context.dev};
    return std::make_unique<HostedGameInstance<cf::FallsView>>(
        gf::make_control<cf::FallsView>(gf::StableId("canopyfalls.view"), options));
}
} // namespace games::modules
