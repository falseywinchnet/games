#include "game_module.hpp"
#include "template_view.hpp"

namespace games::modules {
std::unique_ptr<GameInstance> templategame_create(ModuleContext& context) {
    const tg::Options options{.hosted = context.hosted, .dev = context.dev};
    return std::make_unique<HostedGameInstance<tg::TemplateView>>(
        gf::make_control<tg::TemplateView>(gf::StableId("templategame.view"), options));
}
} // namespace games::modules
