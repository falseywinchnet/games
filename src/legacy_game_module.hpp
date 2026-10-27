#pragma once
#include "game_module.hpp"
#include "table.hpp"
#include "puzzle_view.hpp"
#include "sudoku_view.hpp"
namespace games {
class CardGameInstance final : public HostedGameInstance<Table> {
  public:
    CardGameInstance(ModuleContext& context,Kind kind)
        : HostedGameInstance<Table>(shared_table(context)),kind_(kind) {}
    void activate() override { (*view_).show_kind(kind_); (*view_).activate(); }
    void preferences(bool,bool,bool,bool) override { (*view_).reload_preferences(); }
  private:
    Kind kind_;
    static std::shared_ptr<Table> shared_table(ModuleContext& context) {
        std::shared_ptr<gf::Control>& shared=context.shared["cards"];
        if(!shared) shared=gf::make_control<Table>(gf::StableId("collection.cards"));
        return std::static_pointer_cast<Table>(shared);
    }
};
}
