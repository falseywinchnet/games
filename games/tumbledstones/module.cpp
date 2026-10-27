#include "game_module.hpp"
#include "tumbledstones_view.hpp"

namespace games::modules {
std::unique_ptr<GameInstance> tumbledstones_create(ModuleContext& context) {
    const ts::Options options{.hosted = context.hosted, .dev = context.dev};
    return std::make_unique<HostedGameInstance<ts::TumbledStonesView>>(
        gf::make_control<ts::TumbledStonesView>(gf::StableId("tumbledstones.view"), options));
}
} // namespace games::modules
