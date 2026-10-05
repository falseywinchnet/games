#include "game_module.hpp"
#include "stillwater_view.hpp"

namespace games::modules {
std::unique_ptr<GameInstance> stillwater_create(ModuleContext& context) {
    const ambient::ViewOptions options{.hosted = context.hosted, .dev = context.dev};
    return std::make_unique<HostedGameInstance<sw::StillwaterView>>(
        gf::make_control<sw::StillwaterView>(gf::StableId("stillwater.view"), options));
}
} // namespace games::modules
