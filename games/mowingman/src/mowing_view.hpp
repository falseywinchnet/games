#pragma once
// Mowing in PlaySuite: the ambient engine's SceneView hosting the garden.
#include "scene_view.hpp"

#include "grass_art.hpp"
#include "sim.hpp"
#include "yard.hpp"

#include <atomic>
#include <future>
#include <memory>
#include <string>

namespace mm {

class LiveMower;
class LiveBand;

class MowingScenery final : public ambient::Scenery {
  public:
    MowingScenery();
    ~MowingScenery() override;
    MowingScenery(const MowingScenery&) = delete;
    MowingScenery& operator=(const MowingScenery&) = delete;

    bool poll(ambient::SceneContext& context) override;
    [[nodiscard]] bool ready() const override;
    [[nodiscard]] ambient::SceneSize size_for(int device_width, int device_height, double scale,
                                              ambient::Detail detail) const override;
    void resize(ambient::SceneSize size, bool settled) override;
    void advance(double seconds, ambient::SceneContext& context) override;
    const std::vector<std::uint32_t>& draw(ambient::SceneContext& context, int& width, int& height) override;
    void press(double x, double y, ambient::SceneContext& context) override;
    void drag(double x, double y, ambient::SceneContext& context) override;
    void release(double x, double y, ambient::SceneContext& context) override;
    [[nodiscard]] bool grabbable(double x, double y) const override;
    bool hold_key(std::uint32_t key, bool down, ambient::SceneContext& context) override;
    void add_commands(std::vector<games::GameCommand>& list, const ambient::Settings& settings) const override;
    bool run_command(std::string_view id, ambient::SceneContext& context) override;
    void add_settings(std::vector<games::GameSetting>& list, const ambient::Settings& settings) const override;
    bool change_setting(std::string_view id, double value, ambient::SceneContext& context) override;
    [[nodiscard]] std::string key_command(std::uint32_t key) const override;
    bool scripted_action(std::string_view action, ambient::SceneContext& context) override;
    void sound(bool running, ambient::SceneContext& context) override;

  private:
    void start_art(std::uint64_t seed);
    void rebuild();
    void next_garden();
    [[nodiscard]] double lawn_x(double fraction) const;
    [[nodiscard]] double lawn_y(double fraction) const;

    std::uint64_t seed_{1};
    Livery livery_{Livery::orange_h};
    bool livery_restored_{};
    std::unique_ptr<Mowing> mowing_{};
    std::unique_ptr<Mowing> next_{};  // the garden waiting for its grass
    GrassArt art_{};
    std::future<GrassArt> art_loading_{};
    std::atomic<bool> cancel_{false};
    Yard yard_{};
    ambient::SceneSize wanted_{};
    bool settled_{};
    bool dirty_all_{true};
    bool keys_[4]{};      // up, left, down, right held
    bool keyboard_{};     // the keys have the controls
    std::vector<std::uint32_t> picture_{};
    std::vector<std::uint32_t> previous_{};  // the last garden's final picture, faded out
    double fade_{};
    double eggy_look_{};
    std::shared_ptr<LiveMower> live_{};  // the synthesized mower, where the toolkit can play one
    std::shared_ptr<LiveBand> band_{};   // the improvising banjo band, likewise
};

class MowingView final : public ambient::SceneView {
  public:
    MowingView(ambient::gf::StableId id, ambient::ViewOptions options);
};

} // namespace mm
