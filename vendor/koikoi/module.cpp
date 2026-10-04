#include "game_module.hpp"
#include "koi_view.hpp"
namespace games::modules {
std::unique_ptr<GameInstance> koikoi_create([[maybe_unused]] ModuleContext& context) {
    return std::make_unique<HostedGameInstance<kk::KoiView>>(gf::make_control<kk::KoiView>(gf::StableId("koikoi.view"),kk::Options{.hosted=context.hosted,.dev=context.dev}));
}
}
