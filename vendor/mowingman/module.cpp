#include "game_module.hpp"
#include "mowing_view.hpp"

namespace games::modules {
std::unique_ptr<GameInstance> mowingman_create(ModuleContext& context) {
    const ambient::ViewOptions options{.hosted = context.hosted, .dev = context.dev};
    return std::make_unique<HostedGameInstance<mm::MowingView>>(
        gf::make_control<mm::MowingView>(gf::StableId("mowingman.view"), options));
}
} // namespace games::modules
