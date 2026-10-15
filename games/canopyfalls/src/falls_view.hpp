#pragma once
// Canopy Falls in PlaySuite: the ambient engine's SceneView hosting the live falls.
//
// The scene is baked for each picture size off the window's thread, in bands over the
// processor's cores, while the last picture stays up; once ready, each frame moves the
// water on and draws it (still.hpp, LiveFalls). The forest's sound (falls_voice.hpp) is
// a live voice under Sound; the music is the suite's "heavenly_harp" style under Music.
#include "scene_view.hpp"

#include "still.hpp"

#include <atomic>
#include <future>
#include <memory>
#include <vector>

namespace games {
class LiveComposer;
}

namespace cf {

class LiveForest;

class FallsScenery final : public ambient::Scenery {
  public:
    FallsScenery();
    ~FallsScenery() override;
    FallsScenery(const FallsScenery&) = delete;
    FallsScenery& operator=(const FallsScenery&) = delete;

    bool poll(ambient::SceneContext& context) override;
    [[nodiscard]] bool ready() const override;
    [[nodiscard]] ambient::SceneSize size_for(int device_width, int device_height, double scale,
                                              ambient::Detail detail) const override;
    void resize(ambient::SceneSize size, bool settled) override;
    void set_detail(ambient::Detail detail) override;
    void advance(double seconds, ambient::SceneContext& context) override;
    const std::vector<std::uint32_t>& draw(ambient::SceneContext& context, int& width, int& height) override;
    bool scripted_action(std::string_view action, ambient::SceneContext& context) override;
    void sound(bool running, ambient::SceneContext& context) override;

  private:
    void start_bake();

    std::unique_ptr<LiveFalls> falls_;
    std::future<void> baking_{};          // the size being baked, on other threads
    std::atomic<bool> cancel_{false};
    ambient::SceneSize wanted_{};         // the size the view asks for
    ambient::SceneSize baking_size_{};    // the size being baked
    ambient::SceneSize shown_size_{};     // the size shown
    bool settled_{};
    ambient::Detail detail_{ambient::Detail::balanced};
    std::vector<std::uint32_t> picture_{};
    std::shared_ptr<LiveForest> forest_{};        // the falls and the forest, where the toolkit can play a live voice
    std::shared_ptr<games::LiveComposer> music_{};  // the harp, likewise
};

class FallsView final : public ambient::SceneView {
  public:
    FallsView(ambient::gf::StableId id, ambient::ViewOptions options);
};

} // namespace cf
