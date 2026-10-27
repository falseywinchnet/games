#include "game_module.hpp"
#include "legacy_game_module.hpp"
namespace games::modules {
std::unique_ptr<GameInstance> solitaire_create([[maybe_unused]] ModuleContext& context) {
    return std::make_unique<CardGameInstance>(context,Kind::solitaire);
}
}
