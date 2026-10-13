#include "game_module.hpp"
#include "koi_view.hpp"
#include "suite_settings.hpp"
namespace games::modules {
namespace {
// Koi-Koi takes the PlaySuite card finish too: its hanafuda are printed as the patience
// table's cards are, with the Settings screen's emboss and glint.
class KoiInstance final : public HostedGameInstance<kk::KoiView> {
  public:
    explicit KoiInstance(std::shared_ptr<kk::KoiView> view) : HostedGameInstance<kk::KoiView>(std::move(view)) {}
    void preferences(bool active, bool music, bool sound, bool reduced) override {
        HostedGameInstance<kk::KoiView>::preferences(active, music, sound, reduced);
        const SuiteSettings& masters = SettingsStore::shared().values();
        (*view_).set_card_finish({masters.card_emboss, masters.card_emboss_size, masters.card_sparkle,
                                  masters.card_grain, masters.card_glare},
                                 masters.card_glint);
    }
};
}  // namespace
std::unique_ptr<GameInstance> koikoi_create([[maybe_unused]] ModuleContext& context) {
    return std::make_unique<KoiInstance>(gf::make_control<kk::KoiView>(gf::StableId("koikoi.view"),kk::Options{.hosted=context.hosted,.dev=context.dev}));
}
}
