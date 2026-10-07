#include "game_module.hpp"
#include "table_view.hpp"
namespace games::modules {
std::unique_ptr<GameInstance> parrots_create([[maybe_unused]] ModuleContext& context) {
    return std::make_unique<HostedGameInstance<pt::TableView>>(gf::make_control<pt::TableView>(gf::StableId("parrots.view"),pt::Options{.hosted=context.hosted,.dev=context.dev}));
}
}
